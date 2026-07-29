/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025 Spacemit Corporation.
 */

#ifndef _K1XMAC_ETHDEV_H_
#define _K1XMAC_ETHDEV_H_

#include <rte_ethdev.h>
#include <rte_io.h>

#define u64	uint64_t
#define u32	uint32_t
#define u16	uint16_t
#define u8	uint8_t

typedef u32  dma_addr_t;

/* full duplex */
#define FULL_DUPLEX		0x00

/* TX and RX Descriptor Length, these need to be power of two.
 * TX descriptor length less than 64 may cause transmit queue timed out error.
 * RX descriptor length less than 64 may cause inconsistent Rx chain error.
 */
#define DMA_MIN_TX_SIZE		64
#define DMA_MAX_TX_SIZE		1024
#define DMA_DEFAULT_TX_SIZE	1024
#define DMA_MIN_RX_SIZE		64
#define DMA_MAX_RX_SIZE		1024
#define DMA_DEFAULT_RX_SIZE	1024

#define AXI_CLK_CYCLES_PER_US	312

#define K1XMAC_MAX_RX_PKT_LEN	4000

#define MAX_TX_STATS_NUM	12
#define MAX_RX_STATS_NUM	25

#if defined(RTE_SOC_SPACEMIT_K1)
#define cbo_clean(start)			\
	({								\
		unsigned long __v = (unsigned long)(start); \
		__asm__ __volatile__("cbo.clean"	\
							 " 0(%0)"		\
							 :				\
							 : "rK"(__v)	\
							 : "memory");	\
	})

#define cbo_invalid(start)			\
	({								\
		unsigned long __v = (unsigned long)(start); \
		__asm__ __volatile__("cbo.inval"	\
							 " 0(%0)"		\
							 :				\
							 : "rK"(__v)	\
							 : "memory");	\
	})

#define cbo_flush(start)			\
	({								\
		unsigned long __v = (unsigned long)(start); \
		__asm__ __volatile__("cbo.flush"	\
							 " 0(%0)"		\
							 :				\
							 : "rK"(__v)	\
							 : "memory");	\
	})
#endif

/*
 * K1XMAC can support 1 rx and tx queue..
 */

#define K1XMAC_MAX_Q		1

#define writel(v, p) __extension__ ({*(volatile unsigned int *)(p) = (v); })
#define readl(p) rte_read32(p)

enum rx_frame_status {
	frame_ok = 0,
	frame_discard,
	frame_max,
};

struct emac_hw_stats {
	u32 tx_ok_pkts;
	u32 tx_total_pkts;
	u32 tx_ok_bytes;
	u32 tx_err_pkts;
	u32 tx_singleclsn_pkts;
	u32 tx_multiclsn_pkts;
	u32 tx_lateclsn_pkts;
	u32 tx_excessclsn_pkts;
	u32 tx_unicast_pkts;
	u32 tx_multicast_pkts;
	u32 tx_broadcast_pkts;
	u32 tx_pause_pkts;
	u32 rx_ok_pkts;
	u32 rx_total_pkts;
	u32 rx_crc_err_pkts;
	u32 rx_align_err_pkts;
	u32 rx_err_total_pkts;
	u32 rx_ok_bytes;
	u32 rx_total_bytes;
	u32 rx_unicast_pkts;
	u32 rx_multicast_pkts;
	u32 rx_broadcast_pkts;
	u32 rx_pause_pkts;
	u32 rx_len_err_pkts;
	u32 rx_len_undersize_pkts;
	u32 rx_len_oversize_pkts;
	u32 rx_len_fragment_pkts;
	u32 rx_len_jabber_pkts;
	u32 rx_64_pkts;
	u32 rx_65_127_pkts;
	u32 rx_128_255_pkts;
	u32 rx_256_511_pkts;
	u32 rx_512_1023_pkts;
	u32 rx_1024_1518_pkts;
	u32 rx_1519_plus_pkts;
	u32 rx_drp_fifo_full_pkts;
	u32 rx_truncate_fifo_full_pkts;
};

/* Transmit Descriptor */
struct emac_tx_desc {
	u32 FramePacketStatus:30;
	u32 tx_timestamp:1;
	u32 OWN:1;

	u32 BufferSize1:12;
	u32 BufferSize2:12;
	u32 ForceEOPError:1;
	u32 SecondAddressChained:1;
	u32 EndRing:1;
	u32 DisablePadding:1;
	u32 AddCRCDisable:1;
	u32 FirstSegment:1;
	u32 LastSegment:1;
	u32 InterruptOnCompletion:1;

	u32 BufferAddr1;
	u32 BufferAddr2;
};

/* Receive Descriptor structure */
struct emac_rx_desc {
	u32 FramePacketLength:14;
	u32 ApplicationStatus:15;
	u32 LastDescriptor:1;
	u32 FirstDescriptor:1;
	u32 OWN:1;

	u32 BufferSize1:12;
	u32 BufferSize2:12;
	u32 Reserved1:1;
	u32 SecondAddressChained:1;
	u32 EndRing:1;
	u32 Reserved2:3;
	u32 rx_timestamp:1;
	u32 ptp_pkt:1;

	u32 BufferAddr1;
	u32 BufferAddr2;
};

struct k1xmac_tx_info {
	dma_addr_t buf;
	unsigned len;
	bool last_segment;
	bool is_jumbo;
};

struct k1xmac_tx_queue {
	struct emac_tx_desc	*dma_tx_desc;
	struct rte_mbuf		*tx_mbuf[DMA_MAX_TX_SIZE];
	struct k1xmac_tx_info 	*tx_mbuf_dma;
	unsigned short		nb_tx_desc;
	dma_addr_t		dma_tx_phy;
	struct rte_mempool	*pool;
	struct k1xmac_private	*private;
	RTE_ATOMIC(uint32_t)	tx_tail;
	uint16_t		tx_head;
	uint16_t		tx_free;
};

struct k1xmac_rx_queue {
	uint16_t		port_id;
	uint16_t 		queue_id;
	struct emac_rx_desc	*dma_rx_desc;
	uint16_t		rx_tail;
	struct rte_mbuf		*rx_mbuf[DMA_MAX_RX_SIZE];
	unsigned short		nb_rx_desc;
	dma_addr_t		dma_rx_phy;
	struct rte_mempool	*pool;
	struct k1xmac_private	*private;
};

struct k1xmac_private {
	struct rte_eth_dev 	*dev;
	struct rte_eth_stats	stats;
	uint32_t 		dma_buf_sz;
	int			full_duplex;
	int			flag_pause;
	int			flag_csum;
	uint32_t		quirks;
	uint32_t		cbus_size;
	uint32_t		enetfec_e_cntl;
	uint16_t		max_rx_queues;
	uint16_t		max_tx_queues;
	unsigned int		total_tx_ring_size;
	unsigned int		total_rx_ring_size;
	unsigned int		reg_size;
	unsigned int		bd_size;
	unsigned int		bd_r_size[K1XMAC_MAX_Q];
	unsigned int		bd_t_size[K1XMAC_MAX_Q];
	bool			bufdesc_ex;
	bool			rgmii_txc_delay;
	bool			rgmii_rxc_delay;
	void			*ioaddr_v;
	uint32_t		ioaddr_p;
	void			*bd_addr_r_v;
	uint32_t		bd_addr_r_p;
	void			*bd_addr_t_v;
	uint32_t		bd_addr_t_p;
	void			*hw_baseaddr_v;
	void			*bd_addr_v;
	uint32_t		hw_baseaddr_p;
	uint32_t		bd_addr_p;
	uint32_t		bd_addr_p_r[K1XMAC_MAX_Q];
	uint32_t		bd_addr_p_t[K1XMAC_MAX_Q];
	void			*dma_baseaddr_r[K1XMAC_MAX_Q];
	void			*dma_baseaddr_t[K1XMAC_MAX_Q];
	struct k1xmac_rx_queue *rx_queues[K1XMAC_MAX_Q];
	struct k1xmac_tx_queue *tx_queues[K1XMAC_MAX_Q];
	unsigned long		flags;
	struct emac_hw_stats	*hw_stats;
};

static inline void emac_wr(struct k1xmac_private *priv, u32 reg, u32 val)
{
	writel(val, ((u8 *)(priv->ioaddr_v) + reg));
}

static inline int emac_rd(struct k1xmac_private *priv, u32 reg)
{
	return readl((u8 *)(priv->ioaddr_v) + reg);
}

void print_register(struct k1xmac_private *priv, u32 reg);
void registers_dump(struct k1xmac_private *priv);
void k1xmac_stats_update(struct k1xmac_private *priv);
void print_hw_tx_stats(struct k1xmac_private *priv);
void print_hw_rx_stats(struct k1xmac_private *priv);
u32 k1xmac_get_missed_frames(struct k1xmac_private *priv);
void k1xmac_dma_start_transmit(struct k1xmac_private *priv);
uint16_t k1xmac_recv_pkts(void *rxq1, struct rte_mbuf **rx_pkts,
		uint16_t nb_pkts);
uint16_t k1xmac_xmit_pkts(void *tx_queue, struct rte_mbuf **tx_pkts,
		uint16_t nb_pkts);

#endif /* _K1XMAC_ETHDEV_H_ */
