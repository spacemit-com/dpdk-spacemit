/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025 Spacemit Corporation.
 */

#include <rte_mbuf.h>
#include <rte_io.h>
#include <ethdev_driver.h>

#include "k1xmac_ethdev.h"
#include "k1xmac_pmd_logs.h"
#include "k1xmac_regs.h"

static int k1xmac_rx_frame_status(struct k1xmac_private *priv, struct emac_rx_desc *dsc)
{
	/* if last descritpor isn't set, so we drop it*/
	if (!dsc->LastDescriptor) {
		K1XMAC_PMD_INFO("rx LD bit isn't set, drop it.\n");
		return frame_discard;
	}

	/*
	 * A Frame that is less than 64-bytes (from DA thru the FCS field)
	 * is considered as Runt Frame.
	 * Most of the Runt Frames happen because of collisions.
	 */
	if (dsc->ApplicationStatus & EMAC_RX_FRAME_RUNT) {
		K1XMAC_PMD_INFO("rx frame less than 64.\n");
		return frame_discard;
	}

	/*
	 * When the frame fails the CRC check,
	 * the frame is assumed to have the CRC error
	 */
	if (dsc->ApplicationStatus & EMAC_RX_FRAME_CRC_ERR) {
		K1XMAC_PMD_INFO("rx frame crc error\n");
		return frame_discard;
	}

	/*
	 * When the length of the frame exceeds
	 * the Programmed Max Frame Length
	 */
	if (dsc->ApplicationStatus & EMAC_RX_FRAME_MAX_LEN_ERR) {
		K1XMAC_PMD_INFO("rx frame too long\n");
		return frame_discard;
	}

	/*
	 * frame reception is truncated at that point and
	 * frame is considered to have Jabber Error
	 */
	if (dsc->ApplicationStatus & EMAC_RX_FRAME_JABBER_ERR) {
		K1XMAC_PMD_INFO("rx frame has been truncated\n");
		return frame_discard;
	}

	/* this bit is only for 802.3 Type Frames */
	if (dsc->ApplicationStatus & EMAC_RX_FRAME_LENGTH_ERR) {
		K1XMAC_PMD_INFO("rx frame length err for 802.3\n");
		return frame_discard;
	}

	if (dsc->FramePacketLength <= RTE_ETHER_CRC_LEN ||
	    dsc->FramePacketLength > priv->dma_buf_sz) {
		K1XMAC_PMD_INFO("rx frame len too small or too long\n");
		return frame_discard;
	}
	return frame_ok;
}

/* This function does enetfec_rx_queue processing. Dequeue packet from Rx queue
 * When update through the ring, just set the empty indicator.
 */
uint16_t
k1xmac_recv_pkts(void *rx_queue, struct rte_mbuf **rx_pkts,
		 uint16_t nb_pkts)
{
        struct k1xmac_rx_queue *rxq = (struct k1xmac_rx_queue *)rx_queue;
        struct rte_eth_dev *dev = &rte_eth_devices[rxq->port_id];
        struct rte_mempool *pool = rxq->pool;
	struct rte_mbuf *new_mbuf = NULL;
	struct rte_mbuf *rmb;
        uint16_t tail = rxq->rx_tail;
        uint16_t nb_rx = 0;
        uint16_t pkt_len = 0;
        struct rte_eth_stats *stats;
        struct emac_rx_desc *rx_desc;
        uint64_t   buf_phy_addr;
        dma_addr_t dma_addr;
#if defined(RTE_SOC_SPACEMIT_K1)
        void *new_data;
        int i;
#endif

        stats = &rxq->private->stats;

        while (nb_rx < nb_pkts) {
                rx_desc = &rxq->dma_rx_desc[tail];

                /* read the status of the incoming frame */
		if (rx_desc->OWN)
			break;
		rte_rmb();

                new_mbuf = rte_pktmbuf_alloc(pool);
		if (unlikely(new_mbuf == NULL)) {
                        K1XMAC_PMD_ERR("RX mbuf alloc failed port_id=%u "
					   "queue_id=%u\n",
					   (uint32_t)rxq->port_id, (uint32_t)rxq->queue_id);
			dev->data->rx_mbuf_alloc_failed++;
			stats->rx_nombuf++;
			break;
		}

#if defined(RTE_SOC_SPACEMIT_K1)
		new_data = rte_pktmbuf_mtod(new_mbuf, uint8_t *);
		for (i = 0; i < new_mbuf->buf_len; i += 64) {
			cbo_invalid((uint8_t *)new_data + i);
		}
		rte_io_mb();
#endif

                /* Process the incoming frame. */
                if (k1xmac_rx_frame_status(rxq->private, rx_desc) == frame_discard) {
			K1XMAC_PMD_ERR("rx error\n");
			stats->ierrors++;
                        tail = (tail + 1) % (rxq->nb_rx_desc);
                        rx_desc->OWN = 1;
			continue;
		}

                pkt_len = rte_cpu_to_le_16(rx_desc->FramePacketLength);
                stats->ipackets++;
                pkt_len -= RTE_ETHER_CRC_LEN;
                stats->ibytes += pkt_len;

		rmb = rxq->rx_mbuf[tail];

                /* retrieve the received mbuf */
                rmb->data_off = RTE_PKTMBUF_HEADROOM;
                rte_prefetch1((char *)rmb->buf_addr + rmb->data_off);
                rmb->nb_segs = 1;
                rmb->next = NULL;
                rmb->pkt_len = pkt_len;
                rmb->data_len = pkt_len;
                rmb->port = rxq->port_id;
                rmb->ol_flags =  0;	

		/* Store the mbuf address into the next entry of the array
		 * of returned packets.
		 */
                rx_pkts[nb_rx++] = rmb;

                /* refill the descriptor */
		rxq->rx_mbuf[tail] = new_mbuf;
                memset(rx_desc, 0, sizeof(struct emac_rx_desc));
		buf_phy_addr = rte_mbuf_data_iova_default(new_mbuf);
#ifndef RTE_SOC_SPACEMIT_K1
		dma_addr = rte_cpu_to_le_32(buf_phy_addr);
#else
		if (buf_phy_addr >= 0x80000000) {
			buf_phy_addr = buf_phy_addr - 0x80000000;
		}
		dma_addr = rte_cpu_to_le_32(buf_phy_addr);
#endif
		rx_desc->BufferAddr1 = dma_addr;
		rx_desc->BufferSize1 = rte_cpu_to_le_16(new_mbuf->buf_len);
		rx_desc->FirstDescriptor = 0;
		rx_desc->LastDescriptor = 0;
		if (++tail == rxq->nb_rx_desc) {
			rx_desc->EndRing = 1;
			tail = 0;
		}
		rte_wmb();
		rx_desc->OWN = 1;
	}

        rxq->rx_tail = tail;
        return nb_rx;
}

static inline void
k1xmac_xmit_pkt(struct k1xmac_private *priv, struct k1xmac_tx_queue *txq,
	     struct rte_mbuf *tx_pkt)
{
        struct rte_eth_stats *stats = &priv->stats;
        struct rte_mbuf *m_seg, *tx_mbuf;
        struct emac_tx_desc *txd;
        uint16_t desc_count = 0;
        const uint16_t nb_tx_desc = txq->nb_tx_desc;
        uint16_t tail;
        u32 len;
        uint64_t buf_phy_addr;

        tail = txq->tx_tail;

        for (m_seg = tx_pkt; m_seg; m_seg = m_seg->next) {
                len = m_seg->data_len;

		if (len == 0)
			break;

                txd = &txq->dma_tx_desc[tail];

                buf_phy_addr = rte_mbuf_data_iova(tx_pkt);
#ifdef RTE_SOC_SPACEMIT_K1
                if (buf_phy_addr >= 0x80000000) {
                        buf_phy_addr = buf_phy_addr - 0x80000000;
                }
#endif
                txd->BufferAddr1 = rte_cpu_to_le_32(buf_phy_addr);

#if defined(RTE_SOC_SPACEMIT_K1)
		{
			uint8_t *data;
			u32 i;
			data = rte_pktmbuf_mtod(m_seg, uint8_t *);
			for (i = 0; i < len; i += 64) {
				cbo_flush(data + i);
			}
		}
		rte_io_mb();
#endif

                /* Store mbuf for freeing later */
                tx_mbuf = txq->tx_mbuf[tail];
                if (tx_mbuf)
                        rte_pktmbuf_free_seg(tx_mbuf);
                txq->tx_mbuf[tail] = m_seg;

                txd->BufferSize1 = rte_cpu_to_le_16(len);
                if (m_seg == tx_pkt)
			txd->FirstSegment = 1;
		if (!m_seg->next)
			txd->LastSegment = 1;
		if (tail == nb_tx_desc - 1)
			txd->EndRing = 1;

		rte_wmb();
		txd->OWN = 1;

                tail = (tail + 1) % nb_tx_desc;
                desc_count++;

                stats->obytes += len;
        }

        txq->tx_tail += desc_count;
        txq->tx_tail %= nb_tx_desc;
        txq->tx_free -= desc_count;

        stats->opackets++;
}

static void
k1xmac_tx_clean(struct k1xmac_private *priv __rte_unused, struct k1xmac_tx_queue *txq)
{
	const uint16_t nb_tx_desc = txq->nb_tx_desc;
	const int tx_tail = txq->tx_tail % nb_tx_desc;
	int head = txq->tx_head;
	uint16_t desc_freed = 0;
        struct emac_tx_desc *txd;
        struct rte_mbuf *tx_mbuf;

	if (!txq)
		return;

	while (1) {
		txd = txq->dma_tx_desc + head;

		if (txd->OWN)
			break;

		tx_mbuf = txq->tx_mbuf[head];
		if (tx_mbuf) {
			rte_pktmbuf_free_seg(tx_mbuf);
			txq->tx_mbuf[head] = NULL;
		}

		head = (head + 1) % nb_tx_desc;
		desc_freed++;

		if (head == tx_tail)
			break;
	}

	txq->tx_free += desc_freed;
	txq->tx_head = head;
}

uint16_t
k1xmac_xmit_pkts(void *tx_queue, struct rte_mbuf **tx_pkts, uint16_t nb_pkts)
{
        struct k1xmac_tx_queue *txq = (struct k1xmac_tx_queue *)tx_queue;
        struct k1xmac_private *priv = txq->private;
        struct rte_mbuf *tx_pkt;
        uint16_t nb_tx;

        for (nb_tx = 0; nb_tx < nb_pkts; nb_tx++) {
                tx_pkt = *tx_pkts++;

                if (txq->tx_free < tx_pkt->nb_segs)
			break;

                /* Check mbuf is valid */
		if (tx_pkt->nb_segs == 0 || tx_pkt->pkt_len == 0 ||
		    (tx_pkt->nb_segs > 1 && tx_pkt->next == NULL))
			break;

                k1xmac_xmit_pkt(priv, txq, tx_pkt);
        }

        rte_wmb();

        /* start tx dma */
	if (nb_tx > 0)
		k1xmac_dma_start_transmit(priv);

	k1xmac_tx_clean(priv, txq);
        return nb_tx;
}
