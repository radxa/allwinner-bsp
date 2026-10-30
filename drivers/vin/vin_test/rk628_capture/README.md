# RK628F VIN capture / hotplug acceptance

面向 A7A / aw2511 Linux 6.6 的最小 V4L2 NV16 内存采集程序，不含显示或 ISP RAW 处理。必须明确提供视频节点及其对应的 RK628 sensor subdev，不能用示例节点名代替实际拓扑。

```sh
make                         # 本机编译，-Wall -Wextra -Werror
make clean
make CROSS_COMPILE=aarch64-linux-gnu-
```

使用板端实际路径设置两个变量：

```sh
VIDEO=/dev/video实际编号
SENSOR=/dev/v4l-subdev实际编号

# 输入源设为 4K30，持续 1 小时内存采集。
./rk628_capture -v "$VIDEO" -d "$SENSOR" -t 3600

# 每轮 30 个有效 buffer，停流、释放缓冲并关闭视频 fd 后重开，共 100 轮。
./rk628_capture -v "$VIDEO" -d "$SENSOR" -n 30 -r 100 -t 1800

# 期间完成 20 次 HDMI 拔插和 20 次 720p/1080p/4K 输入模式切换。
./rk628_capture -v "$VIDEO" -d "$SENSOR" -t 1800

# 仅保存整个运行过程最先收到的两个有效 buffer，其余仍在内存采集。
mkdir -p samples
./rk628_capture -v "$VIDEO" -d "$SENSOR" -n 300 -s 2 -o samples
```

`-i 0|1` 选择 VIN rear/front input，默认 `0`；必须与所给 sensor subdev 相匹配。每次打开视频节点后先 `QUERYCAP → VIDIOC_S_INPUT` 建立 VIN pipeline，再通过 video fd 调用 `VIDIOC_QUERY_DV_TIMINGS` / `VIDIOC_S_DV_TIMINGS`，这些接口在 `vin-video/vin_video.c` 中转调 sensor ops。事件通过 sensor subdev fd 订阅：`V4L2_EVENT_SOURCE_CHANGE` 与 `V4L2_EVENT_PRIVATE_START + 1`（`SIGNAL_LOST`）。不自动搜索或修改媒体拓扑。

程序用一个 `poll()` 同时处理视频与事件。收到 SOURCE_CHANGE / SIGNAL_LOST 或取帧错误时，先执行 `STREAMOFF → munmap → REQBUFS(0) → close(video)`，再 `open → QUERYCAP → S_INPUT` 后进入 timing 查询；显式 `-n/-r` 重复启停和配置/查询失败的恢复也走同一释放、重开流程。当前 BSP 的 `STREAMOFF` 会关闭上游 media links 并进入 `VIN_LPM`，直接再次 `S_FMT` 无法沿 graph 到达 RK628/MIPI/CSI；仅在旧 fd 上重复 `S_INPUT` 又会累加 pipeline 上电引用，因此必须先关闭视频 fd，每个新 fd 只执行一次 `S_INPUT`。打开、能力检查或选择输入失败会关闭已获得的 fd 并保留原始错误；sensor subdev 事件 fd 全程保留，不重复订阅。

相隔至少 250 ms 的两次有效 timing 一致后执行 `S_DV_TIMINGS → S_PARM → S_FMT → REQBUFS → G_FMT → QUERYBUF/MMAP/QBUF → STREAMON`。现有 VIN 仅在 `REQBUFS` 触发的 `queue_setup()` 中更新 `payload/bytesperline`，因此必须在分配之后读取 `G_FMT`，避免拿到零或上次分辨率的布局。同分辨率时钟允许 500 ppm 测量误差；只接受驱动支持的逐行扫描。`S_PARM` 使用总像素数 / pixelclock 的非零帧间隔，不强制输入帧率。

稳定开流后只取帧、处理事件和检查 3 秒无帧 watchdog，不同步查询 timing：板端测得单次 `QUERY_DV_TIMINGS` 约阻塞 0.2 秒，会耗尽 60 Hz 下的四个采集 buffer。查询仅在等待/恢复状态执行，未增加线程或缓冲数量。若事件遗漏，驱动在失信号/尺寸改变时先停 CSI 并使格式失效，程序随后由无帧 watchdog 停流并重新查询。`DQEVENT` 的 `EAGAIN`、`ENOENT` 和 `EINTR` 均视为本次事件读取结束；当前 6.6 core 的空队列会返回 `ENOENT`。

`ENOLINK`、`ENOLCK`、`ERANGE`、`EAGAIN` 进入等待状态，其他异常最多连续恢复 5 次，有效帧到达后清零。默认一直运行；`-n` 是每轮有效 buffer 上限，`-r` 大于 1 时必须有 `-n`，`-t` 是包含等待在内的全程秒数上限。限时结束时没有任何 buffer 则返回失败；SIGINT/SIGTERM 执行释放后退出。`-t` 可以先于所有重复轮次结束，通过最终 `cycles` 判断轮次是否完成。

采集固定请求 `V4L2_PIX_FMT_NV16` 和 multi-planar API；内存平面数量、`bytesperline`、`sizeimage` 均读取 `G_FMT`，映射长度和 offset 使用 `QUERYBUF`，保存长度使用 `DQBUF.bytesused - data_offset`。当前 VIN 的 NV16 是一个连续内存平面，不能假定为 NV16M 的两平面。程序核对返回格式、尺寸、平面数及 payload 边界；样本保留驱动的原始 padding，不自行拆分或重排。每个 sample 的每个内存平面各写一个 `.nv16` 文件，文件名包含 PID、样本号、尺寸、平面号，并用 `O_EXCL` 防止覆盖。`-s` 限制为 1..8，必须配合已有的 `-o` 目录。

保存样本会在采集循环中同步写盘。验收丢帧和反复启停时不要使用 `-s`；单独检查样本后，再进行纯内存采集。

本次 A733 1920×1080 NV16 样本返回 `bytesperline=3840`、`sizeimage=4177920`；VIN 报告的是合并后的 16bpp 行大小，Y/UV 各行实际为 1920 字节。`vin_set_addr()` 使用 `ALIGN(width,16)*height`，因此 UV offset 为 `2073600`，有效图像共 `4147200` 字节，其余 `30720` 字节为尾部 padding。不要用 `sizeimage/2` 作为 UV offset，也不要在 Y 与 UV 之间插入按 1088 行计算的 padding。

每秒输出累计 `frames`、区间 `fps`、全程平均帧率、`drops`、重复/重置的 sequence、错误 buffer 数、倒退 timestamp 数，以及最近 buffer 的 sequence、时间戳和 timestamp flags。每次开流另外输出稳定 timing、STREAMON、首个有效帧的相对时间；`first_frame.stable_to_frame_ms` 是程序第二次一致的有效 timing 查询到有效帧之间的间隔，可用于检查稳定后 5 秒内恢复目标。`frames` 包含驱动标记错误的 buffer，`bad` 单列；错误 buffer 不计入 `-n`，不保存样本并触发有界恢复。重开流时重新建立 sequence/timestamp 基线，不把重启清零误算成大量丢帧。`starts` 包括热插拔自动恢复，`cycles` 仅统计显式 `-n/-r` 完成的轮次。

板端验收应保留完整日志并确认：

- 输入不存在时等待，无伪造 640x480 timing；接入后出现有效帧及合理 fps。
- 4K30 持续采集 1 小时；检查 `drops`、`bad`、`repeats`、`backwards`，结合输入源及驱动日志解释异常。
- 完成 20 次 HDMI 拔插和 20 次 720p/1080p/4K 输入切换；拔出后停流，稳定后 5 秒内恢复有效帧，`starts` 增加且尺寸随输入变化，不重启程序或系统。
- `-n 30 -r 100` 最终 `cycles=100`；Ctrl-C 和 SIGTERM 后可立即再次启动同节点。
- 如保存少量 sample，结合已打印的布局检查图像、色彩及 padding；持续采集本身不做磁盘写入。
