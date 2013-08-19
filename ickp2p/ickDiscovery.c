/*$*********************************************************************\

Source File     : ickDiscovery.c

Description     : Internal include file for upnp discovery functions

Comments        : -

Called by       : API wrapper

Calls           : Internal functions

Date            : 14.08.2013

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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
// #include <sys/ioctl.h>
#include <sys/socket.h>
// #include <netinet/in.h>
#include <arpa/inet.h>
// #include <net/if.h>
#include <errno.h>


#include "openssdpsocket.h"

#include "ickP2p.h"
#include "ickP2pInternal.h"
#include "ickIpTools.h"
#include "logutils.h"
#include "ickSSDP.h"
#include "ickDiscovery.h"


/*=========================================================================*\
  Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Private definitions and symbols
\*=========================================================================*/


// Services stored for answering to M-SEARCH
// For ickStream, devices are registered like services, but if the service is "server" more than one service per device may have to be registered
struct _upnp_service {
  upnp_service_t  *prev;
  upnp_service_t  *next;
  int              adinterval;
  char            *st;        // strong, service type
  char            *usn;       // strong, unique identifier
  char            *server;    // strong, server string
  char            *location;  // strong, URL
};


ickDiscovery_t  *_ickDiscoveryHandlerList;
pthread_mutex_t  _ickDiscoveryHandlerListMutex = PTHREAD_MUTEX_INITIALIZER;

// #define LOCALHOST_ADDR "127.0.0.1"

/*=========================================================================*\
  Private prototypes
\*=========================================================================*/
static void _ickDiscoveryDestruct( ickDiscovery_t *dh );



/*=========================================================================*\
  Create a new instance of a discovery handler
\*=========================================================================*/
ickDiscovery_t *ickP2pDiscoveryInit( const char *interface, int port, const char *upnpFolder, ickDiscoveryEndCb_t callback, ickErrcode_t *error )
{
  ickDiscovery_t *dh;
  int             rc, opt;
  in_addr_t       ifaddr;
  char            buffer[64];
  ickErrcode_t    irc;
  debug( "ickP2pDiscoveryInit: if=\"%s:%d\" folder=\"%s\" cb=%p",
         interface, port, upnpFolder, callback );

/*------------------------------------------------------------------------*\
    Need to be initialized
\*------------------------------------------------------------------------*/
  if( ickP2pGetState()==ICKLIB_UNINITIALIZED ) {
    logwarn( "ickP2pDiscoveryInit: not initialized." );
    if( error )
      *error = ICKERR_UNINITIALIZED;
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Create and initialize descriptor
\*------------------------------------------------------------------------*/
  dh = calloc( 1, sizeof(ickDiscovery_t) );
  if( !dh ) {
    logerr( "ickP2pDiscoveryInit: out of memory." );
    if( error )
      *error = ICKERR_NOMEM;
    return NULL;
  }

  // Init mutex
  pthread_mutex_init( &dh->mutex, NULL );
  pthread_mutex_init( &dh->deviceListMutex, NULL );

  // Some defaults
  dh->socket       = -1;
  dh->port         = port;
  dh->services     = ICKP2P_SERVICE_GENERIC;
  dh->ttl          = _ickLib->liveTime/3;
  if( dh->ttl<=0 )
    dh->ttl = 1;

  // Try to init/duplicate strings
  dh->interface    = strdup( interface );
  dh->upnpFolder   = strdup( upnpFolder );
  if( !dh->interface | !dh->upnpFolder ) {
    logerr( "ickP2pDiscoveryInit: out of memory." );
    _ickDiscoveryDestruct( dh );
    if( error )
      *error = ICKERR_NOMEM;
    return NULL;
  }

  // Store exit callback
  dh->exitCallback = callback;

  // Add default callback
  /*
  if( _ickDiscoveryAddRecvCallback(discovery,_ickRegReceiveNotify) ) {
    _ickDiscoveryDestruct( discovery );
    return NULL;
  }
  */

/*------------------------------------------------------------------------*\
    Get IP string from interface
\*------------------------------------------------------------------------*/
  ifaddr   = _ickIpGetIfAddr( interface );
  if( ifaddr==INADDR_NONE ) {
    logwarn( "ickP2pDiscoveryInit: could not get IP address of interface \"%s\"",
             interface );
    _ickDiscoveryDestruct( dh );
    if( error )
      *error = ICKERR_NOINTERFACE;
    return NULL;
  }
  inet_ntop( AF_INET, &ifaddr, buffer, sizeof(buffer) );
  dh->location = strdup( buffer );
  if( !dh->location ) {
    logerr( "ickP2pDiscoveryInit: out of memory." );
    _ickDiscoveryDestruct( dh );
    if( error )
      *error = ICKERR_NOMEM;
    return NULL;
  }
  debug( "ickP2pDiscoveryInit: Using addr %s for interface \"%s\".",
         dh->location, interface );

/*------------------------------------------------------------------------*\
    Create and init SSDP listener socket
\*------------------------------------------------------------------------*/
  dh->socket = socket( PF_INET, SOCK_DGRAM, 0 );
  if( dh->socket<0 ){
    logerr( "ickP2pDiscoveryInit: could not create socket (%s).", strerror(errno) );
    _ickDiscoveryDestruct( dh );
    if( error )
      *error = ICKERR_NOSOCKET;
    return NULL;
  }

  // Nonblocking
  rc = fcntl( dh->socket, F_GETFL );
  if( rc>=0 )
    rc = fcntl( dh->socket, F_SETFL, rc|O_NONBLOCK );
  if( rc<0 )
    logwarn( "ickP2pDiscoveryInit: could not set O_NONBLOCK on socket (%s).",
        strerror(errno) );

  // Reuse address (multiple processes will receive MCASTS)
  opt = 1;
  rc = setsockopt( dh->socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt) );
  if( rc<0 )
    logwarn( "ickP2pDiscoveryInit: could not set SO_REUSEADDR on socket (%s).",
        strerror(errno) );

  // Bind socket to requested port
  rc = _ickIpBind( dh->socket, INADDR_ANY, port );
  if( rc<0 ) {
    logerr( "ickP2pDiscoveryInit: could not bind socket to port %d (%s).",
             port, strerror(rc) );
    _ickDiscoveryDestruct( dh );
    if( error )
      *error = ICKERR_NOSOCKET;
    return NULL;
  }

  // Add socket to multicast group on target interface
  rc = _ickIpAddMcast( dh->socket, ifaddr, inet_addr(ICKSSDP_MCASTADDR) );
  if( rc<0 ) {
    logerr( "ickP2pDiscoveryInit: could not add mcast membership for socket (%s).",
             strerror(rc) );
    _ickDiscoveryDestruct( dh );
    if( error )
      *error = ICKERR_NOSOCKET;
    return NULL;
  }

  // fixme: Need to add localhost??

/*------------------------------------------------------------------------*\
    Create and init websocket
\*------------------------------------------------------------------------*/
  // fixme - we use the discard service for a start
  dh->wsPort = 9;

/*------------------------------------------------------------------------*\
    Link to list of discovery handlers
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &_ickDiscoveryHandlerListMutex );
  dh->next = _ickDiscoveryHandlerList;
  _ickDiscoveryHandlerList = dh;
  pthread_mutex_unlock( &_ickDiscoveryHandlerListMutex );

/*------------------------------------------------------------------------*\
    Start SSDP services for this discovery handler
\*------------------------------------------------------------------------*/
  irc = _ickSsdpNewDiscovery( dh );
  if( irc ) {
    logerr( "ickP2pDiscoveryInit: could not start SSDP (%s).",
            ickStrError( irc ) );
    ickP2pDiscoveryEnd( dh );
    if( error )
      *error = irc;
    return NULL;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  if( error )
    *error = ICKERR_SUCCESS;
  return dh;
}


/*=========================================================================*\
  Terminate a discovery handler
    if wait is nonzero ickEndDiscovery will block until thread finishes
    shall only be called from main thread!
\*=========================================================================*/
ickErrcode_t ickP2pDiscoveryEnd( ickDiscovery_t *dh )
{
  ickDiscovery_t *walk, *last;

  debug( "ickP2pDiscoveryEnd (%p): %s", dh, dh->interface );

/*------------------------------------------------------------------------*\
    The discovery thread might block on receiving messages from the socket,
    so we need to shut it down to make sure the thread ends
\*------------------------------------------------------------------------*/
  if( 0 && shutdown(dh->socket,SHUT_RDWR) )
    logerr( "ickP2pDiscoveryEnd: Unable to shut down SSDP socket (%s)",
             strerror(errno) );

/*------------------------------------------------------------------------*\
    Stop SSDP services and announce termination
\*------------------------------------------------------------------------*/
  _ickSsdpEndDiscovery( dh );

/*------------------------------------------------------------------------*\
    Unlink from list of discovery handlers
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &_ickDiscoveryHandlerListMutex );
  for( last=NULL,walk=_ickDiscoveryHandlerList;
       walk&&walk!=dh;
       last=walk,walk=walk->next );
  if( !walk ) {
    logerr( "ickP2pDiscoveryEnd (%s): handler not valid (already finished?).",
            dh->interface );
    return ICKERR_INVALID;
  }
  if( last )
    last->next = walk->next;
  else
    _ickDiscoveryHandlerList = walk->next;
  pthread_mutex_unlock( &_ickDiscoveryHandlerListMutex );

/*------------------------------------------------------------------------*\
    Execute callback (if any)
\*------------------------------------------------------------------------*/
  if( dh->exitCallback )
    dh->exitCallback( dh );

/*------------------------------------------------------------------------*\
    Free descriptor
\*------------------------------------------------------------------------*/
  _ickDiscoveryDestruct( dh );

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Destruct a discovery instance
    Make sure to unlink first
\*=========================================================================*/
static void _ickDiscoveryDestruct( ickDiscovery_t *dh )
{
  debug( "_ickDesctructDiscovery (%p): %s:%d", dh, dh->interface, dh->port );

/*------------------------------------------------------------------------*\
    Free strings in descriptor
\*------------------------------------------------------------------------*/
  Sfree( dh->interface );
  Sfree( dh->location );
  Sfree( dh->upnpFolder );

/*------------------------------------------------------------------------*\
    Close socket (if any)
\*------------------------------------------------------------------------*/
  if( dh->socket>=0 )
    close( dh->socket );

  //fixme: free device list... also remove references from sender list

/*------------------------------------------------------------------------*\
    Delete mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_destroy( &dh->mutex );
  pthread_mutex_destroy( &dh->deviceListMutex );

/*------------------------------------------------------------------------*\
    Free descriptor
\*------------------------------------------------------------------------*/
  Sfree( dh );
}


/*=========================================================================*\
  Get interface name of a discovery handler
\*=========================================================================*/
const char *ickP2pDiscoveryGetIf( const ickDiscovery_t *dh )
{
  return dh->interface;
}


/*=========================================================================*\
  Get listener port of a discovery handler
\*=========================================================================*/
int ickP2pDiscoveryGetPort( const ickDiscovery_t *dh )
{
  return dh->port;
}


#pragma mark - Managing offered services

/*=========================================================================*\
  Enable services on this device
\*=========================================================================*/
ickErrcode_t ickP2pDiscoveryAddService( ickDiscovery_t *dh, ickP2pServicetype_t type )
{
  ickErrcode_t irc;

/*------------------------------------------------------------------------*\
    Mask out all services that are already registered
\*------------------------------------------------------------------------*/
  type &= ~(dh->ickServices);
  if( !type )
    return ICKERR_SUCCESS;
  dh->ickServices |= type;

/*------------------------------------------------------------------------*\
    Announce services via SSDP
\*------------------------------------------------------------------------*/
  irc = _ickSsdpAnnounceServices( dh, type, SSDPMSGTYPE_ALIVE );
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
ickErrcode_t ickDiscoveryRemoveService( ickDiscovery_t *dh, ickP2pServicetype_t type )
{
  ickErrcode_t irc;

/*------------------------------------------------------------------------*\
    Mask out all services that are not registered
\*------------------------------------------------------------------------*/
  type &= dh->ickServices;
  if( !type )
    return ICKERR_SUCCESS;
  dh->ickServices &= ~type;

/*------------------------------------------------------------------------*\
    Announce service termination via SSDP
\*------------------------------------------------------------------------*/
  irc = _ickSsdpAnnounceServices( dh, type, SSDPMSGTYPE_BYEBYE );
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


#pragma mark - Managing callbacks


/*=========================================================================*\
  Execute callbacks for a discovery handler
\*=========================================================================*/
void _ickDiscoveryExecDeviceCallback( ickDiscovery_t *dh, const upnp_device_t *dev, ickP2pDeviceCommand_t change, ickP2pServicetype_t type )
{
  struct _cb_list *walk;

  pthread_mutex_lock( &_ickLib->mutex );
  for( walk=_ickLib->deviceCbList; walk; walk=walk->next )
    walk->callback( dh, dev->uuid, change, type );
  pthread_mutex_unlock( &_ickLib->mutex );
}



#pragma mark - Device list

/*=========================================================================*\
  Lock device list
\*=========================================================================*/
void _ickDiscoveryDeviceListLock( ickDiscovery_t *dh )
{
  debug ( "_ickDiscoveryDeviceLock (%s): locking...", dh->interface );
  pthread_mutex_lock( &dh->deviceListMutex );
  debug ( "_ickDiscoveryDeviceLock (%s): locked", dh->interface );
}


/*=========================================================================*\
  Unlock device list
\*=========================================================================*/
void _ickDiscoveryDeviceListUnlock( ickDiscovery_t *dh )
{
  debug ( "_ickDiscoveryDeviceUnlock (%s): unlocked", dh->interface );
  pthread_mutex_unlock( &dh->deviceListMutex );
}


/*=========================================================================*\
  Add a device to a discovery handler
    This will NOT execute callbacks or add expiration handlers
    Caller should lock device list
\*=========================================================================*/
void _ickDiscoveryDeviceAdd( ickDiscovery_t *dh, upnp_device_t *dev )
{
  debug ( "_ickDiscoveryDeviceAdd (%s): adding new device \"%s\" at \"%s\".",
          dh->interface, dev->uuid, dev->location );

/*------------------------------------------------------------------------*\
     Don't add twice...
\*------------------------------------------------------------------------*/
  if( _ickDiscoveryDeviceFind(dh,dev->uuid) ) {
    debug ( "_ickDiscoveryDeviceAdd (%s): device \"%s\" already there.",
            dh->interface, dev->uuid );
    return;
  }

/*------------------------------------------------------------------------*\
     Link device to list
\*------------------------------------------------------------------------*/
  dev->next = dh->deviceList;
  if( dh->deviceList )
    dh->deviceList->prev=dev;
  dh->deviceList = dev;

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
void _ickDiscoveryDeviceRemove( ickDiscovery_t *dh, upnp_device_t *dev )
{
  debug ( "_ickDiscoveryDeviceRemove (%s): removing device \"%s\" at \"%s\".",
          dh->interface, dev->uuid, dev->location );

/*------------------------------------------------------------------------*\
     Be paranoid
\*------------------------------------------------------------------------*/
  if( !_ickDiscoveryDeviceFind(dh,dev->uuid) ) {
    debug ( "_ickDiscoveryDeviceRemove (%s): device \"%s\" not in list.",
            dh->interface, dev->uuid );
    return;
  }

/*------------------------------------------------------------------------*\
    Unlink from device list
\*------------------------------------------------------------------------*/
  if( dev->next )
    dev->next->prev = dev->prev;
  if( dev->prev )
    dev->prev->next = dev->next;
  if( dev==dh->deviceList )
    dh->deviceList = dev->next;
}


/*=========================================================================*\
  Find a device associated with a discovery handler
    nt and usn are the SSDP header files identifying the device
\*=========================================================================*/
upnp_device_t *_ickDiscoveryDeviceFind( const ickDiscovery_t *dh, const char *uuid )
{
  upnp_device_t *device;

  debug ( "_ickDiscoveryDeviceFind (%s): UUID=\"%s\".",
          dh->interface, uuid );

/*------------------------------------------------------------------------*\
    Find matching device entry
\*------------------------------------------------------------------------*/
  for( device=dh->deviceList; device; device=device->next ) {
    if( !strcmp(device->uuid,uuid) )
      break;
  }

/*------------------------------------------------------------------------*\
    Thats it
\*------------------------------------------------------------------------*/
  debug ( "_ickDiscoveryDeviceFind (%s): result=%p", dh->interface, device );
  return device;
}




/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
