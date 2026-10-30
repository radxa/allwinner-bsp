CC = $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Werror

.PHONY: all clean
all: rk628_capture

rk628_capture: rk628_capture.c ../sunxi_camera_v2.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f rk628_capture
