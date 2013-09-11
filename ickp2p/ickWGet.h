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
