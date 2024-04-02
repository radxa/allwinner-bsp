/* SPDX-License-Identifier: GPL-2.0-only */
/*******************************************************************************
 * Header File to describe Normal/enhanced descriptor functions used for RING
 * and CHAINED modes.
 *
 * Copyright(C) 2011  STMicroelectronics Ltd
 *
 * It defines all the functions used to handle the normal/enhanced
 * descriptors in case of the DMA is configured to work in chained or
 * in ring mode.
 *
 *
 * Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#ifndef __DESC_COM_H__
#define __DESC_COM_H__

/* Specific functions used for Ring mode */

/* Enhanced descriptors */
static inline void ehn_desc_rx_set_on_ring(struct dma_desc *p, int end,
						int bfsize)
{
	if (bfsize == BUF_SIZE_16KiB)
		p->des1 |= cpu_to_le32((BUF_SIZE_8KiB
				<< ERDES1_BUFFER2_SIZE_SHIFT)
				& ERDES1_BUFFER2_SIZE_MASK);

	if (end)
		p->des1 |= cpu_to_le32(ERDES1_END_RING);
}

static inline void enh_desc_end_tx_desc_on_ring(struct dma_desc *p, int end)
{
	if (end)
		p->des0 |= cpu_to_le32(ETDES0_END_RING);
	else
		p->des0 &= cpu_to_le32(~ETDES0_END_RING);
}

static inline void ndesc_end_tx_desc_on_ring(struct dma_desc *p, int end)
{
	if (end)
		p->des1 |= cpu_to_le32(TDES1_END_RING);
	else
		p->des1 &= cpu_to_le32(~TDES1_END_RING);
}

/* Specific functions used for Chain mode */

/* Enhanced descriptors */
static inline void ehn_desc_rx_set_on_chain(struct dma_desc *p)
{
	p->des1 |= cpu_to_le32(ERDES1_SECOND_ADDRESS_CHAINED);
}

static inline void enh_desc_end_tx_desc_on_chain(struct dma_desc *p)
{
	p->des0 |= cpu_to_le32(ETDES0_SECOND_ADDRESS_CHAINED);
}

static inline void enh_set_tx_desc_len_on_chain(struct dma_desc *p, int len)
{
	p->des1 |= cpu_to_le32(len & ETDES1_BUFFER1_SIZE_MASK);
}

/* Normal descriptors */
static inline void ndesc_rx_set_on_chain(struct dma_desc *p, int end)
{
	p->des1 |= cpu_to_le32(RDES1_SECOND_ADDRESS_CHAINED);
}

static inline void ndesc_tx_set_on_chain(struct dma_desc *p)
{
	p->des1 |= cpu_to_le32(TDES1_SECOND_ADDRESS_CHAINED);
}

static inline void norm_set_tx_desc_len_on_chain(struct dma_desc *p, int len)
{
	p->des1 |= cpu_to_le32(len & TDES1_BUFFER1_SIZE_MASK);
}
#endif /* __DESC_COM_H__ */
