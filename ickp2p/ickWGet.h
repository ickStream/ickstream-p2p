/*$*********************************************************************\

Header File     : ickWGet.h

Description     : Internal include file for HTTP client functions

Comments        : -

Date            : 23.08.2013

Updates         : -

Author          : JS, //MAF

Remarks         : -

*************************************************************************
 * Copyright (c) 2013, ickStream GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of ickStream nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\************************************************************************/

#ifndef __ICKWGET_H
#define __ICKWGET_H


/*=========================================================================*\
  Includes required by definitions from this file
\*=========================================================================*/
#include <pthread.h>
#include <poll.h>
#include "ickP2p.h"


/*=========================================================================*\
  Definition of constants
\*=========================================================================*/
// none


/*=========================================================================*\
  Macro and type definitions
\*=========================================================================*/

typedef enum {
  ICKWGETACT_INIT,
  ICKWGETACT_DESTROY,
//  ICKWGETACT_ADDFD,
//  ICKWGETACT_REMOVEFD,
//  ICKWGETACT_SETFDMODE,
//  ICKWGETACT_CONNECTED,
//  ICKWGETACT_NEWDATA,
  ICKWGETACT_COMPLETE,
  ICKWGETACT_ERROR
} ickWGetAction_t;


typedef enum {
  ICKWGETSTATE_RUNNING,
  ICKWGETSTATE_COMPLETE,
  ICKWGETSTATE_ERROR,
  ICKWGETSTATE_TERMINATE
} ickWGetState_t;


/*------------------------------------------------------------------------*\
  Signatures for function pointers
\*------------------------------------------------------------------------*/
typedef ickErrcode_t (*ickWGetCb_t)( ickWGetContext_t *context, ickWGetAction_t action, int arg );

struct _ickWGetContext {
  ickWGetContext_t    *next;
  ickWGetContext_t    *prev;
  ickWGetState_t       state;
  char                *uri;       // strong
  void                *userData;  // weak
  ickWGetCb_t          callback;
  char                *payload;   // strong
  size_t               psize;
  char                *errorStr;  // strong
  ickP2pContext_t     *ictx;      // fixme: hack to signal main loop
  pthread_t            thread;    // fixme: not necessary
  pthread_mutex_t      mutex;
};


/*=========================================================================*\
  Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Internal prototypes
\*=========================================================================*/
ickWGetContext_t *_ickWGetInit( ickP2pContext_t *ictx, const char *uri, ickWGetCb_t callback, void *userData, ickErrcode_t *error );
void              _ickWGetDestroy( ickWGetContext_t *context );
ickErrcode_t      _ickWGetServiceFd( ickWGetContext_t *context, struct pollfd *pollfd );
void             *_ickWGetUserData( const ickWGetContext_t *context );
void             *_ickWGetPayload( const ickWGetContext_t *context );
size_t            _ickWGetPayloadSize( const ickWGetContext_t *context );
const char       *_ickWGetUri( const ickWGetContext_t *context );
const char       *_ickWGetErrorString( const ickWGetContext_t *context );

#endif /* __ICKWGET_H */
