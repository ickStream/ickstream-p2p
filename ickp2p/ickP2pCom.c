/*$*********************************************************************\

Source File     : ickP2pCom.c

Description     : implement ickp2p communication functions

Comments        : -

Called by       : API wrapper functions, libwebsocket

Calls           : libwebsocket et all

Date            : 25.08.2013

Updates         : -

Author          : JS, //MAF

Remarks         : refactored from ickP2PComm.c

*************************************************************************
 * Copyright (c) 2013, ickStream GmbH
 * All rights reserved.
\************************************************************************/


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libwebsockets.h>

#include "ickP2p.h"
#include "ickP2pInternal.h"
#include "logutils.h"
#include "ickMainThread.h"
#include "ickDescription.h"
#include "ickDevice.h"
#include "ickSSDP.h"
#include "ickP2pCom.h"


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
static int   _ickP2pComTransmit( struct libwebsocket *wsi, ickMessage_t *message );
static char *_ickLwsDupToken( struct libwebsocket *wsi, enum lws_token_indexes h );
#ifdef ICK_DEBUG
static void  _ickLwsDumpHeaders( struct libwebsocket *wsi );
#endif



/*=========================================================================*\
  Send an ickstream message
    ictx           - ickstream context
    uuid           - uuid of target, if NULL all known ickstream devices are
                     addressed
    targetServices - services at target to address
    sourceService  - sending service
    message        - the message
    mSize          - size of message, if 0 the message is interpreted
                     as a 0-terminated string
\*=========================================================================*/
ickErrcode_t ickP2pSendMsg( ickP2pContext_t *ictx, const char *uuid,
                            ickP2pServicetype_t targetServices, ickP2pServicetype_t sourceService,
                            const char *message, size_t mSize )
{
  ickP2pMessageFlag_t mFlags = ICKP2P_MESSAGEFLAG_NONE;
  ickErrcode_t        irc    = ICKERR_SUCCESS;
  ickDevice_t        *device;

/*------------------------------------------------------------------------*\
    Determine size if payload is a string
\*------------------------------------------------------------------------*/
  if( !mSize ) {
    mFlags |= ICKP2P_MESSAGEFLAG_STRING;
    mSize   = strlen( message ) + 1;
  }

  debug( "ickP2pSendMsg: target=\"%s\" targetServices=0x%02x sourceServices=0x%02x size=%ld",
         uuid?uuid:"<Notification>", targetServices, sourceService, (long)mSize );

/*------------------------------------------------------------------------*\
    Lock device list and find (first) device
\*------------------------------------------------------------------------*/
  _ickLibDeviceListLock( ictx );
  if( uuid ) {
    device = _ickLibDeviceFindByUuid( ictx, uuid );

    // Unknown device?
    if( !device ) {
      _ickLibDeviceListUnlock( ictx );
      return ICKERR_NODEVICE;
    }

    // Not connected?
    if( (!device->wsi||device->connectionState==ICKDEVICE_NOTCONNECTED) &&
        device->connectionState!=ICKDEVICE_LOOPBACK ) {
      _ickLibDeviceListUnlock( ictx );
      return ICKERR_NOTCONNECTED;
    }
  }
  else if( ictx->deviceList ) {
    mFlags |= ICKP2P_MESSAGEFLAG_NOTIFICATION;
    device  = ictx->deviceList;
  }
  else {
    _ickLibDeviceListUnlock( ictx );
    return ICKERR_SUCCESS;
  }
  _ickLibDeviceListUnlock( ictx );

/*------------------------------------------------------------------------*\
    Loop over all devices
\*------------------------------------------------------------------------*/
  do {
    ickP2pLevel_t p2pLevel;
    size_t        preambleLen = 1;  // p2pLevel
    size_t        pSize;
    size_t        cSize;
    char         *container;
    char         *ptr;

/*------------------------------------------------------------------------*\
    Ignore unconnected devices in broadcast mode
\*------------------------------------------------------------------------*/
    if( (!device->wsi||device->connectionState==ICKDEVICE_NOTCONNECTED) &&
        device->connectionState!=ICKDEVICE_LOOPBACK )
      goto nextDevice;

/*------------------------------------------------------------------------*\
    Determine preamble length and elements according to cross section of
    device and our local capabilities
\*------------------------------------------------------------------------*/
    p2pLevel = device->ickP2pLevel & ICKP2PLEVEL_SUPPORTED;
    if( p2pLevel&ICKP2PLEVEL_TARGETSERVICES )
      preambleLen++;
    if( p2pLevel&ICKP2PLEVEL_SOURCESERVICE )
      preambleLen++;
    if( p2pLevel&ICKP2PLEVEL_MESSAGEFLAGS )
      preambleLen++;

/*------------------------------------------------------------------------*\
    Allocate payload container, include LWS padding
\*------------------------------------------------------------------------*/
    pSize = preambleLen + mSize;
    cSize = LWS_SEND_BUFFER_PRE_PADDING + pSize + LWS_SEND_BUFFER_POST_PADDING;
    container = malloc( cSize );
    if( !container ) {
      logerr( "ickP2pSendMsg: out of memory (%ld bytes)", (long)cSize );
      irc = ICKERR_NOMEM;
      break;
    }

/*------------------------------------------------------------------------*\
    Collect actual preamble elements in container
\*------------------------------------------------------------------------*/
    ptr = container + LWS_SEND_BUFFER_PRE_PADDING;
    *ptr++ = p2pLevel;

    if( p2pLevel&ICKP2PLEVEL_TARGETSERVICES )
      *ptr++ = (unsigned char)targetServices;

    if( p2pLevel&ICKP2PLEVEL_SOURCESERVICE )
      *ptr++ = (unsigned char)sourceService;

    if( p2pLevel&ICKP2PLEVEL_MESSAGEFLAGS )
      *ptr++ = (unsigned char)mFlags;

/*------------------------------------------------------------------------*\
    Copy payload to container
\*------------------------------------------------------------------------*/
    memcpy( ptr, message, mSize );

/*------------------------------------------------------------------------*\
    Try queue message for transmission
\*------------------------------------------------------------------------*/
    _ickDeviceLock( device );
    irc = _ickDeviceAddOutMessage( device, container, pSize );
    _ickDeviceUnlock( device );
    if( irc ) {
      Sfree( container );
      break;
    }

/*------------------------------------------------------------------------*\
    Book a writable callback for the devices wsi
\*------------------------------------------------------------------------*/
    if( device->wsi )
      libwebsocket_callback_on_writable( ictx->lwsContext, device->wsi );

    // Loopback
    else if( device->connectionState==ICKDEVICE_LOOPBACK ) {
      // Set submission timestamp and count message as sent
      device->tLastTx = _ickTimeNow();
      device->nTx++;
    }


/*------------------------------------------------------------------------*\
    Handle next device in notification mode
\*------------------------------------------------------------------------*/
nextDevice:
    device = device->next;
  } while( !uuid && device );

/*------------------------------------------------------------------------*\
    That's all - unlock device list and break polling in main thread
\*------------------------------------------------------------------------*/
  _ickLibDeviceListUnlock( ictx );
  _ickMainThreadBreak( ictx, 'm' );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return irc;
}


/*=========================================================================*\
  Default ickstream connection matrix
\*=========================================================================*/
int ickP2pDefaultConnectMatrixCb( ickP2pContext_t *ictx, ickP2pServicetype_t localServices, ickP2pServicetype_t remoteServices )
{
  debug( "ickP2pDefaultConnectMatrixCb (%p): local=0x%02x remote=0x%02x", ictx, localServices, remoteServices);

/*------------------------------------------------------------------------*\
    Debug always connects
\*------------------------------------------------------------------------*/
  if( (localServices&ICKP2P_SERVICE_DEBUG) || (remoteServices&ICKP2P_SERVICE_DEBUG) )
    return 1;

/*------------------------------------------------------------------------*\
    Servers connect to controllers and players
\*------------------------------------------------------------------------*/
  if( localServices&ICKP2P_SERVICE_SERVER_GENERIC ) {
    if( (remoteServices&ICKP2P_SERVICE_CONTROLLER) || (remoteServices&ICKP2P_SERVICE_PLAYER) )
      return 1;
  }

/*------------------------------------------------------------------------*\
    Controller connect to servers and players
\*------------------------------------------------------------------------*/
  if( localServices&ICKP2P_SERVICE_CONTROLLER ) {
    if( (remoteServices&ICKP2P_SERVICE_SERVER_GENERIC) || (remoteServices&ICKP2P_SERVICE_PLAYER) )
      return 1;
  }

/*------------------------------------------------------------------------*\
    Players connect to servers and controllers
\*------------------------------------------------------------------------*/
  // I'm a player, so I want to connect to controllers and servers
  if( localServices&ICKP2P_SERVICE_PLAYER ) {
    if( (remoteServices&ICKP2P_SERVICE_SERVER_GENERIC) || (remoteServices&ICKP2P_SERVICE_CONTROLLER) )
      return 1;
  }

/*------------------------------------------------------------------------*\
   No connection wanted
\*------------------------------------------------------------------------*/
 return 0;
}


#pragma mark -- internal functions


/*=========================================================================*\
  Send a Null message (used for heart beat and syncing)
    ictx           - ickstream context
    device         - target device
\*=========================================================================*/
ickErrcode_t _ickP2pSendNullMessage( ickP2pContext_t *ictx, ickDevice_t *device )
{
  ickErrcode_t  irc = ICKERR_SUCCESS;
  size_t        cSize;
  char         *container;

  debug( "_ickP2pSendNullMessage: target=\"%s\"", device->uuid );
  if( device->connectionState==ICKDEVICE_LOOPBACK ) {
    logerr( "_ickP2pSendNullMessage (%s): called for loopback device", device->uuid );
    return ICKERR_INVALID;
  }

/*------------------------------------------------------------------------*\
    Allocate payload container, include LWS padding
\*------------------------------------------------------------------------*/
  cSize = LWS_SEND_BUFFER_PRE_PADDING + 1 + LWS_SEND_BUFFER_POST_PADDING;
  container = malloc( cSize );
  if( !container ) {
    logerr( "_ickP2pSendNullMessage: out of memory (%ld bytes)", (long)cSize );
    return ICKERR_NOMEM;
  }

/*------------------------------------------------------------------------*\
    Create null payload
\*------------------------------------------------------------------------*/
  container[LWS_SEND_BUFFER_PRE_PADDING] = 0;

/*------------------------------------------------------------------------*\
    Try to queue message for transmission
\*------------------------------------------------------------------------*/
  irc = _ickDeviceAddOutMessage( device, container, 1 );
  if( irc ) {
    Sfree( container );
    return irc;
  }

/*------------------------------------------------------------------------*\
    Book a writable callback for the devices wsi
\*------------------------------------------------------------------------*/
  if( device->wsi )
    libwebsocket_callback_on_writable( ictx->lwsContext, device->wsi );
  else
    debug( "_ickP2pSendNullMessage (%s): sending deferred, wsi not yet present.",
           device->uuid );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Deliver a message via loopback
    timer list is already locked
\*=========================================================================*/
ickErrcode_t _ickDeliverLoopbackMessage( ickP2pContext_t *ictx )
{
  ickDevice_t     *device = ictx->deviceLoopback;
  ickMessage_t    *message;

  debug( "_ickDeliverLoopbackMessage: \"%s\"", device->uuid );

/*------------------------------------------------------------------------*\
   There should be a message pending....
\*------------------------------------------------------------------------*/
  _ickDeviceLock( device );
  message = _ickDeviceOutQueue( device );
  if( !message ) {
    _ickDeviceUnlock( device );
    logerr( "_ickDeliverLoopbackMessage: empty out queue for \"%s\"!", device->uuid  );
    return ICKERR_INVALID;
  }
  _ickDeviceUnlinkOutMessage( device, message );
  _ickDeviceUnlock( device );

/*------------------------------------------------------------------------*\
    Deliver message locally, don't use LWS padding
\*------------------------------------------------------------------------*/
  device->tLastRx = _ickTimeNow();
  _ickP2pExecMessageCallback( ictx, device, message->payload+LWS_SEND_BUFFER_PRE_PADDING, message->size );
  device->nRx++;

/*------------------------------------------------------------------------*\
    Release message, that's all
\*------------------------------------------------------------------------*/
  _ickDeviceFreeMessage( message );
  return ICKERR_SUCCESS;
}



/*=========================================================================*\
  Initiate web socket connection to a device
    caller should lock the device,
    this must be called from the main thread
\*=========================================================================*/
ickErrcode_t _ickWebSocketOpen( struct libwebsocket_context *context, ickDevice_t *device )
{
  ickP2pContext_t     *ictx = device->ictx;
  ickErrcode_t         irc = ICKERR_SUCCESS;
  char                *address;
  in_addr_t            ifaddr;
  int                  port;
  ickInterface_t      *interface;
  char                *host;
  char                *ptr;
  _ickLwsP2pData_t    *psd;
  struct libwebsocket *wsi;

  debug( "_ickWebSocketOpen (%s): \"%s\"", device->uuid, device->location );
  if( device->connectionState==ICKDEVICE_LOOPBACK ) {
    logerr( "_ickWebSocketOpen (%s): called for loopback device", device->uuid );
    return ICKERR_INVALID;
  }

/*------------------------------------------------------------------------*\
    Check status
\*------------------------------------------------------------------------*/
  if( device->wsi ) {
    logerr( "_ickWebSocketOpen (%s): already connected", device->uuid );
    return ICKERR_ISCONNECTED;
  }
  if( !ictx ) {
    logerr( "_ickWebSocketOpen (%s): device has no context", device->uuid );
    return ICKERR_GENERIC;
  }

/*------------------------------------------------------------------------*\
    Get address and port from device location
\*------------------------------------------------------------------------*/
  if( strncmp(device->location,"http://",strlen("http://")) ) {
    logerr( "_ickWebSocketOpen (%s): Bad location URI \"%s\"", device->uuid, device->location );
    return ICKERR_BADURI;
  }
  address = strdup( device->location+strlen("http://") );
  if( !address ) {
    logerr( "_ickWebSocketOpen: out of memory" );
    return ICKERR_NOMEM;
  }
  ptr = strchr( address, ':' );
  if( !ptr ) {
    Sfree( address );
    logerr( "_ickWebSocketOpen (%s): Bad location URI \"%s\"", device->uuid, device->location );
    return ICKERR_BADURI;
  }
  *ptr = 0;
  port = atoi( ptr+1 );

/*------------------------------------------------------------------------*\
    Get interface for address
\*------------------------------------------------------------------------*/
  interface =_ickLibInterfaceForHost( ictx, address, &ifaddr );
  if( !interface ) {
    Sfree( address );
    logerr( "_ickWebSocketOpen (%s): Found no interface for URI \"%s\"",
            device->uuid, device->location );
    return ICKERR_NOINTERFACE;
  }

/*------------------------------------------------------------------------*\
    Use hostname to signal the origin address
\*------------------------------------------------------------------------*/
  if( asprintf(&host,"%s:%d",interface->hostname,ictx->lwsPort)<0 ) {
    Sfree( address );
    logerr( "_ickWebSocketOpen: out of memory" );
    return ICKERR_NOMEM;
  }

/*------------------------------------------------------------------------*\
    Create per session data container
\*------------------------------------------------------------------------*/
  psd = calloc( 1, sizeof(_ickLwsP2pData_t) );
  if( !psd ) {
    Sfree( address );
    Sfree( host );
    logerr( "_ickWebSocketOpen: out of memory" );
    return ICKERR_NOMEM;
  }
  psd->ictx   = device->ictx;
  psd->device = device;

  debug( "_ickWebSocketOpen (%s): addr=%s:%d host=%s", device->uuid, address, port, host );

/*------------------------------------------------------------------------*\
    Initiate connection
\*------------------------------------------------------------------------*/
  device->connectionState = ICKDEVICE_CLIENTCONNECTING;
  debug( "_ickWebSocketOpen (%s): device state now \"%s\"",
         device->uuid, _ickDeviceConnState2Str(device->connectionState) );
  wsi = libwebsocket_client_connect_extended(
                    context,
                    address,
                    port,
                    0,                        // ssl_connection
                    "/",                      // path
                    host,                     // host on server (used to submit our server uri)
                    ictx->deviceUuid,         // origin
                    ICKP2P_WS_PROTOCOLNAME,   // protocol
                    -1,                       // ietf_version_or_minus_one
                    psd );
  if( !wsi ) {
    debug( "_ickWebSocketOpen (%s): Could not create lws client socket.",
            device->uuid );
    irc = ICKERR_LWSERR;
    device->connectionState = ICKDEVICE_NOTCONNECTED;
    debug( "_ickWebSocketOpen (%s): device state now \"%s\"",
           device->uuid, _ickDeviceConnState2Str(device->connectionState) );
  }

/*------------------------------------------------------------------------*\
    Clean up, that's all
\*------------------------------------------------------------------------*/
  Sfree( address );
  Sfree( host );
  return irc;
}


/*=========================================================================*\
  Handle LWS callbacks related to the ickp2p protocol
\*=========================================================================*/
int _lwsP2pCb( struct libwebsocket_context *context,
               struct libwebsocket *wsi,
               enum libwebsocket_callback_reasons reason, void *user,
               void *in, size_t len )
{
  ickP2pContext_t   *ictx = libwebsocket_context_user( context );
  _ickLwsP2pData_t  *psd = (_ickLwsP2pData_t*) user;
  int                socket;
  ickDevice_t       *device;
  ickMessage_t      *message;
  int                remainder;
  size_t             rlen;
  unsigned char     *ptr;
  char              *dscrPath;

  debug( "_lwsP2pCb: lws %p, wsi %p, ictx %p, psd %p", context, wsi, ictx, psd );

/*------------------------------------------------------------------------*\
    What to do?
\*------------------------------------------------------------------------*/
  switch( reason ) {

/*------------------------------------------------------------------------*\
    Init protocol
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_PROTOCOL_INIT:
      debug( "_lwsP2pCb: init" );
      break;

/*------------------------------------------------------------------------*\
    Shut down protocol
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_PROTOCOL_DESTROY:
      debug( "_lwsP2pCb: shutdown" );
      break;

/*------------------------------------------------------------------------*\
    Could not connect
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      socket = libwebsocket_get_socket_fd( wsi );
      device = psd->device;
      logwarn( "_lwsP2pCb %d: connection error", socket );

      // Execute discovery callback
      if( device ) {
        device->connectionState = ICKDEVICE_NOTCONNECTED;
        debug( "_lwsP2pCb (%s): device state now \"%s\"",
               device->uuid, _ickDeviceConnState2Str(device->connectionState) );
        _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_ERROR, device->services );
      }

      break;

/*------------------------------------------------------------------------*\
   Filter connections (both inbound and outbound),
   PSD not available at this point...
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
      socket = libwebsocket_get_socket_fd( wsi );
      debug( "_lwsP2pCb %d: filter protocol connection", socket );
#ifdef ICK_DEBUG
      _ickLwsDumpHeaders( wsi );
#endif
      break;

/*------------------------------------------------------------------------*\
    Filter outgoing connection requests
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH:
      socket = libwebsocket_get_socket_fd( wsi );
      debug( "_lwsP2pCb %d: client filter pre-establish", socket );
#ifdef ICK_DEBUG
      _ickLwsDumpHeaders( wsi );
#endif
      break;

/*------------------------------------------------------------------------*\
    A outgoing (client) connection is established
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
      socket = libwebsocket_get_socket_fd( wsi );
      device = psd->device;
      debug( "_lwsP2pCb %d: client connection established, %d messages pending",
             socket, _ickDevicePendingOutMessages(device) );

      // Drop this connection if already connected
      if( device->wsi ) {
        debug( "_lwsP2pCb %d: dropping client connection (already connected)", socket );
        psd->kill = 1;
        libwebsocket_callback_on_writable( context, wsi );
        return -1;  // No effect for LWS_CALLBACK_CLIENT_ESTABLISHED
      }

      // Create heartbeat timer
      _ickTimerListLock( ictx );
      if( device->doConnect ) {
        ickErrcode_t irc;
        debug( "_lwsP2pCb (%s): create heartbeat timer", device->uuid );
        irc = _ickTimerAdd( ictx, device->lifetime*1000, 0, _ickHeartbeatTimerCb, device, 0 );
        if( irc )
          logerr( "_lwsP2pCb (%s): could not create heartbeat timer (%s)",
                  device->uuid, ickStrError(irc) );
      }
      _ickTimerListUnlock( ictx );

      // Book a write event if a message is already pending
      if( _ickDeviceOutQueue(device) )
        libwebsocket_callback_on_writable( context, wsi );

      // Timestamp and mode of connection
      device->tConnect        = _ickTimeNow();
      device->connectionState = ICKDEVICE_ISCLIENT;
      device->wsi             = wsi;
      debug( "_lwsP2pCb (%s): device state now \"%s\"",
             device->uuid, _ickDeviceConnState2Str(device->connectionState) );


      // Execute discovery callback
      _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_CONNECTED, device->services );

      break;

/*------------------------------------------------------------------------*\
    An incoming connection is established
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_ESTABLISHED:
      socket = libwebsocket_get_socket_fd( wsi );
      debug( "_lwsP2pCb %d: server connection established", socket );

#ifdef ICK_DEBUG
      _ickLwsDumpHeaders( wsi );
#endif

      // Reset per session data
      memset( psd, 0, sizeof(_ickLwsP2pData_t) );

      // Get remote host (we use the HOST field for transmission)
      psd->host = _ickLwsDupToken( wsi, WSI_TOKEN_HOST );
      if( !psd->host ) {
        logwarn( "_lwsP2pCb: Incoming connection rejected (no HOST).");
        psd->kill = 1;
        libwebsocket_callback_on_writable( context, wsi );
        return -1; // No effect for LWS_CALLBACK_ESTABLISHED
      }

      // Get origin
      psd->uuid = _ickLwsDupToken( wsi, WSI_TOKEN_ORIGIN );
      if( !psd->uuid ) {
        logwarn( "_lwsP2pCb: Incoming connection rejected (no UUID).");
        psd->kill = 1;
        libwebsocket_callback_on_writable( context, wsi );
        return -1; // No effect for LWS_CALLBACK_ESTABLISHED
      }
      if( !strncmp(psd->uuid,"http://",7) ) {
        int i = 0;
        do {
          psd->uuid[i] = psd->uuid[i+7];
        } while( psd->uuid[i++] );
      }

      // Lock device list and try to find device
      _ickLibDeviceListLock( ictx );
      device = _ickLibDeviceFindByUuid( ictx , psd->uuid );

      // Device already known?
      if( device ) {
        debug( "_lwsP2pCb (%s): (Re)connected already initialized device, state \"%s\"",
               device->uuid, _ickDeviceConnState2Str(device->connectionState) );

        // Already connected?
        if( device->wsi ) {
          debug( "_lwsP2pCb (%s): Connection rejected (exiting or dead connection)", device->uuid );
          _ickLibDeviceListUnlock( ictx );

          // immediately terminate this connection
          psd->kill = 1;
          libwebsocket_callback_on_writable( context, wsi );

          // Queue a heartbeat message to terminate dead connections
          _ickP2pSendNullMessage( ictx, device );

          return -1; // No effect for LWS_CALLBACK_ESTABLISHED
        }

        // Device initializing: a wget task exists
        else if( device->wget ) {
#if 0
          debug( "_lwsP2pCb (%s): Incoming connection has higher priority, deleting running XML retriever \"%s\"",
                 device->uuid, device->location );
          _ickWGetDestroy( device->wget );
          device->wget = NULL;
          _ickLibDeviceListUnlock( ictx );
#else
          debug( "_lwsP2pCb (%s): Connection rejected, XML retriever running", device->uuid );
                    _ickLibDeviceListUnlock( ictx );
          psd->kill = 1;
          libwebsocket_callback_on_writable( context, wsi );
          return -1; // No effect for LWS_CALLBACK_ESTABLISHED
#endif
        }

        // Incoming connect from a device we don't want to connect to (should not happen)
        else if( !device->doConnect ) {
          debug( "_lwsP2pCb (%s): Connection rejected due to local connection matrix", device->uuid );
          _ickLibDeviceListUnlock( ictx );
          psd->kill = 1;
          libwebsocket_callback_on_writable( context, wsi );
          return -1; // No effect for LWS_CALLBACK_ESTABLISHED
        }

        // This is a reconnect: Set (new!) URL of ickstream root device
        debug( "_lwsP2pCb (%s): Reconnecting already initialized device", device->uuid );
        if( asprintf(&dscrPath,"http://%s/%s.xml",psd->host,ICKDEVICE_STRING_ROOT)<0 ) {
          logerr( "_lwsP2pCb: out of memory" );
          _ickLibDeviceListUnlock( ictx );
          psd->kill = 1;
          libwebsocket_callback_on_writable( context, wsi );
          return -1; // No effect for LWS_CALLBACK_ESTABLISHED
        }
        Sfree( device->location );
        device->location = dscrPath;

        // We are server, set connection timestamp
        device->connectionState = ICKDEVICE_ISSERVER;
        device->tConnect        = _ickTimeNow();
        debug( "_lwsP2pCb (%s): device state now \"%s\"",
               device->uuid, _ickDeviceConnState2Str(device->connectionState) );


        // Execute discovery callback
        _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_CONNECTED, device->services );
      }

      // Device unknown (i.e. was not (yet) discovered by SSDP)
      else {
        debug( "_lwsP2pCb (%s): Discovered new device via incoming ws connection from \"%s\".",
               psd->uuid, psd->host );

        // Create and init device
        device = _ickDeviceNew( psd->uuid );
        if( !device ) {
          logerr( "_lwsP2pCb: out of memory" );
          _ickLibDeviceListUnlock( ictx );
          psd->kill = 1;
          libwebsocket_callback_on_writable( context, wsi );
          return -1; // No effect for LWS_CALLBACK_ESTABLISHED
        }

        // Get URL of ickstream root device
        if( asprintf(&dscrPath,"http://%s/%s.xml",psd->host,ICKDEVICE_STRING_ROOT)<0 ) {
          logerr( "_lwsP2pCb: out of memory" );
          _ickLibDeviceListUnlock( ictx );
          psd->kill = 1;
          libwebsocket_callback_on_writable( context, wsi );
          return -1; // No effect for LWS_CALLBACK_ESTABLISHED
        }
        device->location = dscrPath;

        // We are server without XML
        device->connectionState = ICKDEVICE_SERVERCONNECTING;
        debug( "_lwsP2pCb (%s): device state now \"%s\"",
               device->uuid, _ickDeviceConnState2Str(device->connectionState) );

        // Link new device to discovery handler
        _ickLibDeviceAdd( ictx, device );
      }

      _ickLibDeviceListUnlock( ictx );

      // Start retrieval of UPnP descriptor if necessary
      if( device->tXmlComplete==0.0 ) {
        debug( "_lwsP2pCb (%s): need to get XML device descriptor", device->uuid );

        // Don't start wget twice
        if( device->wget ) {
          loginfo( "_lwsP2pCb (%s): xml retriever already exists \"%s\".",
              device->uuid, device->location );
        }

        else {
          ickErrcode_t irc;
          device->wget = _ickWGetInit( ictx, dscrPath, _ickWGetXmlCb, device, &irc );
          if( !device->wget ) {
            logerr( "_lwsP2pCb (%s): could not start xml retriever \"%s\" (%s).",
                psd->uuid, psd->host, ickStrError(irc) );
            _ickDeviceFree( device );
            _ickLibDeviceListUnlock( ictx );
            psd->kill = 1;
            libwebsocket_callback_on_writable( context, wsi );
            return -1; // No effect for LWS_CALLBACK_ESTABLISHED
          }
          _ickLibWGettersLock( ictx );
          _ickLibWGettersAdd( ictx, device->wget );
          _ickLibWGettersUnlock( ictx );
        }
      }

      // Create heartbeat timer
      _ickTimerListLock( ictx );
      if( device->doConnect ) {
        ickErrcode_t irc;
        debug( "_lwsP2pCb (%s): create heartbeat timer", device->uuid );
        irc = _ickTimerAdd( ictx, device->lifetime*1000, 0, _ickHeartbeatTimerCb, device, 0 );
        if( irc )
          logerr( "_lwsP2pCb (%s): could not create heartbeat timer (%s)",
                  device->uuid, ickStrError(irc) );
      }
      _ickTimerListUnlock( ictx );

      // Store wsi and device
      device->wsi             = wsi;
      psd->device             = device;

      break;

/*------------------------------------------------------------------------*\
    A connection (client or server) is writable
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_CLIENT_WRITEABLE:
    case LWS_CALLBACK_SERVER_WRITEABLE:
      socket = libwebsocket_get_socket_fd( wsi );
      device = psd->device;
      debug( "_lwsP2pCb %d: %s connection is writable", socket,
             reason==LWS_CALLBACK_CLIENT_WRITEABLE?"client":"server" );

      // Kill this connection?
      if( psd->kill ) {
        debug( "_lwsP2pCb %d: closing connection on kill request", socket );
        return -1;
      }
      if( wsi!=device->wsi ) {
        debug( "_lwsP2pCb %d: closing dangling connection on reconnect", socket );
        return -1;
      }

      // There should be a pending message...
      _ickDeviceLock( device );
      message = _ickDeviceOutQueue( device );
      if( !message ) {
        _ickDeviceUnlock( device );
        logerr( "_lwsP2pCb %d: received writable signal with empty out queue for \"%s\"", socket, device->uuid );
        break;
      }

      // Try to transmit the current message
      remainder = _ickP2pComTransmit( wsi, message );

      // Error handling
      if( remainder<0 ) {
        logerr( "_lwsP2pCb %d: error writing to \"%s\"", socket, device->uuid );
        // Execute discovery callback
        _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_ERROR, device->services );
        _ickDeviceUnlinkOutMessage( device, message );
        _ickDeviceFreeMessage( message );
        if( _ickDeviceOutQueue(device) )
          libwebsocket_callback_on_writable( context, wsi );
        _ickDeviceUnlock( device );
        break;;
      }

      // Set timestamp for last successful (partial) submission
      device->tLastTx = _ickTimeNow();

      // If not complete book another write callback
      if( remainder )
        libwebsocket_callback_on_writable( context, wsi );

      // If complete, delete message and check for a next one
      else {
        _ickDeviceUnlinkOutMessage( device, message );
        _ickDeviceFreeMessage( message );
        device->nTx++;
        if( _ickDeviceOutQueue(device) )
          libwebsocket_callback_on_writable( context, wsi );
      }

      // That's it
      _ickDeviceUnlock( device );
      break;

/*------------------------------------------------------------------------*\
    A connection (client or server) is readable
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_RECEIVE:
    case LWS_CALLBACK_CLIENT_RECEIVE:
      socket = libwebsocket_get_socket_fd( wsi );
      device = psd->device;
      rlen   = libwebsockets_remaining_packet_payload( wsi );
  int final = libwebsocket_is_final_fragment( wsi );

      debug( "_lwsP2pCb %d: %s ready to receive (%ld bytes, %ld remain, %s) from \"%s\"", socket,
             reason==LWS_CALLBACK_CLIENT_RECEIVE?"client":"server",
             (long)len, (long)rlen, final?"final":"to be continued",
             device?device->uuid:"<unknown UUID>" );

#if 0
      // reset SSDP expiration timer
      ickTimer_t *timer = _ickTimerFind( ictx, _ickDeviceExpireTimerCb, device, 0 );
      if( timer )
        _ickTimerUpdate( ictx, timer, device->lifetime*1000, 1 );
      else
        logerr( "_lwsP2pCb (%s): could not find expiration timer.", psd->uuid );
#endif

      // Set timestamp of last (partial) receive
      device->tLastRx = _ickTimeNow();

      // No fragmentation? Process message directly
      if( !psd->inBuffer && !rlen && final) {

        // buffer messages for servers, which are not yet in connected state
        if( device->connectionState==ICKDEVICE_SERVERCONNECTING ) {
          ickErrcode_t irc;
          ptr = malloc( len );
          if( !ptr ) {
            logerr( "_lwsP2pCb (%s): out of memory", device->uuid );
            _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_ERROR, device->services );
            return -1;
          }
          memcpy( ptr, in, len );
          irc = _ickDeviceAddInMessage( device, ptr, len );
          if( irc ) {
            logerr( "_lwsP2pCb (%s): could not add message to input queue (%s)",
                      device->uuid, ickStrError(irc) );
            _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_ERROR, device->services );
            return -1;
          }
        }

        // else deliver message
        else
          _ickP2pExecMessageCallback( ictx, device, in, len );

        // Count message
        device->nRx++;
        break;
      }

      // Collecting chunks: (Re)allocate message buffer for new total size
      ptr = realloc( psd->inBuffer, psd->inBufferSize+len );
      if( !ptr ) {
        logerr( "_lwsP2pCb: out of memory" );
        _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_ERROR, device->services );
        return -1;
      }
      psd->inBuffer  = ptr;

      // Append data to buffer
      memcpy( psd->inBuffer + psd->inBufferSize, in, len );
      psd->inBufferSize += len;

      // If complete: execute callbacks and clean up
      if( !rlen && final ) {
        // buffer messages for servers, which are not yet in connected state
        if( device->connectionState==ICKDEVICE_SERVERCONNECTING ) {
          ickErrcode_t irc;
          irc = _ickDeviceAddInMessage( device, psd->inBuffer, psd->inBufferSize );
          if( irc ) {
            logerr( "_lwsP2pCb (%s): could not add message to input queue (%s)",
                      device->uuid, ickStrError(irc) );
            _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_ERROR, device->services );
            return -1;
          }
        }

        // else deliver and free message
        else {
          _ickP2pExecMessageCallback( ictx, device, psd->inBuffer, psd->inBufferSize );
          Sfree( psd->inBuffer );
          psd->inBufferSize = 0;
        }

        // Count segmented message
        device->nRxSegmented++;
      }

      break;

/*------------------------------------------------------------------------*\
    A connection was closed
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_CLOSED:
      socket = libwebsocket_get_socket_fd( wsi );
      device = psd->device;
      debug( "_lwsP2pCb %d: connection closed (%s)", socket, device?device->uuid:"<unknown UUID>" );

      // Kill this connection?
      if( psd->kill ) {
        debug( "_lwsP2pCb %d: connection killed (no device cleanup)", socket );
      }

      // Mark and reset devices descriptor
      if( device && !psd->kill ) {

        // Remove heartbeat for this device
        _ickTimerDeleteAll( ictx, _ickHeartbeatTimerCb, device, 0 );

        // Set timestamp
        device->tDisconnect = _ickTimeNow();

        // Reset device state and execute discovery callback
        // A wsi mismatch indicates shutdown of a dangling wsi on reconnect,
        // the discovery callback was already called in that case
        device->connectionState = ICKDEVICE_NOTCONNECTED;
        debug( "_lwsP2pCb (%s): device state now \"%s\"",
               device->uuid, _ickDeviceConnState2Str(device->connectionState) );
        if( device->wsi==wsi ) {
          device->wsi = NULL;
          _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_DISCONNECTED, device->services );
        }
        else
          device->wsi = NULL;

        // Delete unsent messages
        _ickDevicePurgeMessages( device );

        // Get rid of device descriptor if SSDP is not alive
        if( device->ssdpState!=ICKDEVICE_SSDPALIVE ) {
          _ickTimerDeleteAll( ictx, _ickDeviceExpireTimerCb, device, 0 );
          _ickLibDeviceRemove( ictx, device );
          _ickDeviceFree( device );
        }

      }

      // Free per session data
      Sfree( psd->uuid );
      Sfree( psd->host );
      Sfree( psd->inBuffer );
      break;

/*------------------------------------------------------------------------*\
    Unknown/unhandled request
\*------------------------------------------------------------------------*/
    default:
      logerr( "_lwsP2pCb: unknown/unhandled reason (%d)", reason );
      break;
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
  Execute a messaging callback
    Message is the raw message including preamble
\*=========================================================================*/
void _ickP2pExecMessageCallback( ickP2pContext_t *ictx, const ickDevice_t *device,
                                 const void *message, size_t mSize )
{
  ickP2pLevel_t        p2pLevel;
  ickP2pServicetype_t  targetServices = ICKP2P_SERVICE_GENERIC;
  ickP2pServicetype_t  sourceService  = ICKP2P_SERVICE_GENERIC;
  ickP2pMessageFlag_t  mFlags         = ICKP2P_MESSAGEFLAG_NONE;
  const unsigned char *payload = message;
  struct _cblist      *walk;

/*------------------------------------------------------------------------*\
  Ignore empty messages
\*------------------------------------------------------------------------*/
  if( mSize==1 && !*payload ) {
    debug( "_ickP2pExecMessageCallback (%p): Heartbeat from %s",
           ictx, device->uuid );
    return;
  }

/*------------------------------------------------------------------------*\
  Interpret preamble and find start of payload
\*------------------------------------------------------------------------*/
  p2pLevel = *payload++;

  if( p2pLevel&ICKP2PLEVEL_TARGETSERVICES )
    targetServices = *payload++;

  if( p2pLevel&ICKP2PLEVEL_SOURCESERVICE )
    sourceService = *payload++;

  if( p2pLevel&ICKP2PLEVEL_MESSAGEFLAGS )
    mFlags = *payload++;

  if( payload-(const unsigned char*)message>mSize ) {
    logerr( "_ickP2pExecMessageCallback: truncated message from \"%s\"", device->uuid );
    return;
  }

  mSize -= payload-(const unsigned char*)message;
  debug( "_ickP2pExecMessageCallback (%p): (%s,0x%02x) -> 0x%02x, %ld bytes",
         ictx, device->uuid, sourceService, targetServices, (long)mSize );

/*------------------------------------------------------------------------*\
   Lock list mutex and execute all registered callbacks
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &ictx->messageCbsMutex );
  for( walk=ictx->messageCbs; walk; walk=walk->next )
    ((ickP2pMessageCb_t)walk->callback)( ictx, device->uuid, sourceService, targetServices, (const char*)payload, mSize, mFlags );
  pthread_mutex_unlock( &ictx->messageCbsMutex );
}


/*=========================================================================*\
  Transmit a message
    this will return the number of bytes left over (call again if>0 )
                     -1 on error
\*=========================================================================*/
static int _ickP2pComTransmit( struct libwebsocket *wsi, ickMessage_t *message )
{
  size_t left;
  int    len;
  int    wmode;

/*------------------------------------------------------------------------*\
    Calculate remaining bytes
\*------------------------------------------------------------------------*/
  left = message->size - message->issued;
  debug( "_ickP2pComTransmit (%p): pptr=%p, issued=%ld size=%ld",
         wsi, message->payload, (long)message->issued, (long)message->size );
  debug( "_ickP2pComTransmit (%p): %ld/%ld bytes done, %ld left",
         wsi, (long)message->issued, (long)message->size, (long)left );

/*------------------------------------------------------------------------*\
    Generate fragments.
\*------------------------------------------------------------------------*/
  wmode =  message->issued ? LWS_WRITE_CONTINUATION : LWS_WRITE_BINARY;
  if( left>1024 ) {
    left = 1024;
    wmode |= LWS_WRITE_NO_FIN;
  }
  debug( "_ickP2pComTransmit (%p): sending %ld bytes, wmode 0x%02x",
         wsi, (long)left, wmode );

/*------------------------------------------------------------------------*\
    Try to send
\*------------------------------------------------------------------------*/
  len = libwebsocket_write( wsi, message->payload+LWS_SEND_BUFFER_PRE_PADDING+message->issued, left, wmode );
  debug( "_ickP2pComTransmit (%p): libwebsocket_write returned %d", wsi, len );
  if( len<0 )
    return -1;

  // fixme: for success len is psize+padding
  if( len>left )
    len = (int)left;

/*------------------------------------------------------------------------*\
    Calculate new write pointer
\*------------------------------------------------------------------------*/
  message->issued += len;

/*------------------------------------------------------------------------*\
    return new leftover
\*------------------------------------------------------------------------*/
  return (int)(message->size - message->issued);
}


#pragma mark -- Timer callbacks


/*=========================================================================*\
  Send heart bet on a LWS connection. This is done to reset the expiration
    timer in case a ws connection exists but SSDP is not routed.
    timer list is already locked as this is a timer callback
\*=========================================================================*/
void _ickHeartbeatTimerCb( const ickTimer_t *timer, void *data, int tag )
{
  ickDevice_t     *device = data;
  ickP2pContext_t *ictx   = device->ictx;

  debug( "_ickDeviceHeartbeatTimerCb: %s", device->uuid );

/*------------------------------------------------------------------------*\
    Queue heartbeat message
\*------------------------------------------------------------------------*/
  _ickP2pSendNullMessage( ictx, device );

}


#pragma mark -- Tools


/*=========================================================================*\
  Get an allocated copy of a web socket protocol header field
\*=========================================================================*/
static char *_ickLwsDupToken( struct libwebsocket *wsi, enum lws_token_indexes h )
{
  int   len;
  char *token;

/*------------------------------------------------------------------------*\
    Get length of field. This is 0 if not found
\*------------------------------------------------------------------------*/
  len = lws_hdr_total_length( wsi, h );
  if( !len )
    return NULL;
  // Trailing zero
  len++;

/*------------------------------------------------------------------------*\
    Allocate space for copy
\*------------------------------------------------------------------------*/
  token = malloc( len );
  if( !token ) {
    logerr( "_ickLwsDupToken: out of memory" );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Copy token and return
\*------------------------------------------------------------------------*/
  lws_hdr_copy( wsi, token, len, h );
  return token;
}


/*=========================================================================*\
  Dump web socket protocol header fields
\*=========================================================================*/
#ifdef ICK_DEBUG
static void _ickLwsDumpHeaders( struct libwebsocket *wsi )
{
  static const char *token_names[WSI_TOKEN_COUNT] = {

    /*[WSI_TOKEN_GET_URI]                =*/ "GET URI",
    /*[WSI_TOKEN_POST_URI]               =*/ "POST URI",
    /*[WSI_TOKEN_HOST]                   =*/ "Host",
    /*[WSI_TOKEN_CONNECTION]             =*/ "Connection",
    /*[WSI_TOKEN_KEY1]                   =*/ "key 1",
    /*[WSI_TOKEN_KEY2]                   =*/ "key 2",
    /*[WSI_TOKEN_PROTOCOL]               =*/ "Protocol",
    /*[WSI_TOKEN_UPGRADE]                =*/ "Upgrade",
    /*[WSI_TOKEN_ORIGIN]                 =*/ "Origin",
    /*[WSI_TOKEN_DRAFT]                  =*/ "Draft",
    /*[WSI_TOKEN_CHALLENGE]              =*/ "Challenge",

    /* new for 04 */
    /*[WSI_TOKEN_KEY]                    =*/ "Key",
    /*[WSI_TOKEN_VERSION]                =*/ "Version",
    /*[WSI_TOKEN_SWORIGIN]               =*/ "Sworigin",

    /* new for 05 */
    /*[WSI_TOKEN_EXTENSIONS]             =*/ "Extensions",

    /* client receives these */
    /*[WSI_TOKEN_ACCEPT]                 =*/ "Accept",
    /*[WSI_TOKEN_NONCE]                  =*/ "Nonce",
    /*[WSI_TOKEN_HTTP]                   =*/ "Http",

    /*[WSI_TOKEN_HTTP_PRAGMA]            =*/ "Http pragma",
    /*[WSI_TOKEN_HTTP_CACHE_CONTROL]     =*/ "Http cache",
    /*[WSI_TOKEN_HTTP_AUTHORIZATION]     =*/ "Http auth",
    /*[WSI_TOKEN_HTTP_COOKIE]            =*/ "Http cookie",
    /*[WSI_TOKEN_HTTP_CONTENT_LENGTH]    =*/ "Http content len",
    /*[WSI_TOKEN_HTTP_CONTENT_TYPE]      =*/ "Http content type",
    /*[WSI_TOKEN_HTTP_DATE]              =*/ "Http date",
    /*[WSI_TOKEN_HTTP_RANGE]             =*/ "Http range",
    /*[WSI_TOKEN_HTTP_REFERER]           =*/ "Http referer",
    /*[WSI_TOKEN_HTTP_URI_ARGS]          =*/ "Http uri args",
    /*[WSI_TOKEN_MUXURL]                 =*/ "MuxURL",

    /* use token storage to stash these */
    /*[_WSI_TOKEN_CLIENT_SENT_PROTOCOLS] =*/ "Sent protocols",
    /*[_WSI_TOKEN_CLIENT_PEER_ADDRESS]   =*/ "Client address",
    /*[_WSI_TOKEN_CLIENT_URI]            =*/ "Client uri",
    /*[_WSI_TOKEN_CLIENT_HOST]           =*/ "Client host",
    /*[_WSI_TOKEN_CLIENT_ORIGIN]         =*/ "Client origin"

  };
  char buf[256];
  int  n;

  for( n=0; n<WSI_TOKEN_COUNT; n++ ) {
    if( !lws_hdr_total_length(wsi,n) )
      continue;
    lws_hdr_copy( wsi, buf, sizeof(buf), n );
    debug( "%-20s: \"%s\"", token_names[n], buf );
  }
}
#endif


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
