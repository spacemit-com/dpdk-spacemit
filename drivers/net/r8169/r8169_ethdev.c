/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Realtek Corporation. All rights reserved
 */

#include <stdio.h>
#include <errno.h>
#include <stdint.h>

#include <rte_eal.h>

#include <rte_common.h>
#include <rte_interrupts.h>
#include <rte_byteorder.h>
#include <rte_pci.h>
#include <bus_pci_driver.h>
#include <rte_ether.h>
#include <ethdev_driver.h>
#include <ethdev_pci.h>
#include <rte_memory.h>
#include <rte_malloc.h>
#include <dev_driver.h>

#include "r8169_ethdev.h"
#include "r8169_compat.h"
#include "r8169_logs.h"
#include "r8169_hw.h"
#include "r8169_dash.h"

static int rtl_dev_configure(struct rte_eth_dev *dev __rte_unused);
static int rtl_dev_start(struct rte_eth_dev *dev);
static int rtl_dev_stop(struct rte_eth_dev *dev);
static int rtl_dev_reset(struct rte_eth_dev *dev);
static int rtl_dev_close(struct rte_eth_dev *dev);
static int rtl_dev_link_update(struct rte_eth_dev *dev, int wait __rte_unused);
static int rtl_dev_set_link_up(struct rte_eth_dev *dev);
static int rtl_dev_set_link_down(struct rte_eth_dev *dev);
static int rtl_dev_infos_get(struct rte_eth_dev *dev,
			     struct rte_eth_dev_info *dev_info);
static int rtl_dev_stats_get(struct rte_eth_dev *dev,
			     struct rte_eth_stats *rte_stats,
			     struct eth_queue_stats *qstats);
static int rtl_dev_stats_reset(struct rte_eth_dev *dev);
static int rtl_promiscuous_enable(struct rte_eth_dev *dev);
static int rtl_promiscuous_disable(struct rte_eth_dev *dev);
static int rtl_allmulticast_enable(struct rte_eth_dev *dev);
static int rtl_allmulticast_disable(struct rte_eth_dev *dev);
static int rtl_dev_mtu_set(struct rte_eth_dev *dev, uint16_t mtu);
static int rtl_fw_version_get(struct rte_eth_dev *dev, char *fw_version,
			      size_t fw_size);
static int rtl_reta_update(struct rte_eth_dev *dev,
			   struct rte_eth_rss_reta_entry64 *reta_conf,
			   uint16_t reta_size);
static int rtl_reta_query(struct rte_eth_dev *dev,
			  struct rte_eth_rss_reta_entry64 *reta_conf,
			  uint16_t reta_size);
static int rtl_rss_hash_update(struct rte_eth_dev *dev,
			       struct rte_eth_rss_conf *rss_conf);
static int rtl_rss_hash_conf_get(struct rte_eth_dev *dev,
				 struct rte_eth_rss_conf *rss_conf);
/*
 * The set of PCI devices this driver supports
 */
static const struct rte_pci_id pci_id_r8169_map[] = {
	{ RTE_PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8125) },
	{ RTE_PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8162) },
	{ RTE_PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8126) },
	{ RTE_PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x5000) },
	{ RTE_PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8168) },
	{ RTE_PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8127) },
	{ RTE_PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x0E10) },
	{.vendor_id = 0, /* sentinel */ },
};

static const struct rte_eth_desc_lim rx_desc_lim = {
	.nb_max   = RTL_MAX_RX_DESC,
	.nb_min   = RTL_MIN_RX_DESC,
	.nb_align = RTL_DESC_ALIGN,
};

static const struct rte_eth_desc_lim tx_desc_lim = {
	.nb_max         = RTL_MAX_TX_DESC,
	.nb_min         = RTL_MIN_TX_DESC,
	.nb_align       = RTL_DESC_ALIGN,
	.nb_seg_max     = RTL_MAX_TX_SEG,
	.nb_mtu_seg_max = RTL_MAX_TX_SEG,
};

static const struct eth_dev_ops rtl_eth_dev_ops = {
	.dev_configure	      = rtl_dev_configure,
	.dev_start	      = rtl_dev_start,
	.dev_stop	      = rtl_dev_stop,
	.dev_close	      = rtl_dev_close,
	.dev_reset	      = rtl_dev_reset,
	.dev_set_link_up      = rtl_dev_set_link_up,
	.dev_set_link_down    = rtl_dev_set_link_down,
	.dev_infos_get        = rtl_dev_infos_get,

	.promiscuous_enable   = rtl_promiscuous_enable,
	.promiscuous_disable  = rtl_promiscuous_disable,
	.allmulticast_enable  = rtl_allmulticast_enable,
	.allmulticast_disable = rtl_allmulticast_disable,

	.link_update          = rtl_dev_link_update,

	.stats_get            = rtl_dev_stats_get,
	.stats_reset          = rtl_dev_stats_reset,

	.mtu_set              = rtl_dev_mtu_set,

	.fw_version_get       = rtl_fw_version_get,

	.rx_queue_setup       = rtl_rx_queue_setup,
	.rx_queue_release     = rtl_rx_queue_release,
	.rxq_info_get         = rtl_rxq_info_get,

	.tx_queue_setup       = rtl_tx_queue_setup,
	.tx_queue_release     = rtl_tx_queue_release,
	.tx_done_cleanup      = rtl_tx_done_cleanup,
	.txq_info_get         = rtl_txq_info_get,

	.reta_update          = rtl_reta_update,
	.reta_query           = rtl_reta_query,
	.rss_hash_update      = rtl_rss_hash_update,
	.rss_hash_conf_get    = rtl_rss_hash_conf_get,
};

static int
rtl_dev_configure(struct rte_eth_dev *dev __rte_unused)
{
	return 0;
}

static void
rtl_disable_intr(struct rtl_hw *hw)
{
	PMD_INIT_FUNC_TRACE();
	if (rtl_is_8125(hw)) {
		RTL_W32(hw, IMR0_8125, 0x0000);
		RTL_W32(hw, ISR0_8125, RTL_R32(hw, ISR0_8125));
	} else {
		RTL_W16(hw, IntrMask, 0x0000);
		RTL_W16(hw, IntrStatus, RTL_R16(hw, IntrStatus));
	}
}

static void
rtl_enable_intr(struct rtl_hw *hw)
{
	PMD_INIT_FUNC_TRACE();
	if (rtl_is_8125(hw))
		RTL_W32(hw, IMR0_8125, LinkChg);
	else
		RTL_W16(hw, IntrMask, LinkChg);
}

static int
_rtl_setup_link(struct rte_eth_dev *dev)
{
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;
	u64 adv = 0;
	u32 *link_speeds = &dev->data->dev_conf.link_speeds;
	unsigned int speed_mode;

	/* Setup link speed and duplex */
	if (*link_speeds == RTE_ETH_LINK_SPEED_AUTONEG) {
		switch (hw->chipset_name) {
		case RTL8125A:
		case RTL8125B:
		case RTL8125BP:
		case RTL8125D:
		case RTL8125CP:
			speed_mode = SPEED_2500;
			break;
		case RTL8126A:
			speed_mode = SPEED_5000;
			break;
		case RTL8127:
			speed_mode = SPEED_10000;
			break;
		default:
			speed_mode = SPEED_1000;
			break;
		}

		rtl_set_link_option(hw, AUTONEG_ENABLE, speed_mode, DUPLEX_FULL,
				    rtl_fc_full);
	} else if (*link_speeds != 0) {
		if (*link_speeds & ~(RTE_ETH_LINK_SPEED_10M_HD | RTE_ETH_LINK_SPEED_10M |
				     RTE_ETH_LINK_SPEED_100M_HD | RTE_ETH_LINK_SPEED_100M |
				     RTE_ETH_LINK_SPEED_1G | RTE_ETH_LINK_SPEED_2_5G |
				     RTE_ETH_LINK_SPEED_5G | RTE_ETH_LINK_SPEED_10G |
				     RTE_ETH_LINK_SPEED_FIXED))
			goto error_invalid_config;

		if (*link_speeds & RTE_ETH_LINK_SPEED_10M_HD) {
			hw->speed = SPEED_10;
			hw->duplex = DUPLEX_HALF;
			adv |= ADVERTISE_10_HALF;
		}
		if (*link_speeds & RTE_ETH_LINK_SPEED_10M) {
			hw->speed = SPEED_10;
			hw->duplex = DUPLEX_FULL;
			adv |= ADVERTISE_10_FULL;
		}
		if (*link_speeds & RTE_ETH_LINK_SPEED_100M_HD) {
			hw->speed = SPEED_100;
			hw->duplex = DUPLEX_HALF;
			adv |= ADVERTISE_100_HALF;
		}
		if (*link_speeds & RTE_ETH_LINK_SPEED_100M) {
			hw->speed = SPEED_100;
			hw->duplex = DUPLEX_FULL;
			adv |= ADVERTISE_100_FULL;
		}
		if (*link_speeds & RTE_ETH_LINK_SPEED_1G) {
			hw->speed = SPEED_1000;
			hw->duplex = DUPLEX_FULL;
			adv |= ADVERTISE_1000_FULL;
		}
		if (*link_speeds & RTE_ETH_LINK_SPEED_2_5G) {
			hw->speed = SPEED_2500;
			hw->duplex = DUPLEX_FULL;
			adv |= ADVERTISE_2500_FULL;
		}
		if (*link_speeds & RTE_ETH_LINK_SPEED_5G) {
			hw->speed = SPEED_5000;
			hw->duplex = DUPLEX_FULL;
			adv |= ADVERTISE_5000_FULL;
		}
		if (*link_speeds & RTE_ETH_LINK_SPEED_10G) {
			hw->speed = SPEED_10000;
			hw->duplex = DUPLEX_FULL;
			adv |= ADVERTISE_10000_FULL;
		}

		hw->autoneg = AUTONEG_ENABLE;
		hw->advertising = adv;
	}

	rtl_set_speed(hw);

	return 0;

error_invalid_config:
	PMD_INIT_LOG(ERR, "Invalid advertised speeds (%u) for port %u",
		     dev->data->dev_conf.link_speeds, dev->data->port_id);
	rtl_stop_queues(dev);
	return -EINVAL;
}

static int
rtl_setup_link(struct rte_eth_dev *dev)
{
#ifdef RTE_EXEC_ENV_FREEBSD
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;
	struct rte_eth_link link;
	int count;
#endif

	_rtl_setup_link(dev);

#ifdef RTE_EXEC_ENV_FREEBSD
	for (count = 0; count < R8169_LINK_CHECK_TIMEOUT; count++) {
		if (!(RTL_R16(hw, PHYstatus) & LinkStatus)) {
			rte_delay_ms(R8169_LINK_CHECK_INTERVAL);
			continue;
		}

		rtl_dev_link_update(dev, 0);

		rte_eth_linkstatus_get(dev, &link);

		return 0;
	}
#endif
	return 0;
}

/* Set PCI configuration space offset 0x79 to setting */
static void
set_offset79(struct rte_pci_device *pdev, u8 setting)
{
	u8 device_control;

	PCI_READ_CONFIG_BYTE(pdev, &device_control, 0x79);
	device_control &= ~0x70;
	device_control |= setting;
	PCI_WRITE_CONFIG_BYTE(pdev, &device_control, 0x79);
}

/*
 * Configure device link speed and setup link.
 * It returns 0 on success.
 */
static int
rtl_dev_start(struct rte_eth_dev *dev)
{
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;
	struct rte_pci_device *pci_dev = RTE_ETH_DEV_TO_PCI(dev);
	struct rte_intr_handle *intr_handle = pci_dev->intr_handle;
	int err;

	/* Disable uio/vfio intr/eventfd mapping */
	rte_intr_disable(intr_handle);

	rtl_powerup_pll(hw);

	rtl_hw_ephy_config(hw);

	rtl_hw_phy_config(hw);

	rtl_hw_config(hw);

	if (!rtl_is_8125(hw))
		set_offset79(pci_dev, 0x40);

	/* Initialize transmission unit */
	rtl_tx_init(dev);

	/* This can fail when allocating mbufs for descriptor rings */
	err = rtl_rx_init(dev);
	if (err) {
		PMD_INIT_LOG(ERR, "Unable to initialize RX hardware");
		goto error;
	}

	/* This can fail when allocating mem for tally counters */
	err = rtl_tally_init(dev);
	if (err)
		goto error;

	/* Enable uio/vfio intr/eventfd mapping */
	rte_intr_enable(intr_handle);

	/* Resume enabled intr since hw reset */
	rtl_enable_intr(hw);

	rtl_setup_link(dev);

	rtl_mdio_write(hw, 0x1F, 0x0000);

	return 0;
error:
	rtl_stop_queues(dev);
	return -EIO;
}

/*
 * Stop device: disable RX and TX functions to allow for reconfiguring.
 */
static int
rtl_dev_stop(struct rte_eth_dev *dev)
{
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;
	struct rte_eth_link link;

	rtl_disable_intr(hw);

	rtl_nic_reset(hw);

	if (rtl_is_8125(hw))
		rtl_mac_ocp_write(hw, 0xE00A, hw->mcu_pme_setting);

	rtl_powerdown_pll(hw);

	rtl_stop_queues(dev);

	rtl_tally_free(dev);

	/* Clear the recorded link status */
	memset(&link, 0, sizeof(link));
	rte_eth_linkstatus_set(dev, &link);

	return 0;
}

static int
rtl_dev_set_link_up(struct rte_eth_dev *dev)
{
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;

	rtl_powerup_pll(hw);

	return 0;
}

static int
rtl_dev_set_link_down(struct rte_eth_dev *dev)
{
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;

	/* mcu pme intr masks */
	if (rtl_is_8125(hw))
		rtl_mac_ocp_write(hw, 0xE00A, hw->mcu_pme_setting & ~(BIT_11 | BIT_14));

	rtl_powerdown_pll(hw);

	return 0;
}

static int
rtl_dev_infos_get(struct rte_eth_dev *dev, struct rte_eth_dev_info *dev_info)
{
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;

	dev_info->min_rx_bufsize = 1024;
	dev_info->max_rx_pktlen = JUMBO_FRAME_9K;
	dev_info->max_mac_addrs = 1;

	if (hw->mcfg >= CFG_METHOD_69) {
		dev_info->max_rx_queues = 4;
		dev_info->max_tx_queues = 2;
	} else {
		dev_info->max_rx_queues = 1;
		dev_info->max_tx_queues = 1;
	}

	dev_info->default_rxconf = (struct rte_eth_rxconf) {
		.rx_free_thresh = RTL_RX_FREE_THRESH,
	};

	dev_info->default_txconf = (struct rte_eth_txconf) {
		.tx_free_thresh = RTL_TX_FREE_THRESH,
	};

	dev_info->rx_desc_lim = rx_desc_lim;
	dev_info->tx_desc_lim = tx_desc_lim;

	dev_info->speed_capa = RTE_ETH_LINK_SPEED_10M_HD | RTE_ETH_LINK_SPEED_10M |
			       RTE_ETH_LINK_SPEED_100M_HD | RTE_ETH_LINK_SPEED_100M |
			       RTE_ETH_LINK_SPEED_1G;

	switch (hw->chipset_name) {
	case RTL8127:
		dev_info->speed_capa |= RTE_ETH_LINK_SPEED_10G;
	/* fallthrough */
	case RTL8126A:
		dev_info->speed_capa |= RTE_ETH_LINK_SPEED_5G;
	/* fallthrough */
	case RTL8125A:
	case RTL8125B:
	case RTL8125BP:
	case RTL8125D:
	case RTL8125CP:
		dev_info->speed_capa |= RTE_ETH_LINK_SPEED_2_5G;
		break;
	}

	dev_info->min_mtu = RTE_ETHER_MIN_MTU;
	dev_info->max_mtu = dev_info->max_rx_pktlen - RTL_ETH_OVERHEAD;

	dev_info->rx_offload_capa = (rtl_get_rx_port_offloads(hw) |
				     dev_info->rx_queue_offload_capa);
	dev_info->tx_offload_capa = rtl_get_tx_port_offloads();

	dev_info->hash_key_size = RTL_RSS_KEY_SIZE;
	dev_info->reta_size = RTL_MAX_INDIRECTION_TABLE_ENTRIES;
	dev_info->flow_type_rss_offloads = RTL_RSS_CTRL_OFFLOAD_ALL;

	return 0;
}

static int
rtl_promiscuous_enable(struct rte_eth_dev *dev)
{
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;

	int rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys | AcceptAllPhys;

	RTL_W32(hw, RxConfig, rx_mode | (RTL_R32(hw, RxConfig)));
	rtl_allmulticast_enable(dev);

	return 0;
}

static int
rtl_promiscuous_disable(struct rte_eth_dev *dev)
{
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;
	int rx_mode = ~AcceptAllPhys;

	RTL_W32(hw, RxConfig, rx_mode & (RTL_R32(hw, RxConfig)));

	if (dev->data->all_multicast == 1)
		rtl_allmulticast_enable(dev);
	else
		rtl_allmulticast_disable(dev);

	return 0;
}

static int
rtl_allmulticast_enable(struct rte_eth_dev *dev)
{
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;

	RTL_W32(hw, MAR0 + 0, 0xffffffff);
	RTL_W32(hw, MAR0 + 4, 0xffffffff);

	return 0;
}

static int
rtl_allmulticast_disable(struct rte_eth_dev *dev)
{
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;

	if (dev->data->promiscuous == 1)
		return 0; /* Must remain in all_multicast mode */

	RTL_W32(hw, MAR0 + 0, 0);
	RTL_W32(hw, MAR0 + 4, 0);

	return 0;
}

static int
rtl_dev_stats_reset(struct rte_eth_dev *dev)
{
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;

	rtl_clear_tally_stats(hw);

	memset(&adapter->sw_stats, 0, sizeof(adapter->sw_stats));

	return 0;
}

static void
rtl_sw_stats_get(struct rte_eth_dev *dev, struct rte_eth_stats *rte_stats)
{
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_sw_stats *sw_stats = &adapter->sw_stats;

	rte_stats->ibytes = sw_stats->rx_bytes;
	rte_stats->obytes = sw_stats->tx_bytes;
}

static int
rtl_dev_stats_get(struct rte_eth_dev *dev, struct rte_eth_stats *rte_stats,
		  struct eth_queue_stats *qstats __rte_unused)
{
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;

	rtl_get_tally_stats(hw, rte_stats);
	rtl_sw_stats_get(dev, rte_stats);

	return 0;
}

/* Return 0 means link status changed, -1 means not changed */
static int
rtl_dev_link_update(struct rte_eth_dev *dev, int wait __rte_unused)
{
	struct rte_eth_link link, old;
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;
	u32 speed;
	u16 status;

	link.link_status = RTE_ETH_LINK_DOWN;
	link.link_speed = 0;
	link.link_duplex = RTE_ETH_LINK_FULL_DUPLEX;
	link.link_autoneg = RTE_ETH_LINK_AUTONEG;

	memset(&old, 0, sizeof(old));

	/* Load old link status */
	rte_eth_linkstatus_get(dev, &old);

	/* Read current link status */
	status = RTL_R16(hw, PHYstatus);

	if (status & LinkStatus) {
		link.link_status = RTE_ETH_LINK_UP;

		if (status & FullDup) {
			link.link_duplex = RTE_ETH_LINK_FULL_DUPLEX;
			if (!rtl_is_8125(hw) || hw->mcfg == CFG_METHOD_48)
				RTL_W32(hw, TxConfig, (RTL_R32(hw, TxConfig) |
						      (BIT_24 | BIT_25)) & ~BIT_19);
		} else {
			link.link_duplex = RTE_ETH_LINK_HALF_DUPLEX;
			if (!rtl_is_8125(hw) || hw->mcfg == CFG_METHOD_48)
				RTL_W32(hw, TxConfig, (RTL_R32(hw, TxConfig) | BIT_25) &
						      ~(BIT_19 | BIT_24));
		}

		/*
		 * The PHYstatus register for the RTL8168 is 8 bits,
		 * while for the RTL8125, RTL8126 and RTL8127, it is 16 bits.
		 */
		if (status & _10000bpsF && rtl_is_8125(hw))
			speed = 10000;
		else if (status & _5000bpsF && rtl_is_8125(hw))
			speed = 5000;
		else if (status & _2500bpsF && rtl_is_8125(hw))
			speed = 2500;
		else if (status & _1000bpsF)
			speed = 1000;
		else if (status & _100bps)
			speed = 100;
		else
			speed = 10;

		link.link_speed = speed;
	}

	if (link.link_status == old.link_status)
		return -1;

	rte_eth_linkstatus_set(dev, &link);

	return 0;
}

static void
rtl_dev_interrupt_handler(void *param)
{
	struct rte_eth_dev *dev = (struct rte_eth_dev *)param;
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;
	uint32_t intr;

	if (rtl_is_8125(hw))
		intr = RTL_R32(hw, ISR0_8125);
	else
		intr = RTL_R16(hw, IntrStatus);

	/* Clear all cause mask */
	rtl_disable_intr(hw);

	if (intr & LinkChg)
		rtl_dev_link_update(dev, 0);
	else
		PMD_DRV_LOG(ERR, "r8169: interrupt unhandled.");

	rtl_enable_intr(hw);
}

/*
 * Reset and stop device.
 */
static int
rtl_dev_close(struct rte_eth_dev *dev)
{
	struct rte_pci_device *pci_dev = RTE_ETH_DEV_TO_PCI(dev);
	struct rte_intr_handle *intr_handle = pci_dev->intr_handle;
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;
	int retries = 0;
	int ret_unreg, ret_stp;

	if (rte_eal_process_type() != RTE_PROC_PRIMARY)
		return 0;

	if (HW_DASH_SUPPORT_DASH(hw))
		rtl_driver_stop(hw);

	ret_stp = rtl_dev_stop(dev);

	rtl_free_queues(dev);

	/* Reprogram the RAR[0] in case user changed it. */
	rtl_rar_set(hw, hw->mac_addr);

	/* Disable uio intr before callback unregister */
	rte_intr_disable(intr_handle);

	do {
		ret_unreg = rte_intr_callback_unregister(intr_handle, rtl_dev_interrupt_handler,
							 dev);
		if (ret_unreg >= 0 || ret_unreg == -ENOENT)
			break;
		else if (ret_unreg != -EAGAIN)
			PMD_DRV_LOG(ERR, "r8169: intr callback unregister failed: %d", ret_unreg);

		rte_delay_ms(100);
	} while (retries++ < (10 + 90));

	return ret_stp;
}

static int
rtl_dev_mtu_set(struct rte_eth_dev *dev, uint16_t mtu)
{
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;
	uint32_t frame_size = mtu + RTL_ETH_OVERHEAD;

	hw->mtu = mtu;

	RTL_W16(hw, RxMaxSize, frame_size);

	return 0;
}

static int
rtl_fw_version_get(struct rte_eth_dev *dev, char *fw_version, size_t fw_size)
{
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;
	int ret;

	ret = snprintf(fw_version, fw_size, "0x%08x", hw->hw_ram_code_ver);

	ret += 1; /* Add the size of '\0' */
	if (fw_size < (u32)ret)
		return ret;
	else
		return 0;
}

static int
rtl_reta_update(struct rte_eth_dev *dev,
		struct rte_eth_rss_reta_entry64 *reta_conf, uint16_t reta_size)
{
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;
	u32 reta;
	u16 idx, shift;
	u8 mask, rss_indir_tbl;
	int i, j;

	if (reta_size != RTL_MAX_INDIRECTION_TABLE_ENTRIES) {
		PMD_DRV_LOG(ERR, "The size of hash lookup table configured "
			"(%d) doesn't match the number hardware can supported "
			"(%d)", reta_size, RTL_MAX_INDIRECTION_TABLE_ENTRIES);
		return -EINVAL;
	}

	for (i = 0; i < reta_size; i += 4) {
		idx = i / RTE_ETH_RETA_GROUP_SIZE;
		shift = i % RTE_ETH_RETA_GROUP_SIZE;
		mask = (reta_conf[idx].mask >> shift) & 0xf;

		if (!mask)
			continue;

		for (j = 0, reta = 0; j < 4; j++) {
			rss_indir_tbl = (u8)reta_conf[idx].reta[shift + j];
			reta |= rss_indir_tbl << (j * 8);

			if (!(mask & (1 << j)))
				continue;

			hw->rss_indir_tbl[i + j] = rss_indir_tbl;
		}

		RTL_W32(hw, RSS_INDIRECTION_TBL_8125_V2 + i, reta);
	}

	return 0;
}

static int
rtl_reta_query(struct rte_eth_dev *dev,
	       struct rte_eth_rss_reta_entry64 *reta_conf, uint16_t reta_size)
{
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;
	u16 idx, shift;
	int i;

	if (reta_size != RTL_MAX_INDIRECTION_TABLE_ENTRIES) {
		PMD_DRV_LOG(ERR, "The size of hash lookup table configured "
			"(%d) doesn't match the number hardware can supported "
			"(%d)", reta_size, RTL_MAX_INDIRECTION_TABLE_ENTRIES);
		return -EINVAL;
	}

	for (i = 0; i < reta_size; i++) {
		idx = i / RTE_ETH_RETA_GROUP_SIZE;
		shift = i % RTE_ETH_RETA_GROUP_SIZE;

		if (reta_conf[idx].mask & (1ULL << shift))
			reta_conf[idx].reta[shift] = hw->rss_indir_tbl[i];
	}

	return 0;
}

static int
rtl_rss_hash_update(struct rte_eth_dev *dev, struct rte_eth_rss_conf *rss_conf)
{
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;
	u32 rss_ctrl_8125;

	if (!hw->EnableRss || !(rss_conf->rss_hf & RTL_RSS_OFFLOAD_ALL))
		return -EINVAL;

	if (rss_conf->rss_key)
		memcpy(hw->rss_key, rss_conf->rss_key, RTL_RSS_KEY_SIZE);

	rtl8125_store_rss_key(hw);

	rss_ctrl_8125 = RTL_R32(hw, RSS_CTRL_8125) & ~RTL_RSS_CTRL_OFFLOAD_ALL;

	if (rss_conf->rss_hf & RTE_ETH_RSS_IPV4)
		rss_ctrl_8125 |= RSS_CTRL_IPV4_SUPP;
	if (rss_conf->rss_hf & RTE_ETH_RSS_NONFRAG_IPV4_TCP)
		rss_ctrl_8125 |= RSS_CTRL_TCP_IPV4_SUPP;
	if (rss_conf->rss_hf & RTE_ETH_RSS_NONFRAG_IPV6_TCP)
		rss_ctrl_8125 |= RSS_CTRL_TCP_IPV6_SUPP;
	if (rss_conf->rss_hf & RTE_ETH_RSS_IPV6)
		rss_ctrl_8125 |= RSS_CTRL_IPV6_SUPP;
	if (rss_conf->rss_hf & RTE_ETH_RSS_IPV6_EX)
		rss_ctrl_8125 |= RSS_CTRL_IPV6_EXT_SUPP;
	if (rss_conf->rss_hf & RTE_ETH_RSS_IPV6_TCP_EX)
		rss_ctrl_8125 |= RSS_CTRL_TCP_IPV6_EXT_SUPP;
	if (rss_conf->rss_hf & RTE_ETH_RSS_NONFRAG_IPV4_UDP)
		rss_ctrl_8125 |= RSS_CTRL_UDP_IPV4_SUPP;
	if (rss_conf->rss_hf & RTE_ETH_RSS_NONFRAG_IPV6_UDP)
		rss_ctrl_8125 |= RSS_CTRL_UDP_IPV6_SUPP;
	if (rss_conf->rss_hf & RTE_ETH_RSS_IPV6_UDP_EX)
		rss_ctrl_8125 |= RSS_CTRL_UDP_IPV6_EXT_SUPP;

	RTL_W32(hw, RSS_CTRL_8125, rss_ctrl_8125);

	return 0;
}

static int
rtl_rss_hash_conf_get(struct rte_eth_dev *dev, struct rte_eth_rss_conf *rss_conf)
{
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;
	u64 rss_hf = 0;
	u32 rss_ctrl_8125;

	if (!hw->EnableRss) {
		rss_conf->rss_hf = rss_hf;
		return 0;
	}

	if (rss_conf->rss_key) {
		rss_conf->rss_key_len = RTL_RSS_KEY_SIZE;
		memcpy(rss_conf->rss_key, hw->rss_key, RTL_RSS_KEY_SIZE);
	}

	rss_ctrl_8125 = RTL_R32(hw, RSS_CTRL_8125);

	if (rss_ctrl_8125 & RSS_CTRL_IPV4_SUPP)
		rss_hf |= RTE_ETH_RSS_IPV4;
	if (rss_ctrl_8125 & RSS_CTRL_TCP_IPV4_SUPP)
		rss_hf |= RTE_ETH_RSS_NONFRAG_IPV4_TCP;
	if (rss_ctrl_8125 & RSS_CTRL_TCP_IPV6_SUPP)
		rss_hf |= RTE_ETH_RSS_NONFRAG_IPV6_TCP;
	if (rss_ctrl_8125 & RSS_CTRL_IPV6_SUPP)
		rss_hf |= RTE_ETH_RSS_IPV6;
	if (rss_ctrl_8125 & RSS_CTRL_IPV6_EXT_SUPP)
		rss_hf |= RTE_ETH_RSS_IPV6_EX;
	if (rss_ctrl_8125 & RSS_CTRL_TCP_IPV6_EXT_SUPP)
		rss_hf |= RTE_ETH_RSS_IPV6_TCP_EX;
	if (rss_ctrl_8125 & RSS_CTRL_UDP_IPV4_SUPP)
		rss_hf |= RTE_ETH_RSS_NONFRAG_IPV4_UDP;
	if (rss_ctrl_8125 & RSS_CTRL_UDP_IPV6_SUPP)
		rss_hf |= RTE_ETH_RSS_NONFRAG_IPV6_UDP;
	if (rss_ctrl_8125 & RSS_CTRL_UDP_IPV6_EXT_SUPP)
		rss_hf |= RTE_ETH_RSS_IPV6_UDP_EX;

	rss_conf->rss_hf = rss_hf;

	return 0;
}

#if defined(RTE_SOC_SPACEMIT_K1) || defined(RTE_SOC_SPACEMIT_K3)
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <inttypes.h>
#include <sys/types.h>

#define STMMAC_UIO_MAX_DEVICE_FILE_NAME_LENGTH	30
#define STMMAC_UIO_MAX_ATTR_FILE_NAME	100
#define STMMAC_UIO_DEVICE_SYS_ATTR_PATH	"/sys/class/uio"
#define STMMAC_UIO_DEVICE_SYS_MAP_ATTR	"maps/map"
#define STMMAC_UIO_DEVICE_FILE_NAME	"/dev/uio"
#define STMMAC_UIO_REG_MAP_ID		0
#define STMMAC_UIO_RX_BD_MAP_ID	1
#define STMMAC_UIO_TX_BD_MAP_ID	2
#define STMMAC_UIO_RX_BD1_MAP_ID	3
#define STMMAC_UIO_TX_BD1_MAP_ID	4

u64 r8169_gbd_addr_b_p[5];
u64 r8169_gbd_addr_r_p[5];
u64 r8169_gbd_addr_t_p[5];
u64 r8169_gbd_addr_x_p[5];

void *r8169_gbd_addr_b_v[5];
void *r8169_gbd_addr_t_v[5];
void *r8169_gbd_addr_r_v[5];
void *r8169_gbd_addr_x_v[5];

size_t r8169_gbd_b_size[5];
size_t r8169_gbd_r_size[5];
size_t r8169_gbd_t_size[5];
size_t r8169_gbd_x_size[5];

struct uio_job {
	uint32_t fec_id;
	int uio_fd;
	void *bd_start_addr;
	void *register_base_addr;
	int map_size;
	uint64_t map_addr;
	int uio_minor_number;
};
static struct uio_job guio_job;

int r8169_get_uio_dev(struct rte_eth_dev *eth_dev)
{
	struct rte_pci_device *pci_dev = RTE_ETH_DEV_TO_PCI(eth_dev);
	struct rte_pci_addr *loc = &pci_dev->addr;
	int uio_num = -1;
	struct dirent *e;
	DIR *dir;
	char dirname[PATH_MAX];

	/* depending on kernel version, uio can be located in uio/uioX
	 * or uio:uioX */

	snprintf(dirname, sizeof(dirname),
			"%s/" PCI_PRI_FMT "/uio", rte_pci_get_sysfs_path(),
			loc->domain, loc->bus, loc->devid, loc->function);

	dir = opendir(dirname);
	if (dir == NULL) {
		/* retry with the parent directory */
		snprintf(dirname, sizeof(dirname),
				"%s/" PCI_PRI_FMT, rte_pci_get_sysfs_path(),
				loc->domain, loc->bus, loc->devid, loc->function);
		dir = opendir(dirname);

		if (dir == NULL) {
			printf("Error: cannot opendir %s\n", dirname);
			return -1;
		}
	}

	/* take the first file starting with "uio" */
	while ((e = readdir(dir)) != NULL) {
		/* format could be uio%d ...*/
		int shortprefix_len = sizeof("uio") - 1;
		/* ... or uio:uio%d */
		int longprefix_len = sizeof("uio:uio") - 1;
		char *endptr;

		if (strncmp(e->d_name, "uio", 3) != 0)
			continue;

		/* first try uio%d */
		errno = 0;
		uio_num = strtoull(e->d_name + shortprefix_len, &endptr, 10);
		if (errno == 0 && endptr != (e->d_name + shortprefix_len)) {
			break;
		}

		/* then try uio:uio%d */
		errno = 0;
		uio_num = strtoull(e->d_name + longprefix_len, &endptr, 10);
		if (errno == 0 && endptr != (e->d_name + longprefix_len)) {
			break;
		}
	}
	closedir(dir);

	/* No uio resource found */
	if (e == NULL)
		return -1;

	return uio_num;
}

/*
 * @brief Reads first line from a file.
 * Composes file name as: root/subdir/filename
 *
 * @param [in]  root     Root path
 * @param [in]  subdir   Subdirectory name
 * @param [in]  filename File name
 * @param [out] line     The first line read from file.
 *
 * @retval 0 for success
 * @retval other value for error
 */
static int
file_read_first_line(const char root[], const char subdir[],
			const char filename[], char *line)
{
	char absolute_file_name[STMMAC_UIO_MAX_ATTR_FILE_NAME];
	int fd = 0, ret = 0;

	/*compose the file name: root/subdir/filename */
	memset(absolute_file_name, 0, sizeof(absolute_file_name));
	snprintf(absolute_file_name, STMMAC_UIO_MAX_ATTR_FILE_NAME,
		"%s/%s/%s", root, subdir, filename);

	fd = open(absolute_file_name, O_RDONLY);
	if (fd <= 0)
		printf("Error opening file %s\n", absolute_file_name);

	/* read UIO device name from first line in file */
	ret = read(fd, line, STMMAC_UIO_MAX_DEVICE_FILE_NAME_LENGTH);
	if (ret <= 0) {
		printf("Error reading file %s\n", absolute_file_name);
		return ret;
	}
	close(fd);

	/* NULL-ify string */
	line[ret] = '\0';

	return 0;
}

/*
 * @brief Maps rx-tx bd range assigned for a bd ring.
 *
 * @param [in] uio_device_fd    UIO device file descriptor
 * @param [in] uio_device_id    UIO device id
 * @param [in] uio_map_id       UIO allows maximum 5 different mapping for
				each device. Maps start with id 0.
 * @param [out] map_size        Map size.
 * @param [out] map_addr	Map physical address
 *
 * @retval  NULL if failed to map registers
 * @retval  Virtual address for mapped register address range
 */
static void *
guio_map_mem(int uio_device_fd, int uio_device_id,
		int uio_map_id, int *map_size, uint64_t *map_addr)
{
	void *mapped_address = NULL;
	u64 uio_map_size = 0;
	phys_addr_t uio_map_p_addr = 0;
	char uio_sys_root[100];
	char uio_sys_map_subdir[100];
	char uio_map_size_str[30 + 1];
	char uio_map_p_addr_str[32];
	int ret = 0;

	/* compose the file name: root/subdir/filename */
	memset(uio_sys_root, 0, sizeof(uio_sys_root));
	memset(uio_sys_map_subdir, 0, sizeof(uio_sys_map_subdir));
	memset(uio_map_size_str, 0, sizeof(uio_map_size_str));
	memset(uio_map_p_addr_str, 0, sizeof(uio_map_p_addr_str));

	/* Compose string: /sys/class/uio/uioX */
	snprintf(uio_sys_root, sizeof(uio_sys_root), "%s/%s%d",
			"/sys/class/uio", "uio", uio_device_id);
	/* Compose string: maps/mapY */
	snprintf(uio_sys_map_subdir, sizeof(uio_sys_map_subdir), "%s%d",
			"maps/map", uio_map_id);

	printf("US_UIO: uio_map_mem uio_sys_root: %s, uio_sys_map_subdir: %s, uio_map_size_str: %s\n",
			uio_sys_root, uio_sys_map_subdir, uio_map_size_str);

	/* Read first (and only) line from file
	 * /sys/class/uio/uioX/maps/mapY/size
	 */
	ret = file_read_first_line(uio_sys_root, uio_sys_map_subdir,
				"size", uio_map_size_str);
	if (ret < 0) {
		printf("file_read_first_line() failed\n");
		return NULL;
	}
	ret = file_read_first_line(uio_sys_root, uio_sys_map_subdir,
				"addr", uio_map_p_addr_str);
	if (ret < 0) {
		printf("file_read_first_line() failed\n");
		return NULL;
	}

	/* Read mapping size and physical address expressed in hexa(base 16) */
	uio_map_size = strtol(uio_map_size_str, NULL, 16);
	uio_map_p_addr = strtol(uio_map_p_addr_str, NULL, 16);
	printf("kernel size: 0x%lx, addr: 0x%lx\n", uio_map_size, uio_map_p_addr);

	/* Map the BD memory in user space */
	mapped_address = mmap(NULL, uio_map_size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED, uio_device_fd, (uio_map_id * 4096));

	if (mapped_address == MAP_FAILED) {
		printf("Failed to map! errno = %d uio job fd = %d,"
			"uio device id = %d, uio map id = %d\n", errno,
			uio_device_fd, uio_device_id, uio_map_id);
		return NULL;
	}

	/* Save the map size to use it later on for munmap-ing */
	*map_size = uio_map_size;
	*map_addr = uio_map_p_addr;

	printf("UIO dev[%d] mapped region [id =%d] size 0x%lx map_addr_p: 0x%lx, at %p\n",
		uio_device_id, uio_map_id, uio_map_size, *map_addr, mapped_address);

	printf("UIO dev[%d] mapped region [id =%d] size 0x%lx at phy 0x%lx\n",
		uio_device_id, uio_map_id, uio_map_size, rte_mem_virt2phy(mapped_address));

	return mapped_address;
}

static int
rconfig_pcie_uio(struct rte_eth_dev *eth_dev)
{
	char uio_device_file_name[32];
	int index;

	printf("rconfig_pcie_uio\n");

	index = r8169_get_uio_dev(eth_dev);
	if ((index < 0) && (index > 4))
		return -1;

	snprintf(uio_device_file_name, sizeof(uio_device_file_name), "/dev/uio%d",
			index);

	/* Open device file */
	guio_job.uio_fd = open(uio_device_file_name, O_RDWR);
	if (guio_job.uio_fd < 0) {
		printf("Unable to open STMMAC_UIO file\n");
		return -1;
	}

	r8169_gbd_addr_b_v[index] = guio_map_mem(guio_job.uio_fd,
		index, 0,
		&guio_job.map_size, &guio_job.map_addr);
	if (r8169_gbd_addr_b_v[index] == NULL)
		return -ENOMEM;
	r8169_gbd_addr_b_p[index] = guio_job.map_addr;
	r8169_gbd_b_size[index] = guio_job.map_size;

	r8169_gbd_addr_t_v[index] = guio_map_mem(guio_job.uio_fd,
		index, 2,
		&guio_job.map_size, &guio_job.map_addr);
	if (r8169_gbd_addr_t_v[index] == NULL)
		return -ENOMEM;
	r8169_gbd_addr_t_p[index] = guio_job.map_addr;
	r8169_gbd_t_size[index] = guio_job.map_size;

	r8169_gbd_addr_r_v[index] = guio_map_mem(guio_job.uio_fd,
		index, 3,
		&guio_job.map_size, &guio_job.map_addr);
	if (r8169_gbd_addr_r_v[index] == NULL)
		return -ENOMEM;
	r8169_gbd_addr_r_p[index] = guio_job.map_addr;
	r8169_gbd_r_size[index] = guio_job.map_size;

	r8169_gbd_addr_x_v[index] = guio_map_mem(guio_job.uio_fd,
		index, 4,
		&guio_job.map_size, &guio_job.map_addr);
	if (r8169_gbd_addr_x_v[index] == NULL)
		return -ENOMEM;
	r8169_gbd_addr_x_p[index] = guio_job.map_addr;
	r8169_gbd_x_size[index] = guio_job.map_size;

	return 0;
}
#endif

static int
rtl_dev_init(struct rte_eth_dev *dev)
{
	struct rte_pci_device *pci_dev = RTE_ETH_DEV_TO_PCI(dev);
	struct rte_intr_handle *intr_handle = pci_dev->intr_handle;
	struct rtl_adapter *adapter = RTL_DEV_PRIVATE(dev);
	struct rtl_hw *hw = &adapter->hw;
	struct rte_ether_addr *perm_addr = (struct rte_ether_addr *)hw->mac_addr;
	char buf[RTE_ETHER_ADDR_FMT_SIZE];

	dev->dev_ops = &rtl_eth_dev_ops;
	dev->tx_pkt_burst = &rtl_xmit_pkts;
	dev->rx_pkt_burst = &rtl_recv_pkts;

	/* For secondary processes, the primary process has done all the work. */
	if (rte_eal_process_type() != RTE_PROC_PRIMARY) {
		if (dev->data->scattered_rx)
			dev->rx_pkt_burst = &rtl_recv_scattered_pkts;
		return 0;
	}

	/* R8169 uses BAR2 */
	hw->mmio_addr = (u8 *)pci_dev->mem_resource[2].addr;
#if defined(RTE_SOC_SPACEMIT_K1) || defined(RTE_SOC_SPACEMIT_K3)
	rconfig_pcie_uio(dev);
	int index;
	index = r8169_get_uio_dev(dev);

	printf("virt_addr 0x%lx:0x%lx\n", (unsigned long)pci_dev->mem_resource[2].addr, (unsigned long)r8169_gbd_addr_b_v[index]);
	printf("phys_addr 0x%lx:0x%lx\n", pci_dev->mem_resource[2].phys_addr, r8169_gbd_addr_b_p[index]);
#endif

	rtl_get_mac_version(hw, pci_dev);

	if (rtl_set_hw_ops(hw))
		return -ENOTSUP;

	rtl_disable_intr(hw);

	rtl_hw_initialize(hw);

	/* Read the permanent MAC address out of ROM */
	rtl_get_mac_address(hw, perm_addr);

	if (!rte_is_valid_assigned_ether_addr(perm_addr)) {
		rte_eth_random_addr(&perm_addr->addr_bytes[0]);

		rte_ether_format_addr(buf, sizeof(buf), perm_addr);

		PMD_INIT_LOG(NOTICE, "r8169: Assign randomly generated MAC address %s", buf);
	}

	/* Allocate memory for storing MAC addresses */
	dev->data->mac_addrs = rte_zmalloc("r8169", RTE_ETHER_ADDR_LEN, 0);

	if (dev->data->mac_addrs == NULL) {
		PMD_INIT_LOG(ERR, "MAC Malloc failed");
		return -ENOMEM;
	}

	/* Copy the permanent MAC address */
	rte_ether_addr_copy(perm_addr, &dev->data->mac_addrs[0]);

	rtl_rar_set(hw, &perm_addr->addr_bytes[0]);

	rte_intr_callback_register(intr_handle, rtl_dev_interrupt_handler, dev);

	/* Enable uio/vfio intr/eventfd mapping */
	rte_intr_enable(intr_handle);

	return 0;
}

static int
rtl_dev_uninit(struct rte_eth_dev *dev)
{
	if (rte_eal_process_type() != RTE_PROC_PRIMARY)
		return -EPERM;

	rtl_dev_close(dev);

	return 0;
}

static int
rtl_dev_reset(struct rte_eth_dev *dev)
{
	int ret;

	ret = rtl_dev_uninit(dev);
	if (ret)
		return ret;

	ret = rtl_dev_init(dev);

	return ret;
}

static int
rtl_pci_probe(struct rte_pci_driver *pci_drv __rte_unused,
	      struct rte_pci_device *pci_dev)
{
	return rte_eth_dev_pci_generic_probe(pci_dev, sizeof(struct rtl_adapter),
					     rtl_dev_init);
}

static int
rtl_pci_remove(struct rte_pci_device *pci_dev)
{
	return rte_eth_dev_pci_generic_remove(pci_dev, rtl_dev_uninit);
}

static struct rte_pci_driver rte_r8169_pmd = {
	.id_table  = pci_id_r8169_map,
	.drv_flags = RTE_PCI_DRV_NEED_MAPPING | RTE_PCI_DRV_INTR_LSC,
	.probe     = rtl_pci_probe,
	.remove    = rtl_pci_remove,
};

RTE_PMD_REGISTER_PCI(net_r8169, rte_r8169_pmd);
RTE_PMD_REGISTER_PCI_TABLE(net_r8169, pci_id_r8169_map);
RTE_PMD_REGISTER_KMOD_DEP(net_r8169, "* igb_uio | uio_pci_generic | vfio-pci");

RTE_LOG_REGISTER_SUFFIX(r8169_logtype_init, init, NOTICE)
RTE_LOG_REGISTER_SUFFIX(r8169_logtype_driver, driver, NOTICE)
#ifdef RTE_ETHDEV_DEBUG_RX
RTE_LOG_REGISTER_SUFFIX(r8169_logtype_rx, rx, DEBUG)
#endif
#ifdef RTE_ETHDEV_DEBUG_TX
RTE_LOG_REGISTER_SUFFIX(r8169_logtype_tx, tx, DEBUG)
#endif
