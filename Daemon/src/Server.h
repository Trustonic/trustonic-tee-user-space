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
#ifndef SERVER_H_
#define SERVER_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string>
#include <cstdio>
#include <pthread.h>

#include <list>

#include "CThread.h"
#include "ConnectionHandler.h"

/** Number of incoming connections that can be queued.
 * Additional clients will generate the error ECONNREFUSED. */
#define LISTEN_QUEUE_LEN    8

typedef std::list<Connection *>        connectionList_t;
typedef connectionList_t::iterator     connectionIterator_t;

class Server: public CThread
{
public:
    /**
     * Server contructor.
     *
     * @param connectionHanler Connection handler to pass incoming connections to.
     * @param localAdrerss Pointer to a zero terminated
     * string containing the file to listen to.
     */
    Server(ConnectionHandler *connectionHandler,
            const char *localAddr, const int listen_queue_sz = LISTEN_QUEUE_LEN);

    /**
     * Server destructor.
     * All available connections will be terminated. Resources will be freed.
     */
    virtual ~Server();

    /**
     * Start server and listen for incoming connections.
     * Implements the central socket server loop.
     * Incoming connections will be stored.
     */
    virtual void run();

    void start()
    {
        CThread::start(Server::m_server_name);
    }

    void stop();

    bool valid() const
    {
        return m_serverSock != -1;
    }
protected:

    int m_serverSock;
    /**< Connection handler registered to the server */
    ConnectionHandler   * const m_connectionHandler;

private:
    pthread_mutex_t     m_close_lock;
    connectionList_t    m_peerConnections; /**< Connections to devices */
    static const char * const m_server_name;
};

#endif /* SERVER_H_ */

