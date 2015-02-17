/*
 * Copyright (c) 2014 TRUSTONIC LIMITED
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

#ifndef CLIENT_H_
#define CLIENT_H_

#include "pthread.h"

#include "Queue.h"
#include "Connection.h"
#include "MobiCoreDriverCmd.h"

class Client: public CThread {
    Queue<Client*>& queue_;
    ConnectionHandler* handler_;
    Connection* connection_;
    pthread_mutex_t mutex_;
    pthread_cond_t condition_;
    uint32_t command_id_;
    bool locked_;
    bool dead_;
public:
    Client(ConnectionHandler* handler, int sock, struct sockaddr_un* sockaddr, Queue<Client*>& queue):
            queue_(queue), handler_(handler), command_id_(0), locked_(false), dead_(false) {
        connection_ = new Connection(sock, sockaddr);
        pthread_mutex_init(&mutex_, NULL);
        pthread_cond_init(&condition_, NULL);
    }
    ~Client() {
        delete connection_;
        pthread_mutex_destroy(&mutex_);
        pthread_cond_destroy(&condition_);
    }
    Connection* connection() {
        return connection_;
    }
    uint32_t commandId() const {
        return command_id_;
    }
    bool isDead() const {
        return dead_;
    }
    // Safe because called on command MC_DRV_CMD_NQ_CONNECT so thread is locked waiting for command to be treated
    void detachConnection() {
        connection_ = NULL;
    }
    bool isDetached() const {
        return connection_ == NULL;
    }
    void dropConnection() {
        LOG_I("Client: %p shutting down due to error", this);
        handler_->dropConnection(connection_);
    }
    void unlock() {
        pthread_mutex_lock(&mutex_);
        locked_ = false;
        pthread_cond_signal(&condition_);
        pthread_mutex_unlock(&mutex_);
    }
    void run() {
        while (!dead_) {
            if (!connection_ || !handler_->readCommand(connection_, &command_id_)) {
                dead_ = true;
            } else if (command_id_ == MC_DRV_CMD_NOTIFY) {
                // Notification: send immediately
                handler_->handleCommand(connection_, command_id_);
                continue;
            }
            // Standard command or needs dropping: queue
            pthread_mutex_lock(&mutex_);
            queue_.push(this);
            locked_ = true;
            while (locked_) {
                pthread_cond_wait(&condition_, &mutex_);
            }
            pthread_mutex_unlock(&mutex_);
        }
    }
};

#endif /* CLIENT_H_ */

