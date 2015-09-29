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

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define LOG_TAG "McDriverClient"
#include "log.h"
#include "mcVersionHelper.h"
#include "driver_client.h"

MC_CHECK_VERSION(MCDRVMODULEAPI, 2, 0);

int DriverClient::open() {
    int fd = ::open("/dev/" MC_USER_DEVNODE, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        _LOG_E("%s in %s", strerror(errno), __FUNCTION__);
        return -1;
    }

    struct mc_version_info version_info;
    if (::ioctl(fd, MC_IO_VERSION, &version_info) < 0) {
        _LOG_E("%s in %s", strerror(errno), __FUNCTION__);
        (void)::close(fd);
        return -1;
    }

    // Run-time check.
    uint32_t version = version_info.version_nwd;
    char *errmsg;
    if (!checkVersionOkMCDRVMODULEAPI(version, &errmsg)) {
        (void)::close(fd);
        errno = EHOSTDOWN;
        _LOG_E("%s in %s", strerror(errno), __FUNCTION__);
        return -1;
    }

    fd_ = fd;
    return 0;
}

int DriverClient::close() {
    int ret = ::close(fd_);
    if (ret) {
        _LOG_E("%s in %s", strerror(errno), __FUNCTION__);
    }
    fd_ = -1;
    return ret;
}

int DriverClient::freeze() {
    int ret = ::ioctl(fd_, MC_IO_FREEZE);
    if (ret) {
        _LOG_E("%s in %s", strerror(errno), __FUNCTION__);
    }
    return ret;
}

int DriverClient::openSession(struct mc_ioctl_open_session& session) {
    int ret = ::ioctl(fd_, MC_IO_OPEN_SESSION, &session);
    if (ret) {
        _LOG_E("%s in %s", strerror(errno), __FUNCTION__);
    }
    return ret;
}

int DriverClient::openTrustlet(struct mc_ioctl_open_trustlet& trustlet) {
    int ret = ::ioctl(fd_, MC_IO_OPEN_TRUSTLET, &trustlet);
    if (ret) {
        _LOG_E("%s in %s", strerror(errno), __FUNCTION__);
    }
    return ret;
}

int DriverClient::closeSession(uint32_t session_id) {
    int ret = ::ioctl(fd_, MC_IO_CLOSE_SESSION, session_id);
    if (ret) {
        _LOG_E("%s in %s", strerror(errno), __FUNCTION__);
    }
    return ret;
}

int DriverClient::notify(uint32_t session_id) {
    int ret = ::ioctl(fd_, MC_IO_NOTIFY, session_id);
    if (ret) {
        _LOG_E("%s in %s", strerror(errno), __FUNCTION__);
    }
    return ret;
}

int DriverClient::waitNotification(const struct mc_ioctl_wait& wait) {
    int ret = ::ioctl(fd_, MC_IO_WAIT, &wait);
    if (ret) {
        _LOG_E("%s in %s", strerror(errno), __FUNCTION__);
    }
    return ret;
}

int DriverClient::malloc(uint8_t **buffer, uint32_t length) {
    *buffer = static_cast<uint8_t*>(mmap(0, length, PROT_READ | PROT_WRITE,
                                         MAP_SHARED, fd_, 0));
    int ret = 0;
    if (*buffer == MAP_FAILED) {
        _LOG_E("%s in %s", strerror(errno), __FUNCTION__);
        ret = -1;
    }
    return ret;
}

int DriverClient::free(uint8_t *buffer, uint32_t length) {
    int ret = munmap(buffer, length);
    if (ret) {
        _LOG_E("%s in %s", strerror(errno), __FUNCTION__);
    }
    return ret;
}

int DriverClient::map(struct mc_ioctl_map& map) {
    int ret = ::ioctl(fd_, MC_IO_MAP, &map);
    if (ret) {
        _LOG_E("%s in %s", strerror(errno), __FUNCTION__);
    }
    return ret;
}

int DriverClient::unmap(const struct mc_ioctl_map& map) {
    int ret = ::ioctl(fd_, MC_IO_UNMAP, &map);
    if (ret) {
        _LOG_E("%s in %s", strerror(errno), __FUNCTION__);
    }
    return ret;
}

int DriverClient::getError(struct mc_ioctl_geterr& err) {
    int ret = ::ioctl(fd_, MC_IO_ERR, &err);
    if (ret) {
        _LOG_E("%s in %s", strerror(errno), __FUNCTION__);
    }
    return ret;
}

int DriverClient::getVersion(struct mc_version_info& version_info) {
    int ret = ::ioctl(fd_, MC_IO_VERSION, &version_info);
    if (ret) {
        _LOG_E("%s in %s", strerror(errno), __FUNCTION__);
    }
    return ret;
}
