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

#ifndef NQ_H_
#define NQ_H_

/** \name NQ Size Defines
 * Minimum and maximum count of elements in the notification queue.
 * @{ */
#define MIN_NQ_ELEM 1   /**< Minimum notification queue elements. */
#define MAX_NQ_ELEM 64 /**< Maximum notification queue elements. */
/** @} */

/** \name NQ Length Defines
 * Note that there is one queue for NWd->SWd and one queue for SWd->NWd
 * @{ */
#define NQ_SIZE(n)   (2*(sizeof(notificationQueueHeader_t) + (n)*sizeof(notification_t)))
#define MIN_NQ_LEN NQ_SIZE(MIN_NQ_ELEM)   /**< Minimum size for the notification queue data structure */
#define MAX_NQ_LEN NQ_SIZE(MAX_NQ_ELEM)   /**< Maximum size for the notification queue data structure */
/** @} */

/** \name Session ID Defines
 * Standard Session IDs.
 * @{ */
#define SID_MCP       0           /**< MCP session ID is used when directly communicating with the MobiCore (e.g. for starting and stopping of trustlets). */
#define SID_INVALID   0xffffffff  /**< Invalid session id is returned in case of an error. */
/** @} */

/** Notification data structure. */
typedef struct{
    uint32_t sessionId; /**< Session ID. */
    int32_t payload;    /**< Additional notification information. */
} notification_t;

/** Notification payload codes.
 * 0 indicated a plain simple notification,
 * a positive value is a termination reason from the task,
 * a negative value is a termination reason from MobiCore.
 * Possible negative values are given below.
 */
typedef enum {
    ERR_INVALID_EXIT_CODE   = -1, /**< task terminated, but exit code is invalid */
    ERR_SESSION_CLOSE       = -2, /**< task terminated due to session end, no exit code available */
    ERR_INVALID_OPERATION   = -3, /**< task terminated due to invalid operation */
    ERR_INVALID_SID         = -4, /**< session ID is unknown */
    ERR_SID_NOT_ACTIVE      = -5, /**< session is not active */
    ERR_SESSION_KILLED      = -6, /**< session was force-killed (due to an administrative command). */
} notificationPayload_t;

/** Declaration of the notification queue header.
 * layout as specified in the data structure specification.
 */
typedef struct {
    uint32_t writeCnt;  /**< Write counter. */
    uint32_t readCnt;   /**< Read counter. */
    uint32_t queueSize; /**< Queue size. */
} notificationQueueHeader_t;

/** Queue struct which defines a queue object.
 * The queue struct is accessed by the queue<operation> type of
 * function. elementCnt must be a power of two and the power needs
 * to be smaller than power of uint32_t (obviously 32).
 */
typedef struct {
    notificationQueueHeader_t hdr;              /**< Queue header. */
    notification_t notification[MIN_NQ_ELEM];   /**< Notification elements. */
} notificationQueue_t;

#endif /** NQ_H_ */

/** @} */
