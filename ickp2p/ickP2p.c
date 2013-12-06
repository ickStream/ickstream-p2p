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
\************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <netinet/in.h>
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
  static char buffer[20];

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
  if( !*buffer ) {
    const char *gitver = ickP2pGitVersion();
#ifdef GIT_VERSION
    sprintf( buffer, "%d.%d %.7s", ICK_VERSION_MAJOR, ICK_VERSION_MINOR, gitver );
#else
    sprintf( buffer, "%d.%d %s", ICK_VERSION_MAJOR, ICK_VERSION_MINOR, gitver );
#endif
  }

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
  Create an ickstream contex
\*=========================================================================*/
ickP2pContext_t *ickP2pCreate( const char *deviceName, const char *deviceUuid,
                               const char *upnpFolder, int lifetime, int port,
                               ickP2pServicetype_t services, ickErrcode_t *error )
{
  ickP2pContext_t    *ictx;
  int                 rc;
  struct utsname      utsname;
  pthread_mutexattr_t attr;

  debug( "ickP2pCreate: \"%s\" (%s) services=0x%02x lt=%ds folder=\"%s\" port=%d",
         deviceName, deviceUuid, services, lifetime, upnpFolder, port );

/*------------------------------------------------------------------------*\
    (Re-)initialize random number generator
\*------------------------------------------------------------------------*/
  srandom( (unsigned int)time(NULL) );

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
  ictx->state              = ICKLIB_CREATED;
  ictx->tCreation          = _ickTimeNow();
  ictx->lwsConnectMatrixCb = ickP2pDefaultConnectMatrixCb;
  ictx->upnpListenerPort   = port>0?port:ICKSSDP_MCASTPORT;
  ictx->lifetime           = lifetime>0?lifetime:ICKSSDP_DEFAULTLIFETIME;
  ictx->ickServices        = services;
  ictx->upnpListenerSocket = -1;

/*------------------------------------------------------------------------*\
    Init mutexes and conditions
\*------------------------------------------------------------------------*/
  pthread_mutex_init( &ictx->mutex, NULL );
  pthread_cond_init( &ictx->condIsReady, NULL );
  pthread_mutex_init( &ictx->discoveryCbsMutex, NULL );
  pthread_mutex_init( &ictx->messageCbsMutex, NULL );
  pthread_mutex_init( &ictx->timersMutex, NULL );
  pthread_mutex_init( &ictx->wGettersMutex, NULL );
  pthread_mutexattr_init( &attr );
  pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_RECURSIVE );
  pthread_mutex_init( &ictx->deviceListMutex, &attr );
  pthread_mutex_init( &ictx->interfaceListMutex, NULL );

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
    Try to duplicate strings
\*------------------------------------------------------------------------*/
  ictx->deviceName = strdup( deviceName );
  ictx->deviceUuid = strdup( deviceUuid );
  if( upnpFolder )
    ictx->upnpFolder = strdup( upnpFolder );
  if( !ictx->deviceName || !ictx->deviceUuid || !ictx->osName ||
      (upnpFolder&&!ictx->upnpFolder)) {
    logerr( "ickP2pInit: out of memory." );
    _ickLibDestruct( ictx );
    if( error )
      *error = ICKERR_NOMEM;
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Create and init SSDP listener socket bound to all interfaces
\*------------------------------------------------------------------------*/
  ictx->upnpListenerSocket = _ickSsdpCreateListener( INADDR_ANY, ictx->upnpListenerPort );
  if( ictx->upnpListenerSocket<0 ){
    logerr( "ickP2pInit: could not create listener (%s).", strerror(errno) );
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
    That's it
\*------------------------------------------------------------------------*/
  debug( "ickLibCreate: created context %p", ictx );
  if( error )
    *error = ICKERR_SUCCESS;
  return ictx;
}


/*=========================================================================*\
  Start or resume contex main thread
\*=========================================================================*/
ickErrcode_t ickP2pResume( ickP2pContext_t *ictx )
{
  int              rc;
  ickErrcode_t     irc;
  struct timeval   now;
  struct timespec  abstime;

/*------------------------------------------------------------------------*\
    Check state
\*------------------------------------------------------------------------*/
  if( ictx->state!=ICKLIB_SUSPENDED &&  ictx->state!=ICKLIB_CREATED ) {
    logwarn( "ickP2pResume: wrong state (%d)", ictx->state );
    return ICKERR_WRONGSTATE;
  }
  ictx->tResume = _ickTimeNow();

/*------------------------------------------------------------------------*\
    Resuming? Set and signal new state
\*------------------------------------------------------------------------*/
  if( ictx->state==ICKLIB_SUSPENDED ) {
    debug( "ickP2pResume (%p): resuming", ictx );
    ictx->state = ICKLIB_RUNNING;
    return _ickMainThreadBreak( ictx, 'R' );
  }

/*------------------------------------------------------------------------*\
    First start up
\*------------------------------------------------------------------------*/
  debug( "ickP2pResume (%p): starting up", ictx );

/*------------------------------------------------------------------------*\
    Initialize bootID and configID with defaults if necessary
\*------------------------------------------------------------------------*/
  if( !ictx->upnpNextBootId ) {
    ictx->upnpNextBootId = (long)time(NULL);
    ictx->upnpBootId     = ictx->upnpNextBootId;
  }
  if( !ictx->upnpConfigId )
    ictx->upnpConfigId = (long)time(NULL);

/*------------------------------------------------------------------------*\
    Create thread for ickstream communication mainloop
\*------------------------------------------------------------------------*/
  rc = pthread_create( &ictx->thread, NULL, _ickMainThread, &ictx );
  if( rc ) {
    logerr( "ickP2pInit: Unable to start main thread: %s", strerror(rc) );
    return ICKERR_NOTHREAD;
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
    // ictx->state = ICKLIB_TERMINATING;
    ictx = NULL;
    return ICKERR_NOTHREAD;
  }

  // Handle main thread initialization errors
  if( ictx->error ) {
    irc = ictx->error;
    _ickLibDestruct( ictx );
    return irc ? irc : ICKERR_NOTHREAD;
  }

/*------------------------------------------------------------------------*\
    Start SSDP services - this will also init preregistered interfaces
\*------------------------------------------------------------------------*/
  irc = _ickSsdpNewDiscovery( ictx );
  if( irc ) {
    logerr( "ickP2pInit: could not start SSDP (%s).",
            ickStrError( irc ) );
    return irc;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  debug( "ickLibInit: created context %p", ictx );
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Suspend network IO
\*=========================================================================*/
ickErrcode_t ickP2pSuspend( ickP2pContext_t *ictx )
{
  debug( "ickP2pSuspend (%p):", ictx );

/*------------------------------------------------------------------------*\
    Need to be running for this
\*------------------------------------------------------------------------*/
  if( ictx->state!=ICKLIB_RUNNING ) {
    logwarn( "ickP2pSuspend: wrong state (%d)", ictx->state );
    return ICKERR_WRONGSTATE;
  }

/*------------------------------------------------------------------------*\
    Set and signal new state, that's all
\*------------------------------------------------------------------------*/
  ictx->state = ICKLIB_SUSPENDED;
  return _ickMainThreadBreak( ictx, 'S' );
}


/*=========================================================================*\
  Shut down ickstream library
\*=========================================================================*/
ickErrcode_t ickP2pEnd( ickP2pContext_t *ictx, ickP2pEndCb_t callback )
{
  int rc;

/*------------------------------------------------------------------------*\
    Not yet running?
\*------------------------------------------------------------------------*/
  if( ictx->state==ICKLIB_CREATED ) {
    debug( "ickP2pEnd (%p): only deleting (not yet running)", ictx );

    // execute callback (if any)
    if( callback )
      callback( ictx );

    // destroy descriptor
    _ickLibDestruct( ictx );

    // That's it
    return ICKERR_SUCCESS;
  }

/*------------------------------------------------------------------------*\
    Store callback for asynchronous shutdown and request thread termination
\*------------------------------------------------------------------------*/
  debug( "ickP2pEnd (%p): %s", ictx, callback?"asynchronous":"synchronous" );
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
    this only frees resources, does not terminate or join the main thread
\*=========================================================================*/
void _ickLibDestruct( ickP2pContext_t *ictx )
{
  struct _cblist *walkCb, *nextCb;
  ickInterface_t *walkIf, *nextIf;
  int             i;
  debug( "_ickP2pDestruct: %p", ictx );

/*------------------------------------------------------------------------*\
    Be defensive
\*------------------------------------------------------------------------*/
  if( !ictx ) {
    logwarn( "_ickP2pDestruct: cannot free nil pointer" );
    return;
  }

/*------------------------------------------------------------------------*\
    Close SSDP listener socket (if any)
\*------------------------------------------------------------------------*/
  if( ictx->upnpListenerSocket>=0 )
    close( ictx->upnpListenerSocket );

// lwsPolllist's lifecycle is handled by main thread

/*------------------------------------------------------------------------*\
    Free list of interfaces
\*------------------------------------------------------------------------*/
  for( walkIf=ictx->interfaces; walkIf; walkIf=nextIf ) {
    nextIf = walkIf->next;
    _ickLibInterfaceDestruct( walkIf );
  }
  ictx->interfaces = NULL;

/*------------------------------------------------------------------------*\
    Free call back lists
\*------------------------------------------------------------------------*/
  for( walkCb=ictx->discoveryCbs; walkCb; walkCb=nextCb ) {
    nextCb = walkCb->next;
    Sfree( walkCb );
  }
  ictx->discoveryCbs = NULL;
  for( walkCb=ictx->messageCbs; walkCb; walkCb=nextCb ) {
    nextCb = walkCb->next;
    Sfree( walkCb );
  }
  ictx->messageCbs = NULL;

/*------------------------------------------------------------------------*\
    Free strong string references
\*------------------------------------------------------------------------*/
  Sfree( ictx->osName );
  Sfree( ictx->deviceName );
  Sfree( ictx->deviceUuid );
  Sfree( ictx->upnpFolder );

/*------------------------------------------------------------------------*\
    Delete mutex and condition
\*------------------------------------------------------------------------*/
  pthread_mutex_destroy( &ictx->mutex );
  pthread_cond_destroy( &ictx->condIsReady );
  pthread_mutex_destroy( &ictx->discoveryCbsMutex );
  pthread_mutex_destroy( &ictx->messageCbsMutex );
  pthread_mutex_destroy( &ictx->timersMutex );
  pthread_mutex_destroy( &ictx->wGettersMutex );
  pthread_mutex_destroy( &ictx->deviceListMutex );
  pthread_mutex_destroy( &ictx->interfaceListMutex );

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


#pragma mark -- Call backs


/*=========================================================================*\
  Add a discovery callback
\*=========================================================================*/
ickErrcode_t ickP2pRegisterDiscoveryCallback( ickP2pContext_t *ictx, ickP2pDiscoveryCb_t callback )
{
  struct _cblist *new;
  debug( "ickP2pRegisterDiscoveryCallback (%p): %p", ictx, callback );

/*------------------------------------------------------------------------*\
    Lock list of callbacks
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &ictx->discoveryCbsMutex );

/*------------------------------------------------------------------------*\
    Avoid double subscriptions
\*------------------------------------------------------------------------*/
  for( new=ictx->discoveryCbs; new; new=new->next ) {
    if( new->callback!=callback )
      continue;
    logwarn( "ickP2pRegisterDiscoveryCallback: callback already registered." );
    pthread_mutex_unlock( &ictx->discoveryCbsMutex );
    return ICKERR_SUCCESS;
  }

/*------------------------------------------------------------------------*\
    Allocate and init new list element
\*------------------------------------------------------------------------*/
  new = calloc( 1, sizeof(struct _cblist) );
  if( !new ) {
    logerr( "ickP2pRegisterDiscoveryCallback: out of memory" );
    pthread_mutex_unlock( &ictx->discoveryCbsMutex );
    return ICKERR_NOMEM;
  }
  new->callback = callback;
  if( ictx->state!=ICKLIB_CREATED )
    new->isNew = 1;

/*------------------------------------------------------------------------*\
    Add to linked list
\*------------------------------------------------------------------------*/
  new->next = ictx->discoveryCbs;
  if( new->next )
    new->next->prev = new;
  ictx->discoveryCbs = new;

/*------------------------------------------------------------------------*\
    Unlock list, signal main thread, that's all
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &ictx->discoveryCbsMutex );
  _ickMainThreadBreak( ictx, 'l' );
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Remove a discovery callback
\*=========================================================================*/
ickErrcode_t ickP2pRemoveDiscoveryCallback( ickP2pContext_t *ictx, ickP2pDiscoveryCb_t callback )
{
  struct _cblist *walk;
  debug( "ickP2pRemoveDiscoveryCallback (%p): %p", ictx, callback );

/*------------------------------------------------------------------------*\
    Lock list of callbacks
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &ictx->discoveryCbsMutex );

/*------------------------------------------------------------------------*\
    Find entry, check if member
\*------------------------------------------------------------------------*/
  for( walk=ictx->discoveryCbs; walk; walk=walk->next )
    if( walk->callback==callback )
      break;
  if( !walk ) {
    logerr( "ickP2pRemoveDiscoveryCallback: callback is not registered." );
    pthread_mutex_unlock( &ictx->discoveryCbsMutex );
    return ICKERR_NOMEMBER;
  }

/*------------------------------------------------------------------------*\
    Unlink and destruct
\*------------------------------------------------------------------------*/
  if( walk->next )
    walk->next->prev = walk->prev;
  if( walk->prev )
    walk->prev->next = walk->next;
  else
    ictx->discoveryCbs = walk->next;
  Sfree( walk );

/*------------------------------------------------------------------------*\
    Unlock list, that's all
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &ictx->discoveryCbsMutex );
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Add a messaging callback
\*=========================================================================*/
ickErrcode_t ickP2pRegisterMessageCallback( ickP2pContext_t *ictx, ickP2pMessageCb_t callback )
{
  struct _cblist *new;
  debug( "ickP2pRegisterMessageCallback (%p): %p", ictx, callback );

/*------------------------------------------------------------------------*\
    Lock list of callbacks
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &ictx->messageCbsMutex );

/*------------------------------------------------------------------------*\
    Avoid double subscriptions
\*------------------------------------------------------------------------*/
  for( new=ictx->messageCbs; new; new=new->next ) {
    if( new->callback!=callback )
      continue;
    logwarn( "ickP2pRegisterMessageCallback: callback already registered." );
    pthread_mutex_unlock( &ictx->messageCbsMutex );
    return ICKERR_SUCCESS;
  }

/*------------------------------------------------------------------------*\
    Allocate and init new list element
\*------------------------------------------------------------------------*/
  new = calloc( 1, sizeof(struct _cblist) );
  if( !new ) {
    logerr( "ickP2pRegisterMessageCallback: out of memory" );
    pthread_mutex_unlock( &ictx->messageCbsMutex );
    return ICKERR_NOMEM;
  }
  new->callback = callback;

/*------------------------------------------------------------------------*\
    Add to linked list
\*------------------------------------------------------------------------*/
  new->next = ictx->messageCbs;
  if( new->next )
    new->next->prev = new;
  ictx->messageCbs = new;

/*------------------------------------------------------------------------*\
    Unlock list, that's all
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &ictx->messageCbsMutex );
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Remove a messaging callback
\*=========================================================================*/
ickErrcode_t ickP2pRemoveMessageCallback( ickP2pContext_t *ictx, ickP2pMessageCb_t callback )
{
  struct _cblist *walk;
  debug( "ickP2pRemoveMessageCallback (%p): %p", ictx, callback );

/*------------------------------------------------------------------------*\
    Lock list of callbacks
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &ictx->messageCbsMutex );

/*------------------------------------------------------------------------*\
    Find entry, check if member
\*------------------------------------------------------------------------*/
  for( walk=ictx->messageCbs; walk; walk=walk->next )
    if( walk->callback==callback )
      break;
  if( !walk ) {
    logerr( "ickP2pRemoveMessageCallback: callback is not registered." );
    pthread_mutex_unlock( &ictx->messageCbsMutex );
    return ICKERR_NOMEMBER;
  }

/*------------------------------------------------------------------------*\
    Unlink and destruct
\*------------------------------------------------------------------------*/
  if( walk->next )
    walk->next->prev = walk->prev;
  if( walk->prev )
    walk->prev->next = walk->next;
  else
    ictx->messageCbs = walk->next;
  Sfree( walk );

/*------------------------------------------------------------------------*\
    Unlock list, that's all
\*------------------------------------------------------------------------*/
  pthread_mutex_unlock( &ictx->messageCbsMutex );
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Execute a discovery callback
\*=========================================================================*/
void _ickLibExecDiscoveryCallback( ickP2pContext_t *ictx, const ickDevice_t *dev, ickP2pDeviceState_t change, ickP2pServicetype_t type )
{
  struct _cblist *walk;
  debug( "_ickLibExecDiscoveryCallback (%p): \"%s\" change=%d (%s) services=%d",
         ictx, dev->uuid, change, ickLibDeviceState2Str(change), type );

/*------------------------------------------------------------------------*\
   Lock list mutex and execute all registered callbacks
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &ictx->discoveryCbsMutex );
  for( walk=ictx->discoveryCbs; walk; walk=walk->next )
    ((ickP2pDiscoveryCb_t)walk->callback)( ictx, dev->uuid, change, type );
  pthread_mutex_unlock( &ictx->discoveryCbsMutex );
}


/*=========================================================================*\
  Convert device state to a string
\*=========================================================================*/
const char *ickLibDeviceState2Str( ickP2pDeviceState_t change )
{
  switch( change ) {
    case ICKP2P_INITIALIZED:  return "initialized";
    case ICKP2P_CONNECTED:    return "connected";
    case ICKP2P_DISCONNECTED: return "disconnected";
    case ICKP2P_DISCOVERED:   return "discovered";
    case ICKP2P_BYEBYE:       return "byebye";
    case ICKP2P_EXPIRED:      return "expired";
    case ICKP2P_TERMINATE:    return "terminating";
    case ICKP2P_INVENTORY:    return "inventory";
    case ICKP2P_ERROR:        return "error";
  }

  return "Invalid device state";
}


#pragma mark -- Interfaces


/*=========================================================================*\
    Add an interface
\*=========================================================================*/
ickErrcode_t ickP2pAddInterface( ickP2pContext_t *ictx, const char *ifname, const char *hostname )
{
  int              sd;
  int              rc;
  ickErrcode_t     irc;
  in_addr_t        ifaddr;
  in_addr_t        ifmask;
  char            *name = NULL;
  ickInterface_t  *interface;
  char             _buf1[64], _buf2[64];

  debug( "ickP2pAddInterface (%p): \"%s\"", ictx, ifname );

/*------------------------------------------------------------------------*\
    Find interface configuration
\*------------------------------------------------------------------------*/
  irc = _ickIpGetIfAddr( ifname, &ifaddr, &ifmask, &name );
  if( irc ) {
    logwarn( "ickP2pAddInterface (%s): not found",
             ifname );
    return irc;
  }

  inet_ntop( AF_INET, &ifaddr, _buf1, sizeof(_buf1) );
  if( !hostname )
    hostname = _buf1;
  inet_ntop( AF_INET, &ifmask, _buf2, sizeof(_buf2) );
  debug( "ickP2pAddInterface (%p): %s -> %s/%s (%s)",
         ictx, ifname, _buf1, _buf2, name );

/*------------------------------------------------------------------------*\
    Create non blocking SSDP communications socket
\*------------------------------------------------------------------------*/
  sd = socket( PF_INET, SOCK_DGRAM, 0 );
  if( sd<0 ) {
    logerr( "ickP2pAddInterface: could not create socket (%s).", strerror(errno) );
    return ICKERR_NOSOCKET;
  }
  rc = fcntl( sd, F_GETFL );
  if( rc>=0 )
    rc = fcntl( sd, F_SETFL, rc|O_NONBLOCK );
  if( rc<0 )
    logwarn( "ickP2pAddInterface: could not set O_NONBLOCK on socket (%s).",
             strerror(errno) );

/*------------------------------------------------------------------------*\
    Bind socket to interface, any port
\*------------------------------------------------------------------------*/
  rc = _ickIpBind( sd, ifaddr, 0 );
  if( rc<0 ) {
    close( sd );
    Sfree( name );
    logerr( "ickP2pAddInterface: could not bind socket (%s)",
            strerror(rc) );
    return ICKERR_GENERIC;
  }

/*------------------------------------------------------------------------*\
  Create and init interface descriptor
\*------------------------------------------------------------------------*/
  interface = calloc( 1, sizeof(ickInterface_t) );
  if( !interface ) {
    close( sd );
    Sfree( name );
    logerr( "ickP2pAddInterface: out of memory" );
    return ICKERR_NOMEM;
  }
  interface->name          = name;
  interface->addr          = ifaddr;
  interface->netmask       = ifmask;
  interface->upnpComSocket = sd;
  interface->upnpComPort   = _ickIpGetSocketPort( sd );
  interface->hostname      = strdup( hostname );
  if( !interface->hostname ) {
    close( sd );
    Sfree( name );
    Sfree( interface );
    logerr( "ickP2pAddInterface: out of memory" );
    return ICKERR_NOMEM;
  }

/*------------------------------------------------------------------------*\
  Add socket to multicast group on target interface
  fixme: move this to _ssdpNewInterface() and _ickSsdpNewDiscovery() ?
\*------------------------------------------------------------------------*/
  rc = _ickIpAddMcast( ictx->upnpListenerSocket, ifaddr, inet_addr(ICKSSDP_MCASTADDR) );
  if( rc<0 ) {
    close( sd );
    Sfree( name );
    Sfree( interface->hostname );
    Sfree( interface );
    logerr( "ickP2pAddInterface (%s): could not add mcast membership for listener socket (%s)",
             ifname, strerror(rc) );
    return ICKERR_GENERIC;
  }

/*------------------------------------------------------------------------*\
  Link to context
\*------------------------------------------------------------------------*/
  _ickLibInterfaceListLock( ictx );
  interface->next  = ictx->interfaces;
  ictx->interfaces = interface;
  _ickLibInterfaceListUnlock( ictx );

/*------------------------------------------------------------------------*\
  If running, break main loop to announce new interface
\*------------------------------------------------------------------------*/
  if( ictx->state==ICKLIB_RUNNING )
    _ickMainThreadBreak( ictx, 'i' );

/*------------------------------------------------------------------------*\
  That's all
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
    Remove an interface
      ictx      - an ickstream context
      ifname    - the name the interface was registered under
      proactive - if !=0, a byebye message will be sent on interface
    returns an ickstream error code
\*=========================================================================*/
ickErrcode_t ickP2pDeleteInterface( ickP2pContext_t *ictx, const char *ifname, int proactive )
{
  ickInterface_t *interface;
  debug ( "ickP2pDeleteInterface (%p): \"%s\" (%s)", ictx, ifname, proactive?"proactive":"ex post" );

/*------------------------------------------------------------------------*\
  Lock interface list and find interface
\*------------------------------------------------------------------------*/
  _ickLibInterfaceListLock( ictx );
  interface = _ickLibInterfaceByName( ictx, ifname );
  if( !interface ) {
    debug ( "ickP2pDeleteInterface (%p): interface \"%s\" not registered", ictx, ifname );
    _ickLibInterfaceListUnlock( ictx );
    return ICKERR_NOINTERFACE;
  }

/*------------------------------------------------------------------------*\
  Mark for deletion
\*------------------------------------------------------------------------*/
  interface->shutdownMode = proactive ? ICKP2P_INTSHUTDOWN_PROACTIVE :
                                        ICKP2P_INTSHUTDOWN_EXPOST;

/*------------------------------------------------------------------------*\
  If running, break main loop to announce new interface
\*------------------------------------------------------------------------*/
  if( ictx->state==ICKLIB_RUNNING )
    _ickMainThreadBreak( ictx, 'i' );

/*------------------------------------------------------------------------*\
  Unlock interface list, that's all
\*------------------------------------------------------------------------*/
  _ickLibInterfaceListUnlock( ictx );
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Lock interface list
\*=========================================================================*/
void _ickLibInterfaceListLock( ickP2pContext_t *ictx )
{
  debug ( "_ickLibInterfaceListLock (%p): locking...", ictx );
  pthread_mutex_lock( &ictx->interfaceListMutex );
  debug ( "_ickLibInterfaceListLock (%p): locked", ictx );
}


/*=========================================================================*\
  Unlock interface list
\*=========================================================================*/
void _ickLibInterfaceListUnlock( ickP2pContext_t *ictx )
{
  debug ( "_ickLibInterfaceListUnlock (%p): unlocked", ictx );
  pthread_mutex_unlock( &ictx->interfaceListMutex );
}


/*=========================================================================*\
    Unlink interface from list
      This will not delete the descriptor!
      Caller should lock the interface list
\*=========================================================================*/
ickErrcode_t _ickLibInterfaceUnlink( ickP2pContext_t *ictx, ickInterface_t *interface )
{
  ickInterface_t *walk;
  debug( "_ickLibInterfaceUnlink (%p): \"%s\"", ictx, interface->name );

/*------------------------------------------------------------------------*\
    Check if interface is in list
\*------------------------------------------------------------------------*/
  for( walk=ictx->interfaces; walk; walk=walk->next )
    if( walk==interface )
      break;
  if( interface ) {
    logerr( "_ickLibInterfaceUnlink: interface \"%s\" not part of list",
             interface->name );
    return ICKERR_NOINTERFACE;
  }

/*------------------------------------------------------------------------*\
    Unlink from list
\*------------------------------------------------------------------------*/
  if( interface->next )
    interface->next->prev = interface->prev;
  if( interface->prev )
    interface->prev->next = interface->next;
  if( interface==ictx->interfaces )
    ictx->interfaces = interface->next;

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
    Free an interface descriptor
      This will not unlink the descriptor form the interface list
\*=========================================================================*/
void _ickLibInterfaceDestruct( ickInterface_t *interface )
{
  debug( "_ickLibInterfaceDestruct: \"%s\"", interface->name );

/*------------------------------------------------------------------------*\
    Close SSDP socket (if any)
\*------------------------------------------------------------------------*/
  if( interface->upnpComSocket>=0 )
    close( interface->upnpComSocket );

/*------------------------------------------------------------------------*\
    Free strings and descriptor
\*------------------------------------------------------------------------*/
  Sfree( interface->name );
  Sfree( interface->hostname );
  Sfree( interface );
}


/*=========================================================================*\
    Get interface by name
      ictx   - context
      ifname - the name the interface was registered with
      Caller should lock interface list
      returns first matching interface or NULL, if not found
\*=========================================================================*/
ickInterface_t *_ickLibInterfaceByName( const ickP2pContext_t *ictx, const char *ifname )
{
  ickInterface_t *interface;
  debug( "_ickLibInterfaceByName (%p): \"%s\"", ictx, ifname );

/*------------------------------------------------------------------------*\
    Loop over interfaces
\*------------------------------------------------------------------------*/
  for( interface=ictx->interfaces; interface; interface=interface->next )
    if( !strcmp(interface->name,ifname) )
      break;

/*------------------------------------------------------------------------*\
    Return result
\*------------------------------------------------------------------------*/
  debug( "_ickLibInterfaceByName (%p): found %p", ictx, interface );
  return interface;
}


/*=========================================================================*\
    Get interface for an address
      ictx - context
      addr - address (network byte order)
      Caller should lock interface list
      returns first matching interface or NULL, if not found
\*=========================================================================*/
ickInterface_t *_ickLibInterfaceForAddr( const ickP2pContext_t *ictx, in_addr_t addr )
{
  ickInterface_t *interface;

#ifdef ICK_DEBUG
  char _buf[64];
  inet_ntop( AF_INET, &addr, _buf, sizeof(_buf) );
  debug( "_ickLibInterfaceForAddr (%p): %s", ictx, _buf );
#endif

/*------------------------------------------------------------------------*\
    Loop over all interfaces
\*------------------------------------------------------------------------*/
  for( interface=ictx->interfaces; interface; interface=interface->next ) {

    // Check for match of subnets
    if( (interface->addr&interface->netmask) == (addr&interface->netmask) ) {
      debug( "_ickLibInterfaceForAddr (%p): found \"%s\"", ictx, interface->name );
      break;
    }

  }

/*------------------------------------------------------------------------*\
    Return result
\*------------------------------------------------------------------------*/
  return interface;
}


/*=========================================================================*\
    Get interface for a hostname
      addr - pointer to resolved address of hostname (might be NULL)
      *addr will be in network byte order
      Caller should lock interface list
      returns first interface or NULL, if not found
\*=========================================================================*/
ickInterface_t *_ickLibInterfaceForHost( const ickP2pContext_t *ictx, const char *hostname, in_addr_t *addr )
{
  ickInterface_t *interface;
  struct hostent *hentries;
  int             i;

  debug( "_ickLibInterfaceForHost (%p): \"%s\"", ictx, hostname );

/*------------------------------------------------------------------------*\
    Try to get address list for hostname
\*------------------------------------------------------------------------*/
  hentries = gethostbyname( hostname );
  if( !hentries ) {
    logwarn( "_ickLibInterfaceForHost: \"%s\" not found (%s)",
             hostname, hstrerror(h_errno) );
    return NULL;
  }
  debug( "_ickLibInterfaceForHost (%p): \"%s\" -> \"%s\"",
         ictx, hostname, hentries->h_name );

/*------------------------------------------------------------------------*\
    Loop over all addresses and interfaces
\*------------------------------------------------------------------------*/
  for( i=0; hentries->h_addr_list[i]; i++ ) {
    in_addr_t raddr = ((struct in_addr*)(hentries->h_addr_list[i]))->s_addr;
    for( interface=ictx->interfaces; interface; interface=interface->next ) {

      // Check for match of addresses respecting subnets
      if( (interface->addr&interface->netmask) == (raddr&interface->netmask) ) {
        debug( "_ickLibInterfaceForHost (%p): found \"%s\"", ictx, interface->name );
        if( addr )
          *addr = raddr;
        return interface;
      }
    }  // next interface
  }  // next host entry

/*------------------------------------------------------------------------*\
    Not found
\*------------------------------------------------------------------------*/
  return NULL;
}


#pragma mark -- Setters and getters


/*=========================================================================*\
    Set upnp loopback behaviour, i.e. if a context sees itself as a device
\*=========================================================================*/
ickErrcode_t ickP2pUpnpLoopback( ickP2pContext_t *ictx, int enable )
{
  debug( "ickP2pUpnpLoopback (%p): %s", ictx, enable?"on":"off" );

/*------------------------------------------------------------------------*\
    Set flag in context
\*------------------------------------------------------------------------*/
  ictx->upnpLoopback = enable;

/*------------------------------------------------------------------------*\
  That's all
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
}


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
  Convert state to a string
\*=========================================================================*/
const char *ickLibState2Str( ickP2pLibState_t state )
{
  switch( state ) {
    case ICKLIB_CREATED:     return "created";
    case ICKLIB_RUNNING:     return "running";
    case ICKLIB_SUSPENDED:   return "suspended";
    case ICKLIB_TERMINATING: return "terminating";
  }

  return "Invalid state";
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
  Get lifetime of announcements
\*=========================================================================*/
int ickP2pGetLifetime( const ickP2pContext_t *ictx )
{
  return ictx->lifetime;
}


/*=========================================================================*\
  Get ssdp listener port of a context
\*=========================================================================*/
int ickP2pGetUpnpPort( const ickP2pContext_t *ictx )
{
  return ictx->upnpListenerPort;
}


/*=========================================================================*\
  Get loopbback mode of a context
\*=========================================================================*/
int ickP2pGetUpnpLoopback( const ickP2pContext_t *ictx )
{
  return ictx->upnpLoopback;
}


/*=========================================================================*\
  Get services of a context
\*=========================================================================*/
ickP2pServicetype_t ickP2pGetServices( const ickP2pContext_t *ictx )
{
  return ictx->ickServices;
}


#pragma mark -- Get device features (allowed only in callbacks!)


/*=========================================================================*\
  Get device name
    caller must copy value before exit of callback
    returns NULL on error (uuid unknown)
\*=========================================================================*/
char *ickP2pGetDeviceName( const ickP2pContext_t *ictx, const char *uuid )
{
  ickDevice_t *device = _ickLibDeviceFindByUuid( ictx, uuid );
  if( !device )
    return NULL;
  return device->friendlyName;
}


/*=========================================================================*\
  Device service types
    returns ICKP2P_SERVICE_NONE on error (uuid unknown)
\*=========================================================================*/
ickP2pServicetype_t ickP2pGetDeviceServices( const ickP2pContext_t *ictx, const char *uuid )
{
  ickDevice_t *device = _ickLibDeviceFindByUuid( ictx, uuid );
  if( !device )
    return ICKP2P_SERVICE_NONE;
  return device->services;
}


/*=========================================================================*\
  Get device location (the URI of the unpn xml descriptor)
    caller must copy value before exit of callback
    returns NULL on error (uuid unknown) or if not yet known
\*=========================================================================*/
char *ickP2pGetDeviceLocation( const ickP2pContext_t *ictx, const char *uuid )
{
  ickDevice_t *device = _ickLibDeviceFindByUuid( ictx, uuid );
  if( !device )
    return NULL;
  return device->location;
}


/*=========================================================================*\
  Get device lifetime interval
    returns -1 on error (uuid unknown) or 0 if not yet known
\*=========================================================================*/
int ickP2pGetDeviceLifetime( const ickP2pContext_t *ictx, const char *uuid )
{
  ickDevice_t *device = _ickLibDeviceFindByUuid( ictx, uuid );
  if( !device )
    return -1;
  return device->lifetime;
}


/*=========================================================================*\
  Get device unpn service version
    returns -1 on error (uuid unknown) or 0 if not yet known
\*=========================================================================*/
int ickP2pGetDeviceUpnpVersion( const ickP2pContext_t *ictx, const char *uuid )
{
  ickDevice_t *device = _ickLibDeviceFindByUuid( ictx, uuid );
  if( !device )
    return -1;
  return device->ickUpnpVersion;
}


/*=========================================================================*\
  Get device connection matrix entry
    returns -1 on error (uuid unknown)
\*=========================================================================*/
int ickP2pGetDeviceConnect( const ickP2pContext_t *ictx, const char *uuid )
{
  ickDevice_t *device = _ickLibDeviceFindByUuid( ictx, uuid );
  if( !device )
    return -1;
  return device->doConnect;
}


/*=========================================================================*\
  Get number of pending messages (output queue length) to a device
    returns -1 on error (uuid unknown)
\*=========================================================================*/
int ickP2pGetDeviceMessagesPending( const ickP2pContext_t *ictx, const char *uuid )
{
  ickDevice_t *device = _ickLibDeviceFindByUuid( ictx, uuid );
  if( !device )
    return -1;
  return _ickDevicePendingOutMessages( device );
}


/*=========================================================================*\
  Get number of messages sent to a device
    returns -1 on error (uuid unknown)
\*=========================================================================*/
int ickP2pGetDeviceMessagesSent( const ickP2pContext_t *ictx, const char *uuid )
{
  ickDevice_t *device = _ickLibDeviceFindByUuid( ictx, uuid );
  if( !device )
    return -1;
  return device->nTx;
}


/*=========================================================================*\
  Get number of messages received from a device
    returns -1 on error (uuid unknown)
\*=========================================================================*/
int ickP2pGetDeviceMessagesReceived( const ickP2pContext_t *ictx, const char *uuid )
{
  ickDevice_t *device = _ickLibDeviceFindByUuid( ictx, uuid );
  if( !device )
    return -1;
  return device->nRx;
}


/*=========================================================================*\
  Get creation time of a device (time_t plus fractional seconds)
    returns -1.0 on error (uuid unknown)
\*=========================================================================*/
double ickP2pGetDeviceTimeCreated( const ickP2pContext_t *ictx, const char *uuid )
{
  ickDevice_t *device = _ickLibDeviceFindByUuid( ictx, uuid );
  if( !device )
    return -1.0;
  return device->tCreation;
}


/*=========================================================================*\
  Get connection time of a device (time_t plus fractional seconds)
    returns -1.0 on error (uuid unknown) or 0 if not (yet) connected
\*=========================================================================*/
double ickP2pGetDeviceTimeConnected( const ickP2pContext_t *ictx, const char *uuid )
{
  ickDevice_t *device = _ickLibDeviceFindByUuid( ictx, uuid );
  if( !device )
    return -1.0;
  return device->tConnect;
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
  if( !_ickLibDeviceFindByUuid(ictx,device->uuid) ) {
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
    Reset loop back device?
\*------------------------------------------------------------------------*/
  if( device==ictx->deviceLoopback )
    ictx->deviceLoopback = NULL;

/*------------------------------------------------------------------------*\
    Device is not associated
\*------------------------------------------------------------------------*/
  device->ictx = NULL;

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
  Find a device associated with a ickstream context by uuid
    caller should lock the device list of the handler
    nt and usn are the SSDP header files identifying the device
\*=========================================================================*/
ickDevice_t *_ickLibDeviceFindByUuid( const ickP2pContext_t *ictx, const char *uuid )
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


/*=========================================================================*\
  Find a device associated with a ickstream context by web socket interface
    caller should lock the device list of the handler
    nt and usn are the SSDP header files identifying the device
\*=========================================================================*/
ickDevice_t *_ickLibDeviceFindByWsi( const ickP2pContext_t *ictx,struct libwebsocket *wsi )
{
  ickDevice_t *device;
  debug ( "_ickLibDeviceFindByWsi (%p): wsi=%p.", ictx, wsi );

/*------------------------------------------------------------------------*\
    Find matching device entry
\*------------------------------------------------------------------------*/
  for( device=ictx->deviceList; device; device=device->next ) {
    if( device->wsi==wsi )
      break;
  }

/*------------------------------------------------------------------------*\
    Thats it
\*------------------------------------------------------------------------*/
  debug ( "_ickLibDeviceFindByWsi (%p): result=%p", ictx, device );
  return device;
}


#pragma mark -- Other internal functions


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


/*========================================================================*\
   Get time including fractional seconds
\*========================================================================*/
double _ickTimeNow( void )
{
  struct timeval tv;
  gettimeofday( &tv, NULL );
  return tv.tv_sec+tv.tv_usec*1E-6;
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
