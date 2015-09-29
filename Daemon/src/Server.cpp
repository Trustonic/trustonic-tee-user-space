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
/**
 * Connection server.
 *
 * Handles incoming socket connections from registry library clients.
 */
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <log.h>

#include "Server.h"

const char * const Server::m_server_name = "McDaemon.Server";

class LockGuard {
    pthread_mutex_t* mutex_;
public:
    LockGuard(pthread_mutex_t* mutex): mutex_(mutex) {
        pthread_mutex_lock(mutex_);
    }
    ~LockGuard() {
        pthread_mutex_unlock(mutex_);
    }
};

//------------------------------------------------------------------------------
Server::Server(ConnectionHandler *handler, const char *localAddr,
        const int listen_queue_sz) :
    m_serverSock(-1),
    m_connectionHandler(handler)
{
    // Fill in address structure and bind to socket
    struct sockaddr_un serverAddr;
    int sock;

    pthread_mutex_init(&m_close_lock, NULL);
    if(localAddr == NULL || strlen(localAddr) == 0 || listen_queue_sz <= 0)
        return;

    LOG_D("Server: start listening on socket %s", localAddr);

    // Open a socket (a UNIX domain stream socket)
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        LOG_ERRNO("Can't open stream socket, because socket");
        return;
    }

    serverAddr.sun_family = AF_UNIX;
    strncpy(serverAddr.sun_path, localAddr, sizeof(serverAddr.sun_path) - 1);

    socklen_t len = static_cast<socklen_t>(strlen(serverAddr.sun_path) +
                                           sizeof(serverAddr.sun_family));
    // Make the socket in the Abstract Domain(no path but everyone can connect)
    serverAddr.sun_path[0] = 0;

    if (bind(sock, reinterpret_cast<struct sockaddr*>(&serverAddr), len) == 0) {
        if (listen(sock, listen_queue_sz) == 0) {
            m_serverSock = sock;
            return;
        }
        else
            LOG_ERRNO("listen");
    } else
        LOG_ERRNO("Binding to server socket failed, because bind");

    close(sock);
}

//------------------------------------------------------------------------------
void Server::run()
{
    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGUSR1);
    pthread_sigmask(SIG_UNBLOCK, &sigmask, NULL);

    LOG_D("Server::run()====");

    while ( valid() && !shouldTerminate() ) {
        fd_set fdReadSockets;

        // Clear FD for select()
        FD_ZERO(&fdReadSockets);

        LockGuard lock(&m_close_lock);

        // Select server socket descriptor
        FD_SET(m_serverSock, &fdReadSockets);
        int maxSocketDescriptor = m_serverSock;

        // Select socket descriptor of all connections
        for (auto it = m_peerConnections.begin(); it != m_peerConnections.end(); it++) {
            auto& conn = *it;
            int peerSocket = conn->socket();
            FD_SET(peerSocket, &fdReadSockets);
            if (peerSocket > maxSocketDescriptor)
                maxSocketDescriptor = peerSocket;
        }

        // Wait for activities, select() returns the number of sockets
        // which require processing
        LOG_D(" Server: waiting on sockets");
        int numSockets = select(maxSocketDescriptor + 1,
                                    &fdReadSockets, NULL, NULL, NULL);
        // Check if select failed
        if (numSockets < 0) {
            int err = errno;
            if (err == EINTR) {
                LOG_D("Giving up on signal");
            } else {
                LOG_ERRNO("select failed");
            }
            continue;
        }

        // actually, this should not happen.
        if (0 == numSockets) {
            LOG_W(" Server: select() returned 0, spurious event?.");
            continue;
        }

        LOG_D(" Server: events on %d socket(s).", numSockets);

        // Check if a new client connected to the server socket
        if (FD_ISSET(m_serverSock, &fdReadSockets)) {
            LOG_D(" Server: new connection attempt.");
            numSockets--;

            struct sockaddr_un clientAddr;
            socklen_t clientSockLen = sizeof(clientAddr);
            int clientSock = accept(m_serverSock,
                                    reinterpret_cast<struct sockaddr*>(&clientAddr),
                                    &clientSockLen);

            if (clientSock > 0) {
                Connection *connection = new Connection(clientSock,
                        &clientAddr);
                m_peerConnections.push_back(connection);
                LOG_D(" Server: new socket connection established and start listening.");
            } else
                LOG_ERRNO("accept");

            // we can ignore any errors from accepting a new connection.
            // If this fail, the client has to deal with it, we are done
            // and nothing has changed.
        }

        // Handle traffic on existing client connections
        auto it = m_peerConnections.begin();
        while ((it != m_peerConnections.end()) && (numSockets > 0)) {
            Connection *connection = *it;

            if (!FD_ISSET(connection->socket(), &fdReadSockets)) {
                ++it;
                continue;
            }

            numSockets--;

            // the connection will be terminated if command processing
            // fails
            if (!m_connectionHandler->handleConnection(*connection)) {
                LOG_D(" Server: dropping connection.");

                //Inform the driver
                m_connectionHandler->dropConnection(*connection);

                // Remove connection from list
                it = m_peerConnections.erase(it);
                delete connection;
            } else
                it++;
        }
    }

    stop();

    LOG_D("Exiting Server");
}

//------------------------------------------------------------------------------
Server::~Server()
{
    LOG_D("Destroying Server object");
    stop();
}

void Server::stop()
{
        LockGuard lock(&m_close_lock);

	// Destroy all client connections
	while(!m_peerConnections.empty()) {
            Connection *c = m_peerConnections.front();
	    m_peerConnections.pop_front();
	    delete c;
	}

        // Shut down the server socket
        if(m_serverSock != -1) {
            close(m_serverSock);
            m_serverSock = -1;
        }
}
