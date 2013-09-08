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

#include "minixml.h"

#include "ickP2p.h"
#include "ickP2pInternal.h"
#include "logutils.h"
#include "ickDevice.h"
#include "ickSSDP.h"
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
    Check what ickstream service was announced
      // fixme: do version comparison
\*=========================================================================*/
ickP2pServicetype_t _ickDescrFindServiceByUsn( const char *usn )
{

/*------------------------------------------------------------------------*\
    The SSDP root device announcement defines no services
\*------------------------------------------------------------------------*/
  if( strstr(usn, ICKDEVICE_TYPESTR_ROOT) )
    return ICKP2P_SERVICE_GENERIC;

/*------------------------------------------------------------------------*\
    Check for known services
\*------------------------------------------------------------------------*/
#ifdef ICKP2P_DYNAMICSERVICES
  if( strstr(usn, ICKSERVICE_TYPESTR_PLAYER) )
    return ICKP2P_SERVICE_PLAYER;
  if( strstr(usn, ICKSERVICE_TYPESTR_SERVER) )
    return ICKP2P_SERVICE_SERVER_GENERIC;
  if( strstr(usn, ICKSERVICE_TYPESTR_CONTROLLER) )
    return ICKP2P_SERVICE_CONTROLLER;
  if( strstr(usn, ICKSERVICE_TYPESTR_DEBUG) )
    return ICKP2P_SERVICE_DEBUG;
#endif

/*------------------------------------------------------------------------*\
    No compatible ickstream service found
\*------------------------------------------------------------------------*/
  return ICKP2P_SERVICE_NONE;
}


/*=========================================================================*\
  Find discovery handler and ickstream service type for an
  UPnP description request received over HTTP
    caller must lock discovery handler list
    returns NULL if no match
\*=========================================================================*/
ickP2pServicetype_t _ickDescrFindServiceByUrl( const ickP2pContext_t *ictx, const char *uri )
{
  char    buffer[32];

  debug( "_ickDescrFindServiceByUrl: \"%s\"", uri );

/*------------------------------------------------------------------------*\
    Check for root device
\*------------------------------------------------------------------------*/
  snprintf( buffer, sizeof(buffer), "/%s.xml", ICKDEVICE_STRING_ROOT );
  if( !strcmp(uri,buffer) )
    return ICKP2P_SERVICE_GENERIC;

/*------------------------------------------------------------------------*\
    For services check if they are actually up
\*------------------------------------------------------------------------*/
#ifdef ICKP2P_DYNAMICSERVICES
  snprintf( buffer, sizeof(buffer), "/%s.xml", ICKSERVICE_STRING_PLAYER );
  if( (ictx->ickServices&ICKP2P_SERVICE_PLAYER) && !strcmp(uri,buffer) )
    return ICKP2P_SERVICE_PLAYER;
  snprintf( buffer, sizeof(buffer), "/%s.xml", ICKSERVICE_STRING_SERVER );
  if( (ictx->ickServices&ICKP2P_SERVICE_PLAYER) && !strcmp(uri,buffer) )
    return ICKP2P_SERVICE_SERVER_GENERIC;
  snprintf( buffer, sizeof(buffer), "/%s.xml", ICKSERVICE_STRING_CONTROLLER );
  if( (ictx->ickServices&ICKP2P_SERVICE_PLAYER) && !strcmp(uri,buffer) )
    return ICKP2P_SERVICE_CONTROLLER;
  snprintf( buffer, sizeof(buffer), "/%s.xml", ICKSERVICE_STRING_DEBUG );
  if( (ictx->ickServices&ICKP2P_SERVICE_PLAYER) && !strcmp(uri,buffer) )
    return ICKP2P_SERVICE_DEBUG;
#endif

/*------------------------------------------------------------------------*\
    No match
\*------------------------------------------------------------------------*/
  return ICKP2P_SERVICE_NONE;
}


/*=========================================================================*\
  Create an upnp device descriptor for a given ickstream service type
    this includes a corresponding HTTP header
    returns an allocated string (caller must free) or NULL on error
\*=========================================================================*/
char *_ickDescrGetDeviceDescr( const ickP2pContext_t *ictx, struct libwebsocket *wsi, ickP2pServicetype_t stype )
{
  int                        xlen, hlen;
  char                      *xmlcontent = NULL;
  char                      *type;
  char                      *message;
  char                       header[512];

/*------------------------------------------------------------------------*\
    Get type string
\*------------------------------------------------------------------------*/
#ifdef ICKP2P_DYNAMICSERVICES
  switch( stype ) {
    case ICKP2P_SERVICE_GENERIC:
      type = ICKDEVICE_TYPESTR_ROOT;
      break;
    case ICKP2P_SERVICE_PLAYER:
      type = ICKSERVICE_TYPESTR_PLAYER;
      break;
    case ICKP2P_SERVICE_CONTROLLER:
      type = ICKSERVICE_TYPESTR_CONTROLLER;
      break;
    case ICKP2P_SERVICE_SERVER_GENERIC:
      type = ICKSERVICE_TYPESTR_SERVER;
      break;
    case ICKP2P_SERVICE_DEBUG:
      type = ICKSERVICE_TYPESTR_DEBUG;
      break;
    default:
      logerr( "_ickDescrGetDeviceDescr: invalid service type %d", stype );
      return NULL;
  }
#else
  type = ICKDEVICE_TYPESTR_ROOT;
#endif

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
                    " </device>\r\n"
                    "</root>",

                    ICKDEVICE_UPNP_MAJOR,
                    ICKDEVICE_UPNP_MINOR,
                    type,
                    ictx->deviceName,
                    ickUpnpNames.manufacturer,
                    ickUpnpNames.manufacturerUrl,
                    ickUpnpNames.modelDescriptor,
                    ickUpnpNames.modelName,
                    ictx->deviceUuid,
                    ICKP2PLEVEL_SUPPORTED,
                    ictx->ickServices,
                    ictx->lifetime
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
      if( device->services&~_xmlUserData.services )
        logwarn( "_ickWGetXmlCb (%s): found superset of already known services", uri );
      else if( device->services&_xmlUserData.services )
        logwarn( "_ickWGetXmlCb (%s): found subset of already known services", uri );
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

      // Clean up
      Sfree( _xmlUserData.deviceName );

      // Create expiration timer if necessary (could have been created by an SSDP announcement in the meanwhile)
      _ickTimerListLock( ictx );
      if( !_ickTimerFind(ictx,_ickDeviceExpireTimerCb,device,0) ) {
        ickErrcode_t irc;
        debug( "_ickWGetXmlCb (%s): create expiration timer", device->uuid );
        irc = _ickTimerAdd( ictx, device->lifetime*1000, 1, _ickDeviceExpireTimerCb, device, 0 );
        if( irc )
          logerr( "_ickWGetXmlCb (%s): could not create expiration timer (%s)",
                  uri, ickStrError(irc) );
      }

      // Create heartbeat timer if necessary
      if( device->doConnect && !_ickTimerFind(ictx,_ickDeviceHeartbeatTimerCb,device,0) ) {
        ickErrcode_t irc;
        debug( "_ickWGetXmlCb (%s): create heartbeat timer", device->uuid );
        irc = _ickTimerAdd( ictx, device->lifetime*1000, 0, _ickDeviceHeartbeatTimerCb, device, 0 );
        if( irc )
          logerr( "_ickWGetXmlCb (%s): could not create heartbeat timer (%s)",
                  device->uuid, ickStrError(irc) );
      }
      _ickTimerListUnlock( ictx );

      // Signal device readiness to user code
      _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_NEW, device->services );
      if( /* device->doConnect && */ device->localIsServer )
        _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_CONNECTED, device->services );

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
