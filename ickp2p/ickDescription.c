/*$*********************************************************************\

Source File     : ickDescription.c

Description     : implement ickp2p upnp layer 2 (description) functions

Comments        : -

Called by       : libwebsocket http server

Calls           : internal ickstream functions and
                  miniwget and minixml from miniupnp

Date            : 21.08.2013

Updates         : -

Author          : JS, //MAF

Remarks         : -

*************************************************************************
 * Copyright (c) 2013, ickStream GmbH
 * All rights reserved.
\************************************************************************/


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libwebsockets.h>

#include "minixml.h"

#include "ickP2p.h"
#include "ickP2pInternal.h"
#include "logutils.h"
#include "ickDevice.h"
#include "ickSSDP.h"
#include "ickP2pCom.h"
#include "ickWGet.h"
#include "ickMainThread.h"
#include "ickDescription.h"


/*=========================================================================*\
  Global symbols
\*=========================================================================*/

ickUpnpNames_t ickUpnpNames = {
  ICKDEVICE_MANUFACTURER,
  ICKDEVICE_MANUFACTURERURL,
  ICKDEVICE_MODELDESCRIPTION,
  ICKDEVICE_MODELNAME,
  ICKDEVICE_PRODUCTANDVERSION
};


/*=========================================================================*\
  Private definitions
\*=========================================================================*/
typedef struct {
  int                  level;
  const char          *eltPtr;
  int                  eltLen;
  int                  deviceLevel;

  // extracted data
  char                *deviceName;            // strong
  ickP2pLevel_t        protocolLevel;
  ickP2pServicetype_t  services;
  int                  lifetime;
  long                 bootId;
  long                 configId;

} ickXmlUserData_t;


/*=========================================================================*\
  Private prototypes
\*=========================================================================*/
static void _ickParsexmlStartElt( void *data, const char *elt, int len );
static void _ickParsexmlEndElt( void *data, const char *elt, int len );
static void _ickParsexmlProcessData( void *data, const char *content, int len );
static int  _strmcmp( const char *str, const char *ptr, size_t plen );

// none


/*=========================================================================*\
  Private symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Create an upnp device descriptor
    this includes a corresponding HTTP header
    returns an allocated string (caller must free) or NULL on error
\*=========================================================================*/
char *_ickDescrGetDeviceDescr( const ickP2pContext_t *ictx, struct libwebsocket *wsi )
{
  int                        xlen, hlen;
  char                      *xmlcontent = NULL;
  char                      *message;
  char                       header[512];

/*------------------------------------------------------------------------*\
    Construct XML payload
    "UPnP Device Architecture 1.1": chapter 2.3
\*------------------------------------------------------------------------*/
  xlen = asprintf( &xmlcontent,
                    "<root>\r\n"
                    " <specVersion>\r\n"
                    "  <major>%d</major>\r\n"
                    "  <minor>%d</minor>\r\n"
                    " </specVersion>\r\n"
                    " <device>\r\n"
                    "  <deviceType>%s</deviceType>\r\n"
                    "  <friendlyName>%s</friendlyName>\r\n"
                    "  <manufacturer>%s</manufacturer>\r\n"
                    "  <manufacturerURL>%s</manufacturerURL>\r\n"
                    "  <modelDescription>%s</modelDescription>\r\n"
                    "  <modelName>%s</modelName>\r\n"
                    "  <UDN>uuid:%s</UDN>\r\n"
                    "  <protocolLevel>%d</protocolLevel>\r\n"
                    "  <services>%d</services>\r\n"
                    "  <lifetime>%d</lifetime>\r\n"
                    "  <bootId>%ld</bootId>\r\n"
                    "  <configId>%ld</configId>\r\n"
                    " </device>\r\n"
                    "</root>",

                    ICKDEVICE_UPNP_MAJOR,
                    ICKDEVICE_UPNP_MINOR,
                    ICKDEVICE_TYPESTR_ROOT,
                    ictx->deviceName,
                    ickUpnpNames.manufacturer,
                    ickUpnpNames.manufacturerUrl,
                    ickUpnpNames.modelDescriptor,
                    ickUpnpNames.modelName,
                    ictx->deviceUuid,
                    ICKP2PLEVEL_SUPPORTED,
                    ictx->ickServices,
                    ictx->lifetime,
                    ictx->upnpBootId,
                    ictx->upnpConfigId
                  );

/*------------------------------------------------------------------------*\
  Out of memory?
\*------------------------------------------------------------------------*/
  if( xlen<0 || !xmlcontent ) {
    logerr( "_ickDescrGetDeviceDescr: out of memory" );
    return NULL;
  }

/*------------------------------------------------------------------------*\
  Construct header
\*------------------------------------------------------------------------*/
  hlen = sprintf( header, HTTP_200, "text/xml", (long)xlen );

/*------------------------------------------------------------------------*\
  Merge header and payload
\*------------------------------------------------------------------------*/
  message = malloc( hlen+xlen+1 );
  if( !message ) {
    Sfree( xmlcontent );
    logerr( "_ickDescrGetDeviceDescr: out of memory" );
    return NULL;
  }
  strcpy( message, header );
  strcpy( message+hlen, xmlcontent );
  Sfree( xmlcontent );

/*------------------------------------------------------------------------*\
  That's all
\*------------------------------------------------------------------------*/
  return message;
}


/*=========================================================================*\
  Callback for the http client implementation
\*=========================================================================*/
ickErrcode_t _ickWGetXmlCb( ickWGetContext_t *context, ickWGetAction_t action, int arg )
{
  ickErrcode_t      irc = ICKERR_SUCCESS;
  const char       *uri = _ickWGetUri( context );
  struct xmlparser  _xmlParser;
  ickXmlUserData_t  _xmlUserData;
  ickDevice_t       *device;
  ickP2pContext_t   *ictx;

  debug( "_ickWGetXmlCb (%s): action=%d", uri, action );

/*------------------------------------------------------------------------*\
  What to do?
\*------------------------------------------------------------------------*/
  switch( action ) {

/*------------------------------------------------------------------------*\
  HTTP client instance was initialized
\*------------------------------------------------------------------------*/
    case ICKWGETACT_INIT:
      debug( "_ickWGetXmlCb (%s): initialized.", uri );
      break;

/*------------------------------------------------------------------------*\
  HTTP client instance is destroyed
\*------------------------------------------------------------------------*/
    case ICKWGETACT_DESTROY:
      debug( "_ickWGetXmlCb (%s): destroyed.", uri );
      break;

/*------------------------------------------------------------------------*\
  Data could not be retrieved
\*------------------------------------------------------------------------*/
    case ICKWGETACT_ERROR:
      debug( "_ickWGetXmlCb (%s): error \"%s\".", uri,
             _ickWGetErrorString(context) );
      break;

/*------------------------------------------------------------------------*\
  Data is complete
\*------------------------------------------------------------------------*/
    case ICKWGETACT_COMPLETE:
      device = _ickWGetUserData( context );
      ictx   = device->ictx;
      debug( "_ickWGetXmlCb (%s): complete \"%.*s\".", uri,
            _ickWGetPayloadSize(context), _ickWGetPayload(context) );

      // Init and execute xml parser
      memset( &_xmlUserData, 0, sizeof(_xmlUserData) );
      _xmlUserData.protocolLevel = ICKP2PLEVEL_GENERIC;
      _xmlUserData.services      = ICKP2P_SERVICE_GENERIC;
      _xmlUserData.lifetime      = 0;
      _xmlParser.xmlstart        = _ickWGetPayload( context );
      _xmlParser.xmlsize         = _ickWGetPayloadSize( context );
      _xmlParser.data            = &_xmlUserData;
      _xmlParser.starteltfunc    = _ickParsexmlStartElt;
      _xmlParser.endeltfunc      = _ickParsexmlEndElt;
      _xmlParser.datafunc        = _ickParsexmlProcessData;
      _xmlParser.attfunc         = NULL;
      parsexml( &_xmlParser );

      // Interpret result
      if( _xmlUserData.level )
        logwarn( "_ickWGetXmlCb (%s): xml data unbalanced (end level %d)",
                 uri, _xmlUserData.level );
      if( !_xmlUserData.deviceName )
        logwarn( "_ickWGetXmlCb (%s): found no device name (using UUID)", uri );
      if( !_xmlUserData.protocolLevel )
        logwarn( "_ickWGetXmlCb (%s): found no protocol level", uri );

      // Complete device description
      _ickDeviceLock( device );
      _ickDeviceSetName( device, _xmlUserData.deviceName );
      device->ickP2pLevel  = _xmlUserData.protocolLevel;
      device->ssdpBootId   = _xmlUserData.bootId;
      device->ssdpConfigId = _xmlUserData.configId;
      if( device->services && (_xmlUserData.services&~device->services) )
        logwarn( "_ickWGetXmlCb (%s): found superset of already known services (was:0x%02x new:0x%02x)",
                  uri, device->services, _xmlUserData.services );
      else if( device->services!=_xmlUserData.services && (device->services&_xmlUserData.services) )
        logwarn( "_ickWGetXmlCb (%s): found subset of already known services (was:0x%02x new:0x%02x)",
                  uri, device->services, _xmlUserData.services );
      debug( "_ickWGetXmlCb (%s): adding services 0x%02x.", device->uuid, _xmlUserData.services );
      device->services |= _xmlUserData.services;
      if( !_xmlUserData.lifetime ) {
        logwarn( "_ickWGetXmlCb (%s): no lifetime, using default", uri );
        _xmlUserData.lifetime = ICKSSDP_DEFAULTLIFETIME;
      }
      if( !device->lifetime )
         device->lifetime  = _xmlUserData.lifetime;
      _ickDeviceUnlock( device );

      //Evaluate connection matrix
      device->doConnect = 1;
      if( ictx->lwsConnectMatrixCb )
        device->doConnect = ictx->lwsConnectMatrixCb( ictx, ictx->ickServices, device->services );
      debug( "_ickWGetXmlCb (%s): %s need to connect", device->uuid, device->doConnect?"Do":"No" );

      // Set timestamp
      device->tXmlComplete = _ickTimeNow();

      // Clean up, No more wgetter active for this device
      Sfree( _xmlUserData.deviceName );
      device->wget = NULL;

      // Signal device readiness to user code
      _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_INITIALIZED, device->services );
      if( device->connectionState==ICKDEVICE_SERVERCONNECTING ) {

        // We are server, set connection timestamp
        device->connectionState = ICKDEVICE_ISSERVER;
        device->tConnect        = _ickTimeNow();
        debug( "_ickWGetXmlCb (%s): device state now \"%s\"",
               device->uuid, _ickDeviceConnState2Str(device->connectionState) );

        _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_CONNECTED, device->services );

        // Deliver messages received by servers prior to XML completion
        while( device->inQueue ) {
          ickMessage_t *message = device->inQueue;
          _ickP2pExecMessageCallback( ictx, device, message->payload, message->size );
          _ickDeviceUnlinkInMessage( device, message );
          _ickDeviceFreeMessage( message );
        }
      }

      break;
  }

/*------------------------------------------------------------------------*\
  That's all, return
\*------------------------------------------------------------------------*/
  return irc;
}


# pragma mark -- xml parser callbacks

/*=========================================================================*\
  XML parser callback: new element
\*=========================================================================*/
static void _ickParsexmlStartElt( void *data, const char *elt, int len )
{
  ickXmlUserData_t *xmlUserData = data;
  debug( "_ickParsexmlStartElt (level %d): starting element \"%.*s\"",
         xmlUserData->level+1, len, elt );

/*------------------------------------------------------------------------*\
  Increment level
\*------------------------------------------------------------------------*/
  xmlUserData->level++;

/*------------------------------------------------------------------------*\
  Buffer element name and length in user data
\*------------------------------------------------------------------------*/
  xmlUserData->eltPtr = elt;
  xmlUserData->eltLen = len;

/*------------------------------------------------------------------------*\
  Are we entering the top level device bracket?
\*------------------------------------------------------------------------*/
  if( !_strmcmp("device",elt,len) ) {
    if( !xmlUserData->deviceLevel ) {
      debug( "_ickParsexmlStartElt: device top level is %d", xmlUserData->level );
      xmlUserData->deviceLevel = xmlUserData->level;
    }
  }

/*------------------------------------------------------------------------*\
  That's it
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
  XML parser callback: element finished
\*=========================================================================*/
static void _ickParsexmlEndElt( void *data, const char *elt, int len )
{
  ickXmlUserData_t *xmlUserData = data;
  debug( "_ickParsexmlStartElt (level %d): ending element \"%.*s\"",
         xmlUserData->level, len, elt );

/*------------------------------------------------------------------------*\
  Are we leaving the top level device bracket?
\*------------------------------------------------------------------------*/
  if( !_strmcmp("device",elt,len) && xmlUserData->deviceLevel==xmlUserData->level )
    xmlUserData->deviceLevel = 0;

/*------------------------------------------------------------------------*\
  Decrement level
\*------------------------------------------------------------------------*/
  xmlUserData->level--;

/*------------------------------------------------------------------------*\
  That's it
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
  XML parser callback: process element
\*=========================================================================*/
static void _ickParsexmlProcessData( void *data, const char *content, int len )
{
  ickXmlUserData_t *xmlUserData = data;
  debug( "_ickParsexmlProcessData (level %d, element \"%.*s\"): \"%.*s\"",
      xmlUserData->level, xmlUserData->eltLen, xmlUserData->eltPtr, len, content );

/*------------------------------------------------------------------------*\
  Ignore everything outside the top level device bracket
\*------------------------------------------------------------------------*/
  if( xmlUserData->level!=xmlUserData->deviceLevel+1 )
    return;

/*------------------------------------------------------------------------*\
  Cross check device type
\*------------------------------------------------------------------------*/
  if( !_strmcmp("deviceType",xmlUserData->eltPtr,xmlUserData->eltLen) ) {
    debug( "_ickParsexmlProcessData: found device type \"%.*s\"", len, content );
    if( strncmp(ICKDEVICE_TYPESTR_PREFIX,content,strlen(ICKDEVICE_TYPESTR_PREFIX)) ) {
      logwarn( "_ickParsexmlProcessData: this is no ickstream device (%.*s)",
              len, (char*)content );
      xmlUserData->deviceLevel = 0;
    }
    return;
  }

/*------------------------------------------------------------------------*\
  Found device name
\*------------------------------------------------------------------------*/
  if( !_strmcmp("friendlyName",xmlUserData->eltPtr,xmlUserData->eltLen) ) {
    debug( "_ickParsexmlProcessData: found friendly name \"%.*s\"", len, content );
    if( xmlUserData->deviceName ) {
      logwarn( "_ickParsexmlProcessData: found more then one friendly name (ignoring)" );
      return;
    }
    xmlUserData->deviceName = strndup( content, len );
    if( !xmlUserData->deviceName )
      logerr( "_ickParsexmlProcessElt: out of memory" );
    return;
  }

/*------------------------------------------------------------------------*\
  Found ickstream protocol level
\*------------------------------------------------------------------------*/
  if( !_strmcmp("protocolLevel",xmlUserData->eltPtr,xmlUserData->eltLen)) {
    debug( "_ickParsexmlProcessData: found protocol level \"%.*s\"", len, content );
    if( xmlUserData->protocolLevel ) {
      logwarn( "_ickParsexmlProcessData: found more then one protocol levels (ignoring)" );
      return;
    }
    // should be terminated by non-digit, so it's safe to ignore len here
    xmlUserData->protocolLevel = atoi( content );
    return;
  }

/*------------------------------------------------------------------------*\
  Found ickstream services
\*------------------------------------------------------------------------*/
  if( !_strmcmp("services",xmlUserData->eltPtr,xmlUserData->eltLen)) {
    debug( "_ickParsexmlProcessData: found services \"%.*s\"", len, content );
    if( xmlUserData->services ) {
      logwarn( "_ickParsexmlProcessData: found more then one service vector (ignoring)" );
      return;
    }
    // should be terminated by non-digit, so it's safe to ignore len here
    xmlUserData->services = atoi( content );
    return;
  }

/*------------------------------------------------------------------------*\
  Found lifetime
\*------------------------------------------------------------------------*/
  if( !_strmcmp("lifetime",xmlUserData->eltPtr,xmlUserData->eltLen)) {
    debug( "_ickParsexmlProcessData: found lifetime \"%.*s\"", len, content );
    if( xmlUserData->lifetime ) {
      logwarn( "_ickParsexmlProcessData: found more then one lifetime (ignoring)" );
      return;
    }
    // should be terminated by non-digit, so it's safe to ignore len here
    xmlUserData->lifetime = atoi( content );
    return;
  }

/*------------------------------------------------------------------------*\
  Found bootId
\*------------------------------------------------------------------------*/
  if( !_strmcmp("bootId",xmlUserData->eltPtr,xmlUserData->eltLen)) {
    debug( "_ickParsexmlProcessData: found bootId \"%.*s\"", len, content );
    if( xmlUserData->bootId ) {
      logwarn( "_ickParsexmlProcessData: found more then one bootId (ignoring)" );
      return;
    }
    // should be terminated by non-digit, so it's safe to ignore len here
    xmlUserData->bootId = atol( content );
    return;
  }

/*------------------------------------------------------------------------*\
  Found configId
\*------------------------------------------------------------------------*/
  if( !_strmcmp("configId",xmlUserData->eltPtr,xmlUserData->eltLen)) {
    debug( "_ickParsexmlProcessData: found configId \"%.*s\"", len, content );
    if( xmlUserData->configId ) {
      logwarn( "_ickParsexmlProcessData: found more then one configId (ignoring)" );
      return;
    }
    // should be terminated by non-digit, so it's safe to ignore len here
    xmlUserData->configId = atol( content );
    return;
  }

/*------------------------------------------------------------------------*\
  Ignore all other tags
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
  Compare a string to a memory region
\*=========================================================================*/
int _strmcmp( const char *str, const char *ptr, size_t plen )
{

  // String length must match
  if( strlen(str)!=plen )
    return -1;

  // compare memory region
  return memcmp( str, ptr, plen );
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
