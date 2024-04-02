/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright(c) 2020 - 2023 Allwinner Technology Co.,Ltd. All rights reserved. */
/*
 *   Copyright (C) 2015- Henrik Austad <haustad@cisco.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */
#ifndef _TSN_INTERNAL_H_
#define _TSN_INTERNAL_H_
#include <sunxi-tsn.h>

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>

/* TODO:
 * - hide tsn-structs and provide handlers
 * - decouple config/net from core
 */

struct avtpdu_header;
struct tsn_link;
struct tsn_shim_ops;

#define IS_TSN_FRAME(x) (ntohs(x) == ETH_P_TSN)
#define IS_PTP_FRAME(x) (ntohs(x) == ETH_P_1588)
#define IS_1Q_FRAME(x)	(ntohs(x) == ETH_P_8021Q)

/**
 * tsn_add_link - create and add a new link to the system
 *
 * Note: this will not enable the link, just allocate most of the data
 * required for the link. One notable exception being the buffer as we
 * can modify the buffersize before we start the link.
 *
 * @param nic : the nic the link is tied to
 * @returns the new link
 */
struct tsn_link *tsn_create_and_add_link(struct tsn_nic *nic);

/**
 * tsn_get_stream_ids - write all current Stream IDs into the page.
 *
 * @param page the page to write into
 * @param len size of page
 * @returns the number of bytes written
 */
ssize_t tsn_get_stream_ids(char *page, ssize_t len);

/**
 * tsn_find_by_stream_id - given a sid, find the corresponding link
 *
 * @param sid stream_id
 * @returns tsn_link struct or NULL if not found
 */
struct tsn_link *tsn_find_by_stream_id(u64 sid);

/**
 * tsn_readd_link - make sure a link is moved to the correct bucket when
 * stream_id is updated
 *
 * @link the TSN link
 * @old_key previous key for which it can be located in the hashmap
 *
 */
void tsn_readd_link(struct tsn_link *link, u64 old_key);

/**
 * tsn_remove_link: cleanup and remove from internal storage
 *
 * @link: the link to be removed
 */
void tsn_remove_link(struct tsn_link *link);

/**
 * tsn_remove_and_free_link: remove link and remove it from the list
 *
 * @param: the link to completely remove
 */
void tsn_remove_and_free_link(struct tsn_link *link);

/**
 * tsn_set_shim_ops - tie a shim to a link
 *
 * This will just set the shim-ops in the link.
 *
 * @link: active link
 * @shim_ops: the shim to associate with this link
 * @return: 0 on success, negative on error
 */
int tsn_set_shim_ops(struct tsn_link *link, struct tsn_shim_ops *shim_ops);

/**
 * tsn_prepare_link - make link ready for usage
 *
 * Caller is happy with the different knobs, this will create the link and start
 * pushing the data.
 *
 * Requirement:
 *	- callback registered
 *	- State set to either Talker or Listener
 *
 * @param active link
 * @return 0 on success, negative on error
 */
int tsn_prepare_link(struct tsn_link *link);
int tsn_teardown_link(struct tsn_link *link);

/**
 * tsn_set_external_buffer - force an update of the buffer
 *
 * This will cause tsn_core to use an external buffer. If external
 * buffering is already in use, this has the effect of forcing an update
 * of the buffer.
 *
 * This will cause tsn_core to swap buffers. The current buffer is
 * returned and the new is used in place.
 *
 * Note: If the new buffer is NULL or buffer_size is less than
 * max_payload_size, the result can be interesting (by calling this
 * function, you claim to know what you are doing and should pass sane
 * values).
 *
 * This can also be used if you need to resize the buffer in use.
 *
 * Core will continue to use the tsn_shim_swap when the new buffer is
 * full.
 *
 * @param link current link owning the buffer
 * @param buffer new buffer to use
 * @param buffer_size size of new buffer
 * @return old buffer
 */
void *tsn_set_external_buffer(struct tsn_link *link, void *buffer,
				size_t buffer_size);

/**
 * tsn_buffer_write_net - write data *into* link->buffer from the network layer
 *
 * Used by tsn_net and will typicall accept very small pieces of data.
 *
 * @param link  the link associated with the stream_id in the frame
 * @param src   pointer to data in buffer
 * @param bytes number of bytes to copy
 * @return number of bytes copied into the buffer
 */
int tsn_buffer_write_net(struct tsn_link *link, void *src, size_t bytes);

/**
 * tsn_buffer_read_net - read data from link->buffer and give to network layer
 *
 * When we send a frame, we grab data from the buffer and add it to the
 * sk_buff->data, this is primarily done by the Tx-subsystem in tsn_net
 * and is typically done in small chunks
 *
 * @param link current link that holds the buffer
 * @param buffer the buffer to copy into, must be at least of size bytes
 * @param bytes number of bytes.
 *
 * Note that this routine does NOT CARE about channels, samplesize etc,
 * it is a _pure_ copy that handles ringbuffer wraps etc.
 *
 * This function have side-effects as it will update internal tsn_link
 * values and trigger refill() should the buffer run low.
 *
 * @return Bytes copied into link->buffer, negative value upon error.
 */
int tsn_buffer_read_net(struct tsn_link *link, void *buffer, size_t bytes);

/**
 * tsn_core_running(): test if the link is running
 *
 * By running, we mean that it is configured and a proper shim has been
 * loaded. It does *not* mean that we are currently pushing data in any
 * direction, see tsn_net_buffer_disabled() for this
 *
 * @param struct tsn_link active link
 * @returns 1 if core is running
 */
static inline int tsn_core_running(struct tsn_list *list)
{
	if (list)
		return atomic_read(&list->running);
	return 0;
}

/**
 * _tsn_buffer_used - how much of the buffer is filled with valid data
 *
 * - assumes link->running in state running
 * - will ignore change changed state
 *
 * We write to head, read from tail.
 */
static inline size_t _tsn_buffer_used(struct tsn_link *link)
{
	return (link->head - link->tail) % link->used_buffer_size;
}

/* -----------------------------
 * ConfigFS handling
 */
int tsn_configfs_init(struct tsn_list *tlist);
void tsn_configfs_exit(struct tsn_list *tlist);

/* -----------------------------
 * TSN Header
 */

struct avtpdu_header *avtph;
static inline size_t tsnh_len(void)
{
	/* include 802.1Q tag */
	return sizeof(*avtph);
}

static inline u16 tsnh_len_all(void)
{
	return (u16)tsnh_len() + VLAN_ETH_HLEN;
}

static inline u16 tsnh_frame_len(struct tsn_link *link)
{
	if (!link)
		return 0;
	pr_info("max_payload_size=%u, shim_header_size=%u, tsnh_len_all()=%u\n",
		link->max_payload_size, link->shim_header_size, tsnh_len_all());
	return link->max_payload_size + link->shim_header_size + tsnh_len_all();
}

static inline u16 tsnh_data_len(struct avtpdu_header *header)
{
	if (!header)
		return 0;
	return ntohs(header->sd_len);
}

/**
 * tsnh_payload_size_valid - if the entire payload is within size-limit
 *
 * Ensure that max_payload_size and shim_header_size is within acceptable limits
 *
 * We need both values to calculate the payload size when reserving
 * bandwidth, but only payload-size when instructing the shim to copy
 * out data for us.
 *
 * @param max_payload_size requested payload to send in each frame (upper limit)
 * @return 0 on invalid, 1 on valid
 */
static inline int tsnh_payload_size_valid(u16 max_payload_size,
						u16 shim_hdr_size)
{
	/* VLAN_ETH_ZLEN	64 */
	/* VLAN_ETH_FRAME_LEN	1518 */
	u32 framesize = max_payload_size + tsnh_len_all() + shim_hdr_size;

	return framesize >= VLAN_ETH_ZLEN && framesize <= VLAN_ETH_FRAME_LEN;
}

/**
 * tsnh_validate_du_header - basic header validation
 *
 * This expects the parameters to be present and the link-lock to be
 * held.
 *
 * @param header header to verify
 * @param link owner of stream
 * @param socket_buffer
 * @return 0 on valid, negative on invalid/error
 */
int tsnh_validate_du_header(struct tsn_link *link, struct avtp_ch *ch,
				struct sk_buff *skb);

/**
 * tsnh_assemble_du - assemble header and copy data from buffer
 *
 * It expects tsn-lock to be held when called
 *
 * This function will initialize the header and pass final init to
 * shim->assemble_header before copying data into the buffer.
 *
 * It assumes that 'bytes' is a sane value, i.e. that it is a valid
 * multiple of number of channels, sample size etc.
 *
 * @param link   Current TSN link, also holds the buffer
 *
 * @param header header to assemble for data
 *
 * @param bytes  Number of bytes to send in this frame
 *
 * @param ts_pres_ns current for when the frame should be presented or
 *                   considered valid by the receiving end. In
 *                   nanoseconds since epoch, will be converted to gPTP
 *                   compatible timestamp.
 *
 * @return 0 on success, negative on error
 */
int tsnh_assemble_du(struct tsn_link *link, struct avtpdu_header *header,
			 size_t bytes, u64 ts_pres_ns, bool cd);

/**
 * tsnh_handle_du - handle incoming data and store to media-buffer
 *
 * This assumes that the frame actually belongs to the link and that it
 * has passed basic validation. It expects the link-lock to be held.
 *
 * @param link    Link associated with stream_id
 * @param header  Header of incoming frame
 * @return number of bytes copied to buffer or negative on error
 */
int tsnh_handle_du(struct tsn_link *link, struct avtp_ch *ch);

static inline struct avtp_ch *tsnh_ch_from_skb(struct sk_buff *skb)
{
	if (!skb)
		return NULL;
	if (!IS_TSN_FRAME(eth_hdr(skb)->h_proto))
		return NULL;

	return (struct avtp_ch *)skb->data;
}

/**
 * tsn_net_add_rx - add Rx handler for all NICs listed
 *
 * @param list tsn_list to add Rx handler to
 * @return 0 on success, negative on error
 */
int tsn_net_add_rx(struct tsn_list *list);

/**
 * tsn_net_remove_rx - remove Rx-handlers for all tsn_nics
 *
 * Go through all NICs and remove those Rx-handlers we have
 * registred. If someone else has added an Rx-handler to the NIC, we do
 * not touch it.
 *
 * @param list list of all tsn_nics (with links)
 */
void tsn_net_remove_rx(struct tsn_list *list);

/**
 * tsn_net_open_tx - prepare all capable links for Tx
 *
 * This will prepare all NICs for Tx, and those marked as 'capable'
 * will be initialized with DMA regions. Note that this is not the final
 * step for preparing for Tx, it is only when we have active links that
 * we know how much bandwidth we need and then can set the appropriate
 * idleSlope params etc.
 *
 * @tlist: list of all available card
 * @return: negative on error, on success the number of prepared NICS
 *          are returned.
 */
int tsn_net_prepare_tx(struct tsn_list *tlist);

/**
 * tsn_net_disable_tx - disable Tx on card
 *
 * This frees DMA-memory from capable NICs
 *
 * @param tsn_list: link to all available NICs used by TSN
 */
void tsn_net_disable_tx(struct tsn_list *tlist);

/**
 * tsn_net_close - close down link properly
 *
 * @param struct tsn_link * active link to close down
 */
void tsn_net_close(struct tsn_link *link);

/**
 * tsn_net_send_set - send a set of frames
 *
 * We want to assemble a number of sk_buffs at a time and ship them off
 * in a single go and then go back to sleep. Pacing should be done by
 * hardware, or if we are in in_debug, we don't really care anyway
 *
 * @param link        : current TSN-link
 * @param num         : the number of frames to create
 * @param ts_base_ns  : base timestamp for when the frames should be
 *		        considered valid
 * @param ts_delta_ns : time between each frame in the set
 * @param sent_bytes  : upon return is set to the number of bytes sent
 *
 * @returns then number of frames sent or negative on error
 */
int tsn_net_send_set(struct tsn_link *link, size_t num, u64 ts_base_ns,
			u64 ts_delta_ns, size_t *sent_bytes);

#endif	/* _TSN_INTERNAL_H_ */
