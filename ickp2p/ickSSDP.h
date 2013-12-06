/*$*********************************************************************\

Header File     : ickSSDP.h

Description     : Internal include file for upnp discovery protocol functions

Comments        : -

Date            : 15.08.2013

Updates         : -

Author          : JS, //MAF

Remarks         : -

*************************************************************************
 * Copyright (c) 2013, ickStream GmbH
 * All rights reserved.
\************************************************************************/

#ifndef __ICKSSDP_H
#define __ICKSSDP_H


/*=========================================================================*\
  Includes required by definitions from this file
\*=========================================================================*/
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "ickDescription.h"


/*=========================================================================*\
  Definition of constants
\*=========================================================================*/
#define ICKSSDP_DEFAULTLIFETIME 180
#define ICKSSDP_SEARCHINTERVAL  59
#define ICKSSDP_REPEATS         3
#define ICKSSDP_ANNOUNCEDIVIDOR 3
#define ICKSSDP_RNDDELAY        100
#define ICKSSDP_MSEARCH_MX      1
#define ICKSSDP_MCASTADDR       "239.255.255.250"
#define ICKSSDP_MCASTPORT       1900
#define ICKSSDP_INITIALDELAY    500

/*=========================================================================*\
  Macro and type definitions
\*=========================================================================*/

//
// Types for a parsed SSDP request
//
typedef enum {
  SSDP_METHOD_UNDEFINED = 0,
  SSDP_METHOD_MSEARCH,
  SSDP_METHOD_NOTIFY,
  SSDP_METHOD_REPLY
} ssdp_method_t;

typedef enum {
  SSDP_NTS_UNDEFINED = 0,
  SSDP_NTS_ALIVE,
  SSDP_NTS_UPDATE,
  SSDP_NTS_BYEBYE
} ssdp_nts_t;

//
// SSDP message types
//
typedef enum {
  SSDPMSGTYPE_ALIVE,
  SSDPMSGTYPE_BYEBYE,
  SSDPMSGTYPE_UPDATE,
  SSDPMSGTYPE_MRESPONSE,
  SSDPMSGTYPE_MSEARCH
} ickSsdpMsgType_t;

//
// Container for a parsed ssdp packet
//
struct _ickSsdp_t {
  char           *buffer;   // strong, will be modified during parsing
  struct sockaddr addr;
  ssdp_method_t   method;
  ssdp_nts_t      nts;
  const char     *server;   // weak
  const char     *usn;      // weak
  char           *uuid;     // strong
  const char     *location; // weak
  const char     *nt;       // weak
  const char     *st;       // weak
  long            bootid;
  long            configid;
  long            nextbootid;
  int             lifetime;
  int             mx;
};
typedef struct _ickSsdp_t ickSsdp_t;

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
int           _ickSsdpCreateListener( in_addr_t ifaddr, int port );
ickSsdp_t    *_ickSsdpParse( const char *buffer, size_t length, const struct sockaddr *addr, int port );
void          _ickSsdpFree( ickSsdp_t *ssdp );
int           _ickSsdpExecute( ickP2pContext_t *ictx, const ickSsdp_t *ssdp );
ickErrcode_t  _ickSsdpNewDiscovery( ickP2pContext_t *ictx );
void          _ickSsdpEndDiscovery( ickP2pContext_t *ictx );
ickErrcode_t  _ssdpNewInterface( ickP2pContext_t *ictx );
ickErrcode_t  _ssdpByebyeInterface( ickP2pContext_t *ictx, ickInterface_t *interface );


void          _ickDeviceExpireTimerCb( const ickTimer_t *timer, void *data, int tag );


#endif /* __ICKSSDP_H */
