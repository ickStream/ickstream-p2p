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

#ifndef __ICKSSDP_H
#define __ICKSSDP_H


/*=========================================================================*\
  Includes required by definitions from this file
\*=========================================================================*/
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "ickDescription.h"


/*=========================================================================*\
  Definition of constants
\*=========================================================================*/
#define ICKSSDP_DEFAULTLIVETIME 180
#define ICKSSDP_SEARCHINTERVAL  59
#define ICKSSDP_REPEATS         3
#define ICKSSDP_ANNOUNCEDIVIDOR 3
#define ICKSSDP_RNDDELAY        100
#define ICKSSDP_MSEARCH_MX      1
#define ICKSSDP_MCASTADDR       "239.255.255.250"
#define ICKSSDP_MCASTPORT       1900


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
  SSDP_METHOD_REPLY,
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
  int             livetime;
  int             mx;
};
typedef struct _ickSsdp_t ickSsdp_t;


//
// An ickstream device as discovered by the ssdp listener
//
struct _upnp_device;
typedef struct _upnp_device upnp_device_t;
struct _upnp_device {
  upnp_device_t       *prev;
  upnp_device_t       *next;
  ickDiscovery_t      *dh;         // weak
  pthread_mutex_t      mutex;
  int                  livetime;
  char                *uuid;       // strong
  char                *location;   // strong
  int                  ickVersion;
  ickP2pServicetype_t  services;
  char                *xmldata;

  // List of active xml retriever threads
  ickXmlThread_t      *xmlThreads;
  pthread_mutex_t      xmlThreadsMutex;


};

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
ickSsdp_t    *_ickSsdpParse( const char *buffer, size_t length, const struct sockaddr *addr );
void          _ickSsdpFree( ickSsdp_t *ssdp );
int           _ickSsdpExecute( ickDiscovery_t *dh, const ickSsdp_t *ssdp );
ickErrcode_t  _ickSsdpNewDiscovery( const ickDiscovery_t *dh );
void          _ickSsdpEndDiscovery( const ickDiscovery_t *dh );
ickErrcode_t  _ickSsdpAnnounceServices( ickDiscovery_t *dh, ickP2pServicetype_t service, ickSsdpMsgType_t mtype );
void          _ickDeviceLock( upnp_device_t *device );
void          _ickDeviceUnlock( upnp_device_t *device );



#endif /* __ICKSSDP_H */
