/*
 * Copyright (c) 2013 TRUSTONIC LIMITED
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
#ifndef TRUSTLETSESSION_H_
#define TRUSTLETSESSION_H_

#include "NotificationQueue.h"
#include "CWsm.h"
#include "Connection.h"
#include <queue>
#include <map>


class TrustletSession
{
private:
    std::queue<notification_t> notifications;
    std::map<uint32_t, CWsm_ptr> buffers;

public:
    uint32_t sessionId; // Assigned by t-base
    uint32_t sessionMagic; // Random data
    Connection *deviceConnection; // Command socket for client "device"
    Connection *notificationConnection; // Notification socket for client session
    uint32_t gp_level;
    enum TS_STATE {
        TS_TA_RUNNING,//->dead,close_send
        TS_TA_DEAD, //->close_send, closed
        TS_CLOSE_SEND,//->close_send, dead, closed
        TS_CLOSED,//unused
    } sessionState;

    TrustletSession(Connection *deviceConnection, uint32_t sessionId);

    ~TrustletSession(void);

    void queueNotification(notification_t *notification);

    void processQueuedNotifications(void);

    bool addBulkBuff(CWsm_ptr pWsm);

    bool removeBulkBuff(uint32_t handle);

    bool findBulkBuff(uint32_t handle, uint32_t lenBulkMem);

    CWsm_ptr popBulkBuff();

};

typedef std::list<TrustletSession *> trustletSessionList_t;
typedef trustletSessionList_t::iterator trustletSessionIterator_t;

#endif /* TRUSTLETSESSION_H_ */

