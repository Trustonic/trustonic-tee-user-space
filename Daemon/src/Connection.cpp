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
 * Connection data.
 */
#include <sys/ioctl.h>
#include <unistd.h>
#include <assert.h>
#include <cstring>
#include <errno.h>
#include <poll.h>
#include <log.h>

#include "Connection.h"

//------------------------------------------------------------------------------
Connection::Connection(void):
    m_socket(-1)
{
    memset(&m_remote, 0, sizeof(m_remote));
    m_remote.sun_family = AF_UNIX;
}

//------------------------------------------------------------------------------
Connection::Connection(int socketDescriptor, sockaddr_un *remote):
        m_remote(*remote),
        m_socket(socketDescriptor)
{
    assert(NULL != remote);
    assert(-1 != socketDescriptor);
}

//------------------------------------------------------------------------------
Connection::~Connection(void)
{
    LOG_D("closing Connection... fd=%i", m_socket);
    if (m_socket != -1) {
        int ret = ::close(m_socket);
        if(ret)
            LOG_ERRNO("close");
    }
    LOG_D(" Socket connection closed.");
}

//------------------------------------------------------------------------------
bool Connection::connect(const char *dest)
{
    if (m_socket >= 0) {
        LOG_E("Already connected");
        return false;
    }

    if (sizeof(m_remote.sun_path) - 1 < strlen(dest)) {
        LOG_E("Invalid destination socket %s", dest);
        return false;
    }

    LOG_D("Connecting to %s socket", dest);
    m_remote.sun_family = AF_UNIX;
    memset(m_remote.sun_path, 0, sizeof(m_remote.sun_path));
    strncpy(m_remote.sun_path, dest, strlen(dest));

    m_socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_socket < 0) {
        LOG_ERRNO("Can't open stream socket.");
        return false;
    }

    socklen_t len = static_cast<socklen_t>(strlen(m_remote.sun_path) + sizeof(m_remote.sun_family));
    // The Daemon socket is in the Abstract Domain(LINUX ONLY!)
    m_remote.sun_path[0] = 0;
    if (::connect(m_socket, reinterpret_cast<struct sockaddr*>(&m_remote), len) < 0) {
        LOG_ERRNO("connect()");
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
ssize_t Connection::readData(void *buffer, uint32_t len, int32_t timeout)
{
    ssize_t ret = 0;
    struct timeval tv;
    struct timeval *ptv = NULL;
    fd_set readfds;

    if (timeout >= 0) {
        // Calculate timeout value
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout - (tv.tv_sec * 1000)) * 1000;
        ptv = &tv;
    }

    FD_ZERO(&readfds);
    FD_SET(m_socket, &readfds);
    ret = select(m_socket + 1, &readfds, NULL, NULL, ptv);

    // check for read error
    if (ret == -1) {
        LOG_ERRNO("select");
        return -1;
    }

    // Handle case of no descriptor ready
    if (ret == 0) {
        LOG_W("Timeout during select() / No more notifications.");
        return -2;
    }

    // one or more descriptors are ready

    // finally check if fd has been selected -> must socketDescriptor
    if (!FD_ISSET(m_socket, &readfds)) {
        LOG_ERRNO("no fd is set, select");
        return ret;
    }

    ret = recv(m_socket, buffer, len, MSG_DONTWAIT);
    if (ret == 0)
        LOG_D(" readData(): peer orderly closed connection.");

    return ret;
}

ssize_t Connection::readMsg(const iovec *iov, uint32_t nr_seg, int32_t timeout)
{
    ssize_t ret = -1;
    struct msghdr msg;
    struct timeval tv;
    struct timeval *ptv = NULL;
    fd_set readfds;

    memset(&msg, 0, sizeof(msg));
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = const_cast<iovec *>(iov);
    msg.msg_iovlen = nr_seg;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;

    if (timeout >= 0) {
        // Calculate timeout value
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout - (tv.tv_sec * 1000)) * 1000;
        ptv = &tv;
    }

    FD_ZERO(&readfds);
    FD_SET(m_socket, &readfds);
    ret = select(m_socket + 1, &readfds, NULL, NULL, ptv);

    // check for read error
    if (ret == -1) {
        LOG_ERRNO("select");
        return -1;
    }

    // Handle case of no descriptor ready
    if (ret == 0) {
        LOG_W("Timeout during select() / No more notifications.");
        return -2;
    }

    // one or more descriptors are ready

    // finally check if fd has been selected -> must socketDescriptor
    if (!FD_ISSET(m_socket, &readfds)) {
        LOG_ERRNO("no fd is set, select");
        return ret;
    }

    ret = recvmsg(m_socket, &msg, MSG_DONTWAIT);
    if (ret == 0)
        LOG_D("readData(): peer orderly closed connection.");

    return ret;
}


//------------------------------------------------------------------------------
ssize_t Connection::writeData(void *buffer, uint32_t len)
{
    size_t ret = send(m_socket, buffer, len, 0);
    if (ret != len) {
        LOG_ERRNO("could not send all data, because send");
        LOG_E("ret = %zu", ret);
        ret = -1;
    }

    return ret;
}

ssize_t Connection::writeMsg(const iovec *iov, uint32_t nr_seg)
{
    ssize_t ret = -1, len;
    struct msghdr msg;

    memset(&msg, 0, sizeof(msg));
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = const_cast<iovec *>(iov);
    msg.msg_iovlen = nr_seg;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;

    for( len = 0; nr_seg > 0; nr_seg--, iov++)
        len += iov->iov_len;

    ret = sendmsg(m_socket, &msg, 0);
    if (ret != len) {
        LOG_E("%s: could not send all data, ret=%zd, errno: %d (%s)",
               __FUNCTION__, ret, errno, strerror(errno));
        ret = -1;
    }

    return ret;
}

//------------------------------------------------------------------------------
int Connection::waitData(int32_t timeout)
{
    int ret;
    struct timeval tv;
    struct timeval *ptv = NULL;
    fd_set readfds;

    if (timeout >= 0) {
        // Calculate timeout value
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout - (tv.tv_sec * 1000)) * 1000;
        ptv = &tv;
    }

    FD_ZERO(&readfds);
    FD_SET(m_socket, &readfds);
    ret = select(m_socket + 1, &readfds, NULL, NULL, ptv);

    // check for read error
    if (ret == -1) {
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
    int retval;
    struct pollfd ufds[1];
    ufds[0].fd = m_socket;
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
    int ret = getsockopt(m_socket, SOL_SOCKET, SO_PEERCRED, &cred,
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
