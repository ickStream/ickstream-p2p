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
#define ICKDEVICE_UPNP_MINOR             1

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
  ICKP2PLEVEL_MESSAGEFLAGS    = 0x04,  // include message flags in message
  ICKP2PLEVEL_SUPPORTED       = 0x07,  // that's what we currently support: including the service types
  ICKP2PLEVEL_DEFAULT         = 0,     // that's what we currently use as the default
  ICKP2PLEVEL_INVALID         = 0xf8   // mask to find illegal codes. Used to be backward compatible with previous implementations usually starting messages with "{" or "[". Should be deprecated until launch, then we can use 8 bits for protocol properties
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
