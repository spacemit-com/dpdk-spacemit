/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025 Spacemit Corporation.
 */

#include "k1xmac_ethdev.h"

#ifndef _K1XMAC_UIO_H_
#define _K1XMAC_UIO_H_

/* Prefix path to sysfs directory where UIO device attributes are exported.
 * Path for UIO device X is /sys/class/uio/uioX
 */
#define K1XMAC_UIO_DEVICE_SYS_ATTR_PATH	"/sys/class/uio"

/* Subfolder in sysfs where mapping attributes are exported
 * for each UIO device. Path for mapping Y for device X is:
 * /sys/class/uio/uioX/maps/mapY
 */
#define K1XMAC_UIO_DEVICE_SYS_MAP_ATTR	"maps/map"

/* Name of UIO device file prefix. Each UIO device will have a device file
 * /dev/uioX, where X is the minor device number.
 */
#define K1XMAC_UIO_DEVICE_FILE_NAME	"/dev/uio"
/*
 * Name of UIO device. User space K1XMAC will have a corresponding
 * UIO device.
 * Maximum length is #K1XMAC_UIO_MAX_DEVICE_NAME_LENGTH.
 *
 * @note  Must be kept in sync with K1XMAC kernel driver
 * define #K1XMAC_UIO_DEVICE_NAME !
 */
#define K1XMAC_UIO_DEVICE_NAME     "smt-k1x-uio"

/* Maximum length for the name of an UIO device file.
 * Device file name format is: /dev/uioX.
 */
#define K1XMAC_UIO_MAX_DEVICE_FILE_NAME_LENGTH	30

/* Maximum length for the name of an attribute file for an UIO device.
 * Attribute files are exported in sysfs and have the name formatted as:
 * /sys/class/uio/uioX/<attribute_file_name>
 */
#define K1XMAC_UIO_MAX_ATTR_FILE_NAME	100

/* The id for the mapping used to export K1XMAC registers and BD memory to
 * user space through UIO device.
 */
#define K1XMAC_UIO_REG_MAP_ID		0
#define K1XMAC_UIO_RX_BD_MAP_ID         1
#define K1XMAC_UIO_TX_BD_MAP_ID         2
#define K1XMAC_UIO_RX_BD1_MAP_ID	3
#define K1XMAC_UIO_TX_BD1_MAP_ID	4

#define MAP_PAGE_SIZE			4096

struct uio_job {
	uint32_t fec_id;
	int uio_fd;
	void *bd_start_addr;
	void *register_base_addr;
	int map_size;
	uint64_t map_addr;
	int uio_minor_number;
};

int k1xmac_configure(int id);
int config_k1xmac_uio(struct k1xmac_private *private);
void k1xmac_uio_init(void);
void k1xmac_cleanup(struct k1xmac_private *private);

#endif /* _K1XMAC_UIO_H_ */
