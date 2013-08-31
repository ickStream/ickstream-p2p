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
#include <string.h>
#include <stdlib.h>
#include <libwebsockets.h>

#include "ickP2p.h"
#include "ickP2pInternal.h"
#include "logutils.h"
#include "ickMainThread.h"
#include "ickDescription.h"
#include "ickDevice.h"
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
static void  _ickWriteTimerCb( const ickTimer_t *timer, void *data, int tag );
static void  _ickP2pExecMessageCallback( ickP2pContext_t *ictx, const ickDevice_t *device,
                                         const void *message, size_t mSize );
static int   _ickP2pComTransmit( struct libwebsocket *wsi, ickMessage_t *message );
static char *_ickLwsDupToken( struct libwebsocket *wsi, enum lws_token_indexes h );
static void  _ickLwsDumpHeaders( struct libwebsocket *wsi );




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
  ickErrcode_t  irc = ICKERR_SUCCESS;
  ickDevice_t  *device;
// const char   *myUuid = ickP2pGetDeviceUuid();

/*------------------------------------------------------------------------*\
    Determine size if payload is a string
\*------------------------------------------------------------------------*/
  if( !mSize )
    mSize = strlen( message );

  debug( "ickP2pSendMsg: target=\"%s\" targetServices=0x%02x sourceServices=0x%02x size=%ld",
         uuid?uuid:"<Notification>", targetServices, sourceService, (long)mSize );

/*------------------------------------------------------------------------*\
    Lock device list and find (first) device
\*------------------------------------------------------------------------*/
  _ickLibDeviceListLock( ictx );
  if( uuid ) {
    device = _ickLibDeviceFindByUuid( ictx, uuid );
    if( !device ) {
      _ickLibDeviceListUnlock( ictx );
      return ICKERR_NODEVICE;
    }
  }
  else if( ictx->deviceList )
    device = ictx->deviceList;
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
    Determine preamble length and elements according to cross section of
    device and our local capabilities
\*------------------------------------------------------------------------*/
    p2pLevel = device->ickP2pLevel & ICKP2PLEVEL_SUPPORTED;
    if( p2pLevel&ICKP2PLEVEL_TARGETSERVICES )
      preambleLen++;
    if( p2pLevel&ICKP2PLEVEL_SOURCESERVICE )
      preambleLen++;

/*  // fixme: not needed since a device has a unique uuid
    if( p2pLevel&ICKP2PLEVEL_TARGETUUID )
      preambleLen += strlen( device->uuid ) + 1;
    if( p2pLevel&ICKP2PLEVEL_SOURCEUUID )
      preambleLen += strlen( ickP2pGetDeviceUuid(dh->icklib) ) + 1;
*/

/*------------------------------------------------------------------------*\
    Allocate payload container, include LWS padding and trailing zero
\*------------------------------------------------------------------------*/
    pSize = preambleLen + mSize + 1;
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

/*  // fixme: not needed since a device has a unique uuid
    if( p2pLevel&ICKP2PLEVEL_TARGETUUID ) {
      strcpy( ptr, device->uuid );
      ptr += strlen(device->uuid) + 1;
    }

    if( p2pLevel&ICKP2PLEVEL_SOURCEUUID ) {
      strcpy( ptr, myUuid );
      ptr += strlen(myUuid) + 1;
    }
*/

/*------------------------------------------------------------------------*\
    Copy payload to container and add trailing zero
\*------------------------------------------------------------------------*/
    memcpy( ptr, message, mSize );
    ptr[ mSize ] = 0;

/*------------------------------------------------------------------------*\
    Try queue message for transmission
\*------------------------------------------------------------------------*/
    _ickDeviceLock( device );
    irc = _ickDeviceAddMessage( device, container, pSize );
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
    else
      debug( "ickP2pSendMsg (%s): sending deferred, wsi not yet present (%ld bytes).",
             device->uuid, (long)mSize );

/*------------------------------------------------------------------------*\
    Handle next device in notification mode
\*------------------------------------------------------------------------*/
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

/*------------------------------------------------------------------------*\
    Allocate payload container, include LWS padding and trailing zero
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
    Try queue message for transmission
\*------------------------------------------------------------------------*/
  irc = _ickDeviceAddMessage( device, container, 1 );
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
  Initiate web socket connection to a device
    caller should lock the device,
    this must be called from the main thread
\*=========================================================================*/
ickErrcode_t _ickWebSocketOpen( struct libwebsocket_context *context, ickDevice_t *device )
{
  ickP2pContext_t     *ictx = device->ictx;
  ickErrcode_t         irc = ICKERR_SUCCESS;
  char                *address;
  char                *host;
  int                  port;
  char                *ptr;
  _ickLwsP2pData_t    *psd;

  debug( "_ickWebSocketOpen: \"%s\"", device->uuid );

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
    Construct local address
\*------------------------------------------------------------------------*/
  if( asprintf(&host,"%s:%d",ictx->hostName,ictx->lwsPort)<0 ) {
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
  device->wsi = libwebsocket_client_connect_extended(
                    context,
                    address,
                    port,
                    0,                        // ssl_connection
                    "/",                      // path
                    host,                     // host
                    device->ictx->deviceUuid, // origin,
                    ICKP2P_WS_PROTOCOLNAME,   // protocol,
                    -1,                       // ietf_version_or_minus_one
                    psd );
  if( !device->wsi ) {
    logerr( "_ickWebSocketOpen (%s): Could not create lws client socket.",
            device->uuid );
    irc = ICKERR_LWSERR;
  }

/*------------------------------------------------------------------------*\
    Clean up, that's all
\*------------------------------------------------------------------------*/
  Sfree( address );
  Sfree( host );
  return irc;
}


/*=========================================================================*\
  Terminate a web socket connection to a device
    caller should lock the device,
    this must be called from the main thread
\*=========================================================================*/
ickErrcode_t _ickWebSocketClose( struct libwebsocket_context *context, ickDevice_t *device )
{
  debug( "_ickWebSocketClose: \"%s\"", device->uuid );

/*------------------------------------------------------------------------*\
    Check status
\*------------------------------------------------------------------------*/
  if( !device->wsi ) {
    logerr( "_ickWebSocketClose (%s): not connected", device->uuid );
    return ICKERR_NOTCONNECTED;
  }

/*------------------------------------------------------------------------*\
    Close connection
\*------------------------------------------------------------------------*/
  //libwebsocket_close_and_free_session(context, device->wsi, LWS_CLOSE_STATUS_NORMAL);
  device->wsi = NULL;

/*------------------------------------------------------------------------*\
    Delete unsent messages
\*------------------------------------------------------------------------*/
  if( device->outQueue ) {
    ickMessage_t *msg, *next;
    int           num;

    //Loop over output queue, free and count entries
    for( num=0,msg=device->outQueue; msg; msg=next ) {
      next = msg->next;
      Sfree( msg->payload );
      Sfree( msg )
      num++;
    }
    device->outQueue = NULL;
    loginfo( "_ickDeviceFree: Deleted %d unsent messages.", num );
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
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
  ickTimer_t        *timer;

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
      logwarn( "_lwsP2pCb %d: connection error", socket );

      // fixme: mark device descriptor
      break;

/*------------------------------------------------------------------------*\
   Filter incomming request
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
      socket = libwebsocket_get_socket_fd( wsi );
      debug( "_lwsP2pCb %d: filter protocol connection", socket );
      break;

/*------------------------------------------------------------------------*\
    Filter connection requests
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH:
      socket = libwebsocket_get_socket_fd( wsi );
      debug( "_lwsP2pCb %d: client filter pre-establish", socket );
      break;

/*------------------------------------------------------------------------*\
    A outgoing (client) connection is established
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
      socket = libwebsocket_get_socket_fd( wsi );
      device = psd->device;
      debug( "_lwsP2pCb %d: client connection established, %d messages pending",
             socket, _ickDevicePendingMessages(device) );

      // Create heartbeat timer on LWS layer (fallback if no SSDP connection exists)
      _ickTimerListLock( ictx );
      if( !_ickTimerFind(ictx,_ickDeviceHeartbeatTimerCb,device,0) ) {
        ickErrcode_t irc;
        irc = _ickTimerAdd( ictx, device->lifetime*1000, 0, _ickDeviceHeartbeatTimerCb, device, 0 );
        if( irc )
          logerr( "_lwsP2pCb (%s): could not create heartbeat timer (%s)",
                  psd->uuid, ickStrError(irc) );
      }
      _ickTimerListUnlock( ictx );

      // Book a write event if a message is already pending
      if( _ickDeviceOutQueue(device) )
        libwebsocket_callback_on_writable( context, wsi );

      // Timestamp for connection
      device->tConnect = _ickTimeNow();

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

      // Get host
      psd->host = _ickLwsDupToken( wsi, WSI_TOKEN_HOST );
      if( !psd->host ) {
        logwarn( "_lwsP2pCb: Incoming connection rejected (no HOST).");
        return 1;
      }

      // Get origin
      psd->uuid = _ickLwsDupToken( wsi, WSI_TOKEN_ORIGIN );
      if( !psd->uuid ) {
        logwarn( "_lwsP2pCb: Incoming connection rejected (no UUID).");
        return 1;
      }

      // Lock device list and try to find device
      _ickLibDeviceListLock( ictx );
      device = _ickLibDeviceFindByUuid( ictx , psd->uuid );

      // Device already handled as connected or connecting client by openwebsocket?
      if( device /*&& device->wsi*/ ) {
        debug( "_lwsP2pCb: Connection rejected, using present wsi for %s (%s:%d)",
                  device->uuid, device->location, device->wsi, wsi);
        psd->device = device;
        _ickLibDeviceListUnlock( ictx );
        return 1;
      }

      // Device unknown (i.e. was not (yet) discovered by SSDP)
      if( !device ) {
        ickWGetContext_t *wget;
        ickErrcode_t      irc;

        // Need client hostname for device location
        if( !psd->host ) {
          logwarn( "_lwsP2pCb: Incoming connection rejected (no HOST).");
          _ickLibDeviceListUnlock( ictx );
          return 1;
        }
        debug( "_lwsP2pCb (%s): Discovered new device via incoming ws connection from \"%s\".",
               psd->uuid, psd->host );

        // Create and init device
        device = _ickDeviceNew( psd->uuid, ICKDEVICE_WS );
        if( !device ) {
          logerr( "_lwsP2pCb: out of memory" );
          _ickLibDeviceListUnlock( ictx );
          return 1;
        }
        _ickDeviceSetLocation( device, psd->host );
        psd->device = device;
        device->wsi = wsi;

        // Start retrieval of unpn descriptor, use ickstream root device
        if( asprintf(&dscrPath,"http://%s/%s.xml",psd->host,ICKDEVICE_STRING_ROOT)<0 ) {
          logerr( "_lwsP2pCb: out of memory" );
          _ickLibDeviceListUnlock( ictx );
          return 1;
        }
        wget = _ickWGetInit( ictx, dscrPath, _ickWGetXmlCb, device, &irc );
        Sfree( dscrPath );
        if( !wget ) {
          logerr( "_lwsP2pCb (%s): could not start xml retriever \"%s\" (%s).",
              psd->uuid, psd->host, ickStrError(irc) );
          _ickDeviceFree( device );
          _ickLibDeviceListUnlock( ictx );
          return -1;
        }
        _ickLibWGettersLock( ictx );
        _ickLibWGettersAdd( ictx, wget );
        _ickLibWGettersUnlock( ictx );

        // Expiration and heartbeat timer for WS discovered devices will be created in _ickWGetXmlCb()
        // fixme: there might be lws messages received before the xml file is interpreted, ...

        //Link New device to discovery handler
        _ickLibDeviceAdd( ictx, device );
      }
      _ickLibDeviceListUnlock( ictx );

      // We are server, set connection timestamp
      device->localIsServer = 1;
      device->tConnect      = _ickTimeNow();

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

      // Check wsi
      if( wsi!=device->wsi ) {
        logerr( "_lwsP2pCb %d: wsi mismatch for \"%s\"", socket, device->uuid );
        // fixme: mark device descriptor
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

      // Check if protocol level is already known. This might not the case for incoming LWS connections
      // when the XML file was not yet received or interpreted. Then delay the sending of messages as
      // we cannot construct a preamble. Recheck in 100 ms
      if( device->ickP2pLevel==ICKP2PLEVEL_GENERIC ) {
        _ickTimerListLock( ictx );
        if( !_ickTimerFind(ictx,_ickWriteTimerCb,device,0) ) {
          ickErrcode_t irc;
          irc = _ickTimerAdd( ictx, 100, 1, _ickWriteTimerCb, device, 0 );
          if( irc )
            logerr( "_lwsP2pCb (%s): could not create write timer (%s)",
                    psd->uuid, ickStrError(irc) );
        }
        _ickTimerListUnlock( ictx );
        _ickDeviceUnlock( device );
        break;
      }

      // Try to transmit the current message
      remainder = _ickP2pComTransmit( wsi, message );

      // Error handling
      if( remainder<0 ) {
        logerr( "_lwsP2pCb %d: error writing to \"%s\"", socket, device->uuid );
        // fixme: callback and mark device descriptor
        _ickDeviceRemoveAndFreeMessage( device, message );
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

      // If complete, delete message and chack for a next one
      else {
        _ickDeviceRemoveAndFreeMessage( device, message );
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

      debug( "_lwsP2pCb %d: %s ready to receive (%ld bytes, %ld remain, %s)", socket,
             reason==LWS_CALLBACK_CLIENT_RECEIVE?"client":"server",
             (long)len, (long)rlen, final?"final":"to be continued" );

      // reset timer
      timer = _ickTimerFind( ictx, _ickDeviceExpireTimerCb, device, 0 );
      if( timer )
        _ickTimerUpdate( ictx, timer, device->lifetime*1000, 1 );
      else
        logerr( "_lwsP2pCb (%s): could not find expiration timer.", psd->uuid );

      // Set timestamp of last (partial) receive
      device->tLastRx = _ickTimeNow();

      // No fragmentation? Execute callbacks directly
      if( !psd->inBuffer && !rlen && final) {
        _ickP2pExecMessageCallback( ictx, device, in, len );
        device->nRx++;
        break;
      }

      // Collecting chunks: (Re)allocate message buffer for new total size
      ptr = realloc( psd->inBuffer, psd->inBufferSize+len );
      if( !ptr ) {
        logerr( "_lwsP2pCb: out of memory" );
        // fixme: mark device descriptor
        return -1;
      }
      psd->inBuffer  = ptr;

      // Append data to buffer
      memcpy( psd->inBuffer + psd->inBufferSize, in, len );
      psd->inBufferSize += len;

      // If complete: execute callbacks and clean up
      if( !rlen && final ) {
        _ickP2pExecMessageCallback( ictx, device, psd->inBuffer, psd->inBufferSize );
        device->nRxSegmented++;
        Sfree( psd->inBuffer );
        psd->inBufferSize = 0;
      }

      break;

/*------------------------------------------------------------------------*\
    A connection was closed
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_CLOSED:
      socket = libwebsocket_get_socket_fd( wsi );
      device = psd->device;
      debug( "_lwsP2pCb %d: connection closed", socket );

      // Remove heartbeat and delayed write handler for this device
      _ickTimerDeleteAll( ictx, _ickDeviceHeartbeatTimerCb, device, 0 );
      _ickTimerDeleteAll( ictx, _ickWriteTimerCb, device, 0 );

      //fixme: mark devices descriptor
      if( device ) {
        device->wsi = NULL;
        device->tDisconnect = _ickTimeNow();
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
  Execute a timed write request for an wsi
\*=========================================================================*/
static void _ickWriteTimerCb( const ickTimer_t *timer, void *data, int tag )
{
  const ickDevice_t *device = data;
  debug( "_ickWriteTimerCb: \"%s\"", device->uuid );

  libwebsocket_callback_on_writable( device->ictx->lwsContext, device->wsi );
}


/*=========================================================================*\
  Execute a messaging callback
    Message is the raw message including preamble
\*=========================================================================*/
static void _ickP2pExecMessageCallback( ickP2pContext_t *ictx, const ickDevice_t *device,
                                        const void *message, size_t mSize )
{
  ickP2pLevel_t        p2pLevel;
  ickP2pServicetype_t  targetServices = ICKP2P_SERVICE_GENERIC;
  ickP2pServicetype_t  sourceService  = ICKP2P_SERVICE_GENERIC;
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

  // fixme: not needed since a device has a unique uuid
  if( p2pLevel&ICKP2PLEVEL_TARGETUUID ) {
    logwarn( "_ickP2pExecMessageCallback: found targetuuid \"%s\" in message from \"%s\"", payload, device->uuid );
    message = strchr((const char*)payload,0) + 1;
  }
  if( p2pLevel&ICKP2PLEVEL_SOURCEUUID ) {
    logwarn( "_ickP2pExecMessageCallback: found sourceuuid \"%s\" in message from \"%s\"", payload, device->uuid );
    message = strchr((const char*)payload,0) + 1;
  }

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
    ((ickP2pMessageCb_t)walk->callback)( ictx, device->uuid, sourceService, targetServices, (const char*)payload, mSize );
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
