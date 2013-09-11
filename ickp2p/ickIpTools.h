/*$*********************************************************************\

Header File     : ickIpTools.h

Description     : Internal include file for IP related tool functions

Comments        : -

Date            : 15.08.2013

Updates         : -

Author          : JS, //MAF

Remarks         : -

*************************************************************************
 * Copyright (c) 2013, ickStream GmbH
 * All rights reserved.
\************************************************************************/

#ifndef __ICKIPTOOLS_H
#define __ICKIPTOOLS_H


/*=========================================================================*\
  Includes required by definitions from this file
\*=========================================================================*/
#include <arpa/inet.h>


/*=========================================================================*\
  Definition of constants
\*=========================================================================*/
#define ICK_IFRBUFFERSIZE 16384


/*=========================================================================*\
  Macro and type definitions
\*=========================================================================*/


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
int          _ickIpBind( int socket, in_addr_t addr, int port );
int          _ickIpAddMcast( int socket, in_addr_t ifaddr, in_addr_t maddr );
ickErrcode_t _ickIpGetIfAddr( const char *ifname, in_addr_t *addr, in_addr_t *netmask, char **name );
int          _ickIpGetFreePort( const char *ifname );
int          _ickIpGetSocketPort( int s );


#endif /* __ICKIPTOOLS_H */
