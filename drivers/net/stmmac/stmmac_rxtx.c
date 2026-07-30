/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Spacemit Corporation.
 */

#include <rte_mbuf.h>
#include <rte_io.h>
#include <rte_malloc.h>
#include <ethdev_driver.h>
#include "stmmac_regs.h"
#include "common.h"
#include "stmmac_ethdev.h"
#include "stmmac_pmd_logs.h"
#include "dwmac4.h"
#include "dwmac4_dma.h"
#include "dwmac4_descs.h"

#define ETH_FCS_LEN	4

#define RDES3_PACKET_SIZE_MASK	GENMASK(14, 0)

/* This function does stmmac_rx_queue processing. Dequeue packet from Rx queue
 * When update through the ring, just set the empty indicator.
 */
uint16_t
stmmac_recv_pkts(void *rxq, struct rte_mbuf **rx_pkts, uint16_t nb_pkts)
{
	struct stmmac_rx_queue *rx_q = (struct stmmac_rx_queue *)rxq;
	struct rte_mempool *pool = rx_q->pool;
	struct rte_mbuf *mbuf, *new_mbuf = NULL;
	unsigned int entry;
	unsigned int pkt_len;
	int pkt_received = 0;
	void *data;
	struct rte_eth_stats *stats = &rx_q->private->stats;
	struct dma_desc *p;
	int status;
#ifdef RTE_SOC_SPACEMIT_K3
	int i;
	void *new_data;
#endif
	dma_addr_t hw_addr;

	/* check if managed by the DMA otherwise go ahead */
	while (pkt_received < nb_pkts) {
		entry = rx_q->cur_rx;
		p = rx_q->dma_rx + entry;
		/* read the status of the incoming frame */
		status = rte_le_to_cpu_32(p->des3);
		if (status & BIT(31))
			break;
		rte_rmb();

		mbuf = rx_q->rx_mbuf[entry];
		data = rte_pktmbuf_mtod(mbuf, void *);
#ifdef RTE_SOC_SPACEMIT_K3
		for (i = 0; i < STMMAC_ALIGN_LENGTH; i += 64) {
			cbo_invalid((uint8_t *)data + i);
		}
		rte_io_rmb();
#endif

		rte_prefetch0(data);
		pkt_len = status & RDES3_PACKET_SIZE_MASK;

		/*	If frame length is greater than skb buffer size
		 *	(preallocated during init) then the packet is
		 *	ignored
		 */
		if (unlikely(pkt_len > 1522) || unlikely(status & RDES3_CONTEXT_DESCRIPTOR)
		    || unlikely(status & RDES3_ERROR_SUMMARY)) {
			STMMAC_PMD_ERR("rx err, status 0x%x\n", status);
			stats->ierrors++;
			continue;
		}

		new_mbuf = rte_pktmbuf_alloc(pool);
		if (unlikely(new_mbuf == NULL)) {
			stats->rx_nombuf++;
			STMMAC_PMD_ERR("%s rx_nombuf\n", __func__);
			break;
		}
		hw_addr = rte_cpu_to_le_64(rte_pktmbuf_iova(new_mbuf));

#ifdef RTE_SOC_SPACEMIT_K3
		new_data = rte_pktmbuf_mtod(new_mbuf, void *);
		for (i = 0; i < STMMAC_ALIGN_LENGTH; i += 64) {
			cbo_flush((uint8_t *)new_data + i);
		}
#endif

		/* The zero-copy is always used for all the sizes
		 * in case of GMAC4 because it needs
		 * to refill the used descriptors, always.
		 */
		/* Process the incoming frame. */
		stats->ipackets++;
		pkt_len -= ETH_FCS_LEN;
		stats->ibytes += pkt_len;

		rte_pktmbuf_append(mbuf, pkt_len);
		mbuf->ol_flags = RTE_MBUF_F_RX_IP_CKSUM_GOOD;
		rx_pkts[pkt_received] = mbuf;
		pkt_received++;

		p->des3 = 0;
		p->des2 = 0;
		p->des1 = 0;
		p->des0 = 0;
		stmmac_set_desc_addr(rx_q->private, p, hw_addr);
		rte_wmb();
		stmmac_set_rx_owner(rx_q->private, p, 1);

		rx_q->rx_mbuf[entry] = new_mbuf;
		rx_q->cur_rx = STMMAC_GET_ENTRY(entry, rx_q->dma_rx_size);
	}

	if (pkt_received) {
		rx_q->rx_tail_addr = rx_q->dma_rx_phy +
				    (rx_q->cur_rx * sizeof(struct dma_desc));
		rte_io_wmb();
		stmmac_set_rx_tail_ptr(rx_q->private, rx_q->private->ioaddr_v, rx_q->rx_tail_addr, 0);
	}

	return pkt_received;
}

uint16_t
stmmac_xmit_pkts(void *tx_queue, struct rte_mbuf **tx_pkts, uint16_t nb_pkts)
{
	struct stmmac_tx_queue *tx_q  = (struct stmmac_tx_queue *)tx_queue;
	unsigned int entry, desc_size = sizeof(struct dma_desc);
	struct rte_eth_stats *stats = &tx_q->private->stats;
	struct rte_mbuf *mbuf;
	unsigned short buflen;
	unsigned int pkt_transmitted = 0;
	struct dma_desc *desc;
	int status, tx_st = 1;
#if defined(RTE_SOC_SPACEMIT_K3)
	void *txdata;
	uintptr_t i, offset;
#endif

	while (tx_st) {
		if (pkt_transmitted >= nb_pkts) {
			tx_st = 0;
			break;
		}

		entry = tx_q->cur_tx;
		desc = tx_q->dma_tx + entry;

		status = stmmac_tx_status(tx_q->private, stats, desc);
		/* Check if the descriptor is owned by the DMA */
		if (unlikely(status & tx_dma_own)) {
			break;
		}

		rte_rmb();

		if(unlikely(status & TDES3_ERROR_SUMMARY) || unlikely(status & TDES3_DEFERRED)) {
			STMMAC_PMD_ERR("tx err, status 0x%x\n", status);
			stats->oerrors++;
		}

		if (tx_q->tx_mbuf[entry]) {
			rte_pktmbuf_free(tx_q->tx_mbuf[entry]);
			tx_q->tx_mbuf[entry] = NULL;
		}

		desc->des3 = 0;
		desc->des2 = 0;
		desc->des1 = 0;
		desc->des0 = 0;

		mbuf = *(tx_pkts);
		tx_pkts++;
		tx_q->cur_tx = STMMAC_GET_ENTRY(entry, tx_q->dma_tx_size);

		/* Make sure descriptor fields are read after reading
		 * the own bit.
		 */
		/* Set buffer length and buffer pointer */
		buflen = rte_pktmbuf_pkt_len(mbuf);
		stats->opackets++;
		stats->obytes += buflen;

		if (mbuf->nb_segs > 1) {
			STMMAC_PMD_ERR("SG not supported\n");
			return -1;
		}

		/* Save mbuf pointer */
		tx_q->tx_mbuf[entry] = mbuf;
		pkt_transmitted++;

#ifdef RTE_SOC_SPACEMIT_K3
		txdata = rte_pktmbuf_mtod(mbuf, void *);
		offset = (uintptr_t)txdata & 63ULL;
		for (i = 0; i < (buflen + offset); i += 64) {
			cbo_flush((uint8_t *)txdata - offset + i);
		}
#endif
		rte_wmb();

		stmmac_set_desc_addr(tx_q->private, desc, rte_cpu_to_le_64(rte_pktmbuf_iova(mbuf)));
		/* Prepare the first descriptor setting the OWN bit too */
		stmmac_prepare_tx_desc(tx_q->private, desc, 1, buflen,
				       1, tx_q->private->mode, 1, 1, buflen);
		rte_io_wmb();
	}

	if (pkt_transmitted) {
		tx_q->tx_tail_addr = tx_q->dma_tx_phy + (tx_q->cur_tx * desc_size);
		rte_io_wmb();
		stmmac_set_tx_tail_ptr(tx_q->private, tx_q->private->ioaddr_v, tx_q->tx_tail_addr, 0);
	}

	return pkt_transmitted;
}
