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
static int   _ickP2pComTransmit( struct libwebsocket *wsi, ickMessage_t *message );
static char *_ickLwsDupToken( struct libwebsocket *wsi, enum lws_token_indexes h );
static void   dump_handshake_info( struct libwebsocket *wsi );




/*=========================================================================*\
  Send an ickstream message
    dh             - discovery handler
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
    size_t        preambleLen = 2;  // p2pLevel
    size_t        pSize;
    size_t        cSize;
    char         *container;
    int           offset;

/*------------------------------------------------------------------------*\
    Determine preamble length and elements according to cross section of
    device and our local capabilities
\*------------------------------------------------------------------------*/
    p2pLevel = device->ickP2pLevel & ICKP2PLEVEL_SUPPORTED;
    if( p2pLevel&ICKP2PLEVEL_TARGETSERVICES )
      preambleLen ++;
    if( p2pLevel&ICKP2PLEVEL_SOURCESERVICE )
      preambleLen += 1;

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
    offset = LWS_SEND_BUFFER_PRE_PADDING;
    container[offset++] = p2pLevel;

    if( p2pLevel&ICKP2PLEVEL_TARGETSERVICES )
      container[offset++] = (unsigned char)targetServices;

    if( p2pLevel&ICKP2PLEVEL_SOURCESERVICE )
      container[offset++] = (unsigned char)sourceService;

/*  // fixme: not needed since a device has a unique uuid
    if( p2pLevel&ICKP2PLEVEL_TARGETUUID ) {
      strcpy( &message[offset], device->uuid );
      offset += strlen(device->uuid) + 1;
    }

    if( p2pLevel&ICKP2PLEVEL_SOURCEUUID ) {
      strcpy( &message[offset], myUuid );
      offset += strlen(myUuid) + 1;
    }
*/

/*------------------------------------------------------------------------*\
    Copy payload to container and add trailing zero
\*------------------------------------------------------------------------*/
    memcpy( container+LWS_SEND_BUFFER_PRE_PADDING+preambleLen, container, mSize );
    container[ LWS_SEND_BUFFER_PRE_PADDING + preambleLen + mSize ] = 0;

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
  } while( uuid && device );

/*------------------------------------------------------------------------*\
    That's all - unlock device list and break polling in main thread
\*------------------------------------------------------------------------*/
  _ickLibDeviceListUnlock( ictx );
  _ickMainThreadBreak( ictx, 'o' );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return irc;
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
  char              *dscrPath;

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
    Filter connection requests, we use this to figure out the origin device
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
      debug( "_lwsP2pCb %d: client connection established, %d messages pending",
             socket, _ickDevicePendingMessages(psd->device) );

      // Book a write event if a message is already pending
      if( _ickDeviceOutQueue(psd->device) )
        libwebsocket_callback_on_writable( context, wsi );

      break;

/*------------------------------------------------------------------------*\
    An outgoing (client) connection is writable
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_CLIENT_WRITEABLE:
      socket = libwebsocket_get_socket_fd( wsi );
      debug( "_lwsP2pCb %d: client connection is writable", socket );

      // Check wsi
      if( wsi!=psd->device->wsi ) {
        logerr( "_lwsP2pCb %d: wsi mismatch for \"%s\"", socket, psd->device->uuid );
        return -1;
      }

      // There should be a pending message...
      _ickDeviceLock( psd->device );
      message = _ickDeviceOutQueue( psd->device );
      if( !message ) {
        _ickDeviceUnlock( psd->device );
        logerr( "_lwsP2pCb %d: received writable signal with empty out queue for \"%s\"", socket, psd->device->uuid );
        break;
      }

      // Try to transmit the current message
      remainder = _ickP2pComTransmit( wsi, message );

      // Error handling
      if( remainder<0 ) {
        _ickDeviceUnlock( psd->device );
        logerr( "_lwsP2pCb %d: error writing to \"%s\"", socket, psd->device->uuid );
        return -1;
      }

      // If not complete book another write callback
      if( remainder )
        libwebsocket_callback_on_writable( context, wsi );

      // If complete, delete message and chack for a next one
      else {
        _ickDeviceRemoveAndFreeMessage( psd->device, message );
        if( _ickDeviceOutQueue(psd->device) )
          libwebsocket_callback_on_writable( context, wsi );
      }

      // That's it
      _ickDeviceUnlock( psd->device );
      break;

/*------------------------------------------------------------------------*\
    A incoming connection is established
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_ESTABLISHED:
      socket = libwebsocket_get_socket_fd( wsi );
      debug( "_lwsP2pCb %d: server connection established", socket );

      dump_handshake_info( wsi );
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

      // Device already connected
      if( device && device->wsi ) {
        logwarn( "_lwsP2pCb: Connection rejected, wsi present for %s (%s:%d)",
                  device->uuid, device->location, device->wsi, wsi);
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
        psd->device = _ickDeviceNew( psd->uuid, ICKDEVICE_WS );
        if( !psd->device ) {
          logerr( "_lwsP2pCb: out of memory" );
          _ickLibDeviceListUnlock( ictx );
          return 1;
        }
        _ickDeviceSetLocation( psd->device, psd->host );
        psd->device->wsi = wsi;

        // Start retrieval of unpn descriptor, use ickstream root device
        if( asprintf(&dscrPath,"http://%s/%s.xml",psd->host,ICKDEVICE_STRING_ROOT)<0 ) {
          logerr( "_lwsP2pCb: out of memory" );
          _ickLibDeviceListUnlock( ictx );
          return 1;
        }
        wget = _ickWGetInit( ictx, dscrPath, _ickWGetXmlCb, psd->device, &irc );
        Sfree( dscrPath );
        if( !wget ) {
          logerr( "_lwsP2pCb (%s): could not start xml retriever \"%s\" (%s).",
              psd->uuid, psd->host, ickStrError(irc) );
          _ickDeviceFree( psd->device );
          _ickLibDeviceListUnlock( ictx );
          return -1;
        }
        _ickLibWGettersLock( ictx );
        _ickLibWGettersAdd( ictx, wget );
        _ickLibWGettersUnlock( ictx );

        // No expire timer for WS discovered devices

        //Link New device to discovery handler
        _ickLibDeviceAdd( ictx, psd->device );
      }
      _ickLibDeviceListUnlock( ictx );

      break;

/*------------------------------------------------------------------------*\
    A connection was closed
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_CLOSED:
      socket = libwebsocket_get_socket_fd( wsi );
      debug( "_lwsP2pCb %d: connection closed", socket );
      Sfree( psd->uuid );
      Sfree( psd->host );
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
  Transmit a message
    this will return the number of bytes left over (call again if>0 )
                     -1 on error
\*=========================================================================*/
static int _ickP2pComTransmit( struct libwebsocket *wsi, ickMessage_t *message )
{
  size_t left;
  int    len;

/*------------------------------------------------------------------------*\
    Calculate remaining bytes
\*------------------------------------------------------------------------*/
  left = ICKMESSAGE_REMAINDER( message );
  debug( "_ickP2pComTransmit (%p): pptr=%p, wptr=%p (wptr-pptr-pad)=%ld size=%ld",
         wsi, message->payload, message->writeptr,
         message->writeptr - message->payload - LWS_SEND_BUFFER_PRE_PADDING,
         (long)message->size );
  debug( "_ickP2pComTransmit (%p): %ld/%ld bytes",
         wsi, (long)left, (long)message->size );

/*------------------------------------------------------------------------*\
    Try to send
\*------------------------------------------------------------------------*/
  len = libwebsocket_write( wsi, message->writeptr, left, LWS_WRITE_BINARY );
  debug( "_ickP2pComTransmit (%p): libwebsocket_write returned %d", wsi, len );
  if( len<0 )
    return -1;
  if( len>left )
    len = (int)left;

/*------------------------------------------------------------------------*\
    Calculate new write pointer
\*------------------------------------------------------------------------*/
  message->writeptr += len;

/*------------------------------------------------------------------------*\
    return new leftover
\*------------------------------------------------------------------------*/
  return (int)ICKMESSAGE_REMAINDER( message );
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
static void dump_handshake_info( struct libwebsocket *wsi )
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
    debug( "%20s = \"%s\"", token_names[n], buf );
  }
}

/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
