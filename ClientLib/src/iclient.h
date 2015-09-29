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

#ifndef __CLIENT_INTERFACE_H__
#define __CLIENT_INTERFACE_H__

#include <stdint.h>     // *int*_t
#include <sys/types.h>  // uid_t git_t
#include "mc_linux.h"   // ioctl structures

class IClient {
public:
    virtual ~IClient() {}
    virtual int open() = 0;
    virtual int close() = 0;
    virtual int freeze() = 0;
    virtual int openSession(struct mc_ioctl_open_session& session) = 0;
    virtual int openTrustlet(struct mc_ioctl_open_trustlet& trustlet) = 0;
    virtual int closeSession(uint32_t session_id) = 0;
    virtual int notify(uint32_t session_id) = 0;
    virtual int waitNotification(const struct mc_ioctl_wait& wait) = 0;
    virtual int malloc(uint8_t **buffer, uint32_t length) = 0;
    virtual int free(uint8_t *buffer, uint32_t length) = 0;
    virtual int map(struct mc_ioctl_map& map) = 0;
    virtual int unmap(const struct mc_ioctl_map& map) = 0;
    virtual int getError(struct mc_ioctl_geterr& err) = 0;
    virtual int getVersion(struct mc_version_info& version_info) = 0;
};

#endif // __CLIENT_INTERFACE_H__
