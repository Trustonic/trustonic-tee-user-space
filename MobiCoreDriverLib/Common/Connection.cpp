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
 * Connection data.
 */
#include <unistd.h>
#include <assert.h>
#include <cstring>
#include <errno.h>

#include "Connection.h"
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <poll.h>

//#define LOG_VERBOSE
#include "log.h"


//------------------------------------------------------------------------------
Connection::Connection(void)
{
    connectionData = NULL;
    // Set invalid socketDescriptor
    socketDescriptor = -1;

    detached = false;

    remote.sun_family = AF_UNIX;
    memset(remote.sun_path, 0, sizeof(remote.sun_path));
}


//------------------------------------------------------------------------------
Connection::Connection(int socketDescriptor, sockaddr_un *remote)
{
    assert(-1 != socketDescriptor);

    this->socketDescriptor = socketDescriptor;
    this->remote = *remote;
    connectionData = NULL;
    detached = false;
}


//------------------------------------------------------------------------------
Connection::~Connection(void)
{
    LOG_V(" closing Connection... fd=%i", socketDescriptor);
    if (socketDescriptor != -1) {
        int ret = close(socketDescriptor);
        if(ret) {
            LOG_ERRNO("close");
        }
    }
    LOG_I(" Socket connection closed.");
}


//------------------------------------------------------------------------------
bool Connection::connect(const char *dest)
{
    int32_t len;

    if (sizeof(remote.sun_path) - 1 < strlen(dest)) {
        LOG_E("Invalid destination socket %s", dest);
        return false;
    }
    LOG_I(" Connecting to %s socket", dest);
    remote.sun_family = AF_UNIX;
    memset(remote.sun_path, 0, sizeof(remote.sun_path));
    strncpy(remote.sun_path, dest, strlen(dest));
    if ((socketDescriptor = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        LOG_ERRNO("Can't open stream socket.");
        return false;
    }
    len = strlen(remote.sun_path) + sizeof(remote.sun_family);
    // The Daemon socket is in the Abstract Domain(LINUX ONLY!)
    remote.sun_path[0] = 0;
    if (::connect(socketDescriptor, (struct sockaddr *) &remote, len) < 0) {
        LOG_ERRNO("connect()");
        return false;
    }

    return true;
}


//------------------------------------------------------------------------------
ssize_t Connection::readData(void *buffer, uint32_t len)
{
    return readData(buffer, len, -1);
}


//------------------------------------------------------------------------------
ssize_t Connection::readData(void *buffer, uint32_t len, int32_t timeout)
{
    int ret_s;
    ssize_t ret = 0;
    struct timeval tv;
    struct timeval *ptv = NULL;
    fd_set readfds;

    assert(socketDescriptor != -1);

    if (timeout >= 0) {
        // Calculate timeout value
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout - (tv.tv_sec * 1000)) * 1000;
        ptv = &tv;
    }

    FD_ZERO(&readfds);
    FD_SET(socketDescriptor, &readfds);
    ret_s = select(socketDescriptor + 1, &readfds, NULL, NULL, ptv);

    // check for read error
    if (ret_s == -1) {
        LOG_ERRNO("select");
        return -1;
    }

    // Handle case of no descriptor ready
    if (ret_s == 0) {
        LOG_W(" Timeout during select() / No more notifications.");
        return -2;
    }

    // one or more descriptors are ready

    // finally check if fd has been selected -> must socketDescriptor
    if (!FD_ISSET(socketDescriptor, &readfds)) {
        LOG_ERRNO("no fd is set, select");
        return ret_s;
    }

    ret = recv(socketDescriptor, buffer, len, MSG_DONTWAIT);
    if (ret == 0) {
        LOG_V(" readData(): peer orderly closed connection.");
    }

    return ret;
}


//------------------------------------------------------------------------------
ssize_t Connection::writeData(void *buffer, uint32_t len)
{
    assert(socketDescriptor != -1);

    ssize_t ret = send(socketDescriptor, buffer, len, 0);
    if ((uint32_t)ret != len) {
        LOG_ERRNO("could not send all data, because send");
        LOG_E("ret = %d", (uint32_t)ret);
        ret = -1;
    }

    return ret;
}


//------------------------------------------------------------------------------
int Connection::waitData(int32_t timeout)
{
    size_t ret;
    struct timeval tv;
    struct timeval *ptv = NULL;
    fd_set readfds;

    assert(socketDescriptor != -1);

    if (timeout >= 0) {
        // Calculate timeout value
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout - (tv.tv_sec * 1000)) * 1000;
        ptv = &tv;
    }

    FD_ZERO(&readfds);
    FD_SET(socketDescriptor, &readfds);
    ret = select(socketDescriptor + 1, &readfds, NULL, NULL, ptv);

    // check for read error
    if ((int)ret == -1) {
        LOG_ERRNO("select");
        return ret;
    } else if (ret == 0) {
        LOG_E("select() timed out");
        return -1;
    }

    return 0;
}

//------------------------------------------------------------------------------
bool Connection::isConnectionAlive(void)
{
    assert(socketDescriptor != -1);
    int retval;
    struct pollfd ufds[1];
    ufds[0].fd = socketDescriptor;
    ufds[0].events = POLLRDHUP;

    retval = poll(ufds, 1, 10);
    if (retval < 0 || retval > 0) {
        LOG_ERRNO("poll");
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------
bool Connection::getPeerCredentials(struct ucred &cr)
{
    struct ucred cred;
    socklen_t len = sizeof (cred);
    assert(socketDescriptor != -1);
    int ret = getsockopt(socketDescriptor, SOL_SOCKET, SO_PEERCRED, &cred,
                         &len);
    if (ret != 0) {
        LOG_ERRNO("getsockopt");
        return false;
    }
    if (len == sizeof(cred)) {
        cr = cred;
        return true;
    }
    return false;
}

