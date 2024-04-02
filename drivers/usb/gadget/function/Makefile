# SPDX-License-Identifier: GPL-2.0
#
# USB peripheral controller drivers
#

# Gadget
ccflags-y				+= -I $(srctree)/drivers/usb/gadget
ccflags-y				+= -I $(srctree)/include/linux

#ifeq ($(CC),clang)
#ccflags-y += -fsanitize-blacklist=$(srctree)/$(src)/blacklist.txt
#endif

# USB Functions
obj-$(CONFIG_USB_SUNXI_CONFIGFS_F_HID)	+= usb_sunxi_f_hid.o

usb_sunxi_f_hid-objs := f_hid.o

ifeq ($(CC),clang)

ifdef CONFIG_ARM64
sunxi_f_hid_asm = hid_v8_clang.S

ifeq ($(CONFIG_USB_SUNXI_CONFIGFS_F_HID),m)
sunxi_f_hid_o := hid_v8_clang.o
else
obj-$(CONFIG_USB_SUNXI_CONFIGFS_F_HID) += hid_v8_clang.o
endif
endif

ifdef CONFIG_ARM
sunxi_f_hid_asm = hid_v7_clang.S

ifeq ($(CONFIG_USB_SUNXI_CONFIGFS_F_HID),m)
sunxi_f_hid_o := hid_v7_clang.o
else
obj-$(CONFIG_USB_SUNXI_CONFIGFS_F_HID) += hid_v7_clang.o
endif
endif

else

ifdef CONFIG_ARM64
sunxi_f_hid_asm = hid_v8.S

ifeq ($(CONFIG_USB_SUNXI_CONFIGFS_F_HID),m)
sunxi_f_hid_o := hid_v8.o
else
obj-$(CONFIG_USB_SUNXI_CONFIGFS_F_HID) += hid_v8.o
endif
endif

ifdef CONFIG_ARM
sunxi_f_hid_asm = hid_v7.S

ifeq ($(CONFIG_USB_SUNXI_CONFIGFS_F_HID),m)
sunxi_f_hid_o := hid_v7.o
else
obj-$(CONFIG_USB_SUNXI_CONFIGFS_F_HID) += hid_v7.o
endif
endif

endif #clang

usb_sunxi_f_hid-objs += $(sunxi_f_hid_o)
