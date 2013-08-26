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
static char *_ickLwsDupToken( struct libwebsocket *wsi, enum lws_token_indexes h );
static void dump_handshake_info( struct libwebsocket *wsi );




/*=========================================================================*\
  Send an ickstream message
    dh             - discovery handler
    uuid           - uuid of target, if NULL all known ickstream devices are
                     addressed
    targetServices - services at target to address
    sourceService  - sending service
    payload        - the message
    pSize          - size of message, if 0 the message is interpreted
                     as a 0-terminated string
\*=========================================================================*/
ickErrcode_t ickP2pSendMsg( ickP2pContext_t *ictx, const char *uuid,
                            ickP2pServicetype_t targetServices, ickP2pServicetype_t sourceService,
                            const char *payload, size_t pSize )
{
  ickErrcode_t  irc = ICKERR_SUCCESS;
  ickDevice_t  *device;
// const char   *myUuid = ickP2pGetDeviceUuid();

/*------------------------------------------------------------------------*\
    Determine size if payload is a string
\*------------------------------------------------------------------------*/
  if( !pSize )
    pSize = strlen( payload );

  debug( "ickP2pSendMsg: target=\"%s\" targetServices=0x%02x sourceServices=0x%02x size=%ld",
         uuid?uuid:"<Notification>", targetServices, sourceService, (long)pSize );

/*------------------------------------------------------------------------*\
    Lock device list and find device
\*------------------------------------------------------------------------*/
  _ickLibDeviceListLock( ictx );
   device = uuid ? _ickLibDeviceFind(ictx,uuid) : ictx->deviceList ;
  if( !device ) {
    _ickLibDeviceListUnlock( ictx );
    return ICKERR_NODEVICE;
  }

/*------------------------------------------------------------------------*\
    Loop over all devices
\*------------------------------------------------------------------------*/
  do {
    ickP2pLevel_t p2pLevel;
    size_t        preambleLen = 2;  // p2pLevel
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
    cSize = LWS_SEND_BUFFER_PRE_PADDING + preambleLen + pSize + 1 + LWS_SEND_BUFFER_POST_PADDING;
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
    memcpy( container+LWS_SEND_BUFFER_PRE_PADDING+preambleLen, container, pSize );
    container[ LWS_SEND_BUFFER_PRE_PADDING + preambleLen + pSize ] = 0;

/*------------------------------------------------------------------------*\
    Try queue message for transmission
\*------------------------------------------------------------------------*/
    _ickDeviceLock( device );
    irc = _ickDeviceAddMessage( device, container, cSize );
    _ickDeviceUnlock( device );
    if( irc ) {
      Sfree( container );
      break;
    }

/*------------------------------------------------------------------------*\
    Handle next device in notification mode
\*------------------------------------------------------------------------*/
    device = device->next;
  } while( uuid && device );

/*------------------------------------------------------------------------*\
    That's all - unlock device list
\*------------------------------------------------------------------------*/
  _ickLibDeviceListUnlock( ictx );
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
  struct libwebsocket *wsi;

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
  wsi = libwebsocket_client_connect_extended(
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
  if( !wsi ) {
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
    A outgoing connection is established
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
      socket = libwebsocket_get_socket_fd( wsi );
      debug( "_lwsP2pCb %d: client connection established", socket );
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
      device = _ickLibDeviceFind( ictx , psd->uuid );

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
