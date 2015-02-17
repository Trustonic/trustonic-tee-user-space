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
/**
 * Connection data.
 */
#ifndef CONNECTION_H_
#define CONNECTION_H_

#include <list>
#include <exception>

#include <inttypes.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>


class Connection
{
public:
    struct sockaddr_un remote; /**< Remote address */
    int32_t socketDescriptor; /**< Local socket descriptor */
    void *connectionData; /**< reference to data related with the connection */
    bool detached; /**< Connection state */

    Connection(void);

    Connection(int socketDescriptor, sockaddr_un *remote);

    virtual ~Connection(void);

    /**
     * Connect to destination.
     *
     * @param Destination pointer.
     * @return true on success.
     */
    virtual bool connect(const char *dest);

    /**
     * Read bytes from the connection.
     *
     * @param buffer    Pointer to destination buffer.
     * @param len       Number of bytes to read.
     * @param timeout   Timeout in milliseconds
     * @return Number of bytes read.
     * @return -1 if select() failed (returned -1)
     * @return -2 if no data available, i.e. timeout
     */
    virtual ssize_t readData(void *buffer, uint32_t len, int32_t timeout);

    /**
     * Read bytes from the connection.
     *
     * @param buffer    Pointer to destination buffer.
     * @param len       Number of bytes to read.
     * @return Number of bytes read.
     */
    virtual ssize_t readData(void *buffer, uint32_t len);

    /**
     * Write bytes to the connection.
     *
     * @param buffer    Pointer to source buffer.
     * @param len       Number of bytes to read.
     * @return Number of bytes written.
     * @return -1 if written bytes not equal to len.
     */
    virtual ssize_t writeData(void *buffer, uint32_t len);

    /**
     * Wait for data to be available.
     *
     * @param timeout   Timeout in milliseconds
     * @return 0 if data is available
     * @return error code if otherwise
     */
    virtual int waitData(int32_t timeout);

    /*
     * Checks if the socket is  still connected to the daemon
     *
     * @return true if connection is still alive.
     */
    virtual bool isConnectionAlive(void);

    /*
     * Retrieve the peer's credentials(uid, pid, gid)
     *
     * @return true if connection peers could be retrieved
     */
    virtual bool getPeerCredentials(struct ucred &cr);

};

typedef std::list<Connection *>         connectionList_t;
typedef connectionList_t::iterator     connectionIterator_t;


#endif /* CONNECTION_H_ */

