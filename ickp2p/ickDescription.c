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

#include "miniwget.h"
#include "minixml.h"

#include "ickP2p.h"
#include "ickP2pInternal.h"
#include "ickSSDP.h"
#include "ickDiscovery.h"
#include "ickIpTools.h"
#include "logutils.h"
#include "ickDescription.h"


/*=========================================================================*\
  Global symbols
\*=========================================================================*/

ickUpnpNames_t ickUpnpNames = {
  ICKDEVICE_MANUFACTURER,
  ICKDEVICE_MANUFACTURERURL,
  ICKDEVICE_MODELDESCRIPTION,
  ICKDEVICE_MODELNAME
};


/*=========================================================================*\
  Private definitions
\*=========================================================================*/

//
// A descriptor for xml retrieving threads
//
struct _ickXmlThread {
  ickXmlThread_t        *next;
  ickXmlThread_t        *prev;
  pthread_mutex_t        mutex;
  pthread_t              thread;
  upnp_device_t         *device;
  char                  *uuid;
  char                  *uri;
};


/*=========================================================================*\
  Private prototypes
\*=========================================================================*/
static void *_xmlLoaderThread( void *arg );


/*=========================================================================*\
  Private symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Find discovery handler and ickstream service type for an
  UPnp description request received over HTTP
    caller must lock discovery handler list
    returns NULL if no match
\*=========================================================================*/
ickDiscovery_t *_ickDescrFindDicoveryHandlerByUrl( const _ickP2pLibContext_t *icklib, const char *uri, ickP2pServicetype_t *stype )
{
  ickDiscovery_t *walk;
  char            buffer[32];

  debug( "_ickDescrFindDicoveryHandlerByUrl: \"%s\"", uri );

/*------------------------------------------------------------------------*\
    Loop over all interfaces
\*------------------------------------------------------------------------*/
  for( walk=icklib->discoveryHandlers; walk; walk=walk->next ) {
    char   *prefix = walk->locationRoot;
    size_t  plen;

    // Harry way to skip over server name in prefix
    if( strlen(prefix)>8 ) { // strlen( "https://" );
      prefix += 8;
      prefix = strchr( prefix, '/' );
    }
    if( !prefix ) {
      logerr( "_ickDescrFindDicoveryHandlerByUrl: bad local location root \"%s\"", walk->locationRoot );
      continue;
    }
    plen = strlen( prefix );

    // First check match of location prefix
    if( strncmp(prefix,uri,plen) )
      continue;
    uri += plen;

    // Check for root device
    snprintf( buffer, sizeof(buffer), "/%s.xml", ICKDEVICE_STRING_ROOT );
    if( !strcmp(uri,buffer) ) {
      if( stype )
        *stype = ICKP2P_SERVICE_GENERIC;
      return walk;
    }

    // For services check if they are actually up
    snprintf( buffer, sizeof(buffer), "/%s.xml", ICKSERVICE_STRING_PLAYER );
    if( (walk->ickServices&ICKP2P_SERVICE_PLAYER) && !strcmp(uri,buffer) ) {
      if( stype )
        *stype = ICKP2P_SERVICE_PLAYER;
      return walk;
    }
    snprintf( buffer, sizeof(buffer), "/%s.xml", ICKSERVICE_STRING_SERVER );
    if( (walk->ickServices&ICKP2P_SERVICE_PLAYER) && !strcmp(uri,buffer) ) {
      if( stype )
        *stype = ICKP2P_SERVICE_SERVER_GENERIC;
      return walk;
    }
    snprintf( buffer, sizeof(buffer), "/%s.xml", ICKSERVICE_STRING_CONTROLLER );
    if( (walk->ickServices&ICKP2P_SERVICE_PLAYER) && !strcmp(uri,buffer) ) {
      if( stype )
        *stype = ICKP2P_SERVICE_CONTROLLER;
      return walk;
    }
    snprintf( buffer, sizeof(buffer), "/%s.xml", ICKSERVICE_STRING_DEBUG );
    if( (walk->ickServices&ICKP2P_SERVICE_PLAYER) && !strcmp(uri,buffer) ) {
      if( stype )
        *stype = ICKP2P_SERVICE_DEBUG;
      return walk;
    }

  } // next discovery handler

/*------------------------------------------------------------------------*\
    No match
\*------------------------------------------------------------------------*/
  return NULL;
}


/*=========================================================================*\
  Create an upnp device descriptor for a given ickstream service type
    this includes a corresponding HTTP header
    returns an allocated string (caller must free) or NULL on error
\*=========================================================================*/
char *_ickDescrGetDeviceDescr( const ickDiscovery_t *dh, struct libwebsocket *wsi, ickP2pServicetype_t stype )
{
  const _ickP2pLibContext_t *icklib = dh->icklib;
  int                        xlen, hlen;
  char                      *xmlcontent = NULL;
  char                      *type;
  char                      *message;
  char                       header[512];

/*------------------------------------------------------------------------*\
    Get type string
\*------------------------------------------------------------------------*/
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
                    "  <protocolLevel>%d</protocolLevel>"
                    " </device>\r\n"
                    "</root>",

                    ICKDEVICE_UPNP_MAJOR,
                    ICKDEVICE_UPNP_MINOR,
                    type,
                    icklib->deviceName,
                    ickUpnpNames.manufacturer,
                    ickUpnpNames.manufacturerUrl,
                    ickUpnpNames.modelDescriptor,
                    ickUpnpNames.modelName,
                    icklib->deviceUuid,
                    ICKPROTOCOL_P2P_CURRENT_SUPPORT
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
  Start retrieval of upnp descriptor for a device
    The device list must be locked by the caller
    returns 0 on success
\*=========================================================================*/
ickErrcode_t _ickDescrRetriveXml( upnp_device_t *device )
{
  ickXmlThread_t *xmlThread;
  int             rc;
  pthread_attr_t  attr;
  debug( "_ickDescrRetriveXml (%s): uri=\"%s\"", device->uuid, device->location );

/*------------------------------------------------------------------------*\
  Create thread descriptor
\*------------------------------------------------------------------------*/
  xmlThread = calloc( 1, sizeof(ickXmlThread_t) );
  if( !xmlThread ) {
    logerr( "_ickDescrRetriveXml: out of memory" );
    return ICKERR_NOMEM;
  }
  xmlThread->device = device;

/*------------------------------------------------------------------------*\
  Duplicate strings
\*------------------------------------------------------------------------*/
  xmlThread->uuid = strdup( device->uuid );
  xmlThread->uri  = strdup( device->location );
  if( !xmlThread->uuid || !xmlThread->uri ) {
    Sfree( xmlThread->uuid );
    Sfree( xmlThread->uri );
    Sfree( xmlThread );
    logerr( "_ickDescrRetriveXml: out of memory" );
    return ICKERR_NOMEM;
  }

/*------------------------------------------------------------------------*\
  Init Mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_init( &xmlThread->mutex, NULL );

/*------------------------------------------------------------------------*\
  We will not ever join this thread
\*------------------------------------------------------------------------*/
  pthread_attr_init( &attr );
  pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );

/*------------------------------------------------------------------------*\
  Start worker thread
\*------------------------------------------------------------------------*/
  rc = pthread_create( &xmlThread->thread, &attr, _xmlLoaderThread, xmlThread );
  if( rc ) {
    Sfree( xmlThread->uuid );
    Sfree( xmlThread->uri );
    Sfree( xmlThread );
    logerr( "_ickDescrRetriveXml: Unable to start thread: %s", strerror(rc) );
    return ICKERR_NOTHREAD;
  }

/*------------------------------------------------------------------------*\
  Link to list of threads
\*------------------------------------------------------------------------*/
//  pthread_mutex_lock( &device->xmlThreadsMutex );
  xmlThread->next = device->xmlThreads;
  if( device->xmlThreads )
    device->xmlThreads->prev = xmlThread;
  device->xmlThreads = xmlThread;
//  pthread_mutex_unlock( &device->xmlThreadsMutex );

/*------------------------------------------------------------------------*\
  That's all
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Lock a xml thread descriptor for access or modification
\*=========================================================================*/
void _ickXmlThreadLock( ickXmlThread_t *descr )
{
  debug ( "_ickXmlThreadLock (%s): locking...", descr->uri );
  pthread_mutex_lock( &descr->mutex );
  debug ( "_ickXmlThreadLock (%s): locked", descr->uri );
}

/*=========================================================================*\
  Unlock a xml thread descriptor
\*=========================================================================*/
void _ickXmlThreadUnlock( ickXmlThread_t *descr )
{
  debug ( "_ickXmlThreadUnlock (%s): locking...", descr->uri );
  pthread_mutex_unlock( &descr->mutex );
  debug ( "_ickXmlThreadUnlock (%s): locked", descr->uri );
}


/*=========================================================================*\
  Worker thread for retrieving XML data
    This is asynchronously operating on the device and executing the
    user callbacks. It will lock the device list of the discovery handler
    which will block the main thread! So callback execution is time critical.
    We have to deal with devices and discovery handlers vanishing.
\*=========================================================================*/
static void *_xmlLoaderThread( void *arg )
{
  ickXmlThread_t *xmlThread = arg;
  void          *xmlData;
  int            xmlSize;

  debug( "xml loader thread (%s): starting (%s)...", xmlThread->uuid, xmlThread->uri );
  PTHREADSETNAME( "XmlLoader" );

/*------------------------------------------------------------------------*\
  Load data
\*------------------------------------------------------------------------*/
  xmlData = miniwget( xmlThread->uri, &xmlSize, 5 );
  if( !xmlData ) {
    logerr( "xml loader thread (%s): could not get xml data.", xmlThread->uuid );
    goto terminate;
  }
  debug( "xml loader thread (%s): got descriptor \"%s\"", xmlThread->uuid, xmlData );

/*------------------------------------------------------------------------*\
  Try to find and lock the device
\*------------------------------------------------------------------------*/

/*------------------------------------------------------------------------*\
  Init xml parser
\*------------------------------------------------------------------------*/
#if 0
  struct _ick_xmlparser_s device_parser;
  device_parser.name = NULL;
  device_parser.level = 0;
  device_parser.writeme = 0;
  device_parser.protocolLevel = ICKPROTOCOL_P2P_GENERIC;

  struct xmlparser parser;
  /* xmlparser object */
  parser.xmlstart = data;
  parser.xmlsize = size;
  parser.data = &device_parser;
  parser.starteltfunc = _ick_parsexml_startelt;
  parser.endeltfunc = _ick_parsexml_endelt;
  parser.datafunc = _ick_parsexml_processelt;
  parser.attfunc = 0;
  parsexml(&parser);

  pthread_mutex_lock(&_device_mutex);
  iDev = _ickDeviceGet(UUID);  // need to re-check
  if (iDev) {
    iDev->xmlData = data;
    iDev->xmlSize = size;
    if ((device_parser.writeme && device_parser.name) && (iDev->name == NULL)) {
      iDev->name = device_parser.name;
      _ick_execute_DeviceCallback(iDev, ICKDISCOVERY_ADD_DEVICE);
      if (_discovery->exitCallback)
        _discovery->exitCallback();
    } else {
      if (device_parser.name)
        free(device_parser.name);
    }
    iDev->protocolLevel = device_parser.protocolLevel;
  }
  pthread_mutex_unlock(&_device_mutex);
#endif

/*------------------------------------------------------------------------*\
  Unlink from list of threads, if device is still active
\*------------------------------------------------------------------------*/
terminate:
  _ickXmlThreadLock( xmlThread );
  if( xmlThread->device ) {
    _ickDeviceLock( xmlThread->device );
    pthread_mutex_lock ( &xmlThread->device->xmlThreadsMutex );
    if( xmlThread->prev )
      xmlThread->prev->next = xmlThread->next;
    else
      xmlThread->device->xmlThreads = xmlThread->next;
    if( xmlThread->next )
      xmlThread->next->prev = xmlThread->prev;
    pthread_mutex_unlock ( &xmlThread->device->xmlThreadsMutex );
  }
  _ickXmlThreadUnlock( xmlThread );

/*------------------------------------------------------------------------*\
  Destroy Mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_destroy( &xmlThread->mutex );

/*------------------------------------------------------------------------*\
  Free strings and thread descriptor
\*------------------------------------------------------------------------*/
  Sfree( xmlThread->uuid );
  Sfree( xmlThread->uri );
  Sfree( xmlThread );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return NULL;
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
