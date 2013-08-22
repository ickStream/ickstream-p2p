/*$*********************************************************************\

Source File     : ickP2p.c

Description     : implement ickp2p API calls

Comments        : -

Called by       : User code

Calls           : Internal functions

Date            : 11.08.2013

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
 * this SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS for A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE for ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF this SOFTWARE,
 * EVEN if ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <sys/utsname.h>

#include "ickP2p.h"
#include "ickP2pInternal.h"
#include "ickMainThread.h"
#include "ickDiscovery.h"
#include "logutils.h"


/*=========================================================================*\
  Global symbols
\*=========================================================================*/
_ickP2pLibContext_t *_ickLib;


/*=========================================================================*\
  Private definitions and symbols
\*=========================================================================*/
static  pthread_t _ickThread;


/*=========================================================================*\
  Private prototypes
\*=========================================================================*/
// none

/*=========================================================================*\
  Get major and minor version
    major, minor might be NULL
    returns string of form "major.minor"
\*=========================================================================*/
const char *ickP2pGetVersion( int *major, int *minor )
{
  static char buffer[8];

/*------------------------------------------------------------------------*\
    Store numerical versions to pointers, if given
\*------------------------------------------------------------------------*/
  if( major )
    *major = ICK_VERSION_MAJOR;
  if( minor )
    *minor = ICK_VERSION_MINOR;

/*------------------------------------------------------------------------*\
    Build version strong only once
\*------------------------------------------------------------------------*/
  if( !*buffer )
    sprintf( buffer, "%d.%d", ICK_VERSION_MAJOR, ICK_VERSION_MINOR );

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return buffer;
}


/*=========================================================================*\
  Get git version as string
\*=========================================================================*/
const char *ickP2pGitVersion( void )
{

/*------------------------------------------------------------------------*\
    play some preprocessor tricks
\*------------------------------------------------------------------------*/
#ifdef GIT_VERSION
#define STRINGIZE(X) #X
#define GIT_STRING(X) STRINGIZE(X)
  return GIT_STRING(GIT_VERSION);
#else
  return "<unknown>";
#endif
}


/*=========================================================================*\
  Initialize ickstream library
\*=========================================================================*/
ickErrcode_t ickP2pInit( const char *deviceName, const char *deviceUuid, const char *upnpFolder, int liveTime, long bootId, long configId )
{
  int             rc;
  ickErrcode_t    irc;
  struct utsname  name;
  struct timeval  now;
  struct timespec abstime;

  debug( "ickP2pInit: \"%s\" (%s) lt=%d bid=%ld cid=%d folder=\"\%s\"",
         deviceName, deviceUuid, liveTime, bootId, configId, upnpFolder );

/*------------------------------------------------------------------------*\
    Check status
\*------------------------------------------------------------------------*/
  if( _ickLib ) {
    logerr( "ickP2pInit: already initialized." );
    return ICKERR_INITIALIZED;
  }

/*------------------------------------------------------------------------*\
    Initialize random number generator
\*------------------------------------------------------------------------*/
  srandom( (unsigned int)time(NULL) );

/*------------------------------------------------------------------------*\
    Allocate and initialize context descriptor
\*------------------------------------------------------------------------*/
  _ickLib = calloc( 1, sizeof(_ickP2pLibContext_t) );
  if( !_ickLib ) {
    logerr( "ickP2pInit: out of memory." );
    return ICKERR_NOMEM;
  }
  _ickLib->liveTime = liveTime;

/*------------------------------------------------------------------------*\
    Init mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_init( &_ickLib->mutex, NULL );
  pthread_mutex_init( &_ickLib->discoveryHandlersMutex, NULL );
  pthread_mutex_init( &_ickLib->timersMutex, NULL );

/*------------------------------------------------------------------------*\
    Get name and version of operating system
\*------------------------------------------------------------------------*/
  if( !uname(&name) )
    asprintf( &_ickLib->osName, "%s/%s", name.sysname, name.release );
  else {
    logerr( "ickInitDiscovery: could not get uname (%s)", strerror(errno) );
    _ickLib->osName = strdup( "Generic/1.0" );
  }

/*------------------------------------------------------------------------*\
    Store name and UUID and folder name
\*------------------------------------------------------------------------*/
  _ickLib->deviceName = strdup( deviceName );
  _ickLib->deviceUuid = strdup( deviceUuid );
  if( upnpFolder )
    _ickLib->upnpFolder = strdup( upnpFolder );
  if( !_ickLib->deviceName || !_ickLib->deviceName || !_ickLib->osName ||
      (upnpFolder&&!_ickLib->upnpFolder)) {
    logerr( "ickP2pInit: out of memory." );
    _ickLibDestruct( &_ickLib );
    return ICKERR_NOMEM;
  }

/*------------------------------------------------------------------------*\
    Initialize bootID and configID
\*------------------------------------------------------------------------*/
  _ickLib->upnpBootId   = (bootId>0)   ? bootId   : (long)time(NULL);
  _ickLib->upnpConfigId = (configId>0) ? configId : (long)time(NULL);

/*------------------------------------------------------------------------*\
    Create pipe for poll breaking
\*------------------------------------------------------------------------*/
  rc = pipe( _ickLib->pollBreakPipe );
  if( rc ) {
    logerr( "ickP2pInit: Unable to start main thread: %s", strerror(errno) );
    _ickLibDestruct( &_ickLib );
    return ICKERR_NOTHREAD;
  }

/*------------------------------------------------------------------------*\
    Create thread for ickstream communication mainloop
\*------------------------------------------------------------------------*/
  rc = pthread_create( &_ickThread, NULL, _ickMainThread, &_ickLib );
  if( rc ) {
    logerr( "ickP2pInit: Unable to start main thread: %s", strerror(rc) );
    _ickLibDestruct( &_ickLib );
    return ICKERR_NOTHREAD;
  }

/*------------------------------------------------------------------------*\
    Wait for max. 5 seconds till thread is up and running
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &_ickLib->mutex );
  gettimeofday( &now, NULL );
  abstime.tv_sec  = now.tv_sec + 5;
  abstime.tv_nsec = now.tv_usec*1000UL;
  while( _ickLib->state!=ICKLIB_RUNNING ) {
    rc = pthread_cond_timedwait( &_ickLib->condIsReady, &_ickLib->mutex, &abstime );
    if( rc )
      break;
  }
  if( !rc )
    pthread_mutex_unlock( &_ickLib->mutex );

  // Something went terribly wrong. Can't free descriptor, since thread state is undefined.
  if( rc ) {
    logerr( "ickP2pInit: Unable to wait for main thread: %s", strerror(rc) );
    _ickLib->state = ICKLIB_TERMINATING;
    _ickLib = NULL;
    return ICKERR_NOTHREAD;
  }

  // Handle main thread initialization errors
  if( _ickLib->error ) {
    irc = _ickLib->error;
    _ickLibDestruct( &_ickLib );
    return irc ? irc : ICKERR_NOTHREAD;
  }

/*------------------------------------------------------------------------*\
    Set status and return
\*------------------------------------------------------------------------*/
  debug( "ickLibInit: created context %p", _ickLib );

  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Shut down ickstream library
\*=========================================================================*/
ickErrcode_t ickP2pEnd( ickP2pEndCb_t callback )
{
  int rc;

  debug( "ickP2pEnd: %s", callback?"asynchronous":"synchronous" );

/*------------------------------------------------------------------------*\
    Check status
\*------------------------------------------------------------------------*/
  if( !_ickLib ) {
    logerr( "ickP2pEnd: not initialized." );
    return ICKERR_UNINITIALIZED;
  }

/*------------------------------------------------------------------------*\
    Store callback for asynchronous shutdown and request thread termination
\*------------------------------------------------------------------------*/
  _ickLib->cbEnd = callback;
  _ickLib->state = ICKLIB_TERMINATING;

/*------------------------------------------------------------------------*\
    Wait for actual thread termination in synchronous mode
\*------------------------------------------------------------------------*/
  if( !callback ) {
    rc = pthread_join( _ickThread, NULL );
    if( rc ) {
      logerr( "ickP2pEnd: Unable to join main thread: %s", strerror(rc) );
      return ICKERR_GENERIC;
    }
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Free library context
\*=========================================================================*/
void _ickLibDestruct( _ickP2pLibContext_t **icklibptr )
{
  struct _cb_list *walk;
  int              i;
  debug( "_ickLibDestruct: %p", *icklibptr );

/*------------------------------------------------------------------------*\
    Be defensive
\*------------------------------------------------------------------------*/
  if( !*icklibptr ) {
    logwarn( "_ickLibDestruct: cannot free nil pointer" );
    return;
  }

/*------------------------------------------------------------------------*\
    Clear list of callbacks
\*------------------------------------------------------------------------*/
  // pthread_mutex_lock( &(*icklibptr)->mutex );
  walk = (*icklibptr)->deviceCbList;
  (*icklibptr)->deviceCbList = NULL;
  while( walk ) {
    struct _cb_list *next = walk->next;
    Sfree( walk );
    walk = next;
  }

  // lwsPolllist's livecycle is handled by main thread

/*------------------------------------------------------------------------*\
    Free strong string references
\*------------------------------------------------------------------------*/
  Sfree( (*icklibptr)->osName );
  Sfree( (*icklibptr)->deviceName );
  Sfree( (*icklibptr)->deviceUuid );
  Sfree( (*icklibptr)->upnpFolder );

/*------------------------------------------------------------------------*\
    Delete mutex and condition
\*------------------------------------------------------------------------*/
  pthread_mutex_destroy( &(*icklibptr)->mutex );
  pthread_cond_destroy( &(*icklibptr)->condIsReady );
  pthread_mutex_destroy( &(*icklibptr)->discoveryHandlersMutex );
  pthread_mutex_destroy( &(*icklibptr)->timersMutex );

/*------------------------------------------------------------------------*\
    Close help pipe for breaking polls
\*------------------------------------------------------------------------*/
  for( i=0; i<2; i++ ) {
    if( _ickLib->pollBreakPipe[i]>=0 )
      close( _ickLib->pollBreakPipe[i] );
  }

/*------------------------------------------------------------------------*\
    Free context descriptor
\*------------------------------------------------------------------------*/
  Sfree( *icklibptr );
  *icklibptr = NULL;
}


/*=========================================================================*\
  Get library state
\*=========================================================================*/
ickP2pLibState_t ickP2pGetState( void )
{
  return _ickLib ? _ickLib->state : ICKLIB_UNINITIALIZED;
}


/*=========================================================================*\
  Get device name
\*=========================================================================*/
const char *ickP2pGetDeviceName( void )
{
  return _ickLib ? _ickLib->deviceName : NULL;
}


/*=========================================================================*\
  Get device name
\*=========================================================================*/
const char *ickP2pGetDeviceUuid( void )
{
  return _ickLib ? _ickLib->deviceUuid : NULL;
}


/*=========================================================================*\
  Get os name
\*=========================================================================*/
const char *ickP2pGetOsName( void )
{
  return _ickLib ? _ickLib->osName : NULL;
}


/*=========================================================================*\
  Get os name
\*=========================================================================*/
int ickP2pGetLwsPort( void )
{
  return _ickLib ? _ickLib->lwsPort : -1;
}


/*=========================================================================*\
  Get current boot ID
\*=========================================================================*/
long ickP2pGetBootId( void )
{
  return _ickLib ? _ickLib->upnpBootId : -1;
}


/*=========================================================================*\
  Get current config ID
\*=========================================================================*/
long ickP2pGetConfigId( void )
{
  return _ickLib ? _ickLib->upnpConfigId : -1;
}

/*=========================================================================*\
  Get livetime of announcements
\*=========================================================================*/
int ickP2pGetLiveTime( void )
{
  return _ickLib ? _ickLib->liveTime : -1;
}


/*=========================================================================*\
  Suspend network IO
    This will also send upnp byebye messages
\*=========================================================================*/
ickErrcode_t ickP2pSuspend( ickP2pSuspendCb_t callback  )
{
  debug( "ickP2pSuspend: %ld", ickP2pGetBootId() );

/*------------------------------------------------------------------------*\
    Need to be running or at least resuming for this
\*------------------------------------------------------------------------*/
  if( ickP2pGetState()!=ICKLIB_RUNNING && ickP2pGetState()!=ICKLIB_RESUMING ) {
    logwarn( "ickP2pSuspend: wrong state (%d)", ickP2pGetState() );
    return ICKERR_WRONGSTATE;
  }

  // fixme; send byebye for all config elements
  logerr( "ickP2pSuspend: not yet implemented" );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  resume network IO
    This will increase bootID and reannounce all upnp data
\*=========================================================================*/
ickErrcode_t ickP2pResume( void )
{

/*------------------------------------------------------------------------*\
    Need to be suspended for this
\*------------------------------------------------------------------------*/
  if( ickP2pGetState()!=ICKLIB_SUSPENDED ) {
    logwarn( "ickP2pResume: wrong state (%d)", ickP2pGetState() );
    return ICKERR_WRONGSTATE;
  }

/*------------------------------------------------------------------------*\
    Increment boot counter
\*------------------------------------------------------------------------*/
  _ickLib->upnpBootId++;
  debug( "ickP2pResume: %ld", ickP2pGetBootId() );

  //fixme: readvertise configuration
  logerr( "ickP2pResume: not yet implemented" );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
    Rename device
\*=========================================================================*/
ickErrcode_t ickP2pSetDeviceName( const char *deviceName )
{
  debug( "ickP2pSetDeviceName: \"%s\"", deviceName );

  // fixme
  return ICKERR_NOTIMPLEMENTED;

  // increment config ID
  // for all handlers:
  //    _ick_notifications_send( ICK_SEND_CMD_NOTIFY_ADD, NULL );
}


/*=========================================================================*\
  Add a discovery callback
    return 0 on success
\*=========================================================================*/
ickErrcode_t ickP2pRegisterDiscoveryCallback( ickDiscoveryDeviceCb_t callback )
{
  struct _cb_list *new;

/*------------------------------------------------------------------------*\
    Need to be initialized
\*------------------------------------------------------------------------*/
  if( ickP2pGetState()==ICKLIB_UNINITIALIZED ) {
    logwarn( "ickP2pRegisterDeviceCallback: not initialized." );
    return ICKERR_UNINITIALIZED;
  }

/*------------------------------------------------------------------------*\
    Avoid double subscitions
\*------------------------------------------------------------------------*/
  for( new=_ickLib->deviceCbList; new; new=new->next ) {
    if( new->callback!=callback )
      continue;
    logwarn( "ickP2RegisterDeviceCallback: callback already registered" );
    return ICKERR_SUCCESS;
  }

/*------------------------------------------------------------------------*\
    Allocate and init new list element
\*------------------------------------------------------------------------*/
  new = calloc( 1, sizeof(struct _cb_list) );
  if( !new ) {
    logerr( "ickP2pRegisterDeviceCallback: out of memory" );
    return ICKERR_NOMEM;
  }
  new->callback = callback;

/*------------------------------------------------------------------------*\
    Add to linked list
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &_ickLib->mutex );
  new->next = _ickLib->deviceCbList;
  _ickLib->deviceCbList = new;
  pthread_mutex_unlock( &_ickLib->mutex );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}

/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
