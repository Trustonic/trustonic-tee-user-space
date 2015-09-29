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

#ifndef _MC_LINUX_H_
#define _MC_LINUX_H_

#define MCDRVMODULEAPI_VERSION_MAJOR 2
#define MCDRVMODULEAPI_VERSION_MINOR 0

#include <linux/types.h>

#define MC_USER_DEVNODE		"mobicore-user"

/** Maximum length of MobiCore product ID string. */
#define MC_PRODUCT_ID_LEN	64

/** Number of buffers that can be mapped at once */
#define MC_MAP_MAX		4

/** Flags for buffers to map (aligned on GP) */
#define MC_IO_MAP_INPUT		0x1
#define MC_IO_MAP_OUTPUT	0x2

/*
 * Universally Unique Identifier (UUID) according to ISO/IEC 11578.
 */
struct mc_uuid_t {
	__u8		value[16];	/* Value of the UUID. */
};

/*
 * GP TA login types.
 */
enum mc_login_type {
	TEEC_LOGIN_PUBLIC = 0,
	TEEC_LOGIN_USER,
	TEEC_LOGIN_GROUP,
	TEEC_LOGIN_APPLICATION = 4,
	TEEC_LOGIN_USER_APPLICATION,
	TEEC_LOGIN_GROUP_APPLICATION,
};

/*
 * GP TA identity structure.
 */
struct mc_identity {
	enum mc_login_type	login_type;
	union {
		__u8		login_data[16];
		gid_t		gid;		/* Requested group id */
		struct {
			uid_t	euid;
			uid_t	ruid;
		} uid;
	};
};

/*
 * Data exchange structure of the MC_IO_OPEN_SESSION ioctl command.
 */
struct mc_ioctl_open_session {
	struct mc_uuid_t uuid;		/* trustlet uuid */
	__u32		is_gp_uuid;	/* uuid is for GP TA */
	__u32		sid;            /* session id (out) */
	__u64		tci;		/* tci buffer pointer */
	__u32		tcilen;		/* tci length */
	struct mc_identity identity;	/* GP TA identity */
};

/*
 * Data exchange structure of the MC_IO_OPEN_TRUSTLET ioctl command.
 */
struct mc_ioctl_open_trustlet {
	__u32		sid;		/* session id (out) */
	__u32		spid;		/* trustlet spid */
	__u64		buffer;		/* trustlet binary pointer */
	__u32		tlen;		/* binary length  */
	__u64		tci;		/* tci buffer pointer */
	__u32		tcilen;		/* tci length */
};

/*
 * Data exchange structure of the MC_IO_WAIT ioctl command.
 */
struct mc_ioctl_wait {
	__u32		sid;		/* session id (in) */
	__s32		timeout;	/* notification timeout */
};

/*
 * Data exchange structure of the MC_IO_ALLOC ioctl command.
 */
struct mc_ioctl_alloc {
	__u32		len;		/* buffer length  */
	__u32		handle;		/* user handle for the buffer (out) */
};

/*
 * Buffer mapping incoming and outgoing information.
 */
struct mc_ioctl_buffer {
	__u64		va;		/* user space address of buffer */
	__u32		len;		/* buffer length  */
	__u64		sva;		/* SWd virt address of buffer (out) */
	__u32		flags;		/* buffer flags  */
};

/*
 * Data exchange structure of the MC_IO_MAP and MC_IO_UNMAP ioctl commands.
 */
struct mc_ioctl_map {
	__u32		sid;		/* session id */
	struct mc_ioctl_buffer bufs[MC_MAP_MAX]; /* buffers info */
};

/*
 * Data exchange structure of the MC_IO_ERR ioctl command.
 */
struct mc_ioctl_geterr {
	__u32		sid;		/* session id */
	__s32		value;		/* error value (out) */
};

/*
 * Global MobiCore Version Information.
 */
struct mc_version_info {
	char product_id[MC_PRODUCT_ID_LEN]; /** Product ID string */
	__u32 version_mci;		/** Mobicore Control Interface */
	__u32 version_so;		/** Secure Objects */
	__u32 version_mclf;		/** MobiCore Load Format */
	__u32 version_container;	/** MobiCore Container Format */
	__u32 version_mc_config;	/** MobiCore Config. Block Format */
	__u32 version_tl_api;		/** MobiCore Trustlet API */
	__u32 version_dr_api;		/** MobiCore Driver API */
	__u32 version_nwd;		/** This Driver */
};

/*
 * defines for the ioctl mobicore driver module function call from user space.
 */
/* MobiCore IOCTL magic number */
#define MC_IOC_MAGIC	'M'

/*
 * Implement corresponding functions from user api
 */
#define MC_IO_OPEN_SESSION	\
	_IOWR(MC_IOC_MAGIC, 0, struct mc_ioctl_open_session)
#define MC_IO_OPEN_TRUSTLET	\
	_IOWR(MC_IOC_MAGIC, 1, struct mc_ioctl_open_trustlet)
#define MC_IO_CLOSE_SESSION	_IO(MC_IOC_MAGIC, 2)
#define MC_IO_NOTIFY		_IO(MC_IOC_MAGIC, 3)
#define MC_IO_WAIT		_IOW(MC_IOC_MAGIC, 4, struct mc_ioctl_wait)
#define MC_IO_MAP		_IOWR(MC_IOC_MAGIC, 5, struct mc_ioctl_map)
#define MC_IO_UNMAP		_IOW(MC_IOC_MAGIC, 6, struct mc_ioctl_map)
#define MC_IO_ERR		_IOWR(MC_IOC_MAGIC, 7, struct mc_ioctl_geterr)
#define MC_IO_FREEZE		_IO(MC_IOC_MAGIC, 8)
#define MC_IO_VERSION		_IOR(MC_IOC_MAGIC, 9, struct mc_version_info)

#endif /* _MC_LINUX_H_ */
