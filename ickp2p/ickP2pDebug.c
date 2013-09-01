/*$*********************************************************************\

Source File     : ickP2pDebug.c

Description     : implement ickp2p debugging functions

Comments        : -

Called by       : API wrapper functions, libwebsocket

Calls           : -

Date            : 31.08.2013

Updates         : -

Author          : JS, //MAF

Remarks         : Library needs to be compiled with ICK_P2PENABLEDEBUGAPI
                  to use theses functions

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

#include "miniwget.h"

#include "ickP2p.h"
#include "ickP2pInternal.h"
#include "logutils.h"
#include "ickDevice.h"
#include "ickDescription.h"
#include "ickP2pCom.h"
#include "ickP2pDebug.h"

#ifdef ICK_P2PENABLEDEBUGAPI
#include <jansson.h>
#endif


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
#ifdef ICK_P2PENABLEDEBUGAPI
static json_t *_ickP2pGetLocalDebugInfoForContext( ickP2pContext_t *ictx );
static json_t *_ickContextStateJson( ickP2pContext_t *ictx );
static json_t *_ickDeviceStateJson( ickDevice_t *device );
// static json_t *_ickWsiStateJson( _ickLwsP2pData_t *psd );
static json_t *_ickMessageStateJson( ickMessage_t *message );
#endif



/*=========================================================================*\
  Permit or deny access for remote debugging via API or HTTP
\*=========================================================================*/
ickErrcode_t ickP2pRemoteDebugApi( ickP2pContext_t *ictx, int enable )
{
  debug( "ickP2pRemoteDebugApi (%p): %s", ictx, enable?"enable":"disable" );

#ifndef ICK_P2PENABLEDEBUGAPI
  logwarn( "ickP2pRemoteDebugApi: p2plib not compiled with debugging API support." );
  return ICKERR_NOTIMPLEMENTED;
#else
  ictx->debugApiEnabled = enable;
#endif

  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Get debug info from a context for a device registered there
    ictx       - the ickstream context
    uuid       - the device of interest or NULL for complete debugging info
                 of the context
    the returned string is allocated and must be freed by caller
    returns NULL on error or if debugging is not enabled
\*=========================================================================*/
char *ickP2pGetLocalDebugInfo( ickP2pContext_t *ictx, const char *uuid )
{
  char *result = NULL;
  debug( "ickP2pGetLocalDebugInfoForDevice (%p): \"%s\"", ictx, uuid );

#ifndef ICK_P2PENABLEDEBUGAPI
  logwarn( "ickP2pGetLocalDebugInfoForDevice: p2plib not compiled with debugging API support." );
#else
  ickDevice_t *device;
  json_t      *jDebugInfo;

/*------------------------------------------------------------------------*\
    Get complete context info?
\*------------------------------------------------------------------------*/
  if( !uuid )
    jDebugInfo = _ickP2pGetLocalDebugInfoForContext( ictx );

/*------------------------------------------------------------------------*\
    Get info for specific device identified by UUID
\*------------------------------------------------------------------------*/
  else {

    // Lock device list and try to find device
    _ickLibDeviceListLock( ictx );
    device = _ickLibDeviceFindByUuid( ictx, uuid );
    if( !device ) {
      logwarn( "ickP2pGetLocalDebugInfoForDevice: no such device (%s)", uuid );
      _ickLibDeviceListUnlock( ictx );
      return NULL;
    }

    // Get debug info as JSON container
    jDebugInfo = _ickDeviceStateJson( device );
    if( !jDebugInfo ) {
      _ickLibDeviceListUnlock( ictx );
      return NULL;
    }

    // Unlock device list
    _ickLibDeviceListUnlock( ictx );
  }

/*------------------------------------------------------------------------*\
    Convert JSON result to string
\*------------------------------------------------------------------------*/
  result = json_dumps( jDebugInfo, JSON_PRESERVE_ORDER|JSON_INDENT(2) );
  json_decref( jDebugInfo );
  if( !result )
    logerr( "ickP2pGetLocalDebugInfoForDevice: out of memory or JSON error", uuid );

#endif

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return result;
}


/*=========================================================================*\
  Get debug info from a remote device for a device registered there
    ictx       - the ickstream context
    remoteUuid - the remote device to be queried
    uuid       - the device of interest or NULL for complete debugging info
                 of remoteUuid
    this will engage a HTTP connection and block until the result was received
    the returned string is allocated and must be freed by caller
    returns NULL on error
\*=========================================================================*/
char *ickP2pGetRemoteDebugInfo( ickP2pContext_t *ictx, const char *remoteUuid, const char *uuid )
{
  ickDevice_t *device;
  char        *ptr;
  char        *url;
  int          rc;
  int          size;
  void        *data;
  char        *result;
  debug( "ickP2pGetRemoteDebugInfo (%p): remote=%s device=%s", ictx, remoteUuid, uuid );

/*------------------------------------------------------------------------*\
    Lock device list and try to find device
\*------------------------------------------------------------------------*/
  _ickLibDeviceListLock( ictx );
  device = _ickLibDeviceFindByUuid( ictx, remoteUuid );
  if( !device ) {
    logwarn( "ickP2pGetRemoteDebugInfo: no such device (%s)", remoteUuid );
    _ickLibDeviceListUnlock( ictx );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Get root for target url
\*------------------------------------------------------------------------*/
  if( strncmp(device->location,"http://",strlen("http://")) ) {
    logerr( "ickP2pGetRemoteDebugInfo (%s): Bad location URI \"%s\"", device->uuid, device->location );
    _ickLibDeviceListUnlock( ictx );
    return NULL;
  }
  ptr = strchr( device->location+strlen("http://"), '/' );
  if( !ptr ) {
    logerr( "ickP2pGetRemoteDebugInfo (%s): Bad location URI \"%s\"", device->uuid, device->location );
    _ickLibDeviceListUnlock( ictx );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Construct target url
\*------------------------------------------------------------------------*/
  if( !uuid )
    rc = asprintf( &url, "%.*s%s", ptr-device->location, device->location, ICK_P2PDEBUGURI );
  else
    rc = asprintf( &url, "%.*s%s/%s", ptr-device->location, device->location, ICK_P2PDEBUGURI, uuid );
  _ickLibDeviceListUnlock( ictx );
  if( rc<0 ) {
    logerr( "ickP2pGetRemoteDebugInfo: out of memory" );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Get data - this will block
\*------------------------------------------------------------------------*/
  data = miniwget( url, &size, 5 );
  if( size<0 ) {
    logerr( "ickP2pGetRemoteDebugInfo (%s): could not get data", url );
    Sfree( url );
    return NULL;
  }
  Sfree( url );

/*------------------------------------------------------------------------*\
    Terminate string
\*------------------------------------------------------------------------*/
  result = strndup( data, size );
  if( !result ) {
    Sfree( data );
    logerr( "ickP2pGetRemoteDebugInfo: out of memory" );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
   return result;
}


#ifdef ICK_P2PENABLEDEBUGAPI

/*=========================================================================*\
  Get HTTP debug info
    this includes a corresponding HTTP header
    will lock the device list
    returns an allocated string (caller must free) or NULL on error
\*=========================================================================*/
char *_ickP2pGetDebugFile( ickP2pContext_t *ictx, const char *uri )
{
  const char  *uuid = NULL;
  int          dlen, hlen;
  char        *debugContent = NULL;
  char        *message;
  char         header[512];

  debug( "_ickP2pGetDebugFile (%p): \"%s\"", ictx, uri );

/*------------------------------------------------------------------------*\
    Get device info from path
\*------------------------------------------------------------------------*/
  if( strlen(uri)>strlen(ICK_P2PDEBUGURI) ) {
    uuid = uri + strlen(ICK_P2PDEBUGURI);
    if( *uuid!='/' )
      return strdup( HTTP_404 );
    uuid++;
  }

/*------------------------------------------------------------------------*\
    Get debug info
\*------------------------------------------------------------------------*/
  debugContent = ickP2pGetLocalDebugInfo( ictx, uuid );

/*------------------------------------------------------------------------*\
    Not found or error?
\*------------------------------------------------------------------------*/
  if( !debugContent )
    return strdup( HTTP_404 );

/*------------------------------------------------------------------------*\
  Construct header
\*------------------------------------------------------------------------*/
  dlen = strlen( debugContent);
  hlen = sprintf( header, HTTP_200, "application/json", (long)dlen );

/*------------------------------------------------------------------------*\
  Merge header and payload
\*------------------------------------------------------------------------*/
  message = malloc( hlen+dlen+1 );
  if( !message ) {
    Sfree( debugContent );
    logerr( "_ickP2pGetDebugFile: out of memory" );
    return NULL;
  }
  strcpy( message, header );
  strcpy( message+hlen, debugContent );
  Sfree( debugContent );

/*------------------------------------------------------------------------*\
  That's all
\*------------------------------------------------------------------------*/
  return message;
}


/*=========================================================================*\
  Get debug info for a context
    the returned string is allocated and must be freed by caller
    returns NULL on error or if debugging is not enabled
\*=========================================================================*/
static json_t *_ickP2pGetLocalDebugInfoForContext( ickP2pContext_t *ictx )
{
  ickDevice_t *device;
  json_t      *jResult;
  json_t      *jDeviceArray;
  debug( "_ickP2pGetLocalDebugInfoForContext (%p): ", ictx );

/*------------------------------------------------------------------------*\
    Get context info
\*------------------------------------------------------------------------*/
  jResult = _ickContextStateJson( ictx );
  if( !jResult )
    return NULL;

/*------------------------------------------------------------------------*\
    Allocate array for device infos
\*------------------------------------------------------------------------*/
  jDeviceArray = json_array();
  if( !jDeviceArray ) {
    logerr( "_ickP2pGetLocalDebugInfoForContext: out of memory or JSON error" );
    json_decref( jResult );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Lock device list and loop over all devices
\*------------------------------------------------------------------------*/
  _ickLibDeviceListLock( ictx );
  for( device=ictx->deviceList; device; device=device->next ) {
    json_t *jDeviceInfo;

    // Get debug info
    jDeviceInfo = _ickDeviceStateJson( device );
    if( !jDeviceInfo )
      continue;

    // Add to list
    if( json_array_append_new(jDeviceArray,jDeviceInfo) ) {
      logerr( "_ickP2pGetLocalDebugInfoForContext: out of memory or JSON error" );
      continue;
    }
  }

/*------------------------------------------------------------------------*\
    Unlock device list
\*------------------------------------------------------------------------*/
  _ickLibDeviceListUnlock( ictx );

/*------------------------------------------------------------------------*\
    Append device info to context info
\*------------------------------------------------------------------------*/
  if( json_object_set_new(jResult,"devices",jDeviceArray) ) {
    logerr( "_ickP2pGetLocalDebugInfoForContext: out of memory or JSON error" );
    json_decref( jResult );
    json_decref( jDeviceArray );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return jResult;
}


/*=========================================================================*\
  Get context info as JSON container
    this will not include the device list
    returns NULL on error
\*=========================================================================*/
static json_t *_ickContextStateJson( ickP2pContext_t *ictx )
{
  json_t *jResult;
  debug( "_ickContextStateJson (%p): %s", ictx->deviceUuid );

/*------------------------------------------------------------------------*\
    Compile all debug info
\*------------------------------------------------------------------------*/
  jResult = json_pack( "{ss ss si ss si si so si si sb sb sf sf ss ss ss ss ss si ss si si}",
                       "uuid",                 ictx->deviceUuid,
                       "name",                 ictx->deviceName,
                       "services",        (int)ictx->ickServices,
                       "hostname",             ictx->hostName,
                       "upnpPort",             ictx->upnpPort,
                       "wsPort",               ictx->lwsPort,
                       "folder",               ictx->upnpFolder?json_string(ictx->upnpFolder):json_null(),
                       "lifetime",             ictx->lifetime,
                       "state",                ictx->state,
                       "loopback",             ictx->upnpLoopback,
                       "customConnectMatrix",  ictx->lwsConnectMatrixCb==ickP2pDefaultConnectMatrixCb,
                       "tCreation",            ictx->tCreation,
                       "tResume",              ictx->tResume,
                       "mainInterface",        ictx->interface,
                       "locationRoot",         ictx->locationRoot,
                       "osName",               ictx->osName,
                       "p2pVersion",           ickP2pGetVersion( NULL, NULL ),
                       "gitVersion",           ickP2pGitVersion(),
                       "p2pLevel",        (int)ICKP2PLEVEL_SUPPORTED,
                       "lwsVersion",           lws_get_library_version(),
                       "bootId",          (int)ictx->upnpBootId,
                       "configId",        (int)ictx->upnpConfigId
                     );

/*------------------------------------------------------------------------*\
    Error
\*------------------------------------------------------------------------*/
  if( !jResult )
    logerr( "_ickContextStateJson: out of memory or JSON error" );

/*------------------------------------------------------------------------*\
    Return result
\*------------------------------------------------------------------------*/
  return jResult;
}

/*=========================================================================*\
  Get device info as JSON container
    caller should lock the device
    returns NULL on error
\*=========================================================================*/
static json_t *_ickDeviceStateJson( ickDevice_t *device )
{
  json_t *jResult;
  json_t *jWsi;
  json_t *jMessage;
  debug( "_ickDeviceStateJson: %s", device->uuid );

/*------------------------------------------------------------------------*\
    Create websocket information (if any)
\*------------------------------------------------------------------------*/
  /* fixme
  psd  = // no way to get user data from a wsi outside a lws callback scope
  jWsi = _ickWsiStateJson( psd );
  */
  jWsi = device->wsi ? json_true() : json_false();
  if( !jWsi ) {
    logerr( "_ickDeviceStateJson: out of memory or JSON error" );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Create message information (if any)
\*------------------------------------------------------------------------*/
  jMessage = _ickMessageStateJson( device->outQueue );
  if( !jMessage ) {
    logerr( "_ickDeviceStateJson: out of memory or JSON error" );
    json_decref( jWsi );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Compile all debug info
\*------------------------------------------------------------------------*/
  jResult = json_pack( "{ss si sf ss ss si si si si sf sb sf sf sb si si si si sf sf sb so}",
                       "name",            device->friendlyName,
                       "type",            device->type,
                       "tCreation",       device->tCreation,
                       "UUID",            device->uuid,
                       "location",        device->location,
                       "upnpVersion",     device->ickUpnpVersion,
                       "p2pLevel",        device->ickP2pLevel,
                       "lifetime",        device->lifetime,
                       "services",        device->services,
                       "tXmlComplete",    device->tXmlComplete,
                       "doConnect",       device->doConnect,
                       "tConnect",        device->tConnect,
                       "tDisconnect",     device->tDisconnect,
                       "localIsServer",   device->localIsServer,
                       "rx",              device->nRx,
                       "rxSegmented",     device->nRxSegmented,
                       "tx",              device->nTx,
                       "txPending",       _ickDevicePendingMessages(device),
                       "rxLast",          device->tLastRx,
                       "txLast",          device->tLastTx,
                       "wsi",             jWsi,
                       "message",         jMessage );

/*------------------------------------------------------------------------*\
    Error
\*------------------------------------------------------------------------*/
  if( !jResult ) {
    logerr( "_ickDeviceStateJson: out of memory or JSON error" );
    json_decref( jWsi );
    json_decref( jMessage );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Return result
\*------------------------------------------------------------------------*/
  return jResult;
}


/*=========================================================================*\
  Get wsi info as JSON container
    caller should lock the device
    returns NULL on error
\*=========================================================================*/
#if 0
static json_t *_ickWsiStateJson( _ickLwsP2pData_t *psd )
{
  json_t           *jResult;
  debug( "_ickWsiStateJson (%p)", psd );

/*------------------------------------------------------------------------*\
    Empty data?
\*------------------------------------------------------------------------*/
  if( !psd )
    return json_null();

/*------------------------------------------------------------------------*\
    Compile wsi debug info
\*------------------------------------------------------------------------*/
  jResult = json_pack( "{ss ss si}",
      "UUID",            psd->uuid,
      "host",            psd->host,
      "inBufferSize",    (int)psd->inBufferSize );

/*------------------------------------------------------------------------*\
    Error
\*------------------------------------------------------------------------*/
  if( !jResult )
    logerr( "_ickWsiStateJson: out of memory or JSON error" );

/*------------------------------------------------------------------------*\
    Return result
\*------------------------------------------------------------------------*/
  return jResult;
}
#endif

/*=========================================================================*\
  Get message info as JSON container
    caller should lock the device
    returns NULL on error
\*=========================================================================*/
static json_t *_ickMessageStateJson( ickMessage_t *message )
{
  json_t *jResult;
  debug( "_ickMessageStateJson (%p): ", message );

/*------------------------------------------------------------------------*\
    Empty data?
\*------------------------------------------------------------------------*/
  if( !message )
    return json_null();

/*------------------------------------------------------------------------*\
    Compile message debug info
\*------------------------------------------------------------------------*/
  jResult = json_pack( "{sf si si}",
      "tCreated",        message->tCreated,
      "size",       (int)message->size,
      "issued",     (int)message->issued );

/*------------------------------------------------------------------------*\
    Error
\*------------------------------------------------------------------------*/
  if( !jResult )
    logerr( "_ickMessageStateJson: out of memory or JSON error" );

/*------------------------------------------------------------------------*\
    Return result
\*------------------------------------------------------------------------*/
  return jResult;
}

#endif

/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
