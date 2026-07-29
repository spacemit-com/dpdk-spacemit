/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025 Spacemit Corporation.
 */

#ifndef _K1XMAC_LOGS_H_
#define _K1XMAC_LOGS_H_

#include <rte_log.h>

extern int k1xmac_logtype_pmd;

/* PMD related logs */
#define K1XMAC_PMD_LOG(level, fmt, args...) \
	rte_log(RTE_LOG_ ## level, k1xmac_logtype_pmd, "k1xmac_net: %s() " \
		fmt, __func__, ##args)

#define PMD_INIT_FUNC_TRACE() K1XMAC_PMD_LOG(DEBUG, " >>")

#define K1XMAC_PMD_DEBUG(fmt, args...) \
	K1XMAC_PMD_LOG(DEBUG, fmt, ## args)
#define K1XMAC_PMD_ERR(fmt, args...) \
	K1XMAC_PMD_LOG(ERR, fmt, ## args)
#define K1XMAC_PMD_INFO(fmt, args...) \
	K1XMAC_PMD_LOG(INFO, fmt, ## args)

#define K1XMAC_PMD_WARN(fmt, args...) \
	K1XMAC_PMD_LOG(WARNING, fmt, ## args)

/* DP Logs, toggled out at compile time if level lower than current level */
#define K1XMAC_DP_LOG(level, fmt, args...) \
	RTE_LOG_DP(level, PMD, fmt, ## args)

#endif /* _K1XMAC_LOGS_H_ */
