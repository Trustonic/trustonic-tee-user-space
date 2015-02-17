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

#ifndef QUEUE_H_
#define QUEUE_H_

#include "pthread.h"

template <class T>
class Queue {
    std::list<T> queue_;
    pthread_mutex_t mutex_;
    pthread_cond_t condition_;
public:
    Queue() {
        pthread_mutex_init(&mutex_, NULL);
        pthread_cond_init(&condition_, NULL);
    }
    ~Queue() {
        pthread_mutex_destroy(&mutex_);
        pthread_cond_destroy(&condition_);
    }
    void push(T data) {
        pthread_mutex_lock(&mutex_);
        queue_.push_back(data);
        pthread_cond_signal(&condition_);
        pthread_mutex_unlock(&mutex_);
    }
    T pop() {
        pthread_mutex_lock(&mutex_);
        while (queue_.empty()) {
            pthread_cond_wait(&condition_, &mutex_);
        }
        T data = queue_.front();
        queue_.pop_front();
        pthread_mutex_unlock(&mutex_);
        return data;
    }
};

#endif /* QUEUE_H_ */
