/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 * Allwinner SoCs display driver.
 *
 * Copyright (C) 2022 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/version.h>
#if LINUX_VERSION_CODE <= KERNEL_VERSION(6, 1, 0)
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#else
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_gem_dma_helper.h>
#endif
#include <drm/drm_vblank.h>
#include <drm/drm_client.h>
#include <linux/memblock.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 1, 0)
#include <drm/drm_blend.h>
#endif
#include "sunxi_fbdev.h"
#include "sunxi_drm_crtc.h"

struct fb_hw_info {
	struct fb_create_info create_info;
	unsigned int size;
	struct drm_client_dev client;
	struct work_struct free_wq;
	struct drm_client_buffer *buffer;
	struct display_channel_state state;
	u32 pseudo_palette[16];
};

static void fb_free_reserve_mem(struct work_struct *work)
{
	fb_debug_inf("%s finish\n", __FUNCTION__);
}

static void get_output_size(struct drm_device *dev, int hw_id, unsigned int *width, unsigned int *height)
{
	struct drm_crtc *crtc = drm_crtc_from_index(dev, hw_id);
	struct drm_crtc_state *state;

	*width = *height = 0;
	if (!crtc) {
		DRM_ERROR("%s, crtc %d not find\n", __func__, hw_id);
		return;
	}
	state = crtc->state;
	*width = state->adjusted_mode.hdisplay;
	*height = state->adjusted_mode.vdisplay;
}

static int fb_layer_config_init(struct fb_hw_info *info, struct fb_output_map *map)
{
	unsigned int hw_output_width = 0;
	unsigned int hw_output_height = 0;

	memset(&info->state, 0, sizeof(info->state));
	get_output_size(info->create_info.drm,
			  map->hw_display, &hw_output_width, &hw_output_height);
	info->state.base.crtc_x = 0;//TODO add ADAPTIVE_STRETCH
	info->state.base.crtc_y = 0;
	info->state.base.crtc_w = hw_output_width;
	info->state.base.crtc_h = hw_output_height;
	info->state.base.src_x = (0LL) << 16;
	info->state.base.src_y = (0LL) << 16;
	info->state.base.src_w = ((long long)info->create_info.width) << 16;
	info->state.base.src_h = ((long long)info->create_info.height) << 16;
	info->state.base.alpha = 0xffff;
	info->state.base.pixel_blend_mode = DRM_MODE_BLEND_COVERAGE;
	info->state.base.rotation = DRM_MODE_ROTATE_0;
	info->state.base.normalized_zpos = 0;/* force minimum zpos */
	info->state.base.visible = true;
	info->state.eotf = DE_EOTF_BT709;
	info->state.color_space = DE_COLOR_SPACE_BT709;
	info->state.color_range = DE_COLOR_RANGE_0_255;
	return 0;
}

int platform_get_private_size(void)
{
	return sizeof(struct fb_hw_info);
}

int platform_update_fb_output(struct fb_hw_info *hw_info, const struct fb_var_screeninfo *var)
{
	struct drm_device *drm = hw_info->create_info.drm;
	unsigned int de = hw_info->create_info.map[0].hw_display;
	unsigned int channel = hw_info->create_info.map[0].hw_channel;

	hw_info->state.base.src_y = ((long long)var->yoffset) << 16;
	/* TODO: if dual output, use two thread to call sunxi_fbdev_plane_update,
	 *	and block until two thread finish
	 */
	sunxi_fbdev_plane_update(drm, de, channel, &hw_info->state);
	return 0;
}

int platform_fb_mmap(struct fb_hw_info *hw_info, struct vm_area_struct *vma)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
	struct drm_device *drm = hw_info->create_info.drm;

	if (drm->driver->gem_prime_mmap)
		return drm->driver->gem_prime_mmap(hw_info->buffer->gem, vma);
	else
		return -ENODEV;
#else
	return drm_gem_prime_mmap(hw_info->buffer->gem, vma);
#endif
}

struct dma_buf *platform_fb_get_dmabuf(struct fb_hw_info *hw_info)
{
	return NULL;
}

void *fb_map_kernel(unsigned long phys_addr, unsigned long size)
{
	int npages = PAGE_ALIGN(size) / PAGE_SIZE;
	struct page **pages = vmalloc(sizeof(struct page *) * npages);
	struct page **tmp;
	struct page *cur_page = phys_to_page(phys_addr);
	pgprot_t pgprot;
	void *vaddr = NULL;
	int i;

	if (!pages)
		return NULL;

	for (i = 0, tmp = pages; i < npages; i++)
		*(tmp++) = cur_page++;

	pgprot = pgprot_noncached(PAGE_KERNEL);
	vaddr = vmap(pages, npages, VM_MAP, pgprot);

	vfree(pages);
	return vaddr;
}

void *fb_map_kernel_cache(unsigned long phys_addr, unsigned long size)
{
	int npages = PAGE_ALIGN(size) / PAGE_SIZE;
	struct page **pages = vmalloc(sizeof(struct page *) * npages);
	struct page **tmp;
	struct page *cur_page = phys_to_page(phys_addr);
	pgprot_t pgprot;
	void *vaddr = NULL;
	int i;

	if (!pages)
		return NULL;

	for (i = 0, tmp = pages; i < npages; i++)
		*(tmp++) = cur_page++;

	pgprot = PAGE_KERNEL;
	vaddr = vmap(pages, npages, VM_MAP, pgprot);
	vfree(pages);
	return vaddr;
}

void Fb_unmap_kernel(void *vaddr)
{
	vunmap(vaddr);
}

int logo_display_file(struct fb_hw_info *info, unsigned long phy_addr)
{
	/* TODO */
	return 0;
}

int platform_fb_set_blank(struct fb_hw_info *hw_info, bool is_blank)
{
	/* TODO */
	return 0;
}

int platform_fb_init_finish(struct fb_hw_info *hw_info, const struct fb_var_screeninfo *var)
{
	platform_update_fb_output(hw_info, var);
	schedule_work(&hw_info->free_wq);
	return 0;
}

static int fb_fmt2_drm_fmt(enum fb_format fmt)
{
	if (fmt == ARGB8888)
		return DRM_FORMAT_ARGB8888;
	if (fmt == RGB888)
		return DRM_FORMAT_RGB888;
	WARN_ON(1);
	return DRM_FORMAT_ARGB8888;
}

int platform_fb_memory_alloc(struct fb_hw_info *hw_info, void **vir_addr, unsigned long *device_addr, unsigned int w, unsigned int h, int fmt)
{
	int ret;
	u64 addr;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 6, 0)
	struct iosys_map map;
#else
	struct dma_buf_map map;
#endif
#endif
#if LINUX_VERSION_CODE <= KERNEL_VERSION(6, 1, 0)
	struct drm_gem_cma_object *gem;
#else
	struct drm_gem_dma_object *gem;
#endif

	hw_info->buffer = drm_client_framebuffer_create(&hw_info->client, w, h, fb_fmt2_drm_fmt(fmt));
	if (IS_ERR(hw_info->buffer))
		return PTR_ERR(hw_info->buffer);

#if LINUX_VERSION_CODE <= KERNEL_VERSION(6, 1, 0)
	gem = drm_fb_cma_get_gem_obj(hw_info->buffer->fb, 0);
	if (gem) {
		addr = (u64)(gem->paddr) + hw_info->buffer->fb->offsets[0];
	}
#else
	gem = drm_fb_dma_get_gem_obj(hw_info->buffer->fb, 0);
	if (gem) {
		addr = (u64)(gem->dma_addr) + hw_info->buffer->fb->offsets[0];
	}
#endif
	if (!gem || !addr) {
		DRM_ERROR("Failed framebuffer alloc for fbdev fail\n");
		return -ENOMEM;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
	*vir_addr = drm_client_buffer_vmap(hw_info->buffer);
#else
	ret = drm_client_buffer_vmap(hw_info->buffer, &map);
	if (ret) {
		DRM_ERROR("drm_client_buffer_vmap fail\n");
		return -ENOMEM;
	}
	*vir_addr = map.vaddr;
#endif
	hw_info->state.base.fb = hw_info->buffer->fb;
	*device_addr = (unsigned long)addr;
	return 0;
}

int platform_fb_memory_free(struct fb_hw_info *info)
{
//TODO
	return 0;
}

int platform_fb_pan_display_post_proc(struct fb_hw_info *info)
{
/* nothing to do becase we have wait finish in platform_update_fb_out */
/*
	int hw_id = info->create_info.map[0].hw_display;
	struct drm_device *drm = info->create_info.drm;
	drm_wait_one_vblank(drm, hw_id);
*/
	return 0;
}

int platform_fb_init(struct fb_create_info *create, struct fb_hw_info *info, void **pseudo_palette)
{
	int ret;
	memcpy(&info->create_info, create, sizeof(*create));
	fb_layer_config_init(info, &create->map[0]);
	ret = drm_client_init(create->drm, &info->client, "sunxi_fbdev", NULL);
	if (ret) {
		DRM_ERROR("Failed to register client: %d\n", ret);
		return -ENOMEM;
	}
	drm_client_register(&info->client);
	INIT_WORK(&info->free_wq, fb_free_reserve_mem);
	*pseudo_palette = info->pseudo_palette;
	return 0;
}

int platform_fb_exit(void)
{
	return 0;
}
