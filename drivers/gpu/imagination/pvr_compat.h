/* SPDX-License-Identifier: GPL-2.0-only OR MIT */

#ifndef PVR_COMPAT_H
#define PVR_COMPAT_H

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/xarray.h>

#include <drm/drm_exec.h>
#include <drm/gpu_scheduler.h>
#include <linux/dma-fence.h>
#include <linux/sizes.h>

/*
 * kmalloc_obj() family was added in v7.0.
 * kzalloc_obj(P) allocates sizeof(P) bytes, so callers pass *ptr.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
#define kzalloc_obj(p)			kzalloc(sizeof(p), GFP_KERNEL)
#define kzalloc_objs(p, n)		kcalloc((n), sizeof(p), GFP_KERNEL)
#define kvmalloc_objs(p, n, gfp)	kvmalloc_array((n), sizeof(p), (gfp))
#endif

/* mem_is_zero() was added in v6.12. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
static inline bool mem_is_zero(const void *s, size_t n)
{
	return memchr_inv(s, 0, n) == NULL;
}
#endif

/*
 * drm_exec_init() gained an initial-object-count argument in v6.8.
 * The function-like macro is not expanded recursively, so the 2-argument
 * kernel helper is still called on older kernels.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
static inline void pvr_drm_exec_init(struct drm_exec *exec, uint32_t flags,
				     unsigned int nr)
{
	(void)nr;
	drm_exec_init(exec, flags);
}
#define drm_exec_init(exec, flags, nr)	pvr_drm_exec_init((exec), (flags), (nr))
#endif

/* Locked shmem vmap helpers were split out in v6.16; 6.6 already requires the resv. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 16, 0)
#define drm_gem_shmem_vmap_locked	drm_gem_shmem_vmap
#define drm_gem_shmem_vunmap_locked	drm_gem_shmem_vunmap
#endif

/* drm_sched_job_has_dependency() was added in v6.15. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
static inline bool drm_sched_job_has_dependency(struct drm_sched_job *job,
						struct dma_fence *fence)
{
	struct dma_fence *f;
	unsigned long index;

	xa_for_each(&job->dependencies, index, f) {
		if (f == fence)
			return true;
	}

	return false;
}
#endif

/*
 * drm_sched_init_args landed in v6.15. Older kernels take positional arguments,
 * and pre-v6.8 also lacks submit_wq / credit_limit.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)
struct drm_sched_init_args {
	const struct drm_sched_backend_ops *ops;
	struct workqueue_struct *submit_wq;
	struct workqueue_struct *timeout_wq;
	u32 num_rqs;
	u32 credit_limit;
	unsigned int hang_limit;
	long timeout;
	atomic_t *score;
	const char *name;
	struct device *dev;
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
#define drm_sched_init(sched, args)					\
	drm_sched_init((sched), (args)->ops, (args)->submit_wq,		\
		       (args)->num_rqs, (args)->credit_limit,		\
		       (args)->hang_limit, (args)->timeout,		\
		       (args)->timeout_wq, (args)->score,		\
		       (args)->name, (args)->dev)
#else
#define drm_sched_init(sched, args)					\
	drm_sched_init((sched), (args)->ops, 1, (args)->hang_limit,	\
		       (args)->timeout, (args)->timeout_wq,		\
		       (args)->score, (args)->name, (args)->dev)
#endif
#endif

/*
 * drm_sched_job_init() grew credits in v6.8 and drm_client_id in v6.17.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 17, 0)
#define pvr_sched_job_init(job, entity, credits, owner, client_id)	\
	drm_sched_job_init((job), (entity), (credits), (owner), (client_id))
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
#define pvr_sched_job_init(job, entity, credits, owner, client_id)	\
	drm_sched_job_init((job), (entity), (credits), (owner))
#else
#define pvr_sched_job_init(job, entity, credits, owner, client_id)	\
	drm_sched_job_init((job), (entity), (owner))
#endif

/*
 * drm_gpu_sched_stat was renamed in v6.17:
 * NOMINAL -> RESET, and NO_HANG was added.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 17, 0)
#ifndef DRM_GPU_SCHED_STAT_RESET
#define DRM_GPU_SCHED_STAT_RESET	DRM_GPU_SCHED_STAT_NOMINAL
#endif
#ifndef DRM_GPU_SCHED_STAT_NO_HANG
#define DRM_GPU_SCHED_STAT_NO_HANG	DRM_GPU_SCHED_STAT_NOMINAL
#endif
#endif

/* SZ_128G was added in v6.8. */
#ifndef SZ_128G
#define SZ_128G				_AC(0x2000000000, ULL)
#endif

#if IS_ENABLED(CONFIG_ARCH_SUN60IW2)
#define pwrseq_enable pwrseq_power_on
#define pwrseq_disable pwrseq_power_off
#define drm_gpuvm_bo_obtain_locked drm_gpuvm_bo_obtain
#define drm_gpuva_map drm_gpuva_map_img
#define drm_gpuva_link drm_gpuva_link_img
#define drm_gpuva_unmap drm_gpuva_unmap_img
#define drm_gpuva_unlink drm_gpuva_unlink_img
#define drm_gpuva_remap drm_gpuva_remap_img
#define drm_gpuva_find drm_gpuva_find_img
#define drm_gpuva_find_first drm_gpuva_find_first_img
#define PVR_TRACE_H
#define trace_pvr_job_submit_ioctl(...)
#define trace_pvr_job_done(...)
#define trace_pvr_job_create(...)
#define trace_pvr_job_submit_fw(...)
#endif

#endif /* PVR_COMPAT_H */
