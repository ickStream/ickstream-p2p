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
//#include <sys/socket.h>
#include <arpa/inet.h>

#include "ickP2p.h"
#include "ickP2pInternal.h"
#include "logutils.h"
#include "ickIpTools.h"
#include "ickSSDP.h"
#include "ickWGet.h"
#include "ickDevice.h"
#include "ickMainThread.h"


/*=========================================================================*\
  Private definitions and symbols
\*=========================================================================*/
// none

/*=========================================================================*\
  Private prototypes
\*=========================================================================*/
// none


#pragma mark -- Global functions not bound to an inckStream context

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



#pragma mark -- Lifecycle of ickstream instances

/*=========================================================================*\
  Initialize ickstream library
\*=========================================================================*/
ickP2pContext_t *ickP2pInit( const char *deviceName, const char *deviceUuid,
                             const char *upnpFolder, int liveTime,
                             long bootId, long configId,
                             const char *hostname, const char *ifname, int port,
                             ickP2pDeviceCb_t deviceCb, ickP2pMessageCb_t messageCb,
                             ickErrcode_t *error )
{
  ickP2pContext_t *ictx;
  int              rc;
  ickErrcode_t     irc;
  in_addr_t        ifaddr;
  char             buffer[64];
  struct utsname   utsname;
  struct timeval   now;
  struct timespec  abstime;

  debug( "ickP2pInit: \"%s\" (%s) lt=%d bid=%ld cid=%d folder=\"\%s\"",
         deviceName, deviceUuid, liveTime, bootId, configId, upnpFolder );

/*------------------------------------------------------------------------*\
    (Re-)initialize random number generator
\*------------------------------------------------------------------------*/
  srandom( (unsigned int)time(NULL) );

/*------------------------------------------------------------------------*\
    Get IP string from interface
\*------------------------------------------------------------------------*/
  ifaddr   = _ickIpGetIfAddr( ifname );
  if( ifaddr==INADDR_NONE ) {
    logwarn( "ickP2pInit: could not get IP address of interface \"%s\"",
             ifname );
    if( error )
      *error = ICKERR_NOINTERFACE;
    return NULL;
  }
  inet_ntop( AF_INET, &ifaddr, buffer, sizeof(buffer) );
  debug( "ickP2pInit: Using addr %s for interface \"%s\".", buffer, ifname );

/*------------------------------------------------------------------------*\
    Allocate and initialize context descriptor
\*------------------------------------------------------------------------*/
  ictx = calloc( 1, sizeof(ickP2pContext_t) );
  if( !ictx ) {
    logerr( "ickP2pInit: out of memory." );
    if( error )
      *error = ICKERR_NOMEM;
    return NULL;
  }
  ictx->upnpPort = port;
  ictx->liveTime = liveTime;
  ictx->deviceCb = deviceCb;
  ictx->messageCb = messageCb;

/*------------------------------------------------------------------------*\
    Init mutexes
\*------------------------------------------------------------------------*/
  pthread_mutex_init( &ictx->mutex, NULL );
  pthread_mutex_init( &ictx->timersMutex, NULL );
  pthread_mutex_init( &ictx->wGettersMutex, NULL );
  pthread_mutex_init( &ictx->deviceListMutex, NULL );

/*------------------------------------------------------------------------*\
    Get name and version of operating system
\*------------------------------------------------------------------------*/
  if( !uname(&utsname) )
    asprintf( &ictx->osName, "%s/%s", utsname.sysname, utsname.release );
  else {
    logerr( "ickP2pInit: could not get uname (%s)", strerror(errno) );
    ictx->osName = strdup( "Generic/1.0" );
  }

/*------------------------------------------------------------------------*\
    Get hostname
\*------------------------------------------------------------------------*/
  if( !hostname )
    ictx->hostName = strdup( buffer );
  else
    ictx->hostName = strdup( hostname );

/*------------------------------------------------------------------------*\
    Try to duplicate strings
\*------------------------------------------------------------------------*/
  ictx->deviceName = strdup( deviceName );
  ictx->deviceUuid = strdup( deviceUuid );
  ictx->interface    = strdup( ifname );
  if( upnpFolder )
    ictx->upnpFolder = strdup( upnpFolder );
  if( !ictx->deviceName || !ictx->deviceName || !ictx->osName || !ictx->interface ||
      !ictx->hostName || (upnpFolder&&!ictx->upnpFolder)) {
    logerr( "ickP2pInit: out of memory." );
    _ickLibDestruct( ictx );
    if( error )
      *error = ICKERR_NOMEM;
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Initialize bootID and configID
\*------------------------------------------------------------------------*/
  ictx->upnpBootId   = (bootId>0)   ? bootId   : (long)time(NULL);
  ictx->upnpConfigId = (configId>0) ? configId : (long)time(NULL);

/*------------------------------------------------------------------------*\
    Create and init SSDP listener socket
\*------------------------------------------------------------------------*/
  ictx->upnpSocket = _ickSsdpCreateListener( ifaddr, port );
  if( ictx->upnpSocket<0 ){
    logerr( "ickP2pInit: could not create socket (%s).", strerror(errno) );
    _ickLibDestruct( ictx );
    if( error )
      *error = ICKERR_NOSOCKET;
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Create pipe for poll breaking
\*------------------------------------------------------------------------*/
  rc = pipe( ictx->pollBreakPipe );
  if( rc ) {
    logerr( "ickP2pInit: Unable to start main thread: %s", strerror(errno) );
    _ickLibDestruct( ictx );
    if( error )
      *error = ICKERR_NOTHREAD;
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Create thread for ickstream communication mainloop
\*------------------------------------------------------------------------*/
  rc = pthread_create( &ictx->thread, NULL, _ickMainThread, &ictx );
  if( rc ) {
    logerr( "ickP2pInit: Unable to start main thread: %s", strerror(rc) );
    _ickLibDestruct( ictx );
    if( error )
      *error = ICKERR_NOTHREAD;
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Wait for max. 5 seconds till thread is up and running
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &ictx->mutex );
  gettimeofday( &now, NULL );
  abstime.tv_sec  = now.tv_sec + 5;
  abstime.tv_nsec = now.tv_usec*1000UL;
  while( ictx->state!=ICKLIB_RUNNING ) {
    rc = pthread_cond_timedwait( &ictx->condIsReady, &ictx->mutex, &abstime );
    if( rc )
      break;
  }
  if( !rc )
    pthread_mutex_unlock( &ictx->mutex );

  // Something went terribly wrong. Can't free descriptor, since thread state is undefined.
  if( rc ) {
    logerr( "ickP2pInit: Unable to wait for main thread: %s", strerror(rc) );
    ictx->state = ICKLIB_TERMINATING;
    ictx = NULL;
    if( error )
      *error = ICKERR_NOTHREAD;
    return NULL;
  }

  // Handle main thread initialization errors
  if( ictx->error ) {
    irc = ictx->error;
    _ickLibDestruct( ictx );
    if( error )
      *error = irc ? irc : ICKERR_NOTHREAD;
    return NULL;
  }

  // fixme: Need to add localhost??

/*------------------------------------------------------------------------*\
    Start SSDP services
\*------------------------------------------------------------------------*/
  irc = _ickSsdpNewDiscovery( ictx );
  if( irc ) {
    logerr( "ickP2pInit: could not start SSDP (%s).",
            ickStrError( irc ) );
    // fixme
    if( error )
      *error = irc;
    return NULL;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  debug( "ickLibInit: created context %p", ictx );
  if( error )
    *error = ICKERR_SUCCESS;
  return ictx;
}


/*=========================================================================*\
  Shut down ickstream library
\*=========================================================================*/
ickErrcode_t ickP2pEnd( ickP2pContext_t *ictx, ickP2pEndCb_t callback )
{
  int rc;

  debug( "ickP2pEnd (%p): %s", ictx, callback?"asynchronous":"synchronous" );

/*------------------------------------------------------------------------*\
    Store callback for asynchronous shutdown and request thread termination
\*------------------------------------------------------------------------*/
  ictx->cbEnd = callback;
  ictx->state = ICKLIB_TERMINATING;
  _ickMainThreadBreak( ictx, 'X' );

/*------------------------------------------------------------------------*\
    Wait for actual thread termination in synchronous mode
\*------------------------------------------------------------------------*/
  if( !callback ) {
    rc = pthread_join( ictx->thread, NULL );
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
  Lock context decriptor
\*=========================================================================*/
void _ickLibLock( ickP2pContext_t *ictx )
{
  debug ( "_ickLibLock (%p): locking...", ictx );
  pthread_mutex_lock( &ictx->mutex );
  debug ( "_ickLibLock (%p): locked", ictx );
}


/*=========================================================================*\
  Unlock context descriptor
\*=========================================================================*/
void _ickLibUnlock( ickP2pContext_t *ictx )
{
  debug ( "_ickLibUnlock (%p): unlocked", ictx );
  pthread_mutex_unlock( &ictx->mutex );
}


/*=========================================================================*\
  Free library context
\*=========================================================================*/
void _ickLibDestruct( ickP2pContext_t *ictx )
{
  int              i;
  debug( "_ickP2pDestruct: %p", ictx );

/*------------------------------------------------------------------------*\
    Be defensive
\*------------------------------------------------------------------------*/
  if( !ictx ) {
    logwarn( "_ickP2pDestruct: cannot free nil pointer" );
    return;
  }

/*------------------------------------------------------------------------*\
    Close SSDP socket (if any)
\*------------------------------------------------------------------------*/
  if( ictx->upnpSocket>=0 )
    close( ictx->upnpSocket );

// lwsPolllist's livecycle is handled by main thread

/*------------------------------------------------------------------------*\
    Free strong string references
\*------------------------------------------------------------------------*/
  Sfree( ictx->osName );
  Sfree( ictx->deviceName );
  Sfree( ictx->deviceUuid );
  Sfree( ictx->upnpFolder );
  Sfree( ictx->interface );
  Sfree( ictx->locationRoot );

/*------------------------------------------------------------------------*\
    Delete mutex and condition
\*------------------------------------------------------------------------*/
  pthread_mutex_destroy( &ictx->mutex );
  pthread_cond_destroy( &ictx->condIsReady );
  pthread_mutex_destroy( &ictx->timersMutex );
  pthread_mutex_destroy( &ictx->wGettersMutex );
  pthread_mutex_destroy( &ictx->deviceListMutex );

/*------------------------------------------------------------------------*\
    Close help pipe for breaking polls
\*------------------------------------------------------------------------*/
  for( i=0; i<2; i++ ) {
    if( ictx->pollBreakPipe[i]>=0 )
      close( ictx->pollBreakPipe[i] );
  }

/*------------------------------------------------------------------------*\
    Free context descriptor
\*------------------------------------------------------------------------*/
  Sfree( ictx );
}


/*=========================================================================*\
  Suspend network IO
    This will also send upnp byebye messages
\*=========================================================================*/
ickErrcode_t ickP2pSuspend( ickP2pContext_t *ictx, ickP2pSuspendCb_t callback  )
{
  debug( "ickP2pSuspend (%p): bootid is %ld", ictx, ickP2pGetBootId(ictx) );

/*------------------------------------------------------------------------*\
    Need to be running or at least resuming for this
\*------------------------------------------------------------------------*/
  if( ictx->state!=ICKLIB_RUNNING && ictx->state!=ICKLIB_RESUMING ) {
    logwarn( "ickP2pSuspend: wrong state (%d)", ictx->state );
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
ickErrcode_t ickP2pResume( ickP2pContext_t *ictx )
{

/*------------------------------------------------------------------------*\
    Need to be suspended for this
\*------------------------------------------------------------------------*/
  if( ictx->state!=ICKLIB_SUSPENDED ) {
    logwarn( "ickP2pResume: wrong state (%d)", ictx->state );
    return ICKERR_WRONGSTATE;
  }

/*------------------------------------------------------------------------*\
    Increment boot counter
\*------------------------------------------------------------------------*/
  ictx->upnpBootId++;
  debug( "ickP2pResume (%s): bootid is %ld", ickP2pGetBootId(ictx) );

  //fixme: readvertise configuration
  logerr( "ickP2pResume: not yet implemented" );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
}


#pragma mark -- Setters and getters

/*=========================================================================*\
    Rename device
\*=========================================================================*/
ickErrcode_t ickP2pSetName( ickP2pContext_t *ictx, const char *name )
{
  debug( "ickP2pSetName (%s): \"%s\"", ictx, name );

  // fixme
  return ICKERR_NOTIMPLEMENTED;

  // increment config ID
  // for all handlers:
  //    _ick_notifications_send( ICK_SEND_CMD_NOTIFY_ADD, NULL );
}

/*=========================================================================*\
  Get library state
\*=========================================================================*/
ickP2pLibState_t ickP2pGetState( const ickP2pContext_t *ictx )
{
  return ictx->state;
}

/*=========================================================================*\
  Get os name
\*=========================================================================*/
const char *ickP2pGetOsName( const ickP2pContext_t *ictx )
{
  return ictx->osName;

}

/*=========================================================================*\
  Get device name
\*=========================================================================*/
const char *ickP2pGetName( const ickP2pContext_t *ictx )
{
  return ictx->deviceName;
}


/*=========================================================================*\
  Get device name
\*=========================================================================*/
const char *ickP2pGetDeviceUuid( const ickP2pContext_t *ictx )
{
  return ictx->deviceUuid;
}


/*=========================================================================*\
  Get web socket server port
\*=========================================================================*/
int ickP2pGetLwsPort( const ickP2pContext_t *ictx )
{
  return ictx->lwsPort;
}


/*=========================================================================*\
  Get current boot ID
\*=========================================================================*/
long ickP2pGetBootId( const ickP2pContext_t *ictx )
{
  return ictx->upnpBootId;
}


/*=========================================================================*\
  Get current config ID
\*=========================================================================*/
long ickP2pGetConfigId( const ickP2pContext_t *ictx )
{
  return ictx->upnpConfigId;
}

/*=========================================================================*\
  Get livetime of announcements
\*=========================================================================*/
int ickP2pGetLiveTime( const ickP2pContext_t *ictx )
{
  return ictx->liveTime;
}


/*=========================================================================*\
  Get hostname
\*=========================================================================*/
const char *ickP2pGetHostname( const ickP2pContext_t *ictx )
{
  return ictx->hostName;
}


/*=========================================================================*\
  Get interface name
\*=========================================================================*/
const char *ickP2pGetIf( const ickP2pContext_t *ictx )
{
  return ictx->interface;
}


/*=========================================================================*\
  Get listener port of a discovery handler
\*=========================================================================*/
int ickP2pGetUpnpPort( const ickP2pContext_t *ictx )
{
  return ictx->upnpPort;
}


#pragma mark - Managing offered services

/*=========================================================================*\
  Enable services on this device
\*=========================================================================*/
ickErrcode_t ickP2pAddService( ickP2pContext_t *ictx, ickP2pServicetype_t type )
{
  ickErrcode_t irc;

/*------------------------------------------------------------------------*\
    Mask out all services that are already registered
\*------------------------------------------------------------------------*/
  type &= ~(ictx->ickServices);
  if( !type )
    return ICKERR_SUCCESS;
  ictx->ickServices |= type;

/*------------------------------------------------------------------------*\
    Announce services via SSDP
\*------------------------------------------------------------------------*/
  irc = _ickSsdpAnnounceServices( ictx, type, SSDPMSGTYPE_ALIVE );
  if( !irc ) {
    logerr( "ickP2pDiscoveryAddService: could not announce service (%s)",
            ickStrError(irc) );
    // return irc;
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Disable services on this device
\*=========================================================================*/
ickErrcode_t ickP2pRemoveService( ickP2pContext_t *ictx, ickP2pServicetype_t type )
{
  ickErrcode_t irc;

/*------------------------------------------------------------------------*\
    Mask out all services that are not registered
\*------------------------------------------------------------------------*/
  type &= ictx->ickServices;
  if( !type )
    return ICKERR_SUCCESS;
  ictx->ickServices &= ~type;

/*------------------------------------------------------------------------*\
    Announce service termination via SSDP
\*------------------------------------------------------------------------*/
  irc = _ickSsdpAnnounceServices( ictx, type, SSDPMSGTYPE_BYEBYE );
  if( !irc ) {
    logerr( "ickP2pDiscoveryRemoveService: could not announce service (%s)",
            ickStrError(irc) );
    // return irc;
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
}


#pragma mark - Device list

/*=========================================================================*\
  Lock device list
\*=========================================================================*/
void _ickLibDeviceListLock( ickP2pContext_t *ictx )
{
  debug ( "_ickLibDeviceListLock (%p): locking...", ictx );
  pthread_mutex_lock( &ictx->deviceListMutex );
  debug ( "_ickLibDeviceListLock (%p): locked", ictx );
}


/*=========================================================================*\
  Unlock device list
\*=========================================================================*/
void _ickLibDeviceListUnlock( ickP2pContext_t *ictx )
{
  debug ( "_ickLibDeviceListUnlock (%p): unlocked", ictx );
  pthread_mutex_unlock( &ictx->deviceListMutex );
}


/*=========================================================================*\
  Add a device to a discovery handler
    This will NOT execute callbacks or add expiration handlers
    Caller should lock device list
\*=========================================================================*/
void _ickLibDeviceAdd( ickP2pContext_t *ictx, ickDevice_t *device )
{
  debug ( "_ickLibDeviceAdd (%p): adding new device \"%s\".",
          ictx, device->uuid );

/*------------------------------------------------------------------------*\
     Don't add twice...
\*------------------------------------------------------------------------*/
  if( device->ictx ) {
    debug ( "_ickLibDeviceAdd (%p): device \"%s\" is already member of %p.",
            ictx, device->uuid, device->ictx );
    return;
  }

/*------------------------------------------------------------------------*\
     Link device to list
\*------------------------------------------------------------------------*/
  device->next = ictx->deviceList;
  if( ictx->deviceList )
    ictx->deviceList->prev=device;
  ictx->deviceList = device;

/*------------------------------------------------------------------------*\
     Associate device with this context
\*------------------------------------------------------------------------*/
  device->ictx = ictx;

/*------------------------------------------------------------------------*\
     That's all
\*------------------------------------------------------------------------*/
  return;
}


/*=========================================================================*\
  Remove a device from a discovery handler
    This will NOT execute callbacks or delete expiration handlers
    Caller should lock device list
\*=========================================================================*/
void _ickLibDeviceRemove( ickP2pContext_t *ictx, ickDevice_t *device )
{
  debug ( "_ickLibDeviceRemove (%p): removing device \"%s\" at \"%s\".",
          ictx, device->uuid, device->location );

/*------------------------------------------------------------------------*\
     Be paranoid
\*------------------------------------------------------------------------*/
  if( !_ickLibDeviceFind(ictx,device->uuid) ) {
    debug ( "_ickLibDeviceRemove (%p): device \"%s\" not in list.",
            ictx, device->uuid );
    return;
  }

/*------------------------------------------------------------------------*\
    Unlink from device list
\*------------------------------------------------------------------------*/
  if( device->next )
    device->next->prev = device->prev;
  if( device->prev )
    device->prev->next = device->next;
  if( device==ictx->deviceList )
    ictx->deviceList = device->next;

/*------------------------------------------------------------------------*\
    Device is not associated
\*------------------------------------------------------------------------*/
  device->ictx = NULL;

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
  Find a device associated with a discovery handler
    caller should lock the device list of the handler
    nt and usn are the SSDP header files identifying the device
\*=========================================================================*/
ickDevice_t *_ickLibDeviceFind( ickP2pContext_t *ictx, const char *uuid )
{
  ickDevice_t *device;
  debug ( "_ickLibDeviceFind (%p): UUID=\"%s\".", ictx, uuid );

/*------------------------------------------------------------------------*\
    Find matching device entry
\*------------------------------------------------------------------------*/
  for( device=ictx->deviceList; device; device=device->next ) {
    if( !strcmp(device->uuid,uuid) )
      break;
  }

/*------------------------------------------------------------------------*\
    Thats it
\*------------------------------------------------------------------------*/
  debug ( "_ickLibDeviceFind (%p): result=%p", ictx, device );
  return device;
}


#pragma mark -- Other internal functions


/*=========================================================================*\
  Execute callbacks for a discovery handler
\*=========================================================================*/
void _ickLibExecDeviceCallback( ickP2pContext_t *ictx, const ickDevice_t *dev, ickP2pDeviceCommand_t change, ickP2pServicetype_t type )
{
  debug( "_ickLibExecDeviceCallback (%p): \"%s\" change=%d services=%d",
         ictx, dev->uuid, change, type );

/*------------------------------------------------------------------------*\
   Use friendly name as indicator for LWS initialization
\*------------------------------------------------------------------------*/
  if( !dev->friendlyName )
    return;

/*------------------------------------------------------------------------*\
   Execute callback (if any)
\*------------------------------------------------------------------------*/
  if( ictx->deviceCb )
    ictx->deviceCb( ictx, dev->uuid, change, type );

/*------------------------------------------------------------------------*\
   That's all
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
  Lock list of active HTTP clients for access or modification
\*=========================================================================*/
void _ickLibWGettersLock( ickP2pContext_t *ictx )
{
  debug ( "_ickLibWGettersLock (%p): locking...", ictx );
  pthread_mutex_lock( &ictx->wGettersMutex );
  debug ( "_ickLibWGettersLock (%p): locked", ictx );
}


/*=========================================================================*\
  unlock list of active HTTP clients for access or modification
\*=========================================================================*/
void _ickLibWGettersUnlock( ickP2pContext_t *ictx )
{
  debug ( "_ickLibWGettersUnlock (%p): unlocked", ictx );
  pthread_mutex_unlock( &ictx->wGettersMutex );
}


/*=========================================================================*\
  Add an active HTTP client
\*=========================================================================*/
void _ickLibWGettersAdd( ickP2pContext_t *ictx, ickWGetContext_t *wget )
{
  debug( "_ickLibWGettersAdd (%p): \"%s\"", ictx, _ickWGetUri(wget) );
  wget->next = ictx->wGetters;
  if( wget->next )
    wget->next->prev = wget;
  ictx->wGetters = wget;
}

/*=========================================================================*\
  Remove a HTTP client
\*=========================================================================*/
void _ickLibWGettersRemove( ickP2pContext_t *ictx, ickWGetContext_t *wget )
{
  debug( "_ickLibWGettersRemove (%p): \"%s\"", ictx, _ickWGetUri(wget) );
  if( wget->next )
    wget->next->prev = wget->prev;
  if( wget->prev )
    wget->prev->next = wget->next;
  else
    ictx->wGetters = wget->next;
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
