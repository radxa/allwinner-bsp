# SPDX-License-Identifier: GPL-2.0-only
mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
mkfile_dir := $(dir $(mkfile_path))

ccflags-y += -I$(mkfile_dir)/../sunxi_device/hardware

# Although now only support v35x, new platform support should not add new dir
ccflags-y += -I$(mkfile_dir)/../sunxi_device/hardware/lowlevel_v35x/tcon
ccflags-y += -I$(mkfile_dir)/../sunxi_device/hardware/lowlevel_v35x/



obj-$(CONFIG_AW_DRM_PANEL) += sunxi_drm_panel.o
#sunxi_drm_panel-y += panels.o
sunxi_drm_panel-$(CONFIG_PANEL_SQ101D_Q5DI404_84H501H) += SQ101D_Q5DI404_84H501H.o
sunxi_drm_panel-$(CONFIG_PANEL_BP101WX1) += panel-bp101wx1.o
sunxi_drm_panel-$(CONFIG_PANEL_EDP_GENERAL) += edp_general_panel.o
