/*$*********************************************************************\

Source File     : ickWGet.c

Description     : HTTP client functions

Comments        : -

Called by       : Internal functions

Calls           : standard posix network functions, user code call nack

Date            : 23.08.2013

Updates         : -

Author          : JS, //MAF

Remarks         : We want to have an asynchronous engine that implements
                  an HTTP client and integrates with a main poll loop,
                  just like libwebsocket does for the server part.
                  Actually the best would be a libwebsocket extension...
                  As a starter, we simply use miniwget in a separate
                  thread

*************************************************************************
 * Copyright (c) 2013, ickStream GmbH
 * All rights reserved.
\************************************************************************/

#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>

#include "miniwget.h"

#include "ickP2p.h"
#include "ickP2pInternal.h"
#include "ickMainThread.h"
#include "logutils.h"
#include "ickWGet.h"


/*=========================================================================*\
  Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Private definitions and symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Private prototypes
\*=========================================================================*/
static void  _ickWGetLock( ickWGetContext_t *context );
static void  _ickWGetUnlock( ickWGetContext_t *context );
static void  _ickWGetFree( ickWGetContext_t *context );
static void *_ickWGetThread( void *arg );


/*=========================================================================*\
  Create a client context
    return NULL on error
\*=========================================================================*/
ickWGetContext_t *_ickWGetInit( ickP2pContext_t *ictx, const char *uri, ickWGetCb_t callback, void *user, ickErrcode_t *error )
{
  ickWGetContext_t *context;
  int               rc;
  pthread_attr_t    attr;
  debug( "_ickWGetInit: uri=\"%s\"", uri );

/*------------------------------------------------------------------------*\
    Create and init context
\*------------------------------------------------------------------------*/
  context = calloc( 1, sizeof(ickWGetContext_t) );
  if( !context ) {
    debug( "_ickWGetInit: out of memory" );
    if( error )
      *error = ICKERR_NOMEM;
    return NULL;
  }
  context->ictx     = ictx;
  context->userData = user;
  context->callback = callback;
  context->state    = ICKWGETSTATE_RUNNING;

/*------------------------------------------------------------------------*\
  Duplicate strings
\*------------------------------------------------------------------------*/
  context->uri  = strdup( uri );
  if( !context->uri ) {
    Sfree( context );
    logerr( "_ickWGetInit: out of memory" );
    if( error )
      *error = ICKERR_NOMEM;
    return NULL;
  }

/*------------------------------------------------------------------------*\
  Init Mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_init( &context->mutex, NULL );

/*------------------------------------------------------------------------*\
  We will not ever join this thread
\*------------------------------------------------------------------------*/
  pthread_attr_init( &attr );
  pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );

/*------------------------------------------------------------------------*\
  Start worker thread
\*------------------------------------------------------------------------*/
  rc = pthread_create( &context->thread, &attr, _ickWGetThread, context );
  if( rc ) {
    pthread_mutex_destroy( &context->mutex );
    Sfree( context->uri );
    Sfree( context );
    logerr( "_ickWGetInit: Unable to start thread: %s", strerror(rc) );
    if( error )
      *error = ICKERR_NOMEM;
    return NULL;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return context;
}


/*=========================================================================*\
  Delete an client context
\*=========================================================================*/
void _ickWGetDestroy( ickWGetContext_t *context )
{
  debug( "_ickWGetDestroy (%s): state=%d", context->uri, context->state );

/*------------------------------------------------------------------------*\
    If thread is running, pass destruction to thread.
    We won't call the callback in that case...
\*------------------------------------------------------------------------*/
  _ickWGetLock( context );
  if( context->state==ICKWGETSTATE_RUNNING ) {
    context->state = ICKWGETSTATE_TERMINATE;
    _ickWGetUnlock( context );
    return;
  }

/*------------------------------------------------------------------------*\
    Execute callback
\*------------------------------------------------------------------------*/
  context->callback( context, ICKWGETACT_DESTROY, 0 );

/*------------------------------------------------------------------------*\
    Free descriptor
\*------------------------------------------------------------------------*/
  _ickWGetFree( context );
}


/*=========================================================================*\
  Free a client context
\*=========================================================================*/
static void _ickWGetFree( ickWGetContext_t *context )
{
  debug( "_ickWGetFree (%s): state=%d", context->uri, context->state );

/*------------------------------------------------------------------------*\
    Destroy mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_destroy( &context->mutex );

/*------------------------------------------------------------------------*\
    Delete data and descriptor
\*------------------------------------------------------------------------*/
  Sfree( context->uri );
  Sfree( context->payload );
  Sfree( context->errorStr );
  Sfree( context );

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
  Lock instance
\*=========================================================================*/
static void _ickWGetLock( ickWGetContext_t *context )
{
  debug ( "_ickWGetLock (%s): locking...", context->uri );
  pthread_mutex_lock( &context->mutex );
  debug ( "_ickWGetLock (%s): locked", context->uri );
}


/*=========================================================================*\
  Unlock instance
\*=========================================================================*/
static void _ickWGetUnlock( ickWGetContext_t *context )
{
  debug ( "_ickWGetUnlock (%s): unlocked", context->uri );
  pthread_mutex_unlock( &context->mutex );
}


/*=========================================================================*\
  Get user data
\*=========================================================================*/
void *_ickWGetUserData( const ickWGetContext_t *context )
{
  return context->userData;
}


/*=========================================================================*\
  Get payload
\*=========================================================================*/
void *_ickWGetPayload( const ickWGetContext_t *context )
{
  return context->payload;
}


/*=========================================================================*\
  Get size of payload
\*=========================================================================*/
size_t _ickWGetPayloadSize( const ickWGetContext_t *context )
{
  return context->psize;
}


/*=========================================================================*\
  Get URI
\*=========================================================================*/
const char *_ickWGetUri( const ickWGetContext_t *context )
{
  return context->uri;
}

/*=========================================================================*\
  Get Error string
\*=========================================================================*/
const char *_ickWGetErrorString( const ickWGetContext_t *context )
{
  return context->errorStr;
}



/*=========================================================================*\
  Service file descriptor and call user callback
\*=========================================================================*/
ickErrcode_t _ickWGetServiceFd( ickWGetContext_t *context, struct pollfd *pollfd )
{
  int rc = 0;
  debug( "_ickWGetServiceFd (%s): state=%d", context->uri, context->state );

  _ickWGetLock( context );
  switch( context->state ) {
    case ICKWGETSTATE_ERROR:
      context->callback( context, ICKWGETACT_ERROR, 0 );
      rc = -1;
      break;

    case ICKWGETSTATE_COMPLETE:
      context->callback( context, ICKWGETACT_COMPLETE, 0 );
      rc = 1;
      break;

    default:
      break;
  }
  _ickWGetUnlock( context );

  return rc;
}



/*=========================================================================*\
  Worker thread for retrieving XML data
    This is asynchronously operating on the device and executing the
    user callbacks. It will lock the device list of the discovery handler
    which will block the main thread! So callback execution is time critical.
    We have to deal with devices and discovery handlers vanishing.
\*=========================================================================*/
static void *_ickWGetThread( void *arg )
{
  ickWGetContext_t *context = arg;
  int               size;
  debug( "ickWGet thread (%s): starting...", context->uri );
  PTHREADSETNAME( "ickWGet" );

/*------------------------------------------------------------------------*\
  Load data using miniupnp function
\*------------------------------------------------------------------------*/
  context->payload = miniwget( context->uri, &size, 5 );
  context->psize   = size;
  _ickWGetLock( context );
  if( context->state==ICKWGETSTATE_TERMINATE )
    goto terminate_and_destroy;
  if( !context->payload ) {
    logerr( "ickWGet thread (%s): could not get xml data.", context->uri );
    context->errorStr = strdup( "miniwget returned NULL" );
    context->state    = ICKWGETSTATE_ERROR;
  }
  else {
    debug( "ickWGet thread (%s): got data \"%s\"", context->uri, context->payload );
    context->state = ICKWGETSTATE_COMPLETE;
  }
  _ickWGetUnlock( context );

/*------------------------------------------------------------------------*\
    Signal main thread to start main loop prior to timeout
\*------------------------------------------------------------------------*/
  _ickMainThreadBreak( context->ictx, 'W' );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  debug( "ickWGet thread (%s): exiting...", context->uri );
  return NULL;

/*------------------------------------------------------------------------*\
    Terminate and destroy descriptor
\*------------------------------------------------------------------------*/
terminate_and_destroy:
  debug( "ickWGet thread (%s): exiting and destroying...", context->uri );
  pthread_mutex_destroy( &context->mutex );
  Sfree( context->uri );
  Sfree( context->payload );
  Sfree( context->errorStr );
  Sfree( context );
  return NULL;
}



/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
