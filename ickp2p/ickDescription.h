/*$*********************************************************************\

Header File     : ickDescription.h

Description     : Internal include file for upnp layer 2 functions

Comments        : -

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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\************************************************************************/

#ifndef __ICKDESCRIPTION_H
#define __ICKDESCRIPTION_H


/*=========================================================================*\
  Includes required by definitions from this file
\*=========================================================================*/
#include "ickP2p.h"
#include "ickWGet.h"


/*=========================================================================*\
  Definition of constants
\*=========================================================================*/

//
// Some HTTP headers
//
#define HTTP_200   "HTTP/1.0 200 OK\r\n" \
                   "Server: libwebsockets\r\n" \
                   "Content-Type: %s\r\n" \
                   "Content-Length: %ld\r\n" \
                   "\r\n"
#define HTTP_400   "HTTP/1.0 400 Bad Request\r\n" \
                   "Server: libwebsockets\r\n" \
                   "\r\n"
#define HTTP_404   "HTTP/1.0 404 Not Found\r\n" \
                   "Server: libwebsockets\r\n" \
                   "Content-Length: 0\r\n" \
                   "\r\n"


/*=========================================================================*\
  Macro and type definitions
\*=========================================================================*/

#define ICKP2P_WS_PROTOCOLNAME "ickstream-p2p-message-protocol"

//
// Default values of ickUpnpNames
//
#define ICKDEVICE_MANUFACTURER           "ickStream"
#define ICKDEVICE_MANUFACTURERURL        "http://ickstream.com"
#define ICKDEVICE_MODELDESCRIPTION       "ickStreamDevice"
#define ICKDEVICE_MODELNAME              "ickStreamDevice"
#define ICKDEVICE_PRODUCTANDVERSION      "ickStreamDevice/1.0"

//
// Announced UPnP version
//
#define ICKDEVICE_UPNP_MAJOR             1
#define ICKDEVICE_UPNP_MINOR             0

//
// Upnp strings defining ickstream devices and services
//
#define ICKDEVICE_TYPESTR_PREFIX         "urn:schemas-ickstream-com:device:"
#define ICKDEVICE_STRING_ROOT            "Root"
#define ICKDEVICE_TYPESTR_ROOT           "urn:schemas-ickstream-com:device:Root:1"
#define ICKDEVICE_URI_ROOT               "/Root.xml"

//
// ickstream preamble elements
// this is used to negotiate the protocol on websocket level, don't confuse with upnp device/service level
//
typedef enum {
  ICKP2PLEVEL_GENERIC         = 0,     // first protocol version or unknown
  ICKP2PLEVEL_TARGETSERVICES  = 0x01,  // include target service type with messages
  ICKP2PLEVEL_SOURCESERVICE   = 0x02,  // include source service in message
  ICKP2PLEVEL_SUPPORTED       = 0x03,  // that's what we currently support: including the service types
  ICKP2PLEVEL_DEFAULT         = 0,     // that's what we currently use as the default
  ICKP2PLEVEL_INVALID         = 0xf0   // mask to find illegal codes. Used to be backward compatible with previous implementations usually starting messages with "{" or "[". Should be deprecated until launch, then we can use 8 bits for protocol properties
} ickP2pLevel_t;


/*------------------------------------------------------------------------*\
  Macros
\*------------------------------------------------------------------------*/

/*------------------------------------------------------------------------*\
  Signatures for function pointers
\*------------------------------------------------------------------------*/


/*=========================================================================*\
  Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Internal prototypes
\*=========================================================================*/
char         *_ickDescrGetDeviceDescr( const ickP2pContext_t *ictx, struct libwebsocket *wsi );
ickErrcode_t  _ickWGetXmlCb( ickWGetContext_t *context, ickWGetAction_t action, int arg );


#endif /* __ICKDESCRIPTION_H */
