/*
 * Copyright (c) 2013-2014 TRUSTONIC LIMITED
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
/**
 * Client library device management.
 *
 * Device and Trustlet Session management Funtions.
 */
#include <stdint.h>
#include <vector>

#include "mc_linux.h"
#include "Device.h"

#include "log.h"
#include <assert.h>


//------------------------------------------------------------------------------
Device::Device(uint32_t deviceId, Connection *connection)
{
    this->deviceId = deviceId;
    this->connection = connection;
    this->openCount = 0;

    pMcKMod = new CMcKMod();
}


//------------------------------------------------------------------------------
Device::~Device(void)
{
    /* Delete all session objects. Usually this should not be needed as closeDevice()
     * requires that all sessions have been closed before.
     */
    sessionIterator_t  sessionIterator = sessionList.begin();
    while (sessionIterator != sessionList.end()) {
        delete (*sessionIterator);
        sessionIterator = sessionList.erase(sessionIterator);
    }

    // Free all allocated WSM descriptors
    wsmIterator_t  wsmIterator = wsmL2List.begin();
    while (wsmIterator != wsmL2List.end()) {
        CWsm_ptr pWsm = *wsmIterator;

        // ignore return code
        pMcKMod->free(pWsm->handle, pWsm->virtAddr, pWsm->len);

        delete (*wsmIterator);
        wsmIterator = wsmL2List.erase(wsmIterator);
    }
    delete connection;
    delete pMcKMod;
}


//------------------------------------------------------------------------------
bool Device::open(const char *deviceName)
{
    return pMcKMod->open(deviceName);
}


//------------------------------------------------------------------------------
void Device::close(void)
{
    pMcKMod->close();
}


//------------------------------------------------------------------------------
bool Device::hasSessions(void)
{
    return sessionList.size() > 0;
}


//------------------------------------------------------------------------------
Session *Device::createNewSession(uint32_t sessionId, Connection  *connection)
{
    Session *session = new Session(sessionId, pMcKMod, connection);
    sessionList.push_back(session);
    return session;
}


//------------------------------------------------------------------------------
bool Device::removeSession(uint32_t sessionId)
{
    bool ret = false;

    sessionIterator_t interator = sessionList.begin();
    while (interator != sessionList.end()) {
        if ((*interator)->sessionId == sessionId) {
            delete (*interator);
            interator = sessionList.erase(interator);
            ret = true;
            break;
        } else {
            interator++;
        }
    }
    return ret;
}


//------------------------------------------------------------------------------
Session *Device::resolveSessionId(uint32_t sessionId)
{
    Session  *ret = NULL;

    // Get Session for sessionId
    for ( sessionIterator_t interator = sessionList.begin();
            interator != sessionList.end();
            ++interator) {
        if ((*interator)->sessionId == sessionId) {
            ret = (*interator);
            break;
        }
    }
    return ret;
}


//------------------------------------------------------------------------------
mcResult_t Device::allocateContiguousWsm(uint32_t len, CWsm **wsm)
{
    // Allocate shared memory
    addr_t    virtAddr = NULL;
    uint32_t  handle = 0;
    mcResult_t  ret;

    if (!len) {
        return MC_DRV_ERR_INVALID_LENGTH;
    }

    ret = pMcKMod->mapWsm(len, &handle, &virtAddr);
    if (ret) {
        return ret;
    }

    LOG_I(" mapped handle %d to %p", handle, virtAddr);

    // Register (vaddr,paddr) with device
    *wsm = new CWsm(virtAddr, len, handle, 0);

    wsmL2List.push_back(*wsm);

    // Return pointer to the allocated memory
    return MC_DRV_OK;
}


//------------------------------------------------------------------------------
mcResult_t Device::freeContiguousWsm(CWsm_ptr  pWsm)
{
    mcResult_t ret = MC_DRV_ERR_WSM_NOT_FOUND;
    wsmIterator_t iterator;

    for (iterator = wsmL2List.begin(); iterator != wsmL2List.end(); ++iterator) {
        if (pWsm == *iterator) {
            ret = MC_DRV_OK;
            break;
        }
    }
    // We just looked this up using findContiguousWsm
    assert(ret == MC_DRV_OK);

    LOG_I(" unmapping handle %d from %p, phys=0x%jx",
          pWsm->handle, pWsm->virtAddr, pWsm->physAddr);

    ret = pMcKMod->free(pWsm->handle, pWsm->virtAddr, pWsm->len);
    if (ret != MC_DRV_OK) {
        // developer forgot to free all references of this memory, we do not remove the reference here
        return ret;
    }

    iterator = wsmL2List.erase(iterator);
    delete pWsm;

    return ret;
}


//------------------------------------------------------------------------------
CWsm_ptr Device::findContiguousWsm(addr_t  virtAddr)
{
    CWsm_ptr pWsm = NULL;

    if (virtAddr == NULL) {
        return pWsm;
    }

    for ( wsmIterator_t iterator = wsmL2List.begin();
            iterator != wsmL2List.end();
            ++iterator) {
        CWsm_ptr pTmpWsm = *iterator;
        if (virtAddr == pTmpWsm->virtAddr) {
            pWsm = pTmpWsm;
            break;
        }
    }

    return pWsm;
}


//------------------------------------------------------------------------------
mcResult_t Device::mapBulkBuf(addr_t buf, uint32_t len, BulkBufferDescriptor **blkBuf)
{
    uint64_t PhysWsmL2;
    uint32_t handle = 0;

    *blkBuf = NULL;

    // Prepare the interface structure for memory registration in Kernel Module
    mcResult_t ret = pMcKMod->registerWsmL2(buf, len, 0, &handle, &PhysWsmL2);
    if (ret != MC_DRV_OK) {
        LOG_V(" mcKMod->registerWsmL2() failed with %x", ret);
        return ret;
    }

    LOG_V(" addBulkBuf - Handle of L2 Table = %u", handle);

    // Create new descriptor - secure virtual virtual set to 0, unknown!
          *blkBuf = new BulkBufferDescriptor(buf, 0x0, len, handle);

    return MC_DRV_OK;
}

