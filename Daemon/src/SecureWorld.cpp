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

// Standard
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
// <t-base
#include "Mci/mcimcp.h"
#include "mcVersionHelper.h"
// Logging
#include <log.h>
// Driver
#include "mc_linux.h"
#include "mc_admin.h"
// Registry
#include "PrivateRegistry.h"
// Self
#include "SecureWorld.h"

MC_CHECK_VERSION(MCDRVMODULEAPI, 2, 0);
#define MAX_SO_CONT_SIZE 512
#define CRASHDUMP_PATH "/sys/kernel/debug/trustonic_tee/crashdump"

struct SecureWorld::Impl {
    int device_fd;
    uint32_t command_id;
    bool keep_running;
    Impl(): device_fd(-1), command_id(0), keep_running(true) {}
};

SecureWorld::SecureWorld(): pimpl_(new Impl) {}

SecureWorld::~SecureWorld() {
    delete pimpl_;
}

static void* getRootContainer(const struct mc_admin_request*,
                              struct mc_admin_response* response,
                              bool*) {
    void* data = malloc(MAX_SO_CONT_SIZE);
    uint32_t length = MAX_SO_CONT_SIZE;

    switch (mcRegistryReadRoot(data, &length)) {
        case MC_DRV_OK:
            response->length = length;
            response->error_no = 0;
            break;
        case MC_DRV_ERR_INVALID_PARAMETER:
            response->error_no = EINVAL;
            break;
        case MC_DRV_ERR_INVALID_DEVICE_FILE:
            response->error_no = ENOENT;
            break;
        default:
            // Some kind of default
            response->error_no = EPERM;
    }

    if (response->error_no) {
        free(data);
        return NULL;
    }

    LOG_D("Read root container, size: %u", response->length);
    return data;
}

static void* getSpContainer(const struct mc_admin_request* request,
                            struct mc_admin_response* response,
                            bool*) {
    void* data = malloc(MAX_SO_CONT_SIZE);
    uint32_t length = MAX_SO_CONT_SIZE;

    switch (mcRegistryReadSp(request->spid, data, &length)) {
        case MC_DRV_OK:
            response->length = length;
            response->error_no = 0;
            break;
        case MC_DRV_ERR_INVALID_PARAMETER:
            response->error_no = EINVAL;
            break;
        case MC_DRV_ERR_INVALID_DEVICE_FILE:
            response->error_no = ENOENT;
            break;
        default:
            // Some kind of default
            response->error_no = EPERM;
    }

    if (response->error_no) {
        free(data);
        return NULL;
    }

    LOG_D("Read SP container for %u, size: %u", request->spid, response->length);
    return data;
}

static void* getTrustletContainer(const struct mc_admin_request* request,
                                  struct mc_admin_response* response,
                                  bool*) {
    void* data = malloc(MAX_SO_CONT_SIZE);
    uint32_t length = MAX_SO_CONT_SIZE;

    const mcUuid_t* uuid = reinterpret_cast<const mcUuid_t*>(&request->uuid);
    switch (mcRegistryReadTrustletCon(uuid, request->spid, data, &length)) {
        case MC_DRV_OK:
            response->length = length;
            response->error_no = 0;
            break;
        case MC_DRV_ERR_INVALID_PARAMETER:
            response->error_no = EINVAL;
            break;
        case MC_DRV_ERR_INVALID_DEVICE_FILE:
            response->error_no = ENOENT;
            break;
        case MC_DRV_ERR_OUT_OF_RESOURCES:
            response->error_no = ENOMEM;
            break;
        default:
            // Some kind of default
            response->error_no = EPERM;
    }

    if (response->error_no) {
        free(data);
        return NULL;
    }

    LOG_D("Read trustlet container for %u, size: %u", response->spid, response->length);
    return data;
}

static void* mapTrustlet(const char* path, uint32_t* length,
                         uint32_t* service_type) {
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) {
        LOG_W("Cannot open trustlet %s (%d)", path, errno);
        return NULL;
    }

    void* data = NULL;
    *service_type = SERVICE_TYPE_ILLEGAL;
    do {
        struct stat stat;
        if (::fstat(fd, &stat) < 0) {
            LOG_E("%s: cannot get size for trustlet %s", __func__, path);
            break;
        }

        data = mmap(NULL, stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (data == MAP_FAILED) {
            LOG_E("%s: Failed to map file to memory", __func__);
            break;
        }

        /* Give service type to driver so it knows how to allocate and copy */
        mclfHeaderV2_t* header = static_cast<mclfHeaderV2_t*>(data);
        *service_type = header->serviceType;

        *length = static_cast<uint32_t>(stat.st_size);
    } while (false);

    ::close(fd);

    if (*service_type == SERVICE_TYPE_ILLEGAL) {
        free(data);
        return NULL;
    }

    return data;
}

static void* getTrustlet(const struct mc_admin_request* request,
                         struct mc_admin_response* response,
                         bool* is_mmapped) {
    *is_mmapped = true;
    const mcUuid_t* uuid = reinterpret_cast<const mcUuid_t*>(&request->uuid);
    std::string path;

    mcResult_t res = mcRegistryGetTrustletInfo(uuid, request->is_gp, &response->spid, path);
    switch (res) {
        case MC_DRV_OK:
            response->error_no = 0;
            break;
        case MC_DRV_ERR_INVALID_PARAMETER:
            response->error_no = EINVAL;
            break;
        case MC_DRV_ERR_TRUSTLET_NOT_FOUND:
            response->error_no = ENOENT;
            break;
        default:
            // Some kind of default
            response->error_no = EPERM;
    }

    if (response->error_no) {
        return NULL;
    }

    void* data = mapTrustlet(path.c_str(), &response->length,
                             &response->service_type);
    if (!data) {
        response->error_no = errno;
    } else {
        LOG_D("Read spid %u and mmap'd trustlet from %s, total size: %u", response->spid, path.c_str(), response->length);
    }
    return data;
}

static void* logCrashDump(const struct mc_admin_request*,
                          struct mc_admin_response* response,
                          bool*) {
    std::ifstream crashdump(CRASHDUMP_PATH);
    if (!crashdump.is_open()) {
        LOG_E("%s opening crash dump", strerror(errno));
        response->error_no = errno;
        return NULL;
    }

    LOG_E("<t-base halted. Status dump:");
    std::string line;
    while (std::getline(crashdump, line)) {
        LOG_I("%s", line.c_str());
    }

    return NULL;
}

int SecureWorld::open() {
    pimpl_->device_fd = ::open("/dev/" MC_ADMIN_DEVNODE, O_RDWR);
    if (pimpl_->device_fd < 0) {
        LOG_ERRNO("open");
        return -1;
    }

    LOG_D("Check version of <t-base driver");
    struct mc_admin_driver_info info;
    if (ioctl(pimpl_->device_fd, MC_ADMIN_IO_GET_INFO, &info) != 0) {
        LOG_ERRNO("ioctl MC_ADMIN_IO_GET_INFO");
        ::close(pimpl_->device_fd);
        return -1;
    }

    char *errmsg;
    if (!checkVersionOkMCDRVMODULEAPI(info.drv_version, &errmsg)) {
        LOG_E("%s", errmsg);
        ::close(pimpl_->device_fd);
        return -1;
    }

    LOG_D("%s", errmsg);
    pimpl_->command_id = info.initial_cmd_id;
    return 0;
}

void SecureWorld::close() {
    ::close(pimpl_->device_fd);
}

int SecureWorld::loadDriver(const char* path) {
    struct mc_admin_load_info info;
    uint32_t service_type;

    void* data = mapTrustlet(path, &info.length, &service_type);
    if (!data) {
        return -1;
    }

    LOG_D("Load secure driver %s of size %d", path, info.length);
    info.address = reinterpret_cast<uintptr_t>(data);
    info.spid = 0;

    int ret = ioctl(pimpl_->device_fd, MC_ADMIN_IO_LOAD_DRIVER, &info);
    if (ret != 0) {
        LOG_ERRNO("ioctl MC_ADMIN_IO_LOAD_DRIVER");
    }

    // Free memory occupied by Trustlet data
    munmap(data, info.length);
    return ret == 0;
}

int SecureWorld::loadToken(const void *data, uint32_t length) {
    struct mc_admin_load_info token;

    LOG_D("Load authentication token %p of size %u", data, length);
    token.address = reinterpret_cast<uintptr_t>(data);
    token.length = length;

    int rc = ioctl(pimpl_->device_fd, MC_ADMIN_IO_LOAD_TOKEN, &token);
    if (rc != 0) {
        LOG_ERRNO("ioctl MC_ADMIN_IO_LOAD_TOKEN");
        return -1;
    }

    return 0;
}

int SecureWorld::LoadCheck(mcSpid_t spid, const void *data, uint32_t length) {
    struct mc_admin_load_info info;

    LOG_D("Load secure object %p, length %u, spid %x", data, length, spid);
    info.spid = spid;
    info.address = reinterpret_cast<uintptr_t>(data);
    info.length = length;

    int rc = ioctl(pimpl_->device_fd, MC_ADMIN_IO_LOAD_CHECK, &info);
    if (rc != 0) {
        LOG_ERRNO("ioctl MC_ADMIN_IO_LOAD_CHECK");
        return -1;
    }

    return 0;
}

int SecureWorld::listen()
{
    int rc = 0;

    while (pimpl_->keep_running) {
        struct mc_admin_request request;

        rc = ioctl(pimpl_->device_fd, MC_ADMIN_IO_GET_DRIVER_REQUEST, &request);
        if (rc) {
            if (errno == EINTR) {
                LOG_D("Giving up on signal");
            } else {
                LOG_ERRNO("Getting request from driver");
            }
            break;
        }

        LOG_D("Request %d received (ID %u)", request.command, request.request_id);
        if (pimpl_->command_id != request.request_id) {
            LOG_E("Request ID counters are not synchronised (expected %u, got %u)", pimpl_->command_id, request.request_id);
            break;
        }

        struct mc_admin_response response;
        memset(&response, 0, sizeof(response));
        response.request_id = pimpl_->command_id++;
        void* response_data = NULL;
        bool is_mmapped = false; // Response data needs freeing
        switch (request.command) {
            case MC_DRV_GET_ROOT_CONTAINER:
                response_data = getRootContainer(&request, &response, &is_mmapped);
                break;
            case MC_DRV_GET_SP_CONTAINER:
                response_data = getSpContainer(&request, &response, &is_mmapped);
                break;
            case MC_DRV_GET_TRUSTLET_CONTAINER:
                response_data = getTrustletContainer(&request, &response, &is_mmapped);
                break;
            case MC_DRV_GET_TRUSTLET:
                response_data = getTrustlet(&request, &response, &is_mmapped);
                break;
            case MC_DRV_SIGNAL_CRASH:
                logCrashDump(&request, &response, &is_mmapped);
                break;
            default:
                LOG_E("Unknown command");
                response.error_no = EBADRQC;
        }

        ssize_t ret = ::write(pimpl_->device_fd, &response, sizeof(response));
        if (ret != sizeof(response)) {
            LOG_ERRNO("Sending response to driver");
            ret = -1;
        } else if (response.length > 0) {
            ssize_t expected_length = response.length;
            ret = ::write(pimpl_->device_fd, response_data, response.length);
            if (ret != expected_length) {
                LOG_ERRNO("Sending response data to driver");
                ret = -1;
            }
        }

        if (response_data && is_mmapped) {
            munmap(response_data, response.length);
        } else {
            free(response_data);
        }
    }

    return rc;
}

void SecureWorld::stopListening() {
    pimpl_->keep_running = false;
}
