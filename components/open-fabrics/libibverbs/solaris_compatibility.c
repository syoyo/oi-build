/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

/*
 * OFED Solaris wrapper
 */
#if defined(__SVR4) && defined(__sun)

#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/processor.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <kstat.h>

#include <alloca.h>
#include "../include/infiniband/arch.h"
#include "../include/infiniband/verbs.h"
#include "../../libmthca/libmthca-1.0.5/src/mthca-abi.h"
#include "../../libmlx4/libmlx4-1.0.1/src/mlx4-abi.h"
#include "../../librdmacm/librdmacm-1.0.15/include/rdma/rdma_cma_abi.h"
#include <libdevinfo.h>

#define HW_DRIVER_MAX_NAME_LEN			20
#define UVERBS_KERNEL_SYSFS_NAME		"sol_uverbs@0:uverbs0"
#define UVERBS					"sol_uverbs"
#define UMAD					"sol_umad"
#define	UVERBS_IB_DEVPATH_PREFIX		"/devices/ib"
#define CONNECTX_NAME				"mlx4_"
#define INFINIHOST_NAME				"mthca"

#define	MELLANOX_VENDOR_ID			0x15b3
#define PCI_DEVICE_ID_MELLANOX_TAVOR		0x5a44
#define PCI_DEVICE_ID_MELLANOX_ARBEL		0x6282
#define PCI_DEVICE_ID_MELLANOX_ARBEL_COMPAT	0x6278
#define PCI_DEVICE_ID_MELLANOX_HERMON_SDR	0x6340
#define PCI_DEVICE_ID_MELLANOX_HERMON_DDR	0x634a
#define PCI_DEVICE_ID_MELLANOX_HERMON_QDR	0x6354
#define PCI_DEVICE_ID_MELLANOX_HERMON_DDR_PCIE2	0x6732
#define PCI_DEVICE_ID_MELLANOX_HERMON_QDR_PCIE2	0x673c
#define	INFINIHOST_DEVICE_ID_2			0x5a45
#define	INFINIHOST_DEVICE_ID_4			0x6279



/*
 * sol_uverbs_drv_status is the status of what libibverbs knows
 * about the status of sol_uverbs driver.
 */
#define	SOL_UVERBS_DRV_STATUS_UNKNOWN	0x0
#define	SOL_UVERBS_DRV_STATUS_LOADED	0x1
#define	SOL_UVERBS_DRV_STATUS_UNLOADED	0x02

static kstat_ctl_t	*kc = NULL;	/* libkstat cookie */
static int sol_uverbs_drv_status = SOL_UVERBS_DRV_STATUS_UNKNOWN;

/*
 * check_path() prefixs
 */
typedef enum cp_prefix_e {
	CP_SOL_UVERBS		= 1,
	CP_DEVICE		= 2,	
	CP_D			= 3,
	CP_GIDS			= 4,
	CP_PKEYS		= 5,
	CP_MTHCA		= 6,
	CP_MLX4			= 7,
	CP_PORTS		= 8,
	CP_UMAD			= 9,
	CP_SLASH		= 10,
	CP_SYS			= 11,
	CP_CLASS		= 12,
	CP_INFINIBAND_VERBS	= 13,
	CP_INFINIBAND		= 14,
	CP_INFINIBAND_MAD	= 15,
	CP_MISC			= 16,
	CP_RDMA_CM		= 17
} cp_prefix_t;

void __attribute__((constructor))solaris_init(void);
void __attribute__((destructor))solaris_fini(void);

void
solaris_init(void)
{
	while ((kc = kstat_open()) == NULL) {
		if (errno == EAGAIN)
			(void) poll(NULL, 0, 200);
		else
			fprintf(stderr, "cannot open /dev/kstat: %s\n",
			    strerror(errno));
	}
}

void
solaris_fini(void)
{
	(void) kstat_close(kc);
}

/*
 * Some sysfs emulation software
 */


/* 
 * Check whether a path starts with prefix, and if it does, remove it
 * from the string. The prefix can also contains one %d scan argument.
 */
static int check_path(char *path, cp_prefix_t prefix, unsigned int *arg)
{
	int	ret, pos = 0;

	switch (prefix) {
		case CP_SOL_UVERBS:
			ret = sscanf(path, "sol_uverbs@0:uverbs%d%n/", arg,
			    &pos);	
			break;
		case CP_DEVICE:
			ret = sscanf(path, "device%n/", &pos);
			break;
		case CP_D:
			ret = sscanf(path, "%d%n/", arg, &pos);
			break;
		case CP_GIDS:
			ret = sscanf(path, "gids%n/", &pos);
			break;
		case CP_PKEYS:
			ret = sscanf(path, "pkeys%n/", &pos);
			break;
		case CP_MTHCA:
			ret = sscanf(path, "mthca%d%n/", arg, &pos);	
			break;
		case CP_MLX4:
			ret = sscanf(path, "mlx4_%d%n/", arg, &pos);	
			break;
		case CP_PORTS:
			ret = sscanf(path, "ports%n/", &pos);
			break;
		case CP_UMAD:
			ret = sscanf(path, "umad%d%n/", arg, &pos);	
			break;
		case CP_SLASH:
			ret = sscanf(path, "%n/", &pos);
			break;
		case CP_SYS:
			ret = sscanf(path, "sys%n/", &pos);
			break;
		case CP_CLASS:
			ret = sscanf(path, "class%n/", &pos);
			break;
		case CP_INFINIBAND_VERBS:
			ret = sscanf(path, "infiniband_verbs%n/", &pos);
			break;
		case CP_INFINIBAND:
			ret = sscanf(path, "infiniband%n/", &pos);
			break;
		case CP_INFINIBAND_MAD:
			ret = sscanf(path, "infiniband_mad%n/", &pos);
			break;
		case CP_MISC:
			ret = sscanf(path, "misc%n/", &pos);
			break;
		case CP_RDMA_CM:
			ret = sscanf(path, "rdma_cm%n/", &pos);
			break;
		default:
			/* Unkown prefix */
			return 0;
	}

	if (path[pos] == '/') {
		/* Some requests have several consecutive slashes. */
		while (path[pos] == '/')
			pos ++;

		memmove(path, &path[pos], strlen(path)-pos+1);
		return 1;
	}

	return 0;
}

static struct ibv_context *get_device_context(const char *devname)
{
	struct ibv_device **root_dev_list, **dev_list = NULL;
	struct ibv_context *ctx = NULL;

	/* We need some specific infos. Open the device. */
	root_dev_list = dev_list = ibv_get_device_list(NULL);
	if (!dev_list) {
		goto cleanup;
	}

	while (*dev_list) {
		if (!strcmp(ibv_get_device_name(*dev_list), devname))
			break;
		++dev_list;
	}

	if (!*dev_list) {
		goto cleanup;
	}

	ctx = ibv_open_device(*dev_list);

 cleanup:
	if (root_dev_list) ibv_free_device_list(root_dev_list);

	return ctx;
}

static int
get_device_info(const char *devname, struct ibv_device_attr *device_attr)
{
	struct ibv_context *ctx = NULL;
	int ret;

	ctx = get_device_context(devname);
	if (!ctx) {
		ret = -1;
		goto cleanup;
	}

	if (ibv_query_device(ctx, device_attr)) {
		ret = -1;
		goto cleanup;
	}

	ret = 0;

 cleanup:
	if (ctx) ibv_close_device(ctx);

	return ret;
}

/*
 * Get the IB user verbs port info attributes for the specified device/port.
 * If the address of a gid pointer is passed for "gid_table", the memory
 * will be allocated and the ports gid table returned as well.  The caller
 * must free this memory on successful completion.  If the address of a
 * pkey pointer is passed for "pkey_table", the memory will be allocated
 * and the ports pkey table returned as well.  The caller must free this
 * memory on successful completion.
 */
int
get_port_info(const char *devname, uint8_t port_num,
	 struct ibv_port_attr *port_attr, union ibv_gid **gid_table,
	 uint16_t **pkey_table)
{
	struct ibv_context *ctx = NULL;
	union ibv_gid *gids     = NULL;
	uint16_t      *pkeys    = NULL;
	int ret;
	int i;

	ctx = get_device_context(devname);
	if (!ctx) {
		ret = -1;
		goto cleanup;
	}

	if (ibv_query_port(ctx, port_num, port_attr)) {
		ret = -1;
		goto cleanup;
	}

	if (gid_table) {
		*gid_table = NULL;
		gids = malloc(sizeof(union ibv_gid) * port_attr->gid_tbl_len);
		if (!gids) {
			ret = -1;
			goto cleanup;
		}
		for (i=0; i<port_attr->gid_tbl_len; i++) {
			if (ibv_query_gid(ctx, port_num, i, &gids[i])) {
				ret = -1;
				goto cleanup;
			}
		}
		*gid_table = gids;
		gids = NULL;
	}

	if (pkey_table) {
		*pkey_table = NULL;
		pkeys = malloc(sizeof(uint16_t) * port_attr->pkey_tbl_len);
		if (!pkeys) {
			ret = -1;
			goto cleanup;
		}
		for (i=0; i<port_attr->pkey_tbl_len; i++) {
			if (ibv_query_pkey(ctx, port_num, i, &pkeys[i])) {
				ret = -1;
				goto cleanup;
			}
		}
		*pkey_table = pkeys;
		pkeys = NULL;
	}

	ret = 0;

 cleanup:
	if (ctx)
		ibv_close_device(ctx);
	if (gids) {
		free(gids);
	}
	if (pkeys) {
		free(pkeys);
	}

	return ret;
}

static void uverbs_driver_check()
{
	int	fd;
	char	uverbs_devpath[MAXPATHLEN];

	snprintf(uverbs_devpath, MAXPATHLEN, "%s/%s",
	    UVERBS_IB_DEVPATH_PREFIX, UVERBS_KERNEL_SYSFS_NAME);

	if ((fd = open(uverbs_devpath, O_RDWR)) < 0) {
		sol_uverbs_drv_status = SOL_UVERBS_DRV_STATUS_UNLOADED;
		return;
	}

	sol_uverbs_drv_status = SOL_UVERBS_DRV_STATUS_LOADED;
	close(fd);
}

/*
 * Generic routine to return int attributes associated with a specific
 * user verbs device.  The Solaris user verbs agent exports some 
 * properties that would normally be exported via the sysfs in Linux.
 */
int get_device_int_attr(char *driver, int minor, char *attr)
{
	di_node_t           root_node;
	di_node_t           node;
	di_minor_t          node_minor;
	int                 *ret;
	int                 int_value;

	if (sol_uverbs_drv_status == SOL_UVERBS_DRV_STATUS_UNKNOWN)
		uverbs_driver_check();

	if (sol_uverbs_drv_status == SOL_UVERBS_DRV_STATUS_UNLOADED)
		return (-1);
			
	root_node = di_init("/", DINFOCPYALL);
	if (root_node == DI_NODE_NIL) {
		goto err_dev;
	}

	node = di_drv_first_node(driver, root_node);
	if (node == DI_NODE_NIL) {
		goto err_dev;
	}

	node_minor = di_minor_next(node, DI_MINOR_NIL);
	while (node_minor != DI_MINOR_NIL) {
		if ((di_minor_devt(node_minor) & 0x0000FFFF) == minor)
			break;
		node_minor = di_minor_next(node, node_minor);
	}

	if (node_minor == DI_MINOR_NIL) {
		goto err_dev;
	}

	/*
	 * We have the uverbs node, return the requested int attribute.
	 */
	if (di_prop_lookup_ints(di_minor_devt(node_minor), node,
	                        attr, &ret) != 1) {
		goto err_dev;
	}

	int_value = *ret;
	di_fini(root_node);

	return int_value;

err_dev:
	di_fini(root_node);
	return -1;	
}

static char *
umad_convert_vendorid_deviceid_to_name(int vendor, int device, int hca)
{
	char *hca_name = "<unknown>";
	char *ibdev_name;
	int ret;

	if (vendor == MELLANOX_VENDOR_ID) {
		switch (device) {
		case PCI_DEVICE_ID_MELLANOX_HERMON_SDR:
		case PCI_DEVICE_ID_MELLANOX_HERMON_DDR:
		case PCI_DEVICE_ID_MELLANOX_HERMON_DDR_PCIE2:
		case PCI_DEVICE_ID_MELLANOX_HERMON_QDR_PCIE2:
			hca_name = CONNECTX_NAME;
			break;
		case PCI_DEVICE_ID_MELLANOX_TAVOR:
		case PCI_DEVICE_ID_MELLANOX_ARBEL_COMPAT:
		case INFINIHOST_DEVICE_ID_2:
		case INFINIHOST_DEVICE_ID_4:
			hca_name = INFINIHOST_NAME;
			break;
		}
	}
	ret = snprintf(NULL, 0, "%s%d", hca_name, hca);
	ibdev_name = malloc((size_t) ret);
	if (ibdev_name != NULL)
		(void) snprintf(ibdev_name, ret + 1, "%s%d", hca_name, hca);

	return ibdev_name;
}

static char *
umad_get_ibdev(char *minor_dev)
{
	di_node_t	root, umad_node;
	di_minor_t	minor_node;
	dev_t		dev;
	int		ret, *vendorids, *deviceids, *hcas;
	char		*ibdev, *minor_str;

	root = di_init("/", DINFOCPYALL);
	if (root == DI_NODE_NIL)
		return NULL;

	umad_node = di_drv_first_node("sol_umad", root);
	if (umad_node == DI_MINOR_NIL) {
		di_fini(root);
		return NULL;
	}
	minor_node = di_minor_next(umad_node, DI_MINOR_NIL);
	while (minor_node != DI_NODE_NIL) {
		minor_str = di_minor_name(minor_node);
		if (strcmp(minor_str, minor_dev) == 0)
			break;
		minor_node = di_minor_next(umad_node, minor_node);
	}
	if (minor_node == DI_MINOR_NIL) {
		di_fini(root);
		return NULL;
	}
	dev = di_minor_devt(minor_node);
	ret = di_prop_lookup_ints(dev, umad_node, "vendor-id", &vendorids);
	if (ret < 1) {
		di_fini(root);
		return NULL;
	}
	ret = di_prop_lookup_ints(dev, umad_node, "device-id", &deviceids);
	if (ret < 1) {
		di_fini(root);
		return NULL;
	}
	ret = di_prop_lookup_ints(dev, umad_node, "hca-instance",
	    &hcas);
	if (ret < 1) {
		di_fini(root);
		return NULL;
	}
	ibdev = umad_convert_vendorid_deviceid_to_name(*vendorids,
	    *deviceids, *hcas);

	di_fini(root);

	return ibdev;
}

static int
umad_get_port(char *minor_dev)
{
	di_node_t root, umad_node;
	di_minor_t minor_node;
	int ret;
	int *ports;
	char *minor_str;
	dev_t dev;

	root = di_init("/", DINFOCPYALL);
	if (root == DI_NODE_NIL)
		return -1;

	umad_node = di_drv_first_node("sol_umad", root);
	if (umad_node == DI_MINOR_NIL) {
		di_fini(root);
		return -1;
	}
	minor_node = di_minor_next(umad_node, DI_MINOR_NIL);
	while (minor_node != DI_NODE_NIL) {
		minor_str = di_minor_name(minor_node);
		if (strcmp(minor_str, minor_dev) == 0)
			break;
		minor_node = di_minor_next(umad_node, minor_node);
	}
	if (minor_node == DI_MINOR_NIL) {
		di_fini(root);
		return -1;
	}
	dev = di_minor_devt(minor_node);
	ret = di_prop_lookup_ints(dev, umad_node, "port", &ports);
	if (ret < 1) {
		di_fini(root);
		return -1;
	}
	ret = *ports;

	di_fini(root);

	return ret;
}

/*
 * Given the uverbs device number, determine the appropriate IB device
 * name for the desired uverbs interface, e.g. mthca0, mlx4_0, mlx4_1, etc.
 */
int uverbs_get_device_name(int devnum, char *name, size_t size)
{
	di_node_t           root_node;
	di_node_t           uverbs_node;
	di_minor_t          node_minor;
	int                 vendor_id;
	int                 device_id;
	int                 *ret_vendor_id;
	int                 *ret_device_id;
	int                 mthca_devs = 0;
	int                 mlx4_devs = 0;
	int                 unknown_devs = 0;

	if (name == NULL) {
		return -1;
	}

	root_node = di_init("/", DINFOCPYALL);
	if (root_node == DI_NODE_NIL) {
		goto err_dev;
	}

	uverbs_node = di_drv_first_node(UVERBS, root_node);
	if (uverbs_node == DI_NODE_NIL) {
		goto err_dev;
	}

	/*
	 * Since the actual hardware drivers don't export an OFED equivalent 
	 * name we go through the minor nodes counting the device indices for
	 * each hardware type up to the uverbs minor device requested.  
	 */
	node_minor = di_minor_next(uverbs_node, DI_MINOR_NIL);
	while (node_minor != DI_MINOR_NIL) {
		if (di_prop_lookup_ints(di_minor_devt(node_minor), uverbs_node,
	                                "vendor-id", &ret_vendor_id) != 1) {
			goto err_dev;
		}
		if (di_prop_lookup_ints(di_minor_devt(node_minor), uverbs_node,
	                                "device-id", &ret_device_id) != 1) {
			goto err_dev;
		}

		/*
		 * If the minor number requested, we are done.  Update the
		 * PCI vendor/device ID used to create the OFED IBDEV name;
		 * Otherwise update the appropriate IBDEV count.
		 */
		if ((di_minor_devt(node_minor) & 0x0000FFFF) == devnum) {
			vendor_id = *ret_vendor_id;
			device_id = *ret_device_id;
			break;
		}

		switch (*ret_device_id) {
			case PCI_DEVICE_ID_MELLANOX_TAVOR:
			case PCI_DEVICE_ID_MELLANOX_ARBEL:
			case PCI_DEVICE_ID_MELLANOX_ARBEL_COMPAT:
				mthca_devs++;
			break;

			case PCI_DEVICE_ID_MELLANOX_HERMON_SDR:
			case PCI_DEVICE_ID_MELLANOX_HERMON_DDR:
			case PCI_DEVICE_ID_MELLANOX_HERMON_QDR:
			case PCI_DEVICE_ID_MELLANOX_HERMON_DDR_PCIE2:
			case PCI_DEVICE_ID_MELLANOX_HERMON_QDR_PCIE2:
				mlx4_devs++;
			break;

			default:
				unknown_devs++;
			break;
		}
		node_minor = di_minor_next(uverbs_node, node_minor);
	}

	if (node_minor == DI_MINOR_NIL) {
		goto err_dev;
	}

	if ((di_minor_devt(node_minor) & 0x0000FFFF) != devnum) {
		goto err_dev;
	}

	switch (device_id) {
		case PCI_DEVICE_ID_MELLANOX_TAVOR:
		case PCI_DEVICE_ID_MELLANOX_ARBEL:
		case PCI_DEVICE_ID_MELLANOX_ARBEL_COMPAT:
			snprintf(name, size, "mthca%d", mthca_devs);
		break;

		case PCI_DEVICE_ID_MELLANOX_HERMON_SDR:
		case PCI_DEVICE_ID_MELLANOX_HERMON_DDR:
		case PCI_DEVICE_ID_MELLANOX_HERMON_QDR:
		case PCI_DEVICE_ID_MELLANOX_HERMON_DDR_PCIE2:
		case PCI_DEVICE_ID_MELLANOX_HERMON_QDR_PCIE2:
			snprintf(name, size, "mlx4_%d", mlx4_devs);
		break;

		default:
			snprintf(name, size, "unknown%d", unknown_devs);
		break;
	}

	di_fini(root_node);
	return 0;

err_dev:
	di_fini(root_node);
	return -1;
}

/*
 * For a given Solaris IB verbs psuedo device minor, determine the
 * associated driver name and device index.
 */
static int ibv_get_hw_driver_info(int devnum, char *name,
                                  size_t size, int *index)
{
        di_node_t           root_node;
        di_node_t           uverbs_node;
        di_minor_t          node_minor;
        int                 *ret_vendor_id;
        int                 *ret_device_id;
        int                 arbel_devs = 0;
        int                 tavor_devs = 0;
        int                 hermon_devs = 0;

        if (name == NULL) {
                return -1;
        }

        root_node = di_init("/", DINFOCPYALL);
        if (root_node == DI_NODE_NIL) {
                goto err_dev;

        }

        uverbs_node = di_drv_first_node(UVERBS, root_node);
        if (uverbs_node == DI_NODE_NIL) {
                goto err_dev;
        }
        /*
         * Since the actual hardware drivers don't export an OFED equivalent
         * name we go through the minor nodes counting the device indices for
         * each hardware type up to the uverbs minor device requested.
         */
        node_minor = di_minor_next(uverbs_node, DI_MINOR_NIL);
        while (node_minor != DI_MINOR_NIL) {
                if (di_prop_lookup_ints(di_minor_devt(node_minor), uverbs_node,
                                        "vendor-id", &ret_vendor_id) != 1) {
                        goto err_dev;
                }
                if (di_prop_lookup_ints(di_minor_devt(node_minor), uverbs_node,
                                        "device-id", &ret_device_id) != 1) {
                        goto err_dev;
                }

                /*
                 * If the minor number requested, we are done.  Update the
                 * PCI vendor/device ID used to create the OFED IBDEV name;
                 * Otherwise update the appropriate IBDEV count.
                 */
                if ((di_minor_devt(node_minor) & 0x0000FFFF) == devnum) {
                        break;
                }

		switch (*ret_device_id) {

			case PCI_DEVICE_ID_MELLANOX_TAVOR:
			case PCI_DEVICE_ID_MELLANOX_ARBEL_COMPAT:
				tavor_devs++;
			break;

			case PCI_DEVICE_ID_MELLANOX_ARBEL:
				arbel_devs++;
			break;

			case PCI_DEVICE_ID_MELLANOX_HERMON_SDR:
			case PCI_DEVICE_ID_MELLANOX_HERMON_DDR:
			case PCI_DEVICE_ID_MELLANOX_HERMON_QDR:
			case PCI_DEVICE_ID_MELLANOX_HERMON_DDR_PCIE2:
			case PCI_DEVICE_ID_MELLANOX_HERMON_QDR_PCIE2:
				hermon_devs++;
			break;
	
			default:
				fprintf(stderr, "Unsupported device ID "
				    "0x%04X\n", *ret_device_id);
			break;
		}
                node_minor = di_minor_next(uverbs_node, node_minor);
        }

        if (node_minor == DI_MINOR_NIL) {
                goto err_dev;
        }

        if ((di_minor_devt(node_minor) & 0x0000FFFF) != devnum) {
                goto err_dev;
        }
	switch (*ret_device_id) {

		case PCI_DEVICE_ID_MELLANOX_TAVOR:
		case PCI_DEVICE_ID_MELLANOX_ARBEL_COMPAT:
                	snprintf(name, size, "tavor");
               		*index = tavor_devs;
		break;

		case PCI_DEVICE_ID_MELLANOX_ARBEL:
                	snprintf(name, size, "arbel");
               		*index = arbel_devs;
		break;

		case PCI_DEVICE_ID_MELLANOX_HERMON_SDR:
		case PCI_DEVICE_ID_MELLANOX_HERMON_DDR:
		case PCI_DEVICE_ID_MELLANOX_HERMON_QDR:
		case PCI_DEVICE_ID_MELLANOX_HERMON_DDR_PCIE2:
		case PCI_DEVICE_ID_MELLANOX_HERMON_QDR_PCIE2:
                	snprintf(name, size, "hermon");
               		*index = hermon_devs;
		break;
	
		default:
			fprintf(stderr, "Unsupported driver, device ID "
				"0x%04X\n", *ret_device_id);
                	snprintf(name, size, "unknown");
               		*index = -1;
		break;
	}
        di_fini(root_node);
        return 0;

err_dev:
        di_fini(root_node);
        return -1;
}

/*
 * In Solaris environments, the underlying hardware driver is opened to
 * perform the memory mapping operations of kernel allocated memory
 * into the users address space.
 */
int ibv_open_mmap_driver(char *dev_name)
{
        int                  fd;
#ifndef _LP64
        int                  tmpfd;
#endif
        di_node_t            root_node;
        di_node_t            hca_node;
        int                  uverbs_minor;
        char                 *dev_path;
        char                 path_buf[MAXPATHLEN];
        char                 driver_name[HW_DRIVER_MAX_NAME_LEN];
        int                  driver_index;
	int                  index;

        /*
         * Map the user verbs device to the associated IBD interface to
	 * determine the hardware driver. To avoid ordering issues, we use
	 * associated GUID to perform the mapping.
         */
        uverbs_minor = strtol(dev_name + strlen(UVERBS_KERNEL_SYSFS_NAME) - 1,
								NULL, 0);
        root_node = di_init("/", DINFOCPYALL);
        if (root_node == DI_NODE_NIL) {
                goto err_out;
        }

        if (ibv_get_hw_driver_info(uverbs_minor, driver_name,
	                           HW_DRIVER_MAX_NAME_LEN, &driver_index)) {
                fprintf(stderr, "No hardware driver found for user verbs "
								"instance\n");
                goto err_out;
        }

#if 1 
	/* 
	 * Just map index to driver node instance directly for now; note
	 * that this will not be sufficient for hot-plug.  This will
	 * need to be addressed XXXX - SFW.
	 */
	for (hca_node = di_drv_first_node(driver_name, root_node), index=0;
	     hca_node != DI_NODE_NIL && index < driver_index; index++) {
		hca_node = di_drv_next_node(hca_node);
	}
#else
        hca_node = di_drv_first_node(driver_name, root_node);
        while (hca_node != DI_NODE_NIL) {
                if (di_instance(hca_node) == driver_index) {
                        break;
                }
                hca_node = di_drv_next_node(hca_node);
        }
#endif
        if (hca_node == DI_NODE_NIL) {
                fprintf(stderr, "Could not find hca hardware driver "
				"index: %s #%d\n", driver_name, driver_index);
                goto err_dev;
        }

        dev_path = di_devfs_path(hca_node);
        strncpy(path_buf, "/devices", sizeof path_buf);
        strncat(path_buf, dev_path, sizeof path_buf);
        strncat(path_buf, ":devctl", sizeof path_buf);
        di_devfs_path_free(dev_path);

        fd = open(path_buf, O_RDWR);
        if (fd < 0) {
                goto err_dev;
        }

#ifndef _LP64
        /*
         * libc can't handle fd's greater than 255,  in order to
         * ensure that these values remain available make fd > 255.
         * Note: not needed for LP64
         */
        tmpfd = fcntl(fd, F_DUPFD, 256);
        if (tmpfd >=  0) {
                (void) close(fd);
                fd = tmpfd;
        }
#endif  /* _LP64 */

        if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
                fprintf(stderr, "FD_CLOEXEC failed: %s\n", strerror(errno));
                goto err_close;
        }
        di_fini(root_node);
        return fd;

err_close:
        close(fd);

err_dev:
        di_fini(root_node);

err_out:
        return -1;
}

/*
 * Given the uverbs device number, determine the appropriate ABI
 * revision information.
 */
int uverbs_get_device_abi_version(int devnum, int *abi_version)
{
	di_node_t           root_node;
	di_node_t           uverbs_node;
	di_minor_t          node_minor;
	int                 *ret_vendor_id;
	int                 *ret_device_id;

	root_node = di_init("/", DINFOCPYALL);
	if (root_node == DI_NODE_NIL) {
		goto err_dev;
	}

	uverbs_node = di_drv_first_node(UVERBS, root_node);
	if (uverbs_node == DI_NODE_NIL) {
		goto err_dev;
	}

	/*
	 * Since the actual hardware drivers don't export an OFED equivalent 
	 * ABI revision we go through the minor nodes counting the device
	 * indices for
	 * each hardware type up to the uverbs minor device requested.  
	 */
	node_minor = di_minor_next(uverbs_node, DI_MINOR_NIL);
	while (node_minor != DI_MINOR_NIL) {
		if (di_prop_lookup_ints(di_minor_devt(node_minor), uverbs_node,
	                                "vendor-id", &ret_vendor_id) != 1) {
			node_minor = di_minor_next(uverbs_node, node_minor);
			continue;
		}
		if (di_prop_lookup_ints(di_minor_devt(node_minor), uverbs_node,
	                                "device-id", &ret_device_id) != 1) {
			node_minor = di_minor_next(uverbs_node, node_minor);
			continue;
		}

		if ((di_minor_devt(node_minor) & 0x0000FFFF) == devnum) {
			break;
		}
		node_minor = di_minor_next(uverbs_node, node_minor);
	}

	if (node_minor == DI_MINOR_NIL) {
		goto err_dev;
	}

	switch (*ret_device_id) {
		case PCI_DEVICE_ID_MELLANOX_TAVOR:
		case PCI_DEVICE_ID_MELLANOX_ARBEL:
		case PCI_DEVICE_ID_MELLANOX_ARBEL_COMPAT:
			*abi_version = MTHCA_UVERBS_ABI_VERSION;
		break;

		case PCI_DEVICE_ID_MELLANOX_HERMON_SDR:
		case PCI_DEVICE_ID_MELLANOX_HERMON_DDR:
		case PCI_DEVICE_ID_MELLANOX_HERMON_QDR:
		case PCI_DEVICE_ID_MELLANOX_HERMON_DDR_PCIE2:
		case PCI_DEVICE_ID_MELLANOX_HERMON_QDR_PCIE2:
			*abi_version = MLX4_UVERBS_MAX_ABI_VERSION;
		break;

		default:
			goto err_dev;
		break;
	}

	di_fini(root_node);
	return 0;

err_dev:
	di_fini(root_node);
	return -1;
}

int infiniband_verbs(char *path, char *buf, size_t size)
{
	unsigned int	device_num;
	int 		len = -1;

	if (check_path(path, CP_SOL_UVERBS, &device_num)) {

		if (check_path(path, CP_DEVICE, NULL)) {
			/*
			 * Under Linux, this is a link to the PCI device entry
			 * in /sys/devices/pci...../.... 
			 */
			if (strcmp(path, "vendor") == 0) {
				len = 1 + sprintf(buf, "0x%x",
				    get_device_int_attr(UVERBS, device_num,
				    "vendor-id"));
			} else if (strcmp(path, "device") == 0) {
				len = 1 + sprintf(buf, "0x%x",
				    get_device_int_attr(UVERBS, device_num,
				    "device-id"));
			}
			goto exit;
		}

		if (strcmp(path, "ibdev") == 0) {
			if (uverbs_get_device_name(device_num, buf, size)
			    == 0) {
				len = 1 + strlen(buf);
			}
		} else if (strcmp(path, "abi_version") == 0) {
			int abi_version;

			if (uverbs_get_device_abi_version(device_num,
			    &abi_version) == 0) {
				len = 1 + sprintf(buf, "%d", abi_version);
			}
		}
		goto exit;
	}

	if (strcmp(path, "abi_version") == 0) {
		len = 1 + sprintf(buf, "%d", get_device_int_attr(UVERBS, 0,
		    "abi-version"));
	}
exit:
	return len;
}

int infiniband_ports(char *path, char *buf, size_t size, char *dev_name)
{
	int 			len = -1;
	unsigned int		port_num;
	unsigned int		gid_num;
	union ibv_gid		*gids;
	uint64_t		subnet_prefix;
	uint64_t		interface_id;
	uint16_t		*pkeys;
	unsigned int		pkey_num;
	struct ibv_port_attr	port_attr;
	float			rate;

	if (!(check_path(path, CP_D, &port_num)))
		goto exit;

	
	if (check_path(path, CP_GIDS, NULL)) {
		if (get_port_info(dev_name, port_num, &port_attr, &gids, NULL))
				goto exit;
				
		gid_num = atoi(path);

		if (gid_num <  port_attr.gid_tbl_len) {

			subnet_prefix =
			    htonll(gids[gid_num].global.subnet_prefix);
			interface_id =
			    htonll(gids[gid_num].global.interface_id);
			len = 1 + sprintf(buf,
				"%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x",
				  (unsigned) (subnet_prefix >>  48) & 0xffff,
				  (unsigned) (subnet_prefix >>  32) & 0xffff,
				  (unsigned) (subnet_prefix >>  16) & 0xffff,
				  (unsigned) (subnet_prefix >>   0) & 0xffff,
				  (unsigned) (interface_id  >>  48) & 0xffff,
				  (unsigned) (interface_id  >>  32) & 0xffff,
				  (unsigned) (interface_id  >>  16) & 0xffff,
				  (unsigned) (interface_id  >>   0) & 0xffff);
		}
		if (gids)
			free(gids);
		
	} else if (check_path(path, CP_PKEYS, NULL)) {
		if (get_port_info(dev_name, port_num, &port_attr, NULL, &pkeys))
				goto exit;

		pkey_num = atoi(path);
		if (pkey_num <  port_attr.pkey_tbl_len) {
			len = 1 + sprintf(buf, "0x%04x", pkeys[pkey_num]);
		}
		if (pkeys)
			free(pkeys);
	} else {

		if (get_port_info(dev_name, port_num, &port_attr, NULL, NULL))
				goto exit;
				
		if (strcmp(path, "lid_mask_count") == 0) {
			len = 1 + sprintf(buf, "%d", port_attr.lmc);
		} else if (strcmp(path, "sm_lid") == 0) {
			len = 1 + sprintf(buf, "0x%x", port_attr.sm_lid);
		} else if (strcmp(path, "sm_sl") == 0) {
			len = 1 + sprintf(buf, "%d", port_attr.sm_sl);
		} else if (strcmp(path, "lid") == 0) {
			len = 1 + sprintf(buf, "0x%x", port_attr.lid);
		} else if (strcmp(path, "state") == 0) {
			/* TODO: may need to add a string. Correct
				 * output is "4: ACTIVE". */
			len = 1 + sprintf(buf, "%d:", port_attr.state);
		} else if (strcmp(path, "phys_state") == 0) {
			/* TODO: may need to add a string. Correct
			 * output is "5: LinkUp". */
			len = 1 + sprintf(buf, "%d", port_attr.phys_state);
		} else if (strcmp(path, "rate") == 0) {
			/* rate = speed * width */
			switch (port_attr.active_speed) {
			case 1:
				rate = 2.5;
				break;
			case 2:
				rate = 5;
				break;
			case 4:
				rate = 10;
				break;
			default:
				rate = 0;
			}
			switch (port_attr.active_width) {
			case 1:
				break;
			case 2:
				rate = 4 * rate;
				break;
			case 4:
				rate = 8 * rate;
				break;
			case 8:
				rate = 12 * rate;
				break;
			default:
				rate = 0;
			}
			len = 1 + sprintf(buf, "%f", rate);
		} else if (strcmp(path, "cap_mask") == 0) {
			len = 1 + sprintf(buf, "0x%08x",
				port_attr.port_cap_flags);
		}
	}
exit:
	return (len);
}

int infiniband(char *path, char *buf, size_t size)
{
	int			len = -1;
	unsigned int		device_num;
	char			dev_name[10];
	struct ibv_device_attr	device_attr;

	memset(dev_name, 0, 10);
	
	if (check_path(path, CP_MTHCA, &device_num)) {

		sprintf(dev_name, "mthca%d", device_num);

	} else if (check_path(path, CP_MLX4, &device_num)) {

		sprintf(dev_name, "mlx4_%d", device_num);
	} else {
		goto exit;
	}

	if (check_path(path, CP_PORTS, NULL)) {

		len = infiniband_ports(path, buf, size, dev_name);

	} else if (strcmp(path, "node_type") == 0) {
		len = 1 + sprintf(buf, "%d", IBV_NODE_CA);

	} else if (strcmp(path, "board_id") == 0) {
		len = 1 + sprintf(buf, "unknown");

	} else {

		/* 
		 * We need some specific infos. Open the device.
		 */
		if (get_device_info(dev_name, &device_attr))
			goto exit;

		/* TODO: remove ntohll and swap bytes in sprintf instead */
		if (strcmp(path, "node_guid") == 0) {
			uint64_t node_guid = ntohll(device_attr.node_guid);
			len = 1 + sprintf(buf, "%04x:%04x:%04x:%04x",
				  (unsigned) (node_guid >> 48) & 0xffff,
				  (unsigned) (node_guid >> 32) & 0xffff,
				  (unsigned) (node_guid >> 16) & 0xffff,
				  (unsigned) (node_guid >>  0) & 0xffff);
		} else if (strcmp(path, "sys_image_guid") == 0) {
			uint64_t sys_image_guid =
			    	ntohll(device_attr.sys_image_guid);
			len = 1 + sprintf(buf, "%04x:%04x:%04x:%04x",
				  (unsigned) (sys_image_guid >> 48) & 0xffff,
				  (unsigned) (sys_image_guid >> 32) & 0xffff,
				  (unsigned) (sys_image_guid >> 16) & 0xffff,
				  (unsigned) (sys_image_guid >>  0) & 0xffff);
		} else if (strcmp(path, "fw_ver") == 0) {
			len = 1 + sprintf(buf, "%s", device_attr.fw_ver);
		} else if (strcmp(path, "hw_rev") == 0) {
			len = 1 + sprintf(buf, "%d", device_attr.hw_ver);
		} else if (strcmp(path, "hca_type") == 0) {
			switch (device_attr.vendor_part_id) {
			case PCI_DEVICE_ID_MELLANOX_TAVOR:
			case PCI_DEVICE_ID_MELLANOX_ARBEL_COMPAT:
				len = 1 + sprintf(buf, "unavailable");
				break;

			case PCI_DEVICE_ID_MELLANOX_ARBEL:
				len = 1 + sprintf(buf, "unavailable");
				break;

			case PCI_DEVICE_ID_MELLANOX_HERMON_SDR:
			case PCI_DEVICE_ID_MELLANOX_HERMON_DDR:
			case PCI_DEVICE_ID_MELLANOX_HERMON_QDR:
			case PCI_DEVICE_ID_MELLANOX_HERMON_DDR_PCIE2:
			case PCI_DEVICE_ID_MELLANOX_HERMON_QDR_PCIE2:
				len = 1 + sprintf(buf, "%d", 0);
				break;
			default:
				break;
			}
		}
	}
exit:
	return (len);
}

int infiniband_mad(char *path, char *buf, size_t size)
{
	int		len = -1;
	int		port;
	unsigned int	device_num;
	char		umad_str[32];
	char		*ibdev;

	if (check_path(path, CP_UMAD, &device_num)) {

		(void) snprintf(umad_str, sizeof(umad_str),
					"umad%d", device_num);

		if (strcmp(path, "ibdev") == 0) {

			ibdev = umad_get_ibdev(umad_str);
			if (ibdev != NULL) {
				len = strlcpy(buf, umad_get_ibdev(umad_str),
						      size) + 1;
				free(ibdev);
			} else {
				len = strlcpy(buf, "unknown", size) + 1;
			}
		} else if (strcmp(path, "port") == 0) {
			port = umad_get_port(umad_str);
			len = 1 + sprintf(buf, "%d", port);
		}
	} else if (strcmp(path, "abi_version") == 0) {
		len = 1 + sprintf(buf, "%d", get_device_int_attr(UMAD, 0,
			"abi_version")); 
	}
	return (len);
}

/* 
 * Return -1 on error, or the length of the data (buf) on success.
 */
int sol_read_sysfs_file(char *path, char *buf, size_t size)
{
	int 			len = -1;

	if (!check_path(path, CP_SLASH, NULL))
		goto exit;

	if (!check_path(path, CP_SYS, NULL))
		goto exit;

	if (!check_path(path, CP_CLASS, NULL))
		goto exit;

	if (check_path(path, CP_INFINIBAND_VERBS, NULL)) {

		len = infiniband_verbs(path, buf, size);

	} else if (check_path(path, CP_INFINIBAND, NULL)) {

		len = infiniband(path, buf, size);

	} else if (check_path(path, CP_INFINIBAND_MAD, NULL)) {

		len = infiniband_mad(path, buf, size);

	} else if (check_path(path, CP_MISC, NULL)) {

		if (check_path(path, CP_RDMA_CM, NULL)) {

			if (strcmp(path, "abi_version") == 0) {
				len = 1 + sprintf(buf, "%d",
					RDMA_USER_CM_MAX_ABI_VERSION);
			}
		}	
	}
exit:	
	return len;
}

int
sol_get_cpu_info(sol_cpu_info_t *info)
{
	kstat_t		*ksp;
	kstat_named_t	*knp;

	/*
	 * We should check all CPUS, and make sure they
	 * are all the same or return an array of structs.
	 */
	ksp = kstat_lookup(kc, "cpu_info", 0, NULL);
	if (ksp == NULL)
		return (-1);

	if (kstat_read(kc, ksp, NULL) == -1)
		return (-1);

	knp = (kstat_named_t *)kstat_data_lookup(ksp, "brand");
	if (knp == NULL)
		return (-1);

	(void) strlcpy(info->cpu_name, knp->value.str.addr.ptr,
	    knp->value.str.len);

	knp = (kstat_named_t *)kstat_data_lookup(ksp, "clock_MHz");
	if (knp == NULL)
		return -1;

	info->cpu_mhz = knp->value.ui64;	
	info->cpu_num = sysconf(_SC_NPROCESSORS_ONLN);
	return (0);
}
#endif
