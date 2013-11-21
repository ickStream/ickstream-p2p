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
\************************************************************************/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <libwebsockets.h>
#include <arpa/inet.h>

#include "miniwget.h"

#include "ickP2p.h"
#include "ickP2pInternal.h"
#include "logutils.h"
#include "ickIpTools.h"
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
#define JSON_LONG(i)    ((long)(i))


/*=========================================================================*\
  Private prototypes
\*=========================================================================*/
#ifdef ICK_P2PENABLEDEBUGAPI
static char *_ickContextStateJson( ickP2pContext_t *ictx, int indent );
static char *_ickInterfaceStateJson( ickInterface_t *interface, int indent );
static char *_ickDeviceStateJson( ickDevice_t *device, int indent );
static char *_ickMessageStateJson( ickMessage_t *message, int indent );
#endif



/*=========================================================================*\
  Permit or deny access for remote debugging via API or HTTP
\*=========================================================================*/
ickErrcode_t ickP2pSetHttpDebugging( ickP2pContext_t *ictx, int enable )
{
  debug( "ickP2pRemoteDebugApi (%p): %s", ictx, enable?"enable":"disable" );

#ifndef ICK_P2PENABLEDEBUGAPI
  logwarn( "ickP2pSetHttpDebugging: p2plib not compiled with debugging API support." );
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
char *ickP2pGetDebugInfo( ickP2pContext_t *ictx, const char *uuid )
{
  char *result = NULL;
  debug( "ickP2pGetDebugInfo (%p): \"%s\"", ictx, uuid );

#ifndef ICK_P2PENABLEDEBUGAPI
  logwarn( "ickP2pGetDebugInfo: p2plib not compiled with debugging API support." );
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
      logwarn( "ickP2pGetDebugInfo: no such device (%s)", uuid );
      _ickLibDeviceListUnlock( ictx );
      return NULL;
    }

    // Get debug info as JSON object string
    result = _ickDeviceStateJson( device, 0 );

    // Unlock device list
    _ickLibDeviceListUnlock( ictx );
  }

#endif

/*------------------------------------------------------------------------*\
    That's all
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
  debugContent = ickP2pGetDebugInfo( ictx, uuid );

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
  Get HTTP log info
    this includes a corresponding HTTP header
    returns an allocated string (caller must free) or NULL on error
\*=========================================================================*/
char *_ickP2pGetLogFile( ickP2pContext_t *ictx, const char *uri )
{
  int          level = 7;
  char        *logContent;
  int          dlen, hlen;
  char        *message;
  char         header[512];

  debug( "_ickP2pGetLogFile (%p): \"%s\"", ictx, uri );

/*------------------------------------------------------------------------*\
    Get minimum log level from path
\*------------------------------------------------------------------------*/
  if( strlen(uri)>strlen(ICK_P2PLOGURI) ) {
    const char *ptr = uri + strlen(ICK_P2PLOGURI);
    char       *eptr;
    if( *ptr!='/' )
      return strdup( HTTP_404 );
    ptr++;
    level = (int) strtol( ptr, &eptr, 10 );
    while( isspace(*eptr) )
      eptr++;
    if( *eptr ) {
      logwarn("_ickP2pGetLogFile (%p): Requested minimum level is not a number (%s)", ictx, ptr );
      return strdup( HTTP_404 );
    }
  }

/*------------------------------------------------------------------------*\
    Get debug info
\*------------------------------------------------------------------------*/
  logContent = ickP2pGetLogContent( level );
  if( !logContent )
    return strdup( HTTP_404 );

/*------------------------------------------------------------------------*\
  Construct header
\*------------------------------------------------------------------------*/
  dlen = strlen( logContent);
  hlen = sprintf( header, HTTP_200, "text/plain", (long)dlen );

/*------------------------------------------------------------------------*\
  Merge header and payload
\*------------------------------------------------------------------------*/
  message = malloc( hlen+dlen+1 );
  if( !message ) {
    Sfree( logContent );
    logerr( "_ickP2pGetLogFile: out of memory" );
    return NULL;
  }
  strcpy( message, header );
  strcpy( message+hlen, logContent );
  Sfree( logContent );

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
  ickDevice_t    *device;
  char           *devices    = NULL;
  ickInterface_t *interface;
  char           *interfaces = NULL;
  int             i;
  int             rc;
  char           *result;
  debug( "_ickContextStateJson (%p): %s", ictx, ictx->deviceUuid );
  indent += JSON_INDENT;

/*------------------------------------------------------------------------*\
    Compile array of interfaces
\*------------------------------------------------------------------------*/
  _ickLibInterfaceListLock( ictx );
  interfaces = strdup( "[" );
  for( i=0,interface=ictx->interfaces; interface&&interfaces; i++,interface=interface->next ) {
    char *interfaceInfo;

    // Get debug info
    interfaceInfo = _ickInterfaceStateJson( interface, indent+JSON_INDENT );
    if( !interfaceInfo ) {
      Sfree( interfaces );
      break;
    }

    // Add to list
    rc = asprintf( &result, "%s%s\n%*s%s", interfaces, i?",":"", indent+JSON_INDENT, "", interfaceInfo );
    Sfree( interfaces );
    Sfree( interfaceInfo );
    if( rc<0 )
      break;
    else
      interfaces = result;
  }
  _ickLibInterfaceListUnlock( ictx );

  // Close list
  if( interfaces ) {
    rc = asprintf( &result, "%s\n%*s]", interfaces, indent, "" );
    Sfree( interfaces );
    if( rc>0 )
      interfaces = result;
  }

  // Error ?
  if( !interfaces ) {
    logerr( "_ickContextStateJson: out of memory" );
    return NULL;
  }

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
    Sfree( interfaces );
    logerr( "_ickContextStateJson: out of memory" );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Compile all context debug info
\*------------------------------------------------------------------------*/
  rc = asprintf( &result,
                  "{\n"
                  "%*s\"pid\": %d,\n"
                  "%*s\"uuid\": \"%s\",\n"
                  "%*s\"name\": \"%s\",\n"
                  "%*s\"services\": %d,\n"
                  "%*s\"upnpListenerPort\": %d,\n"
                  "%*s\"wsPort\": %d,\n"
                  "%*s\"folder\": \"%s\",\n"
                  "%*s\"lifetime\": %d,\n"
                  "%*s\"state\": \"%s\",\n"
                  "%*s\"loopback\": %s,\n"
                  "%*s\"customConnectMatrix\": %s,\n"
                  "%*s\"tCreation\": %f,\n"
                  "%*s\"tResume\": %f,\n"
                  "%*s\"osName\": \"%s\",\n"
                  "%*s\"p2pVersion\": \"%s\",\n"
                  "%*s\"p2pLevel\": %d,\n"
                  "%*s\"lwsVersion\": \"%s\",\n"
                  "%*s\"bootId\": %ld,\n"
                  "%*s\"configId\": %ld,\n"
                  "%*s\"interfaces\": %s\n"
                  "%*s\"devices\": %s\n"
                        "%*s}\n",
                  indent, "", JSON_INTEGER( getpid() ),
                  indent, "", JSON_STRING( ictx->deviceUuid ),
                  indent, "", JSON_STRING( ictx->deviceName ),
                  indent, "", JSON_INTEGER( ictx->ickServices ),
                  indent, "", JSON_INTEGER( ictx->upnpListenerPort ),
                  indent, "", JSON_INTEGER( ictx->lwsPort ),
                  indent, "", JSON_STRING( ictx->upnpFolder),
                  indent, "", JSON_INTEGER( ictx->lifetime ),
                  indent, "", JSON_STRING( ickLibState2Str(ictx->state) ),
                  indent, "", JSON_BOOL( ictx->upnpLoopback ),
                  indent, "", JSON_BOOL( ictx->lwsConnectMatrixCb==ickP2pDefaultConnectMatrixCb ),
                  indent, "", JSON_REAL( ictx->tCreation ),
                  indent, "", JSON_REAL( ictx->tResume ),
                  indent, "", JSON_STRING( ictx->osName ),
                  indent, "", JSON_STRING( ickP2pGetVersion(NULL,NULL) ),
                  indent, "", JSON_INTEGER( ICKP2PLEVEL_SUPPORTED ),
                  indent, "", JSON_STRING( lws_get_library_version() ),
                  indent, "", JSON_LONG( ictx->upnpBootId ),
                  indent, "", JSON_LONG( ictx->upnpConfigId ),
                  indent, "", JSON_OBJECT( interfaces ),
                  indent, "", JSON_OBJECT( devices ),
                  indent-JSON_INDENT, ""
                );
  Sfree( devices );
  Sfree( interfaces );

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
  Get interface info as allocated JSON object string
    (no unicode escaping of strings)
    caller must free result
    returns NULL on error
\*=========================================================================*/
static char *_ickInterfaceStateJson( ickInterface_t *interface, int indent )
{
  int   rc;
  char *result;
  char _buf1[64], _buf2[64];

  debug( "_ickInterfaceStateJson (%p): ", interface );
  indent += JSON_INDENT;

/*------------------------------------------------------------------------*\
    Empty data?
\*------------------------------------------------------------------------*/
  if( !interface )
    return strdup( "null" );

  inet_ntop( AF_INET, &interface->addr,    _buf1, sizeof(_buf1) );
  inet_ntop( AF_INET, &interface->netmask, _buf2, sizeof(_buf2) );

/*------------------------------------------------------------------------*\
    Compile message debug info
\*------------------------------------------------------------------------*/
  rc = asprintf( &result,
                  "{\n"
                  "%*s\"name\": \"%s\",\n"
                  "%*s\"hostname\": \"%s\",\n"
                  "%*s\"address\": \"%s\",\n"
                  "%*s\"netmask\": \"%s\",\n"
                  "%*s\"upnpComPort\": %d\n"
                  "%*s\"announceBootId\": %ld\n"
                  "%*s}",
                  indent, "", JSON_STRING( interface->name ),
                  indent, "", JSON_STRING( interface->hostname ),
                  indent, "", _buf1,
                  indent, "", _buf2,
                  indent, "", JSON_INTEGER( _ickIpGetSocketPort(interface->upnpComSocket) ),
                  indent, "", JSON_LONG( interface->announcedBootId ),
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
                  "%*s\"tCreation\": %f,\n"
                  "%*s\"UUID\": \"%s\",\n"
                  "%*s\"location\": \"%s\",\n"
                  "%*s\"upnpVersion\": %d,\n"
                  "%*s\"p2pLevel\": %d,\n"
                  "%*s\"lifetime\": %d,\n"
                  "%*s\"services\": %d,\n"
                  "%*s\"getXml\": \"%s\",\n"
                  "%*s\"doConnect\": %s,\n"
                  "%*s\"ssdpState\": \"%s\",\n"
                  "%*s\"bootId\": \"%ld\",\n"
                  "%*s\"configId\": \"%ld\",\n"
                  "%*s\"connectionState\": \"%s\",\n"
                  "%*s\"tXmlComplete\": %f,\n"
                  "%*s\"tConnect\": %f,\n"
                  "%*s\"tDisconnect\": %f,\n"
                  "%*s\"rx\": %d,\n"
                  "%*s\"rxSegmented\": %d,\n"
                  "%*s\"rxPending\": %d,\n"
                  "%*s\"tx\": %d,\n"
                  "%*s\"txPending\": %d,\n"
                  "%*s\"rxLast\": %f,\n"
                  "%*s\"txLast\": %f,\n"
                  "%*s\"wsi\": %s,\n"
                  "%*s\"message\": %s\n"
                  "%*s}",
                  indent, "", JSON_STRING( device->friendlyName ),
                  indent, "", JSON_REAL( device->tCreation ),
                  indent, "", JSON_STRING( device->uuid ),
                  indent, "", JSON_STRING( device->location ),
                  indent, "", JSON_INTEGER( device->ickUpnpVersion ),
                  indent, "", JSON_INTEGER( device->ickP2pLevel ),
                  indent, "", JSON_INTEGER( device->lifetime ),
                  indent, "", JSON_INTEGER( device->services ),
                  indent, "", JSON_STRING( device->wget?_ickWGetUri(device->wget) : NULL ),
                  indent, "", JSON_BOOL( device->doConnect ),
                  indent, "", JSON_STRING( _ickDeviceSsdpState2Str(device->ssdpState) ),
                  indent, "", JSON_LONG( device->ssdpBootId ),
                  indent, "", JSON_LONG( device->ssdpConfigId ),
                  indent, "", JSON_STRING( _ickDeviceConnState2Str(device->connectionState) ),
                  indent, "", JSON_REAL( device->tXmlComplete ),
                  indent, "", JSON_REAL( device->tConnect ),
                  indent, "", JSON_REAL( device->tDisconnect ),
                  indent, "", JSON_INTEGER( device->nRx ),
                  indent, "", JSON_INTEGER( device->nRxSegmented ),
                  indent, "", JSON_INTEGER( _ickDevicePendingInMessages(device) ),
                  indent, "", JSON_INTEGER( device->nTx ),
                  indent, "", JSON_INTEGER( _ickDevicePendingOutMessages(device) ),
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
