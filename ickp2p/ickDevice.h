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
#include "ickWGet.h"


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
  ickMessage_t        *next;
  ickMessage_t        *prev;
  double               tCreated;
  unsigned char       *payload;    // strong
  size_t               size;
  size_t               issued;
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
// Type of device connection
//
typedef enum {
  ICKDEVICE_NOTCONNECTED,
  ICKDEVICE_CLIENTCONNECTING,
  ICKDEVICE_ISCLIENT,
  ICKDEVICE_ISSERVER
} ickDeviceConnection_t;

//
// An ickstream device
//
struct _ickDevice {
  ickDevice_t          *prev;
  ickDevice_t          *next;
  ickP2pContext_t      *ictx;            // weak
  pthread_mutex_t       mutex;
  ickDeviceType_t       type;
  int                   lifetime;
  char                 *uuid;          // strong
  char                 *location;      // strong
  int                   ickUpnpVersion;
  ickP2pServicetype_t   services;
  char                 *friendlyName;  // strong
  ickP2pLevel_t         ickP2pLevel;
  ickMessage_t         *outQueue;
  double                tCreation;
  ickWGetContext_t     *wget;
  double                tXmlComplete;
  int                   doConnect;
  double                tConnect;
  double                tDisconnect;
  ickDeviceConnection_t connectionState;
  int                   nRx;
  int                   nRxSegmented;
  int                   nTx;
  double                tLastRx;
  double                tLastTx;

  struct libwebsocket  *wsi;

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
