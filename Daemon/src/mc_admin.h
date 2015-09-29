/*
 * Copyright (c) 2013-2015 TRUSTONIC LIMITED
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the TRUSTONIC LIMITED nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __MC_ADMIN_IOCTL_H__
#define __MC_ADMIN_IOCTL_H__

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MC_ADMIN_DEVNODE "mobicore"

/* Driver/daemon commands */
enum {
	/* Command 0 is reserved */
	MC_DRV_GET_ROOT_CONTAINER = 1,
	MC_DRV_GET_SP_CONTAINER = 2,
	MC_DRV_GET_TRUSTLET_CONTAINER = 3,
	MC_DRV_GET_TRUSTLET = 4,
	MC_DRV_SIGNAL_CRASH = 5,
};

/* MobiCore IOCTL magic number */
#define MC_IOC_MAGIC    'M'

struct mc_admin_request {
	__u32		request_id;	/* Unique request identifier */
	__u32		command;	/* Command to daemon */
	struct mc_uuid_t uuid;		/* UUID of trustlet, if relevant */
	__u32		is_gp;		/* Whether trustlet is GP */
	__u32		spid;		/* SPID of trustlet, if relevant */
};

struct mc_admin_response {
	__u32		request_id;	/* Unique request identifier */
	__u32		error_no;	/* Errno from daemon */
	__u32		spid;		/* SPID of trustlet, if relevant */
	__u32		service_type;	/* Type of trustlet being returned */
	__u32		length;		/* Length of data to get */
	/* Any data follows */
};

struct mc_admin_driver_info {
	/* Version, and something else..*/
	__u32		drv_version;
	__u32		initial_cmd_id;
};

struct mc_admin_load_info {
	__u32		spid;		/* SPID of trustlet, if relevant */
	__u64		address;	/* Address of the data */
	__u32		length;		/* Length of data to get */
};

#define MC_ADMIN_IO_GET_DRIVER_REQUEST \
	_IOR(MC_IOC_MAGIC, 0, struct mc_admin_request)
#define MC_ADMIN_IO_GET_INFO  \
	_IOR(MC_IOC_MAGIC, 1, struct mc_admin_driver_info)
#define MC_ADMIN_IO_LOAD_DRIVER \
	_IOW(MC_IOC_MAGIC, 2, struct mc_admin_load_info)
#define MC_ADMIN_IO_LOAD_TOKEN \
	_IOW(MC_IOC_MAGIC, 3, struct mc_admin_load_info)
#define MC_ADMIN_IO_LOAD_CHECK \
	_IOW(MC_IOC_MAGIC, 4, struct mc_admin_load_info)

#ifdef __cplusplus
}
#endif
#endif /* __MC_ADMIN_IOCTL_H__ */
