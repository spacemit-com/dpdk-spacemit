/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (C) 2025 Spacemit Corporation.
 */

#include <ethdev_vdev.h>
#include <ethdev_driver.h>
#include <rte_io.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include "k1xmac_pmd_logs.h"
#include "k1xmac_uio.h"
#include "k1xmac_ethdev.h"
#include "k1xmac_regs.h"

#define K1XMAC_NAME_PMD                net_k1xmac

/* Supported Rx offloads */
static uint64_t dev_rx_offloads_sup =
		RTE_ETH_RX_OFFLOAD_CHECKSUM;

void print_register(struct k1xmac_private *priv, u32 reg)
{
        printf("REG:0x%x:0x%x\n",
               reg,
               emac_rd(priv, reg));
}

void registers_dump(struct k1xmac_private *priv)
{
	int i;

        for (i = 0; i < 16; i++) {
		printf("DMA:0x%x:0x%x\n",
		       DMA_CONFIGURATION + i * 4,
		       emac_rd(priv, DMA_CONFIGURATION + i * 4));
	}
	for (i = 0; i < 60; i++) {
		printf("MAC:0x%x:0x%x\n",
		       MAC_GLOBAL_CONTROL + i * 4,
		       emac_rd(priv, MAC_GLOBAL_CONTROL + i * 4));
	}
}

void k1xmac_dma_start_transmit(struct k1xmac_private *priv)
{
	emac_wr(priv, DMA_TRANSMIT_POLL_DEMAND, 0xFF);
}

/* Name		k1xmac_reset_hw
 * Arguments	priv : pointer to hardware data structure
 * Return	Status: 0 - Success;  non-zero - Fail
 * Description	TBDL
 */
static int k1xmac_reset_hw(struct k1xmac_private *priv)
{
	/* disable all the interrupts */
	emac_wr(priv, MAC_INTERRUPT_ENABLE, 0x0000);
	emac_wr(priv, DMA_INTERRUPT_ENABLE, 0x0000);

	/* disable transmit and receive units */
	emac_wr(priv, MAC_RECEIVE_CONTROL, 0x0000);
	emac_wr(priv, MAC_TRANSMIT_CONTROL, 0x0000);

	/* stop the DMA */
	emac_wr(priv, DMA_CONTROL, 0x0000);

	/* reset mac, statistic counters */
	emac_wr(priv, MAC_GLOBAL_CONTROL, 0x0018);

	emac_wr(priv, MAC_GLOBAL_CONTROL, 0x0000);
	return 0;
}

/* Name		k1xmac_init_hw
 * Arguments	pstHWData	: pointer to hardware data structure
 * Return	Status: 0 - Success;  non-zero - Fail
 * Description	TBDL
 * Assumes that the controller has previously been reset
 * and is in apost-reset uninitialized state.
 * Initializes the receive address registers,
 * multicast table, and VLAN filter table.
 * Calls routines to setup link
 * configuration and flow control settings.
 * Clears all on-chip counters. Leaves
 * the transmit and receive units disabled and uninitialized.
 */
static int k1xmac_init_hw(struct k1xmac_private *priv)
{
	u32 val = 0;

	/* opertation in the k1xmac_uio */
	//emac_enable_axi_single_id_mode(priv, 1);

	/* MAC Init
	 * disable transmit and receive units
	 */
	emac_wr(priv, MAC_RECEIVE_CONTROL, 0x0000);
	emac_wr(priv, MAC_TRANSMIT_CONTROL, 0x0000);

	/* enable mac address 1 filtering */
	emac_wr(priv, MAC_ADDRESS_CONTROL, MREGBIT_MAC_ADDRESS1_ENABLE);

	/* zero initialize the multicast hash table */
	emac_wr(priv, MAC_MULTICAST_HASH_TABLE1, 0x0);
	emac_wr(priv, MAC_MULTICAST_HASH_TABLE2, 0x0);
	emac_wr(priv, MAC_MULTICAST_HASH_TABLE3, 0x0);
	emac_wr(priv, MAC_MULTICAST_HASH_TABLE4, 0x0);

	emac_wr(priv, MAC_TRANSMIT_FIFO_ALMOST_FULL, 0x1f8);

	emac_wr(priv, MAC_TRANSMIT_PACKET_START_THRESHOLD, 1518);

	emac_wr(priv, MAC_RECEIVE_PACKET_START_THRESHOLD, 12);

	/* set emac rx mitigation frame count */
	val = 0x40 & MREGBIT_RECEIVE_IRQ_FRAME_COUNTER_MSK;

	/* set emac rx mitigation timeout */
	val |= ((0x78 * AXI_CLK_CYCLES_PER_US) << MREGBIT_RECEIVE_IRQ_TIMEOUT_COUNTER_OFST) &
		MREGBIT_RECEIVE_IRQ_TIMEOUT_COUNTER_MSK;

	/* enable emac rx irq mitigation */
	val |= MRGEBIT_RECEIVE_IRQ_MITIGATION_ENABLE;

	emac_wr(priv, DMA_RECEIVE_IRQ_MITIGATION_CTRL, val);

	emac_wr(priv, MAC_FC_CONTROL, MREGBIT_FC_DECODE_ENABLE);

	/* reset dma */
	emac_wr(priv, DMA_CONTROL, 0x0000);
        
	emac_wr(priv, DMA_CONFIGURATION, 0x01);
	rte_delay_ms(10);
	emac_wr(priv, DMA_CONFIGURATION, 0x00);
	rte_delay_ms(10);

	val = 0;
	val |= MREGBIT_STRICT_BURST;
	val |= MREGBIT_DMA_64BIT_MODE;

	val |= 1 << 5;
	emac_wr(priv, DMA_CONFIGURATION, val);

	return 0;
}

/* Name		k1xmac_start_tx_dma
 * Arguments	priv : pointer to driver private data structure
 * Return	none
 * Description	Configures the transmit unit of the device
 */
static void k1xmac_start_tx_dma(struct k1xmac_private *priv)
{
	u32 val;

	/* Tx Inter Packet Gap value and enable the transmit */
	val = emac_rd(priv, MAC_TRANSMIT_CONTROL);
	val &= (~MREGBIT_IFG_LEN);
	val |= MREGBIT_TRANSMIT_ENABLE;
	val |= MREGBIT_TRANSMIT_AUTO_RETRY;
	emac_wr(priv, MAC_TRANSMIT_CONTROL, val);

	emac_wr(priv, DMA_TRANSMIT_AUTO_POLL_COUNTER, 0x00);

	/* start tx dma */
	val = emac_rd(priv, DMA_CONTROL);
	val |= MREGBIT_START_STOP_TRANSMIT_DMA;
	emac_wr(priv, DMA_CONTROL, val);
}

static void k1xmac_stop_tx_dma(struct k1xmac_private *priv)
{
        u32 val;

	emac_wr(priv, MAC_TRANSMIT_CONTROL, 0x0000);

        val = emac_rd(priv, DMA_CONTROL);
        val &= ~MREGBIT_START_STOP_TRANSMIT_DMA;
        emac_wr(priv, DMA_CONTROL, val);
}

/* Name		k1xmac_configure_rx
 * Arguments	priv : pointer to driver private data structure
 * Return	none
 * Description	Configures the receive unit of the device
 */
static void k1xmac_start_rx_dma(struct k1xmac_private *priv)
{
	u32 val;

	/* enable the receive */
	val = emac_rd(priv, MAC_RECEIVE_CONTROL);
	val |= MREGBIT_RECEIVE_ENABLE;
	val |= MREGBIT_STORE_FORWARD;
	emac_wr(priv, MAC_RECEIVE_CONTROL, val);

	/* start rx dma */
	val = emac_rd(priv, DMA_CONTROL);
	val |= MREGBIT_START_STOP_RECEIVE_DMA;
	emac_wr(priv, DMA_CONTROL, val);
}

static void k1xmac_stop_rx_dma(struct k1xmac_private *priv)
{
        u32 val;
        
        emac_wr(priv, MAC_RECEIVE_CONTROL, 0x0000);

        val = emac_rd(priv, DMA_CONTROL);
        val &= ~MREGBIT_START_STOP_RECEIVE_DMA;
        emac_wr(priv, DMA_CONTROL, val);
}

static int
k1xmac_start(struct rte_eth_dev *dev)
{
	struct k1xmac_private *private = dev->data->dev_private;

	k1xmac_start_tx_dma(private);
        k1xmac_start_rx_dma(private);
	rte_delay_us(200);

	return 0;
}

static int
k1xmac_disable(struct k1xmac_private *priv)
{
        k1xmac_stop_rx_dma(priv);
        k1xmac_stop_tx_dma(priv);
        return 0;
}

static void
k1xmac_free_buffers(struct rte_eth_dev *dev)
{
	struct k1xmac_private *priv = dev->data->dev_private;
	unsigned int i, q;
	struct rte_mbuf *mbuf;
	struct k1xmac_rx_queue *rxq;
	struct k1xmac_tx_queue *txq;

	for (q = 0; q < dev->data->nb_rx_queues; q++) {
		rxq = priv->rx_queues[q];
		for (i = 0; i < rxq->nb_rx_desc; i++) {
			mbuf = rxq->rx_mbuf[i];
			rxq->rx_mbuf[i] = NULL;
			rte_pktmbuf_free(mbuf);
		}
	}

	for (q = 0; q < dev->data->nb_tx_queues; q++) {
		txq = priv->tx_queues[q];
		for (i = 0; i < txq->nb_tx_desc; i++) {
			mbuf = txq->tx_mbuf[i];
			txq->tx_mbuf[i] = NULL;
			rte_pktmbuf_free(mbuf);
		}
	}
}

static int
k1xmac_eth_configure(struct rte_eth_dev *dev)
{
	if (dev->data->dev_conf.rxmode.offloads & RTE_ETH_RX_OFFLOAD_KEEP_CRC)
		K1XMAC_PMD_ERR("PMD does not support KEEP_CRC offload.\n");

        return 0;
}

static int
k1xmac_eth_start(struct rte_eth_dev *dev)
{
        struct k1xmac_private *priv = dev->data->dev_private;
	int i;

        priv->dma_buf_sz = K1XMAC_MAX_RX_PKT_LEN;

        k1xmac_start(dev);
        dev->rx_pkt_burst = &k1xmac_recv_pkts;
	dev->tx_pkt_burst = &k1xmac_xmit_pkts;
	dev->data->dev_started = 1;

	for (i = 0; i < dev->data->nb_rx_queues; i++)
		dev->data->rx_queue_state[i] = RTE_ETH_QUEUE_STATE_STARTED;
	for (i = 0; i < dev->data->nb_tx_queues; i++)
		dev->data->tx_queue_state[i] = RTE_ETH_QUEUE_STATE_STARTED;

        return 0;
}

static int
k1xmac_eth_stop(struct rte_eth_dev *dev)
{
        struct k1xmac_private *private = dev->data->dev_private;
	struct k1xmac_rx_queue *rxq;
	struct k1xmac_tx_queue *txq;
	int i;

	k1xmac_disable(private);

	for (i = 0; i < dev->data->nb_rx_queues; i++) {
		K1XMAC_PMD_ERR("k1xmac_eth_stop rx queue.\n");
		rxq = private->rx_queues[i];
		rxq->rx_tail = 0;
		dev->data->rx_queue_state[i] = RTE_ETH_QUEUE_STATE_STOPPED;
	}
	for (i = 0; i < dev->data->nb_tx_queues; i++) {
		K1XMAC_PMD_ERR("k1xmac_eth_stop tx queue.\n");
		txq = private->tx_queues[i];
		txq->tx_tail = 0;
		dev->data->tx_queue_state[i] = RTE_ETH_QUEUE_STATE_STOPPED;
	}

	dev->data->dev_started = 0;
        return 0;
}

static int
k1xmac_eth_close(struct rte_eth_dev *dev)
{
	if (rte_eal_process_type() != RTE_PROC_PRIMARY)
		return 0;

        k1xmac_free_buffers(dev);
        return 0;
}

/* return 0 means link status changed, -1 means not changed */
static int
k1xmac_eth_link_update(struct rte_eth_dev *dev, int wait_to_complete __rte_unused)
{
        struct k1xmac_private *priv = dev->data->dev_private;
	struct rte_eth_link link;
	unsigned int lstatus = 1;
        u32 ctrl;

	memset(&link, 0, sizeof(struct rte_eth_link));

	link.link_status = lstatus;
	link.link_speed = RTE_ETH_SPEED_NUM_1G;
        link.link_duplex = RTE_ETH_LINK_FULL_DUPLEX;
	link.link_autoneg = RTE_ETH_LINK_AUTONEG;

        ctrl = emac_rd(priv, MAC_GLOBAL_CONTROL);
        ctrl |= MREGBIT_FULL_DUPLEX_MODE;
        ctrl &= ~MREGBIT_SPEED;
        ctrl |= MREGBIT_SPEED_1000M;
        emac_wr(priv, MAC_GLOBAL_CONTROL, ctrl);

	K1XMAC_PMD_INFO("Port (%d) link is %s\n", dev->data->port_id, "Up");

	return rte_eth_linkstatus_set(dev, &link);
}

static int
k1xmac_promiscuous_enable(struct rte_eth_dev *dev)
{
        struct k1xmac_private *priv = dev->data->dev_private;
        u32 val;

        priv->flags |= IFF_PROMISC;
        
        val = emac_rd(priv, MAC_ADDRESS_CONTROL);
        val |= MREGBIT_PROMISCUOUS_MODE;
        emac_wr(priv, MAC_ADDRESS_CONTROL, val);

        return 0;
}

static int
k1xmac_promiscuous_disable(struct rte_eth_dev *dev)
{
        struct k1xmac_private *priv = dev->data->dev_private;
        u32 val;

        priv->flags &= ~IFF_PROMISC;

        val = emac_rd(priv, MAC_ADDRESS_CONTROL);
        val &= ~MREGBIT_PROMISCUOUS_MODE;
        emac_wr(priv, MAC_ADDRESS_CONTROL, val);

        return 0;
}

static int
k1xmac_allmulticast_enable(struct rte_eth_dev *dev)
{
        struct k1xmac_private *priv = dev->data->dev_private;

	priv->flags |= IFF_ALLMULTI;

        emac_wr(priv, MAC_MULTICAST_HASH_TABLE1, 0xffff);
        emac_wr(priv, MAC_MULTICAST_HASH_TABLE2, 0xffff);
        emac_wr(priv, MAC_MULTICAST_HASH_TABLE3, 0xffff);
        emac_wr(priv, MAC_MULTICAST_HASH_TABLE4, 0xffff);

        return 0;
}

static int
k1xmac_allmulticast_disable(struct rte_eth_dev *dev)
{
        struct k1xmac_private *priv = dev->data->dev_private;

	priv->flags &= ~IFF_ALLMULTI;
	
        emac_wr(priv, MAC_MULTICAST_HASH_TABLE1, 0x0);
        emac_wr(priv, MAC_MULTICAST_HASH_TABLE2, 0x0);
        emac_wr(priv, MAC_MULTICAST_HASH_TABLE3, 0x0);
        emac_wr(priv, MAC_MULTICAST_HASH_TABLE4, 0x0);

        return 0;
}

/* Set a MAC change in hardware. */
static int
k1xmac_set_mac_address(struct rte_eth_dev *dev,
		    struct rte_ether_addr *addr)
{
        struct k1xmac_private *priv = dev->data->dev_private;

        writel(((addr->addr_bytes[1] << 8) | addr->addr_bytes[0]), 
                priv->ioaddr_v + MAC_ADDRESS1_HIGH);
	writel(((addr->addr_bytes[3] << 8) | addr->addr_bytes[2]), 
                priv->ioaddr_v + MAC_ADDRESS1_MED);
	writel(((addr->addr_bytes[5] << 8) | addr->addr_bytes[4]), 
                priv->ioaddr_v + MAC_ADDRESS1_LOW);

        rte_ether_addr_copy(addr, &dev->data->mac_addrs[0]);
        return 0;
} 

static int k1xmac_set_mtu(struct rte_eth_dev *dev, uint16_t mtu)
{
	struct k1xmac_private *priv = dev->data->dev_private;
	uint32_t frame_size;

	frame_size = mtu + RTE_ETHER_HDR_LEN + RTE_ETHER_CRC_LEN;
	if ((frame_size < RTE_ETHER_MIN_LEN) || (frame_size > K1XMAC_MAX_RX_PKT_LEN)) {
		K1XMAC_PMD_ERR("mtu %u is invalid setting\n", mtu);
		return -EINVAL;
	}

	priv->dma_buf_sz = frame_size;

	return 0;
}

static int
k1xmac_stats_get(struct rte_eth_dev *dev, struct rte_eth_stats *stats,
		 struct eth_queue_stats *qstats __rte_unused)
{
        struct k1xmac_private *priv = dev->data->dev_private;
	struct rte_eth_stats *eth_stats = &priv->stats;
	uint32_t rx_missed_pkts = 0;

	/* update rx missed packets */
	rx_missed_pkts = k1xmac_get_missed_frames(priv);
	eth_stats->imissed += rx_missed_pkts;

	stats->ipackets = eth_stats->ipackets;
	stats->ibytes = eth_stats->ibytes;
	stats->ierrors = eth_stats->ierrors;
	stats->imissed = eth_stats->imissed;
	stats->opackets = eth_stats->opackets;
	stats->obytes = eth_stats->obytes;
	stats->oerrors = eth_stats->oerrors;
	stats->rx_nombuf = eth_stats->rx_nombuf;

        return 0;
}

static int
k1xmac_stats_reset(struct rte_eth_dev *dev)
{
	struct k1xmac_private *priv = dev->data->dev_private;

	k1xmac_get_missed_frames(priv);
	memset(&priv->stats, 0, sizeof(struct rte_eth_stats));
	return 0;
}

static int
k1xmac_eth_info(struct rte_eth_dev *dev __rte_unused,
		    struct rte_eth_dev_info *dev_info)
{
        dev_info->max_rx_pktlen = K1XMAC_MAX_RX_PKT_LEN;
	dev_info->max_rx_queues = K1XMAC_MAX_Q;
	dev_info->max_tx_queues = K1XMAC_MAX_Q;
	dev_info->rx_offload_capa = dev_rx_offloads_sup;
        return 0;
}

static int
k1xmac_rx_queue_setup(struct rte_eth_dev *dev,
			uint16_t queue_idx,
			uint16_t nb_rx_desc,
			unsigned int socket_id __rte_unused,
			const struct rte_eth_rxconf *rx_conf __rte_unused,
			struct rte_mempool *mb_pool)
{
        struct k1xmac_private *private = dev->data->dev_private;
        struct k1xmac_rx_queue *rxq;
        int i, ret;

        /* allocate receive queue */
	rxq = rte_zmalloc(NULL, sizeof(*rxq), RTE_CACHE_LINE_SIZE);
	if (rxq == NULL) {
		K1XMAC_PMD_ERR("receive queue allocation failed\n");
		return -ENOMEM;
	}

	if (nb_rx_desc > DMA_MAX_RX_SIZE) {
		nb_rx_desc = DMA_MAX_RX_SIZE;
		K1XMAC_PMD_WARN("modified the nb_desc to MAX_RX_BD_RING_SIZE\n");
	}

        rxq->pool = mb_pool;
        rxq->nb_rx_desc = nb_rx_desc;
        rxq->private = private;
        rxq->queue_id = queue_idx;
        rxq->port_id = dev->data->port_id;
	rxq->dma_rx_desc = private->bd_addr_r_v;
	rxq->dma_rx_phy = private->bd_addr_r_p;
        private->total_rx_ring_size += rxq->nb_rx_desc;
        private->rx_queues[queue_idx] = rxq;

        K1XMAC_PMD_INFO("nb_rx_desc: %d, total_rx_ring_size: %d\n",
			nb_rx_desc, private->total_rx_ring_size);
        
        for (i = 0; i < rxq->nb_rx_desc; i++) {
                struct rte_mbuf *mbuf;
                struct emac_rx_desc *p;
#ifdef RTE_SOC_SPACEMIT_K1
		void *data;
                unsigned int offset;
                uint64_t mbuf_phy_addr;
#endif
                uint32_t dma_addr;

                p = rxq->dma_rx_desc + i;
                memset(p, 0, sizeof(struct emac_rx_desc));

                /* Initialize Rx buffers from pktmbuf pool */
		mbuf = rte_pktmbuf_alloc(mb_pool);
		if (mbuf == NULL) {
			K1XMAC_PMD_ERR("mbuf failed.\n");
			ret = -ENOMEM;
			goto err_alloc;
		}

#ifdef RTE_SOC_SPACEMIT_K1
		data = rte_pktmbuf_mtod(mbuf, uint8_t *);
		for (offset = 0; offset < mbuf->buf_len; offset += 64) {
                        cbo_invalid((uint8_t *)data + offset);
		}
		rte_io_mb();
#endif

#ifndef RTE_SOC_SPACEMIT_K1
		dma_addr = rte_cpu_to_le_32(rte_mbuf_data_iova_default(mbuf));
#else
		mbuf_phy_addr = rte_mbuf_data_iova_default(mbuf);
		if (mbuf_phy_addr >= 0x80000000) {
			mbuf_phy_addr = mbuf_phy_addr - 0x80000000;
		}
		dma_addr = rte_cpu_to_le_32(mbuf_phy_addr);
#endif
		p->BufferAddr1 = dma_addr;
                p->BufferSize1 = rte_cpu_to_le_16(mbuf->buf_len);
                if (i == (rxq->nb_rx_desc - 1)) {
                        p->EndRing = 1;
                }
                rte_wmb();
                p->OWN = 1;
		rxq->rx_mbuf[i] = mbuf;
        }

	rxq->rx_tail = 0;
        dev->data->rx_queues[queue_idx] = private->rx_queues[queue_idx];

        /* setup rx dma desc addr */
        emac_wr(private, DMA_RECEIVE_BASE_ADDRESS, rxq->dma_rx_phy);

        return 0;

err_alloc:
	rte_free(rxq);

	return ret;
}

static int
k1xmac_tx_queue_setup(struct rte_eth_dev *dev,
			uint16_t queue_idx,
			uint16_t nb_desc,
			unsigned int socket_id __rte_unused,
			const struct rte_eth_txconf *tx_conf __rte_unused)
{
        struct k1xmac_private *priv = dev->data->dev_private;
        struct k1xmac_tx_queue *txq;
        int i, ret;

	K1XMAC_PMD_INFO("k1xmac_tx_queue_setup called.\n");

        /* allocate transmit queue */
	txq = rte_zmalloc(NULL, sizeof(*txq), RTE_CACHE_LINE_SIZE);
	if (txq == NULL) {
		K1XMAC_PMD_ERR("transmit queue allocation failed\n");
		return -ENOMEM;
	}

	if (nb_desc > DMA_MAX_TX_SIZE) {
		nb_desc = DMA_MAX_TX_SIZE;
		K1XMAC_PMD_WARN("modified the nb_desc to DMA_MAX_TX_SIZE\n");
	}

        txq->nb_tx_desc = nb_desc;
        txq->dma_tx_desc = (struct emac_tx_desc *)(priv->bd_addr_t_v);
        txq->dma_tx_phy = priv->bd_addr_t_p;
        txq->private = priv;
        priv->total_tx_ring_size += txq->nb_tx_desc;
        priv->tx_queues[queue_idx] = txq;

        K1XMAC_PMD_INFO("nb_desc: %d, total_tx_ring_size: %d\n",
			nb_desc, priv->total_tx_ring_size);

        txq->tx_mbuf_dma = rte_zmalloc(NULL, nb_desc * sizeof(struct k1xmac_tx_info), 0);
	if (txq->tx_mbuf_dma == NULL) {
		K1XMAC_PMD_ERR("transmit queue tx_mbuf_dma allocation failed\n");
		ret = -ENOMEM;
		goto fail;
	}

        for (i = 0; i < txq->nb_tx_desc; i++) {
		struct emac_tx_desc *p;
		p = txq->dma_tx_desc + i;

		memset(p, 0, sizeof(struct emac_tx_desc));

		txq->tx_mbuf_dma[i].buf = 0;
		txq->tx_mbuf_dma[i].len = 0;
		txq->tx_mbuf_dma[i].last_segment = false;
		txq->tx_mbuf[i] = NULL;
	}

        txq->tx_tail = 0;
        txq->tx_head = 0;
        txq->tx_free = txq->nb_tx_desc - 1;

        /* setup tx dma desc addr */
	emac_wr(priv, DMA_TRANSMIT_BASE_ADDRESS, txq->dma_tx_phy);

	dev->data->tx_queues[queue_idx] = priv->tx_queues[queue_idx];
	dev->data->tx_queue_state[queue_idx] = RTE_ETH_QUEUE_STATE_STARTED;

        return 0;

fail:
        if (txq)
                rte_free(txq);

	return ret;
}

static void
k1xmac_free_queue(struct rte_eth_dev *dev)
{
        struct k1xmac_private *priv = dev->data->dev_private;
	unsigned int i;

	for (i = 0; i < dev->data->nb_rx_queues; i++) {
		rte_free(priv->rx_queues[i]);
		priv->rx_queues[i] = NULL;
	}
	dev->data->nb_rx_queues = 0;

	for (i = 0; i < dev->data->nb_tx_queues; i++) {
		rte_free(priv->tx_queues[i]);
		priv->tx_queues[i] = NULL;
	}
	dev->data->nb_tx_queues = 0;
}

static int
k1xmac_hw_init(struct k1xmac_private *priv)
{
        k1xmac_reset_hw(priv);
        k1xmac_init_hw(priv);

        return 0;
}

static const struct eth_dev_ops k1xmac_dev_ops = {
	.dev_configure          = k1xmac_eth_configure,
	.dev_start              = k1xmac_eth_start,
	.dev_stop               = k1xmac_eth_stop,
	.dev_close              = k1xmac_eth_close,

	.promiscuous_enable     = k1xmac_promiscuous_enable,
	.promiscuous_disable	= k1xmac_promiscuous_disable,
	.allmulticast_enable	= k1xmac_allmulticast_enable,
	.allmulticast_disable	= k1xmac_allmulticast_disable,

	.link_update            = k1xmac_eth_link_update,

	.mac_addr_set           = k1xmac_set_mac_address,
	.mtu_set		= k1xmac_set_mtu,

	.stats_get              = k1xmac_stats_get,
	.stats_reset		= k1xmac_stats_reset,
	.dev_infos_get          = k1xmac_eth_info,

	.rx_queue_setup         = k1xmac_rx_queue_setup,
	.tx_queue_setup         = k1xmac_tx_queue_setup
};

static int
k1xmac_eth_init(struct rte_eth_dev *dev)
{
	struct k1xmac_private *private = dev->data->dev_private;

	private->full_duplex = FULL_DUPLEX;
	dev->dev_ops = &k1xmac_dev_ops;

	return 0;
}

static int
pmd_k1xmac_probe(struct rte_vdev_device *vdev)
{
        struct rte_eth_dev *dev = NULL;
        struct k1xmac_private *private;
        const char *name;
	struct rte_ether_addr macaddr = {
		.addr_bytes = { 0x1, 0x1, 0x1, 0x1, 0x1, 0x1 }
	};
        int rc, id = 0, fd;
        struct ifreq req;
	char if_name[16] = {0};

        name = rte_vdev_device_name(vdev);
        K1XMAC_PMD_LOG(INFO, "Probing K1XMAC PMD: %s\n", name);

	if (rte_eal_process_type() == RTE_PROC_SECONDARY &&
	    strlen(rte_vdev_device_args(vdev)) == 0) {
		dev = rte_eth_dev_attach_secondary(name);
		if (!dev) {
			K1XMAC_PMD_ERR("Failed to probe %s\n", name);
			return -1;
		}
		/* TODO: request info from primary to set up Rx and Tx */
		dev->dev_ops = &k1xmac_dev_ops;
		dev->tx_pkt_burst = &k1xmac_xmit_pkts;
		dev->rx_pkt_burst = &k1xmac_recv_pkts;
		dev->device = &vdev->device;
		rte_eth_dev_probing_finish(dev);
		return 0;
	}

        if (strncmp(name, RTE_STR(K1XMAC_NAME_PMD), sizeof(RTE_STR(K1XMAC_NAME_PMD))) > 0) {
		sscanf(&name[strlen(RTE_STR(K1XMAC_NAME_PMD))], "%d", &id);
		K1XMAC_PMD_LOG(INFO, "Initializing pmd_k1xmac for id %d\n", id);
	}

        dev = rte_eth_vdev_allocate(vdev, sizeof(*private));
	if (dev == NULL)
		return -ENOMEM;
        private = dev->data->dev_private;
	private->dev = dev;

        private->max_rx_queues = K1XMAC_MAX_Q;
	private->max_tx_queues = K1XMAC_MAX_Q;

	memset(&(private->stats), 0, sizeof(private->stats));
	rc = k1xmac_configure(id);
	if (rc != 0)
		return -ENOMEM;
	rc = config_k1xmac_uio(private);
	if (rc != 0)
		return -ENOMEM;

        /* Copy the station address into the dev structure, */
	dev->data->mac_addrs = rte_zmalloc("mac_addr", RTE_ETHER_ADDR_LEN, 0);
	if (dev->data->mac_addrs == NULL) {
		K1XMAC_PMD_ERR("Failed to allocate mem %d to store MAC addresses\n",
			       RTE_ETHER_ADDR_LEN);
		rc = -ENOMEM;
		goto err;
	}

	/* Initialize HW Interface */
	rc = k1xmac_hw_init(private);
	if (rc )
		goto err;

	rc = k1xmac_eth_init(dev);
	if (rc)
		goto failed_init;

        fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
                rc = -EINVAL;
                goto failed_init;
        }

	memset(&req, 0, sizeof(req));
	snprintf(if_name, sizeof(if_name), "%s%d", "end", id);
	strcpy(req.ifr_name, if_name);
	rc = ioctl(fd, SIOCGIFHWADDR, &req);
	if (rc)
		goto failed_init;
	memcpy(macaddr.addr_bytes,
	       req.ifr_addr.sa_data, RTE_ETHER_ADDR_LEN);
	/*
	 * Set default mac address
	 */
	k1xmac_set_mac_address(dev, &macaddr);

        private->hw_stats = rte_zmalloc("k1xmac_hw_stats",
                                        sizeof(struct emac_hw_stats), 0);
        if (private->hw_stats == NULL) {
                K1XMAC_PMD_ERR("Failed to allocate mem for hw_stats\n");
                rc = -ENOMEM;
                goto failed_init;
        }

	rte_eth_dev_probing_finish(dev);

	return 0;

failed_init:
	K1XMAC_PMD_ERR("Failed to init");
err:
	rte_eth_dev_release_port(dev);

	return rc;
}

static int
pmd_k1xmac_remove(struct rte_vdev_device *vdev)
{
        struct rte_eth_dev *eth_dev = NULL;
	struct k1xmac_private *private;
	int ret;

	/* find the ethdev entry */
	eth_dev = rte_eth_dev_allocated(rte_vdev_device_name(vdev));
	if (eth_dev == NULL)
		return -ENODEV;

	if (rte_eal_process_type() != RTE_PROC_PRIMARY)
                return 0;

	private = eth_dev->data->dev_private;

	k1xmac_eth_stop(eth_dev);
	k1xmac_reset_hw(private);

	k1xmac_free_queue(eth_dev);
	ret = rte_eth_dev_release_port(eth_dev);
	if (ret != 0)
		return -EINVAL;

	K1XMAC_PMD_INFO("Release k1xmac sw device.\n");
	k1xmac_cleanup(private);

	return 0;
}

static struct rte_vdev_driver pmd_k1xmac_drv = {
	.probe = pmd_k1xmac_probe,
	.remove = pmd_k1xmac_remove,
};

RTE_PMD_REGISTER_VDEV(K1XMAC_NAME_PMD, pmd_k1xmac_drv);
RTE_LOG_REGISTER_DEFAULT(k1xmac_logtype_pmd, NOTICE);
