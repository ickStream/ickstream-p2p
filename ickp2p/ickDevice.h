/*$*********************************************************************\

Header File     : ickDevice.h

Description     : Internal include file for device descriptor functions

Comments        : -

Date            : 25.08.2013

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

#ifndef __ICKDEVICE_H
#define __ICKDEVICE_H


/*=========================================================================*\
  Includes required by definitions from this file
\*=========================================================================*/
#include <pthread.h>
#include <libwebsockets.h>
#include "ickP2p.h"
#include "ickDescription.h"


/*=========================================================================*\
  Definition of constants
\*=========================================================================*/
// none

/*=========================================================================*\
  Macro and type definitions
\*=========================================================================*/

//
// Descriptor for ickstream messages
//
struct _ickMessage;
typedef struct _ickMessage ickMessage_t;
struct _ickMessage {
  ickMessage_t  *next;
  ickMessage_t  *prev;
  double         tCreated;
  unsigned char *payload;    // strong
  size_t         size;
  size_t         issued;
};

//
// Type of device creation
//
typedef enum {
  ICKDEVICE_LOOPBACK,
  ICKDEVICE_SSDP,
  ICKDEVICE_WS
} ickDeviceType_t;

//
// An ickstream device
//
struct _ickDevice;
typedef struct _ickDevice ickDevice_t;
struct _ickDevice {
  ickDevice_t         *prev;
  ickDevice_t         *next;
  ickP2pContext_t     *ictx;            // weak
  pthread_mutex_t      mutex;
  ickDeviceType_t      type;
  int                  lifetime;
  char                *uuid;          // strong
  char                *location;      // strong
  int                  ickUpnpVersion;
  ickP2pServicetype_t  services;
  char                *friendlyName;  // strong
  ickP2pLevel_t        ickP2pLevel;
  ickMessage_t        *outQueue;
  double               tCreation;
  double               tXmlComplete;
  int                  doConnect;
  double               tConnect;
  double               tDisconnect;
  int                  localIsServer;
  int                  nRx;
  int                  nRxSegmented;
  int                  nTx;
  double               tLastRx;
  double               tLastTx;

  struct libwebsocket *wsi;

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
ickDevice_t  *_ickDeviceNew( const char *uuid, ickDeviceType_t type );
void          _ickDeviceFree( ickDevice_t *device );
void          _ickDeviceLock( ickDevice_t *device );
void          _ickDeviceUnlock( ickDevice_t *device );

ickErrcode_t  _ickDeviceSetLocation( ickDevice_t *device, const char *location );
ickErrcode_t  _ickDeviceSetName( ickDevice_t *device, const char *name );
ickErrcode_t  _ickDeviceAddMessage( ickDevice_t *device, void *container, size_t size );
ickErrcode_t  _ickDeviceRemoveAndFreeMessage( ickDevice_t *device, ickMessage_t *message );
ickMessage_t *_ickDeviceOutQueue( ickDevice_t *device );
int           _ickDevicePendingMessages( ickDevice_t *device );
size_t        _ickDevicePendingBytes( ickDevice_t *device );


#endif /* __ICKSSDP_H */
