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


/*=========================================================================*\
  Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Private definitions and symbols
\*=========================================================================*/
#define JSON_INDENT     2
#define JSON_BOOL(a)    ((a)?"true":"false")
#define JSON_STRING(s)  ((s)?(s):"(nil)")
#define JSON_OBJECT(o)  ((o)?(o):"null")
#define JSON_REAL(r)    ((double)(r))
#define JSON_INTEGER(i) ((int)(i))


/*=========================================================================*\
  Private prototypes
\*=========================================================================*/
#ifdef ICK_P2PENABLEDEBUGAPI
static char *_ickContextStateJson( ickP2pContext_t *ictx, int indent );
static char *_ickDeviceStateJson( ickDevice_t *device, int indent );
static char *_ickMessageStateJson( ickMessage_t *message, int indent );
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

/*------------------------------------------------------------------------*\
    Get complete context info?
\*------------------------------------------------------------------------*/
  if( !uuid )
    result = _ickContextStateJson( ictx, 0 );

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

    // Get debug info as JSON object string
    result = _ickDeviceStateJson( device, 2 );

    // Unlock device list
    _ickLibDeviceListUnlock( ictx );
  }

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
  Get context info as allocated JSON object string including device list
    (no unicode escaping of strings)
    caller must free result
    returns NULL on error
\*=========================================================================*/
static char *_ickContextStateJson( ickP2pContext_t *ictx, int indent )
{
  ickDevice_t *device;
  char        *devices = NULL;
  int          i;
  int          rc;
  char        *result;
  debug( "_ickContextStateJson (%p): %s", ictx->deviceUuid );
  indent += JSON_INDENT;

/*------------------------------------------------------------------------*\
    Compile array of devices
\*------------------------------------------------------------------------*/
  _ickLibDeviceListLock( ictx );
  devices = strdup( "[" );
  for( i=0,device=ictx->deviceList; device&&devices; i++,device=device->next ) {
    char *deviceInfo;

    // Get debug info
    deviceInfo = _ickDeviceStateJson( device, indent+JSON_INDENT );
    if( !deviceInfo ) {
      Sfree( devices );
      break;
    }

    // Add to list
    rc = asprintf( &result, "%s%s\n%*s%s", devices, i?",":"", indent+JSON_INDENT, "", deviceInfo );
    Sfree( devices );
    Sfree( deviceInfo );
    if( rc<0 )
      break;
    else
      devices = result;

  }
  _ickLibDeviceListUnlock( ictx );

  // Close list
  if( devices ) {
    rc = asprintf( &result, "%s\n%*s]", devices, indent, "" );
    Sfree( devices );
    if( rc>0 )
      devices = result;
  }

  // Error ?
  if( !devices ) {
    logerr( "_ickContextStateJson: out of memory" );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Compile all context debug info
\*------------------------------------------------------------------------*/
  rc = asprintf( &result,
                  "{\n"
                  "%*s\"uuid\": \"%s\",\n"
                  "%*s\"name\": \"%s\",\n"
                  "%*s\"services\": %d,\n"
                  "%*s\"hostname\": \"%s\",\n"
                  "%*s\"upnpPort\": \"%d\",\n"
                  "%*s\"wsPort\": %d,\n"
                  "%*s\"folder\": \"%s\",\n"
                  "%*s\"lifetime\": %d,\n"
                  "%*s\"state\": %d,\n"
                  "%*s\"loopback\": %s,\n"
                  "%*s\"customConnectMatrix\": %s,\n"
                  "%*s\"tCreation\": %f,\n"
                  "%*s\"tResume\": %f,\n"
                  "%*s\"mainInterface\": \"%s\",\n"
                  "%*s\"locationRoot\": \"%s\",\n"
                  "%*s\"osName\": \"%s\",\n"
                  "%*s\"p2pVersion\": \"%s\",\n"
                  "%*s\"p2pLevel\": %d,\n"
                  "%*s\"lwsVersion\": \"%s\",\n"
                  "%*s\"bootId\": %d,\n"
                  "%*s\"configId\": %d,\n"
                  "%*s\"devices\": %s\n"
                  "%*s}\n",
                  indent, "", JSON_STRING( ictx->deviceUuid ),
                  indent, "", JSON_STRING( ictx->deviceName ),
                  indent, "", JSON_INTEGER( ictx->ickServices ),
                  indent, "", JSON_STRING( ictx->hostName ),
                  indent, "", JSON_INTEGER( ictx->upnpPort ),
                  indent, "", JSON_INTEGER( ictx->lwsPort ),
                  indent, "", JSON_STRING( ictx->upnpFolder),
                  indent, "", JSON_INTEGER( ictx->lifetime ),
                  indent, "", JSON_INTEGER( ictx->state ),
                  indent, "", JSON_BOOL( ictx->upnpLoopback ),
                  indent, "", JSON_BOOL( ictx->lwsConnectMatrixCb==ickP2pDefaultConnectMatrixCb ),
                  indent, "", JSON_REAL( ictx->tCreation ),
                  indent, "", JSON_REAL( ictx->tResume ),
                  indent, "", JSON_STRING( ictx->interface ),
                  indent, "", JSON_STRING( ictx->locationRoot ),
                  indent, "", JSON_STRING( ictx->osName ),
                  indent, "", JSON_STRING( ickP2pGetVersion(NULL,NULL) ),
                  indent, "", JSON_INTEGER( ICKP2PLEVEL_SUPPORTED ),
                  indent, "", JSON_STRING( lws_get_library_version() ),
                  indent, "", JSON_INTEGER(ictx->upnpBootId ),
                  indent, "", JSON_INTEGER(ictx->upnpConfigId ),
                  indent, "", JSON_OBJECT( devices ),
                  indent-JSON_INDENT, ""
                );
  Sfree( devices );

/*------------------------------------------------------------------------*\
    Error
\*------------------------------------------------------------------------*/
  if( rc<0 ) {
    logerr( "_ickContextStateJson: out of memory" );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Return result
\*------------------------------------------------------------------------*/
  return result;
}


/*=========================================================================*\
  Get device info as allocated JSON object string
    (no unicode escaping of strings)
    caller must free result and should lock the device
    returns NULL on error
\*=========================================================================*/
static char *_ickDeviceStateJson( ickDevice_t *device, int indent )
{
  int   rc;
  char *result;
  char *wsi;
  char *message;
  debug( "_ickDeviceStateJson: %s", device->uuid );
  indent += JSON_INDENT;

/*------------------------------------------------------------------------*\
    Empty data?
\*------------------------------------------------------------------------*/
  if( !device )
    return strdup( "null" );

/*------------------------------------------------------------------------*\
    Create websocket information (if any)
\*------------------------------------------------------------------------*/
  /* fixme
  psd  = // no way to get user data from a wsi outside a lws callback scope
  jWsi = _ickWsiStateJson( psd );
  */
  wsi = strdup( JSON_BOOL(device->wsi) );
  if( !wsi ) {
    logerr( "_ickDeviceStateJson: out of memory" );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Create message information (if any)
\*------------------------------------------------------------------------*/
  message = _ickMessageStateJson( device->outQueue, indent+JSON_INDENT );
  if( !message ) {
    Sfree( wsi );
    logerr( "_ickDeviceStateJson: out of memory" );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Compile all debug info
\*------------------------------------------------------------------------*/
  rc = asprintf( &result,
                  "{\n"
                  "%*s\"name\": \"%s\",\n"
                  "%*s\"type\": %d,\n"
                  "%*s\"tCreation\": %f,\n"
                  "%*s\"UUID\": \"%s\",\n"
                  "%*s\"location\": \"%s\",\n"
                  "%*s\"upnpVersion\": %d,\n"
                  "%*s\"p2pLevel\": %d,\n"
                  "%*s\"lifetime\": %d,\n"
                  "%*s\"services\": %d,\n"
                  "%*s\"tXmlComplete\": %f,\n"
                  "%*s\"doConnect\": %s,\n"
                  "%*s\"tConnect\": %f,\n"
                  "%*s\"tDisconnect\": %f,\n"
                  "%*s\"localIsServer\": %s,\n"
                  "%*s\"rx\": %d,\n"
                  "%*s\"rxSegmented\": %d,\n"
                  "%*s\"tx\": %d,\n"
                  "%*s\"txPending\": %d,\n"
                  "%*s\"rxLast\": %f,\n"
                  "%*s\"txLast\": %f,\n"
                  "%*s\"wsi\": %s,\n"
                  "%*s\"message\": %s\n"
                  "%*s}",
                  indent, "", JSON_STRING( device->friendlyName ),
                  indent, "", JSON_INTEGER( device->type ),
                  indent, "", JSON_REAL( device->tCreation ),
                  indent, "", JSON_STRING( device->uuid ),
                  indent, "", JSON_STRING( device->location ),
                  indent, "", JSON_INTEGER( device->ickUpnpVersion ),
                  indent, "", JSON_INTEGER( device->ickP2pLevel ),
                  indent, "", JSON_INTEGER( device->lifetime ),
                  indent, "", JSON_INTEGER( device->services ),
                  indent, "", JSON_REAL( device->tXmlComplete ),
                  indent, "", JSON_BOOL( device->doConnect ),
                  indent, "", JSON_REAL( device->tConnect ),
                  indent, "", JSON_REAL( device->tDisconnect ),
                  indent, "", JSON_BOOL( device->localIsServer ),
                  indent, "", JSON_INTEGER( device->nRx ),
                  indent, "", JSON_INTEGER( device->nRxSegmented ),
                  indent, "", JSON_INTEGER( device->nTx ),
                  indent, "", JSON_INTEGER( _ickDevicePendingMessages(device) ),
                  indent, "", JSON_REAL( device->tLastRx ),
                  indent, "", JSON_REAL( device->tLastTx ),
                  indent, "", JSON_OBJECT( wsi ),
                  indent, "", JSON_OBJECT( message ),
                  indent-JSON_INDENT, ""
                );
  Sfree( wsi );
  Sfree( message );

/*------------------------------------------------------------------------*\
    Error
\*------------------------------------------------------------------------*/
  if( rc<0 ) {
    logerr( "_ickDeviceStateJson: out of memory" );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Return result
\*------------------------------------------------------------------------*/
  return result;
}


/*=========================================================================*\
  Get message info as allocated JSON object string
    (fixed indent of 6, no unicode escaping of strings)
    caller must free result
    returns NULL on error
\*=========================================================================*/
static char *_ickMessageStateJson( ickMessage_t *message, int indent )
{
  int   rc;
  char *result;
  debug( "_ickMessageStateJson (%p): ", message );
  indent += JSON_INDENT;

/*------------------------------------------------------------------------*\
    Empty data?
\*------------------------------------------------------------------------*/
  if( !message )
    return strdup( "null" );

/*------------------------------------------------------------------------*\
    Compile message debug info
\*------------------------------------------------------------------------*/
  rc = asprintf( &result,
                  "{\n"
                  "%*s\"tCreated\": %f,\n"
                  "%*s\"size\": %d,\n"
                  "%*s\"issued\": %d\n"
                  "%*s}",
                  indent, "", JSON_REAL( message->tCreated ),
                  indent, "", JSON_INTEGER( message->size ),
                  indent, "", JSON_INTEGER( message->issued ),
                  indent-JSON_INDENT, ""
                );

/*------------------------------------------------------------------------*\
    Error
\*------------------------------------------------------------------------*/
  if( rc<0 )
    logerr( "_ickMessageStateJson: out of memory" );

/*------------------------------------------------------------------------*\
    Return result
\*------------------------------------------------------------------------*/
  return result;
}

#endif

/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
