# SPDX-License-Identifier: GPL-2.0-only
mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
mkfile_dir := $(dir $(mkfile_path))

ccflags-y += -I$(mkfile_dir)/sunxi_device/hardware

# Although now only support v35x, new platform support should not add new dir
ccflags-y += -I$(mkfile_dir)/sunxi_device/hardware/lowlevel_v35x/tcon
ccflags-y += -I$(mkfile_dir)/sunxi_device/hardware/lowlevel_v35x/
ccflags-y += -DDE_VERSION_V35X
include $(srctree)/$(obj)/sunxi_device/hardware/lowlevel_v35x/Makefile

obj-$(CONFIG_AW_DRM)        += sunxidrm.o

sunxidrm-y += sunxi_drm_drv.o sunxi_drm_crtc.o sunxi_drm_intf.o sunxi_drm_fbdev.o
sunxidrm-y += $(obj_low)

sunxidrm-$(CONFIG_AW_DRM_DE) += sunxi_device/sunxi_de_v35x.o
sunxidrm-$(CONFIG_AW_DRM_TCON) += sunxi_device/sunxi_tcon_v35x.o
sunxidrm-$(CONFIG_AW_DRM_TCON_TOP) += sunxi_device/sunxi_tcon_top.o
sunxidrm-$(CONFIG_AW_DRM_LCD) += sunxi_drm_lcd.o panel/panels.o
sunxidrm-$(CONFIG_AW_DRM_LCD_DSI) += sunxi_device/sunxi_dsi.o
sunxidrm-$(CONFIG_AW_DRM_LCD_DSI_COMBO_PHY) += sunxi_device/sunxi_dsi_combo_phy.o

# add HDMI
sunxidrm-$(CONFIG_AW_DRM_HDMI_TX) += sunxi_drm_hdmi.o
sunxidrm-$(CONFIG_AW_DRM_HDMI_TX) += sunxi_device/sunxi_hdmi.o
ccflags-$(CONFIG_AW_DRM_HDMI20) += -I$(mkfile_dir)/sunxi_device/hardware/lowlevel_hdmi20/
include $(srctree)/$(obj)/sunxi_device/hardware/lowlevel_hdmi20/Makefile
sunxidrm-$(CONFIG_AW_DRM_HDMI20) += $(obj_low_hdmi)

# edp
sunxidrm-$(CONFIG_AW_DRM_EDP) += sunxi_drm_edp.o
sunxidrm-$(CONFIG_AW_DRM_EDP) += sunxi_device/sunxi_edp.o
sunxidrm-$(CONFIG_AW_DRM_INNO_EDP13) += sunxi_device/hardware/lowlevel_edp/inno_edp13/inno_edp13.o

# panel
obj-$(CONFIG_AW_DRM_PANEL) += panel/
