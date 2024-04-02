# Makefile for the focaltech touchscreen drivers.

obj-$(CONFIG_TOUCHSCREEN_FTS) += focaltech_touch.o

focaltech_touch-objs	+=  focaltech_core.o
focaltech_touch-objs	+=  focaltech_ex_fun.o
focaltech_touch-objs	+=  focaltech_ex_mode.o
focaltech_touch-objs	+=  focaltech_gesture.o
focaltech_touch-objs	+=  focaltech_esdcheck.o
focaltech_touch-objs   +=  focaltech_point_report_check.o


focaltech_touch-objs	+=  focaltech_flash.o
#obj-y   +=  focaltech_flash/
focaltech_touch-objs	+=  focaltech_flash/focaltech_upgrade_ft3427.o


focaltech_touch-objs   +=  focaltech_i2c.o

