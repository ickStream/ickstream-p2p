/*$*********************************************************************\

Source File     : ickDescription.c

Description     : implement ickp2p upnp layer 2 (description) functions

Comments        : -

Called by       : libwebsocket http server

Calls           : internal ickstream functions

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

#include "ickP2p.h"
#include "ickP2pInternal.h"
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
// none


/*=========================================================================*\
  Private prototypes
\*=========================================================================*/
// none


/*=========================================================================*\
  Private symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Find a discovery handler for an UPnp description request over HTTP
    caller must lock discovery handler list
    returns NULL if no match
\*=========================================================================*/
ickDiscovery_t *_ickDescrFindDicoveryHandlerByUrl( const _ickP2pLibContext_t *icklib, const char *uri )
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
    if( !strcmp(uri,buffer) )
      return walk;

    // For services check if they are actually up
    snprintf( buffer, sizeof(buffer), "/%s.xml", ICKSERVICE_STRING_PLAYER );
    if( (walk->ickServices&ICKP2P_SERVICE_PLAYER) && !strcmp(uri,buffer) )
      return walk;
    snprintf( buffer, sizeof(buffer), "/%s.xml", ICKSERVICE_STRING_SERVER );
    if( (walk->ickServices&ICKP2P_SERVICE_PLAYER) && !strcmp(uri,buffer) )
      return walk;
    snprintf( buffer, sizeof(buffer), "/%s.xml", ICKSERVICE_STRING_CONTROLLER );
    if( (walk->ickServices&ICKP2P_SERVICE_PLAYER) && !strcmp(uri,buffer) )
      return walk;
    snprintf( buffer, sizeof(buffer), "/%s.xml", ICKSERVICE_STRING_DEBUG );
    if( (walk->ickServices&ICKP2P_SERVICE_PLAYER) && !strcmp(uri,buffer) )
      return walk;

  } // next discovery handler

/*------------------------------------------------------------------------*\
    No match
\*------------------------------------------------------------------------*/
  return NULL;
}


/*=========================================================================*\
  Serve upnp device descriptor
    returns libwebsocket status (0 - continue, 1 - finished, -1 - error)
\*=========================================================================*/
int _ickDescrServeDeviceDescr( const ickDiscovery_t *dh, struct libwebsocket *wsi )
{
  const _ickP2pLibContext_t *icklib = dh->icklib;
  int                        xlen, hlen, len;
  char                      *xmlcontent = NULL;
  char                      *message;
  char                       header[512];

/*------------------------------------------------------------------------*\
    Construct XML payload
    "UPnP Device Architecture 1.1": chapter 2.3
    fixme: get rid of protocolLevel tag and code that in the service versions
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
                    ICKDEVICE_TYPESTR_ROOT,
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
    logerr( "_ickDesrcServeDeviceDescr: out of memory" );
    return -1;
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
    logerr( "_ickDesrcServeDeviceDescr: out of memory" );
    return -1;
  }
  strcpy( message, header );
  strcpy( message+hlen, xmlcontent );
  Sfree( xmlcontent );

  debug( "_ickDescrServeDeviceDescr: sending upnp descriptor \"%s\"", message );

/*------------------------------------------------------------------------*\
  Tty to transmit message
\*------------------------------------------------------------------------*/
  len = libwebsocket_write( wsi, (unsigned char*)message, hlen+xlen, LWS_WRITE_HTTP );
  if( len<0 ) {
    Sfree( message );
    logerr( "_ickDesrcServeDeviceDescr: lws write failed" );
    return -1;
  }
  else if( len!=hlen+xlen ) {
    // fixme: in that case we should use a buffer and queue a writable callback...
    Sfree( message );
    logerr( "_ickDesrcServeDeviceDescr: truncated lws write (%d of %d bytes)",
            len, hlen+xlen );
    return -1;
  }

/*------------------------------------------------------------------------*\
  Free buffer and return
\*------------------------------------------------------------------------*/
  Sfree( message );
  return 0;
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
