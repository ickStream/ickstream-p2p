/*$*********************************************************************\

Header File     : ickDiscoveryh

Description     : Internal include file for upnp discovery functions

Comments        : -

Date            : 14.08.2013

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

#ifndef __ICKDISCOVERY_H
#define __ICKDISCOVERY_H


/*=========================================================================*\
  Includes required by definitions from this file
\*=========================================================================*/

#include <pthread.h>

#include "ickP2p.h"
#include "ickP2pInternal.h"


/*=========================================================================*\
  Definition of constants
\*=========================================================================*/
// none

/*=========================================================================*\
  Macro and type definitions
\*=========================================================================*/

struct _upnp_device;
typedef struct _upnp_device upnp_device_t;

struct _upnp_service;
typedef struct _upnp_service upnp_service_t;

//
// struct defining a discovery handler.
// consolidated to contain connection information.
// holds socket for server side loopback connection
//
struct _ickDiscovery {
  ickDiscovery_t              *next;
  int                          ttl;
  pthread_mutex_t              mutex;
  int                          socket;

  char                        *interface;      // strong
  int                          port;
  char                        *location;       // strong
  char                        *upnpFolder;     // strong
  ickP2pServicetype_t          services;       // strong

  // List of remote devices seen by this interface
  upnp_device_t               *deviceList;
  pthread_mutex_t              deviceListMutex;

  // List of local services offered to the world
  ickP2pServicetype_t          ickServices;


  int                          wsPort;
//  struct libwebsocket         *wsi; // server side loopback connection.

  ickDiscoveryEndCb_t          exitCallback;
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
extern ickDiscovery_t  *_ickDiscoveryHandlerList;
extern pthread_mutex_t  _ickDiscoveryHandlerListMutex;



/*=========================================================================*\
  Private prototypes
\*=========================================================================*/
void _ickDiscoveryExecDeviceCallback( ickDiscovery_t *dh, const upnp_device_t *dev, ickP2pDeviceCommand_t change, ickP2pServicetype_t type );

// Manage device list of a discovery handler
void           _ickDiscoveryDeviceListLock( ickDiscovery_t *dh );
void           _ickDiscoveryDeviceListUnlock( ickDiscovery_t *dh );
void           _ickDiscoveryDeviceAdd( ickDiscovery_t *dh, upnp_device_t *dev );
void           _ickDiscoveryDeviceRemove( ickDiscovery_t *dh, upnp_device_t *dev );
upnp_device_t *_ickDiscoveryDeviceFind( const ickDiscovery_t *dh, const char *uuid );


#endif /* __ICKDISCOVERY_H */
