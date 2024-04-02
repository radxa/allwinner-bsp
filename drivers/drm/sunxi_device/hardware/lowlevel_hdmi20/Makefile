# SPDX-License-Identifier: GPL-2.0-only

hdmidir = sunxi_device/hardware/lowlevel_hdmi20

# dw module.
obj_low_hdmi += $(hdmidir)/dw_dev.o
obj_low_hdmi += $(hdmidir)/dw_avp.o
obj_low_hdmi += $(hdmidir)/dw_edid.o
obj_low_hdmi += $(hdmidir)/dw_fc.o
obj_low_hdmi += $(hdmidir)/dw_i2cm.o
obj_low_hdmi += $(hdmidir)/dw_mc.o
obj_low_hdmi += $(hdmidir)/dw_phy.o

# phy model select. TODO!!!
obj_low_hdmi += $(hdmidir)/phy_aw.o
obj_low_hdmi += $(hdmidir)/phy_snps.o
obj_low_hdmi += $(hdmidir)/phy_inno.o

# cec module
ifeq ($(CONFIG_AW_HDMI20_CEC),y)
obj_low_hdmi += $(hdmidir)/dw_cec.o
endif # CONFIG_AW_HDMI20_CEC

# hdcp module
ifeq ($(CONFIG_AW_HDMI20_HDCP14),y)
obj_low_hdmi += $(hdmidir)/dw_hdcp.o
endif # CONFIG_AW_HDMI20_HDCP14

ifeq ($(CONFIG_AW_HDMI20_HDCP22),y)
obj_low_hdmi += $(hdmidir)/dw_hdcp.o
obj_low_hdmi += $(hdmidir)/dw_hdcp22.o
obj_low_hdmi += $(hdmidir)/esm_lib/esm_host_lib_init.o
obj_low_hdmi += $(hdmidir)/esm_lib/esm_host_lib_log.o
obj_low_hdmi += $(hdmidir)/esm_lib/esm_host_lib_auth.o
obj_low_hdmi += $(hdmidir)/esm_lib/esm_host_lib_code.o
obj_low_hdmi += $(hdmidir)/esm_lib/esm_host_lib_exceptions.o
obj_low_hdmi += $(hdmidir)/esm_lib/esm_host_lib_mailbox.o
obj_low_hdmi += $(hdmidir)/esm_lib/esm_host_lib_status.o
endif # CONFIG_AW_HDMI20_HDCP22
