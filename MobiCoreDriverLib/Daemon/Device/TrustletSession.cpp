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
#include "TrustletSession.h"
#include <cstdlib>

#include "log.h"

using namespace std;

//------------------------------------------------------------------------------
TrustletSession::TrustletSession(Connection *deviceConnection, uint32_t sessionId)
{
    this->deviceConnection = deviceConnection;
    this->notificationConnection = NULL;
    this->sessionId = sessionId;
    sessionMagic = rand();
    this->gp_level=0;
    this->sessionState=TS_TA_RUNNING;
}


//------------------------------------------------------------------------------
TrustletSession::~TrustletSession(void)
{
    map<uint32_t, CWsm_ptr>::iterator it;
    delete notificationConnection;

    if (!buffers.empty()) {
        LOG_W("%s: Mapped buffers still available %zu", __func__, buffers.size());
    }
    for ( it = buffers.begin() ; it != buffers.end(); it++ )
        delete (*it).second;

    buffers.clear();
}

//------------------------------------------------------------------------------
void TrustletSession::queueNotification(notification_t *notification)
{
    if (sessionState == TS_TA_DEAD) {
        return;
    }
    if ((gp_level == 1) && (notification->payload != 0)) {
        LOG_I("  Mark session %03x dead", sessionId);
        sessionState = TS_TA_DEAD;
    }
    // Note this is a very subtle synchronization requirement:
    //  The TrustletSession object is manipulated by several threads
    //  If the sessionState is set to closed,
    //  it means another thread could soon delete the object
    //  Also in this case we don't care about any notifications anymore
    if (sessionState == TS_CLOSE_SEND) {
        return;
    }

    notifications.push(*notification);
}

//------------------------------------------------------------------------------
void TrustletSession::processQueuedNotifications(void)
{
    LOG_I(" %s:%i", __FILE__, __LINE__ );

    // Nothing to do here!
    if (notificationConnection == NULL)
        return;

    while (!notifications.empty()) {
        // Forward session ID and additional payload of
        // notification to the just established connection
        notificationConnection->writeData((void *)&notifications.front(),
                                          sizeof(notification_t));
        notifications.pop();
    }
}

//------------------------------------------------------------------------------
bool TrustletSession::addBulkBuff(CWsm_ptr pWsm)
{
    if (!pWsm)
        return false;
    if (buffers.find(pWsm->handle) != buffers.end()) {
        delete pWsm;
        return false;
    }
    buffers[pWsm->handle] = pWsm;
    return true;
}

//------------------------------------------------------------------------------
bool TrustletSession::removeBulkBuff(uint32_t handle)
{
    if (buffers.find(handle) == buffers.end()) {
        return false;
    }
    CWsm_ptr pWsm = buffers[handle];
    delete pWsm;
    buffers.erase(handle);
    return true;
}

//------------------------------------------------------------------------------
bool TrustletSession::findBulkBuff(uint32_t handle, uint32_t lenBulkMem)
{
    if (buffers.find(handle) == buffers.end()) {
        return false;
    }
    CWsm_ptr pWsm = buffers[handle];
    if ((uint32_t)pWsm->len != lenBulkMem) {
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
CWsm_ptr TrustletSession::popBulkBuff()
{
    if (buffers.empty()) {
        return NULL;
    }

    CWsm_ptr pWsm = buffers.begin()->second;
    // Remove it from the map
    buffers.erase(pWsm->handle);
    return pWsm;
}

