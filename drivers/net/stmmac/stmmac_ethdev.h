/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Spacemit Corporation.
 */

#ifndef __STMMAC_ETHDEV_H__
#define __STMMAC_ETHDEV_H__

#include "descs.h"
#include <rte_ethdev.h>

#define STMMAC_RX_COE_NONE	0
#define STMMAC_RX_COE_TYPE1	1
#define STMMAC_RX_COE_TYPE2	2

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define BD_LEN			49152
#define STMMAC_TX_FR_SIZE	2048
#define ETH_HLEN		RTE_ETHER_HDR_LEN

/* full duplex */
#define FULL_DUPLEX		0x00

#define PKT_MAX_BUF_SIZE	1984
#define OPT_FRAME_SIZE		(PKT_MAX_BUF_SIZE << 16)
#define STMMAC_MAX_RX_PKT_LEN	3000

#if defined(RTE_SOC_SPACEMIT_K3)
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

uint16_t stmmac_recv_pkts(void *rxq1, struct rte_mbuf **rx_pkts,
		uint16_t nb_pkts);
uint16_t stmmac_xmit_pkts(void *tx_queue, struct rte_mbuf **tx_pkts,
		uint16_t nb_pkts);

#endif /*__STMMAC_ETHDEV_H__*/
