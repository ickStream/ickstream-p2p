/*$*********************************************************************\

Header File     : ickP2p.h

Description     : Public include file for libickp2p

Comments        : See http://wiki.ickstream.com/index.php/API/ickP2P_Protocol
                  for detailed description.

Date            : 11.08.2013

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

#ifndef __ICKP2P_H
#define __ICKP2P_H


/*=========================================================================*\
  Includes required by definitions from this file
\*=========================================================================*/
// none


/*=========================================================================*\
  Definition of constants
\*=========================================================================*/
// none


/*=========================================================================*\
  Macro and type definitions
\*=========================================================================*/

// General error codes
typedef enum {
  ICKERR_SUCCESS = 0,
  ICKERR_GENERIC,
  ICKERR_NOTIMPLEMENTED,
  ICKERR_INVALID,
  ICKERR_UNINITIALIZED,
  ICKERR_INITIALIZED,
  ICKERR_WRONGSTATE,
  ICKERR_NOMEM,
  ICKERR_NOTHREAD,
  ICKERR_NOINTERFACE,
  ICKERR_NOSOCKET,
  ICKERR_NODEVICE,
  ICKERR_BADURI,
  ICKERR_NOTCONNECTED,
  ICKERR_ISCONNECTED,
  ICKERR_LWSERR,
  ICKERR_MAX
} ickErrcode_t;

// Global state of library
typedef enum {
  ICKLIB_UNINITIALIZED,
  ICKLIB_INITIALIZING,
  ICKLIB_SUSPENDED,
  ICKLIB_RESUMING,
  ICKLIB_RUNNING,
  ICKLIB_TERMINATING
} ickP2pLibState_t;

// Modes of device discovery callback
typedef enum {
  ICKP2P_ADD,
  ICKP2P_REMOVE,
  ICKP2P_EXPIRED,
  ICKP2P_TERMINATE
} ickP2pDeviceCommand_t;

// Service types
typedef enum {
  ICKP2P_SERVICE_NONE              = -1,
  ICKP2P_SERVICE_GENERIC           = 0,
  ICKP2P_SERVICE_PLAYER            = 0x1,
  ICKP2P_SERVICE_CONTROLLER        = 0x2,
  ICKP2P_SERVICE_SERVER_GENERIC    = 0x4,
  ICKP2P_SERVICE_DEBUG             = 0x8,
  ICKP2P_SERVICE_MAX               = ICKP2P_SERVICE_DEBUG,
  ICKP2P_SERVICE_ANY               = 2*ICKP2P_SERVICE_MAX-1,
} ickP2pServicetype_t;

// Names used in upnp descriptions
typedef struct {
  const char *manufacturer;
  const char *manufacturerUrl;
  const char *modelDescriptor;
  const char *modelName;
} ickUpnpNames_t;


struct _ickP2pContext;
typedef struct _ickP2pContext ickP2pContext_t;


/*------------------------------------------------------------------------*\
  Macros
\*------------------------------------------------------------------------*/
// none


/*------------------------------------------------------------------------*\
  Signatures for function pointers
\*------------------------------------------------------------------------*/
typedef void         (*ickP2pEndCb_t)( ickP2pContext_t *ictx );
typedef ickErrcode_t (*ickP2pSuspendCb_t)( void );
typedef void         (*ickP2pDeviceCb_t)( ickP2pContext_t *ictx, const char *uuid, ickP2pDeviceCommand_t change, ickP2pServicetype_t type );
typedef ickErrcode_t (*ickP2pMessageCb_t)( ickP2pContext_t *ictx, const char *sourceUuid, ickP2pServicetype_t sourceService, ickP2pServicetype_t targetService, const char* message, size_t mSize );
typedef void         (*ickP2pLogFacility_t)( const char *file, int line, int prio, const char * format, ... );


/*=========================================================================*\
  Public global symbols
\*=========================================================================*/

// Names used in upnp descriptions. Might be set by application prior to ickP2pInit();
extern ickUpnpNames_t ickUpnpNames;


/*=========================================================================*\
  Public prototypes
\*=========================================================================*/

const char       *ickP2pGetVersion( int *major, int *minor );
const char       *ickP2pGitVersion( void );
void              ickP2pSetLogFacility( ickP2pLogFacility_t facility );
void              ickP2pSetLogLevel( int level );
const char       *ickStrError( ickErrcode_t code );

ickP2pContext_t     *ickP2pInit( const char *deviceName, const char *deviceUuid, const char *upnpFolder, int liveTime, long bootId, long configId, const char *hostname, const char *ifname, int port, ickP2pDeviceCb_t deviceCb, ickP2pMessageCb_t messageCb, ickErrcode_t *error );
ickErrcode_t         ickP2pEnd( ickP2pContext_t *ictx, ickP2pEndCb_t callback );
ickErrcode_t         ickP2pSuspend( ickP2pContext_t *ictx, ickP2pSuspendCb_t callback  );
ickErrcode_t         ickP2pResume( ickP2pContext_t *ictx );
const char          *ickP2pGetOsName( const ickP2pContext_t *ictx );
ickErrcode_t         ickP2pSetName( ickP2pContext_t *ictx, const char *name );
const char          *ickP2pGetName( const ickP2pContext_t *ictx );
const char          *ickP2pGetDeviceUuid( const ickP2pContext_t *ictx );
const char          *ickP2pGetUpnpFolder( const ickP2pContext_t *ictx );
ickP2pLibState_t     ickP2pGetState( const ickP2pContext_t *ictx );
int                  ickP2pGetLiveTime( const ickP2pContext_t *ictx );
const char          *ickP2pGetHostname( const ickP2pContext_t *ictx );
const char          *ickP2pGetIf( const ickP2pContext_t *ictx );
int                  ickP2pGetLwsPort( const ickP2pContext_t *ictx );
int                  ickP2pGetUpnpPort( const ickP2pContext_t *ictx );
long                 ickP2pGetBootId( const ickP2pContext_t *ictx );
long                 ickP2pGetConfigId( const ickP2pContext_t *ictx );
ickErrcode_t         ickP2pDiscoveryAddService( ickP2pContext_t *ictx, ickP2pServicetype_t type );
ickErrcode_t         ickDiscoveryRemoveService( ickP2pContext_t *ictx, ickP2pServicetype_t type );

ickP2pServicetype_t  ickP2pGetDeviceType( const ickP2pContext_t *ictx, const char *uuid );
char                *ickP2pGetDeviceName( const ickP2pContext_t *ictx, const char *uuid );
char                *ickP2pGetDeviceURL( const ickP2pContext_t *ictx, const char *uuid );
int                  ickP2pGetDevicePort( const ickP2pContext_t *ictx, const char *uuid );

ickErrcode_t         ickP2pSendMsg( ickP2pContext_t *ictx, const char *uuid, ickP2pServicetype_t targetServices, ickP2pServicetype_t sourceService, const char *payload, size_t pSize );

ickErrcode_t         ickP2pDiscoverySetDebugging( ickP2pContext_t *ictx, int on );
char                *ickP2pGetLocalDebugInfoForDevice( ickP2pContext_t *ictx, const char *uuid );
char                *ickP2pGetLocalDebugInfo( ickP2pContext_t *ictx );
char                *ickP2pGetRemoteDebugInfoForDevice( const ickP2pContext_t *ictx, const char *remoteUuid, const char uuid );
char                *ickP2pGetRemoteDebugInfo( const ickP2pContext_t *ictx, const char *remoteUuid );


#endif /* __ICKP2P_H */
