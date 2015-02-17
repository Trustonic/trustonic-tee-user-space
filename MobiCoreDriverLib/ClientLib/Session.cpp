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
#include <stdint.h>
#include <vector>

#include "mc_linux.h"

#include "Session.h"

#include "log.h"
#include <assert.h>


//------------------------------------------------------------------------------
Session::Session(
    uint32_t    sessionId,
    CMcKMod     *mcKMod,
    Connection  *connection)
{
    this->sessionId = sessionId;
    this->mcKMod = mcKMod;
    this->notificationConnection = connection;

    sessionInfo.lastErr = SESSION_ERR_NO;
    sessionInfo.state = SESSION_STATE_INITIAL;
}


//------------------------------------------------------------------------------
Session::~Session(void)
{
    BulkBufferDescriptor  *pBlkBufDescr;

    // Unmap still mapped buffers
    for ( bulkBufferDescrIterator_t iterator = bulkBufferDescriptors.begin();
            iterator != bulkBufferDescriptors.end();
            ++iterator) {
        pBlkBufDescr = *iterator;

        LOG_I("removeBulkBuf - handle= %d",
              pBlkBufDescr->handle);

        // ignore any error, as we cannot do anything in this case.
        int ret = mcKMod->unregisterWsmL2(pBlkBufDescr->handle);
        if (ret != 0) {
            LOG_E("removeBulkBuf(): mcKModUnregisterWsmL2 failed: %d", ret);
        }

        //iterator = bulkBufferDescriptors.erase(iterator);
        delete(pBlkBufDescr);
    }

    // Finally delete notification connection
    delete notificationConnection;

    unlock();
}


//------------------------------------------------------------------------------
void Session::setErrorInfo(
    int32_t err
)
{
    sessionInfo.lastErr = err;
}


//------------------------------------------------------------------------------
int32_t Session::getLastErr(
    void
)
{
    return sessionInfo.lastErr;
}


//------------------------------------------------------------------------------
mcResult_t Session::addBulkBuf(addr_t buf, uint32_t len, BulkBufferDescriptor **blkBuf)
{
    uint64_t pPhysWsmL2;
    uint32_t handle = 0;

    assert(blkBuf != NULL);

    // Search bulk buffer descriptors for existing vAddr
    // At the moment a virtual address can only be added one time
    for ( bulkBufferDescrIterator_t iterator = bulkBufferDescriptors.begin();
            iterator != bulkBufferDescriptors.end();
            ++iterator) {
        if ((*iterator)->virtAddr == buf) {
            LOG_E("Cannot map a buffer to multiple locations in one Trustlet.");
            return MC_DRV_ERR_BUFFER_ALREADY_MAPPED;
        }
    }

    // Prepare the interface structure for memory registration in Kernel Module
    mcResult_t ret = mcKMod->registerWsmL2(buf, len, 0, &handle, &pPhysWsmL2);

    if (ret != MC_DRV_OK) {
        LOG_V(" mcKMod->registerWsmL2() failed with %x", ret);
        return ret;
    }

    LOG_V(" addBulkBuf - Handle of L2 Table = %u", handle);

    // Create new descriptor - secure virtual virtual set to 0, unknown!
    *blkBuf = new BulkBufferDescriptor(buf, 0x0, len, handle);

    // Add to vector of descriptors
    bulkBufferDescriptors.push_back(*blkBuf);

    return MC_DRV_OK;
}

//------------------------------------------------------------------------------
void Session::addBulkBuf(BulkBufferDescriptor *blkBuf)
{
    if (blkBuf)
        // Add to vector of descriptors
        bulkBufferDescriptors.push_back(blkBuf);
}

//------------------------------------------------------------------------------
uint32_t Session::getBufHandle(uint32_t sVirtAddr, uint32_t sVirtualLen)
{
    LOG_V("getBufHandle(): Virtual Address = 0x%X", sVirtAddr);

    // Search and remove bulk buffer descriptor
    for ( bulkBufferDescrIterator_t iterator = bulkBufferDescriptors.begin();
            iterator != bulkBufferDescriptors.end();
            ++iterator ) {
        if (((*iterator)->sVirtualAddr == sVirtAddr) && ((*iterator)->len == sVirtualLen)) {
            return (*iterator)->handle;
        }
    }
    return 0;
}

//------------------------------------------------------------------------------
mcResult_t Session::removeBulkBuf(addr_t virtAddr)
{
    BulkBufferDescriptor  *pBlkBufDescr = NULL;

    LOG_V("removeBulkBuf(): Virtual Address = 0x%X", (unsigned int) virtAddr);

    // Search and remove bulk buffer descriptor
    for ( bulkBufferDescrIterator_t iterator = bulkBufferDescriptors.begin();
            iterator != bulkBufferDescriptors.end();
            ++iterator
        ) {

        if ((*iterator)->virtAddr == virtAddr) {
            pBlkBufDescr = *iterator;
            iterator = bulkBufferDescriptors.erase(iterator);
            break;
        }
    }

    if (pBlkBufDescr == NULL) {
        LOG_E("%p not registered in session %03x.", virtAddr, sessionId);
        return MC_DRV_ERR_BLK_BUFF_NOT_FOUND;
    }
    LOG_V("removeBulkBuf():handle=%u", pBlkBufDescr->handle);

    // ignore any error, as we cannot do anything
    mcResult_t ret = mcKMod->unregisterWsmL2(pBlkBufDescr->handle);
    if (ret != MC_DRV_OK) {
        LOG_E("mcKMod->unregisterWsmL2 failed: %x", ret);
        return ret;
    }

    delete (pBlkBufDescr);

    return MC_DRV_OK;
}

