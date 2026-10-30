/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../sunxi_camera_v2.h"

#define SIGNAL_LOST (V4L2_EVENT_PRIVATE_START + 1)
#define TYPE V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
#define MAX_RETRIES 5

struct options {
	const char *video, *subdev, *output;
	uint64_t frames, seconds, repeat, samples, input;
};

struct mapping {
	void *address;
	size_t length;
};

struct capture {
	int fd;
	bool requested, streaming;
	unsigned int count;
	struct mapping (*buffers)[VIDEO_MAX_PLANES];
	struct v4l2_pix_format_mplane format;
};

struct stats {
	uint64_t frames, drops, repeats, resets, bad, backwards;
	uint32_t sequence, timestamp_flags;
	int64_t timestamp_us;
	bool have_previous;
};

static volatile sig_atomic_t interrupted;

static void on_signal(int signal_number)
{
	(void)signal_number;
	interrupted = 1;
}

static double now_seconds(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts)) {
		perror("clock_gettime");
		exit(EXIT_FAILURE);
	}
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000;
}

static int xioctl(int fd, unsigned long request, void *argument)
{
	int result;

	do {
		result = ioctl(fd, request, argument);
	} while (result < 0 && errno == EINTR && !interrupted);
	return result;
}

static bool number(const char *text, uint64_t max, uint64_t *value)
{
	char *end;
	unsigned long long parsed;

	if (!text[0] || text[0] < '0' || text[0] > '9')
		return false;
	errno = 0;
	parsed = strtoull(text, &end, 10);
	if (errno || *end || parsed > max)
		return false;
	*value = parsed;
	return true;
}

static bool valid_options(const struct options *o)
{
	return o->video && o->video[0] && o->subdev && o->subdev[0] &&
		o->repeat && o->input <= 1 && o->samples <= 8 &&
		(o->repeat == 1 || o->frames) &&
		((o->samples && o->output && o->output[0]) ||
		 (!o->samples && !o->output));
}

static void usage(FILE *file, const char *program)
{
	fprintf(file,
		"Usage: %s -v VIDEO -d SENSOR_SUBDEV [options]\n"
		"  -i, --input 0|1    VIN input index (default 0)\n"
		"  -n, --frames N     buffers per cycle (default unlimited)\n"
		"  -t, --seconds N    total wall-time limit, including signal wait\n"
		"  -r, --repeat N     stop/start cycles (default 1; >1 needs -n)\n"
		"  -s, --samples N    save the first N good buffers, 1..8\n"
		"  -o, --output DIR   existing directory, required with -s\n"
		"  -h, --help\n", program);
}

static bool waiting_error(int error)
{
	return error == ENOLINK || error == ENOLCK || error == ERANGE ||
		error == EAGAIN;
}

static bool query_due(bool streaming, double now, double next_query)
{
	return !streaming && now >= next_query;
}

static bool frame_timed_out(bool streaming, double now, double last_frame)
{
	return streaming && now - last_frame >= 3;
}

static bool same_timings(const struct v4l2_dv_timings *a,
			 const struct v4l2_dv_timings *b)
{
	const struct v4l2_bt_timings *x = &a->bt, *y = &b->bt;
	uint64_t delta = x->pixelclock > y->pixelclock ?
		x->pixelclock - y->pixelclock : y->pixelclock - x->pixelclock;

	/* Measured clocks can jitter; 500 ppm still separates 59.94 from 60 Hz. */
	return a->type == b->type && x->width == y->width &&
		x->height == y->height && x->interlaced == y->interlaced &&
		x->polarities == y->polarities && delta <= x->pixelclock / 2000 &&
		x->hfrontporch == y->hfrontporch && x->hsync == y->hsync &&
		x->hbackporch == y->hbackporch &&
		x->vfrontporch == y->vfrontporch && x->vsync == y->vsync &&
		x->vbackporch == y->vbackporch &&
		x->il_vfrontporch == y->il_vfrontporch &&
		x->il_vsync == y->il_vsync && x->il_vbackporch == y->il_vbackporch;
}

static bool timing_interval(const struct v4l2_dv_timings *timings,
			    struct v4l2_fract *interval)
{
	const struct v4l2_bt_timings *bt = &timings->bt;
	uint64_t width = (uint64_t)bt->width + bt->hfrontporch + bt->hsync + bt->hbackporch;
	uint64_t height = (uint64_t)bt->height + bt->vfrontporch + bt->vsync + bt->vbackporch;

	if (timings->type != V4L2_DV_BT_656_1120 || bt->interlaced ||
	    !bt->width || !bt->height || !bt->pixelclock ||
	    bt->pixelclock > UINT32_MAX || width > UINT32_MAX / height)
		return false;
	interval->numerator = (uint32_t)(width * height);
	interval->denominator = (uint32_t)bt->pixelclock;
	return true;
}

static bool valid_format(const struct v4l2_pix_format_mplane *f)
{
	unsigned int p;

	if (f->pixelformat != V4L2_PIX_FMT_NV16 || !f->width || !f->height ||
	    !f->num_planes || f->num_planes > VIDEO_MAX_PLANES)
		return false;
	for (p = 0; p < f->num_planes; ++p)
		if (!f->plane_fmt[p].sizeimage || !f->plane_fmt[p].bytesperline)
			return false;
	return true;
}

static bool valid_payload(size_t length, uint32_t bytesused, uint32_t offset)
{
	return offset <= bytesused && bytesused <= length;
}

static void record_frame(struct stats *s, const struct v4l2_buffer *buffer)
{
	int64_t timestamp = (int64_t)buffer->timestamp.tv_sec * 1000000 +
		buffer->timestamp.tv_usec;
	uint32_t delta = buffer->sequence - s->sequence;

	if (s->have_previous) {
		if (!delta)
			++s->repeats;
		else if (delta < UINT32_MAX / 2)
			s->drops += delta - 1;
		else
			++s->resets;
		if (timestamp < s->timestamp_us)
			++s->backwards;
	}
	s->have_previous = true;
	s->sequence = buffer->sequence;
	s->timestamp_us = timestamp;
	s->timestamp_flags = buffer->flags &
		(V4L2_BUF_FLAG_TIMESTAMP_MASK | V4L2_BUF_FLAG_TSTAMP_SRC_MASK);
	++s->frames;
	if (buffer->flags & V4L2_BUF_FLAG_ERROR)
		++s->bad;
}

static int open_capture(struct capture *c, const struct options *o)
{
	struct v4l2_capability capability = { 0 };
	unsigned int input = (unsigned int)o->input;
	uint32_t caps;
	int error;

	if (c->fd >= 0)
		return 0;
	c->fd = open(o->video, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (c->fd < 0)
		return -1;
	if (xioctl(c->fd, VIDIOC_QUERYCAP, &capability))
		goto fail;
	caps = capability.capabilities & V4L2_CAP_DEVICE_CAPS ?
		capability.device_caps : capability.capabilities;
	if (!(caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) || !(caps & V4L2_CAP_STREAMING)) {
		fprintf(stderr, "Video node needs multi-planar capture and streaming\n");
		errno = ENOTSUP;
		goto fail;
	}
	if (xioctl(c->fd, VIDIOC_S_INPUT, &input))
		goto fail;
	return 0;
fail:
	error = errno;
	close(c->fd);
	c->fd = -1;
	errno = error;
	return -1;
}

static int stop_capture(struct capture *c)
{
	enum v4l2_buf_type type = TYPE;
	struct v4l2_requestbuffers request = { .type = TYPE,
		.memory = V4L2_MEMORY_MMAP };
	unsigned int i, p;
	int result = 0;

	if (c->streaming && xioctl(c->fd, VIDIOC_STREAMOFF, &type)) {
		perror("VIDIOC_STREAMOFF");
		result = -1;
	}
	c->streaming = false;
	if (c->buffers) {
		for (i = 0; i < c->count; ++i)
			for (p = 0; p < c->format.num_planes; ++p)
				if (c->buffers[i][p].length &&
				    munmap(c->buffers[i][p].address,
					   c->buffers[i][p].length)) {
					perror("munmap");
					result = -1;
				}
		free(c->buffers);
		c->buffers = NULL;
	}
	if (c->requested && xioctl(c->fd, VIDIOC_REQBUFS, &request)) {
		perror("VIDIOC_REQBUFS(0)");
		result = -1;
	}
	c->requested = false;
	c->count = 0;
	/* BSP STREAMOFF removes links; close balances S_INPUT's pipeline open. */
	if (c->fd >= 0) {
		if (close(c->fd)) {
			perror("close video");
			result = -1;
		}
		c->fd = -1;
	}
	return result;
}

static int queue_buffer(struct capture *c, unsigned int index)
{
	struct v4l2_plane planes[VIDEO_MAX_PLANES] = { 0 };
	struct v4l2_buffer buffer = { .type = TYPE, .memory = V4L2_MEMORY_MMAP,
		.index = index, .length = c->format.num_planes, .m.planes = planes };

	return xioctl(c->fd, VIDIOC_QBUF, &buffer);
}

static int start_capture(struct capture *c, struct v4l2_dv_timings *timings)
{
	struct v4l2_format format = { .type = TYPE };
	struct v4l2_requestbuffers request = { .count = 4, .type = TYPE,
		.memory = V4L2_MEMORY_MMAP };
	struct v4l2_streamparm parm = { .type = TYPE };
	enum v4l2_buf_type type = TYPE;
	unsigned int i, p;

	if (!timing_interval(timings, &parm.parm.capture.timeperframe)) {
		errno = ERANGE;
		return -1;
	}
	if (xioctl(c->fd, VIDIOC_S_DV_TIMINGS, timings))
		return -1;
	parm.parm.capture.capturemode = V4L2_MODE_VIDEO;
	if (xioctl(c->fd, VIDIOC_S_PARM, &parm))
		return -1;
	format.fmt.pix_mp.width = timings->bt.width;
	format.fmt.pix_mp.height = timings->bt.height;
	format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV16;
	format.fmt.pix_mp.field = V4L2_FIELD_NONE;
	if (xioctl(c->fd, VIDIOC_S_FMT, &format) ||
	    xioctl(c->fd, VIDIOC_REQBUFS, &request))
		return -1;
	c->requested = true;
	c->count = request.count;
	/* VIN fills payload and stride in queue_setup, reached through REQBUFS. */
	if (xioctl(c->fd, VIDIOC_G_FMT, &format))
		return -1;
	c->format = format.fmt.pix_mp;
	if (!valid_format(&c->format) || c->format.width != timings->bt.width ||
	    c->format.height != timings->bt.height) {
		fprintf(stderr, "Driver did not return native-size NV16 with valid planes\n");
		errno = EPROTO;
		return -1;
	}
	printf("format=%ux%u NV16 planes=%u\n", c->format.width,
	       c->format.height, c->format.num_planes);
	for (p = 0; p < c->format.num_planes; ++p)
		printf("  plane=%u bytesperline=%u sizeimage=%u\n", p,
		       c->format.plane_fmt[p].bytesperline,
		       c->format.plane_fmt[p].sizeimage);
	if (!c->count || c->count > 64) {
		errno = EPROTO;
		return -1;
	}
	c->buffers = calloc(c->count, sizeof(*c->buffers));
	if (!c->buffers)
		return -1;
	for (i = 0; i < c->count; ++i) {
		struct v4l2_plane planes[VIDEO_MAX_PLANES] = { 0 };
		struct v4l2_buffer buffer = { .type = TYPE,
			.memory = V4L2_MEMORY_MMAP, .index = i,
			.length = c->format.num_planes, .m.planes = planes };

		if (xioctl(c->fd, VIDIOC_QUERYBUF, &buffer))
			return -1;
		if (buffer.length != c->format.num_planes) {
			errno = EPROTO;
			return -1;
		}
		for (p = 0; p < c->format.num_planes; ++p) {
			struct mapping *m = &c->buffers[i][p];

			if (planes[p].length < c->format.plane_fmt[p].sizeimage) {
				errno = EPROTO;
				return -1;
			}
			m->length = planes[p].length;
			m->address = mmap(NULL, m->length, PROT_READ | PROT_WRITE,
					  MAP_SHARED, c->fd, planes[p].m.mem_offset);
			if (m->address == MAP_FAILED) {
				m->length = 0;
				return -1;
			}
		}
		if (queue_buffer(c, i))
			return -1;
	}
	if (xioctl(c->fd, VIDIOC_STREAMON, &type))
		return -1;
	c->streaming = true;
	return 0;
}

static int save_sample(const struct capture *c, const struct v4l2_buffer *buffer,
		       const char *directory, uint64_t sample)
{
	unsigned int p;

	for (p = 0; p < c->format.num_planes; ++p) {
		char path[PATH_MAX];
		const struct v4l2_plane *plane = &buffer->m.planes[p];
		const char *data = c->buffers[buffer->index][p].address;
		size_t left = plane->bytesused - plane->data_offset;
		int fd, length;

		length = snprintf(path, sizeof(path),
			"%s/rk628-%ld-%" PRIu64 "-%ux%u-p%u.nv16", directory,
			(long)getpid(), sample, c->format.width, c->format.height, p);
		if (length < 0 || (size_t)length >= sizeof(path)) {
			errno = ENAMETOOLONG;
			return -1;
		}
		fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
		if (fd < 0)
			return -1;
		data += plane->data_offset;
		while (left) {
			ssize_t written = write(fd, data, left);

			if (written < 0 && errno == EINTR && !interrupted)
				continue;
			if (written <= 0) {
				int error = written ? errno : EIO;

				close(fd);
				unlink(path);
				errno = error;
				return -1;
			}
			data += written;
			left -= (size_t)written;
		}
		if (close(fd)) {
			int error = errno;

			unlink(path);
			errno = error;
			return -1;
		}
		printf("sample=%s sequence=%u bytes=%u offset=%u\n", path,
		       buffer->sequence, plane->bytesused, plane->data_offset);
	}
	return 0;
}

static int read_frame(struct capture *c, struct stats *stats,
		      const struct options *o, uint64_t *samples)
{
	struct v4l2_plane planes[VIDEO_MAX_PLANES] = { 0 };
	struct v4l2_buffer buffer = { .type = TYPE, .memory = V4L2_MEMORY_MMAP,
		.length = c->format.num_planes, .m.planes = planes };
	unsigned int p;

	if (xioctl(c->fd, VIDIOC_DQBUF, &buffer))
		return errno == EAGAIN || errno == EINTR ? 0 : -1;
	if (buffer.index >= c->count || buffer.length != c->format.num_planes) {
		errno = EPROTO;
		return -1;
	}
	for (p = 0; p < c->format.num_planes; ++p)
		if (!valid_payload(c->buffers[buffer.index][p].length,
				   planes[p].bytesused, planes[p].data_offset) ||
		    (!(buffer.flags & V4L2_BUF_FLAG_ERROR) &&
		     planes[p].bytesused == planes[p].data_offset)) {
			errno = EPROTO;
			return -1;
		}
	record_frame(stats, &buffer);
	if (!(buffer.flags & V4L2_BUF_FLAG_ERROR) && *samples < o->samples) {
		if (save_sample(c, &buffer, o->output, *samples))
			return -2;
		++*samples;
	}
	if (queue_buffer(c, buffer.index))
		return -1;
	return buffer.flags & V4L2_BUF_FLAG_ERROR ? 2 : 1;
}

static int drain_events(int fd)
{
	struct v4l2_event event;
	int changed = 0;
	unsigned int count;

	for (count = 0; count < 64; ++count) {
		memset(&event, 0, sizeof(event));
		if (xioctl(fd, VIDIOC_DQEVENT, &event))
			return errno == EAGAIN || errno == ENOENT || errno == EINTR ? changed : -1;
		if (event.type == V4L2_EVENT_SOURCE_CHANGE ||
		    event.type == SIGNAL_LOST) {
			printf("event=%s sequence=%u timestamp=%lld.%09ld\n",
			       event.type == SIGNAL_LOST ? "SIGNAL_LOST" : "SOURCE_CHANGE",
			       event.sequence, (long long)event.timestamp.tv_sec,
			       (long)event.timestamp.tv_nsec);
			changed = 1;
		}
	}
	return changed;
}

static void report(const struct stats *s, double elapsed, double interval,
		   uint64_t previous, uint64_t starts, uint64_t cycles)
{
	printf("elapsed=%.1f frames=%" PRIu64 " fps=%.2f average=%.2f drops=%" PRIu64
	       " repeats=%" PRIu64 " resets=%" PRIu64 " bad=%" PRIu64
	       " backwards=%" PRIu64 " sequence=%u timestamp=%" PRId64
	       "us flags=0x%x starts=%" PRIu64 " cycles=%" PRIu64 "\n",
	       elapsed, s->frames, interval > 0 ? (s->frames - previous) / interval : 0,
	       elapsed > 0 ? s->frames / elapsed : 0, s->drops, s->repeats,
	       s->resets, s->bad, s->backwards, s->sequence, s->timestamp_us,
	       s->timestamp_flags, starts, cycles);
}

int main(int argc, char **argv)
{
	static const struct option long_options[] = {
		{ "video", required_argument, NULL, 'v' },
		{ "subdev", required_argument, NULL, 'd' },
		{ "input", required_argument, NULL, 'i' },
		{ "frames", required_argument, NULL, 'n' },
		{ "seconds", required_argument, NULL, 't' },
		{ "repeat", required_argument, NULL, 'r' },
		{ "samples", required_argument, NULL, 's' },
		{ "output", required_argument, NULL, 'o' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};
	struct options o = { .repeat = 1 };
	struct capture capture = { .fd = -1 };
	struct stats stats = { 0 };
	struct v4l2_dv_timings pending = { 0 };
	struct sigaction action = { .sa_handler = on_signal };
	uint64_t starts = 0, cycles = 0, cycle_frames = 0, samples = 0, previous = 0;
	double start, last_report, last_frame, next_query, stable_at = 0;
	unsigned int errors = 0;
	int subdev = -1, result = EXIT_FAILURE, option;
	bool have_pending = false, first_frame_pending = false;

	while ((option = getopt_long(argc, argv, "v:d:i:n:t:r:s:o:h", long_options, NULL)) != -1) {
		uint64_t *destination = NULL, max = UINT64_MAX;

		switch (option) {
		case 'v': o.video = optarg; break;
		case 'd': o.subdev = optarg; break;
		case 'o': o.output = optarg; break;
		case 'i': destination = &o.input; max = 1; break;
		case 'n': destination = &o.frames; break;
		case 't': destination = &o.seconds; max = UINT32_MAX; break;
		case 'r': destination = &o.repeat; break;
		case 's': destination = &o.samples; max = 8; break;
		case 'h': usage(stdout, argv[0]); return EXIT_SUCCESS;
		default: usage(stderr, argv[0]); return EXIT_FAILURE;
		}
		if (destination && (!number(optarg, max, destination) ||
				    (!*destination && option != 'i'))) {
			fprintf(stderr, "Invalid numeric option: %s\n", optarg);
			return EXIT_FAILURE;
		}
	}
	if (optind != argc || !valid_options(&o)) {
		usage(stderr, argv[0]);
		return EXIT_FAILURE;
	}
	if (o.output) {
		struct stat status;

		if (stat(o.output, &status) || !S_ISDIR(status.st_mode)) {
			fprintf(stderr, "Output must be an existing directory: %s\n", o.output);
			return EXIT_FAILURE;
		}
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) {
		perror("sigaction");
		return EXIT_FAILURE;
	}
	if (open_capture(&capture, &o)) {
		perror("open/QUERYCAP/S_INPUT");
		goto out;
	}
	subdev = open(o.subdev, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (subdev < 0) {
		perror(o.subdev);
		goto out;
	}
	{
		struct v4l2_event_subscription subscription = {
			.type = V4L2_EVENT_SOURCE_CHANGE };

		if (xioctl(subdev, VIDIOC_SUBSCRIBE_EVENT, &subscription)) {
			perror("subscribe SOURCE_CHANGE");
			goto out;
		}
		subscription.type = SIGNAL_LOST;
		if (xioctl(subdev, VIDIOC_SUBSCRIBE_EVENT, &subscription)) {
			perror("subscribe SIGNAL_LOST");
			goto out;
		}
	}
	printf("video=%s subdev=%s input=%u; waiting for stable HDMI timings\n",
	       o.video, o.subdev, (unsigned int)o.input);
	start = last_report = last_frame = next_query = now_seconds();
	result = EXIT_SUCCESS;
	while (!interrupted) {
		struct pollfd fds[2] = {
			{ .fd = capture.streaming ? capture.fd : -1, .events = POLLIN },
			{ .fd = subdev, .events = POLLPRI }
		};
		double now = now_seconds();
		int event, frame, error = 0;
		const char *reason = NULL;

		if (o.seconds && now - start >= o.seconds)
			break;
		if (poll(fds, 2, query_due(capture.streaming, now, next_query) ? 0 : 250) < 0) {
			if (errno == EINTR)
				continue;
			perror("poll");
			result = EXIT_FAILURE;
			break;
		}
		now = now_seconds();
		if ((fds[0].revents | fds[1].revents) & (POLLNVAL | POLLHUP) ||
		    (fds[1].revents & POLLERR)) {
			fprintf(stderr, "Capture/subdev endpoint unavailable\n");
			result = EXIT_FAILURE;
			break;
		}
		event = fds[1].revents & POLLPRI ? drain_events(subdev) : 0;
		if (event < 0) {
			error = errno;
			reason = "VIDIOC_DQEVENT";
		} else if (event) {
			if (stop_capture(&capture)) {
				result = EXIT_FAILURE;
				break;
			}
			have_pending = false;
			next_query = now + 0.25;
		}
		if (!error && query_due(capture.streaming, now, next_query)) {
			struct v4l2_dv_timings timing = { 0 };

			next_query = now + 0.25;
			if (open_capture(&capture, &o)) {
				error = errno;
				reason = "open/QUERYCAP/S_INPUT";
			} else if (xioctl(capture.fd, VIDIOC_QUERY_DV_TIMINGS, &timing)) {
				error = errno;
				reason = "VIDIOC_QUERY_DV_TIMINGS";
			} else if (!have_pending || !same_timings(&pending, &timing)) {
				pending = timing;
				have_pending = true;
			} else {
				have_pending = false;
				stable_at = now_seconds();
				if (start_capture(&capture, &timing)) {
					error = errno;
					reason = "configure/STREAMON";
				} else {
					++starts;
					stats.have_previous = false;
					first_frame_pending = true;
					last_frame = now_seconds();
					printf("streamon=%" PRIu64 " pixelclock=%" PRIu64
					       " stable_elapsed=%.3f streamon_elapsed=%.3f\n",
					       starts, (uint64_t)timing.bt.pixelclock,
					       stable_at - start, last_frame - start);
				}
			}
		}
		if (!error && capture.streaming && !event && (fds[0].revents & POLLIN)) {
			frame = read_frame(&capture, &stats, &o, &samples);
			if (frame == -2) {
				perror("save sample");
				result = EXIT_FAILURE;
				break;
			}
			if (frame < 0) {
				error = errno;
				reason = "DQBUF/QBUF";
			} else if (frame == 2) {
				error = EIO;
				reason = "V4L2_BUF_FLAG_ERROR";
			} else if (frame == 1) {
				last_frame = now_seconds();
				if (first_frame_pending) {
					printf("first_frame start=%" PRIu64 " elapsed=%.3f"
					       " stable_to_frame_ms=%.1f sequence=%u timestamp=%"
					       PRId64 "us\n", starts, last_frame - start,
					       (last_frame - stable_at) * 1000,
					       stats.sequence, stats.timestamp_us);
					first_frame_pending = false;
				}
				++cycle_frames;
				errors = 0;
				if (o.frames && cycle_frames >= o.frames) {
					if (stop_capture(&capture)) {
						result = EXIT_FAILURE;
						break;
					}
					++cycles;
					printf("cycle=%" PRIu64 " complete buffers=%" PRIu64 "\n",
					       cycles, cycle_frames);
					cycle_frames = 0;
					if (cycles >= o.repeat)
						break;
					have_pending = false;
					next_query = now_seconds() + 0.25;
				}
			}
		}
		if (!error && capture.streaming &&
		    ((fds[0].revents & POLLERR) ||
		     frame_timed_out(capture.streaming, now_seconds(), last_frame))) {
			error = EIO;
			reason = fds[0].revents & POLLERR ? "video poll" : "3s frame timeout";
		}
		if (error) {
			if (stop_capture(&capture)) {
				result = EXIT_FAILURE;
				break;
			}
			have_pending = false;
			next_query = now_seconds() + 0.25;
			if (!waiting_error(error)) {
				fprintf(stderr, "%s: %s (recovery %u/%u)\n", reason,
					strerror(error), ++errors, MAX_RETRIES);
				if (errors >= MAX_RETRIES) {
					result = EXIT_FAILURE;
					break;
				}
			}
		}
		now = now_seconds();
		if (now - last_report >= 1) {
			report(&stats, now - start, now - last_report, previous, starts, cycles);
			previous = stats.frames;
			last_report = now;
		}
	}
	report(&stats, now_seconds() - start, now_seconds() - last_report,
	       previous, starts, cycles);
	if (!stats.frames && !interrupted) {
		fprintf(stderr, "No captured buffers within the requested run\n");
		result = EXIT_FAILURE;
	}
out:
	if (stop_capture(&capture))
		result = EXIT_FAILURE;
	if (subdev >= 0)
		close(subdev);
	return result;
}
