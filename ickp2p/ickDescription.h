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
// none


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

//
// Default values of ickUpnpNames
//
#define ICKDEVICE_MANUFACTURER           "ickStream"
#define ICKDEVICE_MANUFACTURERURL        "http://ickstream.com"
#define ICKDEVICE_MODELDESCRIPTION       "ickStreamDevice"
#define ICKDEVICE_MODELNAME              "ickStreamDevice"

//
// strings defining ickstream devices and services
//
#define ICKDEVICE_STRING_ROOT            "Root"
#define ICKSERVICE_STRING_PLAYER         "Player"
#define ICKSERVICE_STRING_SERVER         "Server"
#define ICKSERVICE_STRING_CONTROLLER     "Controller"
#define ICKSERVICE_STRING_DEBUG          "Debug"

//
// Upnp strings defining ickstream devices and services
// Note: Versions must match for all USNs
//
#define ICKDEVICE_UPNP_MAJOR             1
#define ICKDEVICE_UPNP_MINOR             0
#define ICKDEVICE_TYPESTR_ROOT           "urn:schemas-ickstream-com:device:Root:1"
#define ICKSERVICE_TYPESTR_PLAYER        "urn:schemas-ickstream-com:device:Player:1"
#define ICKSERVICE_TYPESTR_SERVER        "urn:schemas-ickstream-com:device:Server:1"
#define ICKSERVICE_TYPESTR_CONTROLLER    "urn:schemas-ickstream-com:device:Controller:1"
#define ICKSERVICE_TYPESTR_DEBUG         "urn:schemas-ickstream-com:device:Debug:1"

//
// ickstream protocol level
// fixme: use USN version field for this
//
typedef enum ickDiscovery_protocol_level {
  ICKPROTOCOL_P2P_GENERIC             = 0,    // first protocol version or unknown
  ICKPROTOCOL_P2P_INCLUDE_SERVICETYPE = 0x1,  // include target servicetype with messages
  ICKPROTOCOL_P2P_INCLUDE_TARGETUUID  = 0x4,  // include target UUID with services (when supporting more than one UUID per websocket)
  ICKPROTOCOL_P2P_INCLUDE_SOURCEUUID  = 0x8,  // include source UUID with services (when supporting more than one UUID per websocket)
  ICKPROTOCOL_P2P_CURRENT_SUPPORT     = 0x1,  // that's what we currently support: including the service type
  ICKPROTOCOL_P2P_DEFAULT             = 0,    // that's what we curreently use as the default
  ICKPROTOCOL_P2P_INVALID             = 0xe0   // mask to find illegal codes. Used to be backward compatible with previous implementations usually starting messages with "{" or "[". Should be deprecated until launch, then we can use 8 bits for protocol properties
} ickDiscoveryProtocolLevel_t;


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
ickDiscovery_t *_ickDescrFindDicoveryHandlerByUrl( const _ickP2pLibContext_t *icklib, const char *uri );
int             _ickDescrServeDeviceDescr( const ickDiscovery_t *dh, struct libwebsocket *wsi );



#endif /* __ICKDESCRIPTION_H */
