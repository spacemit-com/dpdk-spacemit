/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (C) 2025 Spacemit Corporation.
 */
#include "k1xmac_regs.h"
#include "k1xmac_ethdev.h"
#include "k1xmac_pmd_logs.h"
#include <rte_io.h>

static u32 ReadTxStatCounters(struct k1xmac_private *priv, u8 cnt)
{
	u32 val, tmp;
        int counter = 0;

	val = 0x8000 | cnt;
	emac_wr(priv, MAC_TX_STATCTR_CONTROL, val);
	val = emac_rd(priv, MAC_TX_STATCTR_CONTROL);

        do {
                val = emac_rd(priv, MAC_TX_STATCTR_CONTROL);
                if (counter++ > 100) {
                        K1XMAC_PMD_ERR("timeout waiting for TX stat counter\n");
                        return -EINVAL;
                }
                rte_delay_us(100);
        } while (val & 0x8000);

	tmp = emac_rd(priv, MAC_TX_STATCTR_DATA_HIGH);
	val = tmp << 16;
	tmp = emac_rd(priv, MAC_TX_STATCTR_DATA_LOW);
	val |= tmp;

	return val;
}

static u32 ReadRxStatCounters(struct k1xmac_private *priv, u8 cnt)
{
	u32 val, tmp;
        int counter = 0;

	val = 0x8000 | cnt;
	emac_wr(priv, MAC_RX_STATCTR_CONTROL, val);
	val = emac_rd(priv, MAC_RX_STATCTR_CONTROL);

        do {
                val = emac_rd(priv, MAC_RX_STATCTR_CONTROL);
                if (counter++ > 100) {
                        K1XMAC_PMD_ERR("timeout waiting for TX stat counter\n");
                        return -EINVAL;
                }
                rte_delay_us(100);
        } while (val & 0x8000);

	tmp = emac_rd(priv, MAC_RX_STATCTR_DATA_HIGH);
	val = tmp << 16;
	tmp = emac_rd(priv, MAC_RX_STATCTR_DATA_LOW);
	val |= tmp;
	return val;
}

void k1xmac_stats_update(struct k1xmac_private *priv)
{
	struct emac_hw_stats *hwstats = priv->hw_stats;
	int i;
	u32 *p;

	p = (u32 *)(hwstats);

	for (i = 0; i < MAX_TX_STATS_NUM; i++)
		*(p + i) = ReadTxStatCounters(priv, i);

	p = (u32 *)hwstats + MAX_TX_STATS_NUM;

	for (i = 0; i < MAX_RX_STATS_NUM; i++)
		*(p + i) = ReadRxStatCounters(priv, i);
}

void print_hw_tx_stats(struct k1xmac_private *priv)
{
	struct emac_hw_stats *hwstats = priv->hw_stats;

	printf("**** Hardware TX Statistics ****\n");
	printf("tx ok pkts: %u\n", hwstats->tx_ok_pkts);
	printf("tx total pkts: %u\n", hwstats->tx_total_pkts);
	printf("tx ok bytes: %u\n", hwstats->tx_ok_bytes);
	printf("tx err pkts: %u\n", hwstats->tx_err_pkts);
	printf("tx sigle clsn pkts: %u\n", hwstats->tx_singleclsn_pkts);
	printf("ts mult clsn pkts: %u\n", hwstats->tx_multiclsn_pkts);
	printf("tx late clsn okts: %u\n", hwstats->tx_lateclsn_pkts);
	printf("tx excess clsn pkts: %u\n", hwstats->tx_excessclsn_pkts);
	printf("tx unicast pkts: %u\n", hwstats->tx_unicast_pkts);
	printf("tx multicast pkts: %u\n", hwstats->tx_multicast_pkts);
	printf("tx broadcast pkts: %u\n", hwstats->tx_broadcast_pkts);
	printf("tx pause pkts: %u\n", hwstats->tx_pause_pkts);
	printf("*****************************\n");
}

void print_hw_rx_stats(struct k1xmac_private *priv)
{
	struct emac_hw_stats *hwstats = priv->hw_stats;

	printf("**** Hardware RX Statistics ****\n");
	printf("rx ok pkts: %u\n", hwstats->rx_ok_pkts);
	printf("rx total pkts: %u\n", hwstats->rx_total_pkts);
	printf("rx crc err pkts: %u\n", hwstats->rx_crc_err_pkts);
	printf("rx align err pkts: %u\n", hwstats->rx_align_err_pkts);
	printf("rx total err pkts: %u\n", hwstats->rx_err_total_pkts);
	printf("rx ok bytes: %u\n", hwstats->rx_ok_bytes);
	printf("rx total bytes: %u\n", hwstats->rx_total_bytes);
	printf("rx unicast pkts: %u\n", hwstats->rx_unicast_pkts);
	printf("rx multicast pkts: %u\n", hwstats->rx_multicast_pkts);
	printf("rx broadcast pkts: %u\n", hwstats->rx_broadcast_pkts);
	printf("rx pause pkts: %u\n", hwstats->rx_pause_pkts);
	printf("rx len err pkts: %u\n", hwstats->rx_len_err_pkts);
	printf("rx len undersize pkts: %u\n", hwstats->rx_len_undersize_pkts);
	printf("rx len oversize pkts: %u\n", hwstats->rx_len_oversize_pkts);
	printf("rx len fragment pkts: %u\n", hwstats->rx_len_fragment_pkts);
	printf("rx len jabber pkts: %u\n", hwstats->rx_len_jabber_pkts);
	printf("rx 64 pkts: %u\n", hwstats->rx_64_pkts);
	printf("rx 65-127 pkts: %u\n", hwstats->rx_65_127_pkts);
	printf("rx 128-255 pkts: %u\n", hwstats->rx_128_255_pkts);
	printf("rx 256-511 pkts: %u\n", hwstats->rx_256_511_pkts);
	printf("rx 512-1023 pkts: %u\n", hwstats->rx_512_1023_pkts);
	printf("rx 1024-1518 pkts: %u\n", hwstats->rx_1024_1518_pkts);
	printf("rx 1519 plus pkts: %u\n", hwstats->rx_1519_plus_pkts);
	printf("rx drp fifo full pkts: %u\n", hwstats->rx_drp_fifo_full_pkts);
	printf("rx truncate fifo full pkts: %u\n", hwstats->rx_truncate_fifo_full_pkts);
	printf("*****************************\n");
}

u32 k1xmac_get_missed_frames(struct k1xmac_private *priv)
{
	u32 val;

	val = emac_rd(priv, DMA_MISSED_FRAME_COUNTER);

	return val;
}