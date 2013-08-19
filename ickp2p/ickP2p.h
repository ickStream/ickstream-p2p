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
  ICKP2P_EXPIRED
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


struct _ickDiscovery;
typedef struct _ickDiscovery ickDiscovery_t;


/*------------------------------------------------------------------------*\
  Macros
\*------------------------------------------------------------------------*/
// none


/*------------------------------------------------------------------------*\
  Signatures for function pointers
\*------------------------------------------------------------------------*/
typedef void         (*ickP2pEndCb_t)( void );
typedef ickErrcode_t (*ickP2pSuspendCb_t)( void );
typedef ickErrcode_t (*ickDiscoveryEndCb_t)( ickDiscovery_t *dh );
typedef void         (*ickDiscoveryDeviceCb_t)( ickDiscovery_t *dh, const char *uuid, ickP2pDeviceCommand_t change, ickP2pServicetype_t type );
typedef ickErrcode_t (*ickDiscoveryMessageCb_t)( ickDiscovery_t *dh, const char *sourceUuid, ickP2pServicetype_t sourceService, ickP2pServicetype_t targetService, const char* message );
typedef void         (*ickP2pLogFacility_t)( const char *file, int line, int prio, const char * format, ... );


/*=========================================================================*\
  Public prototypes
\*=========================================================================*/

const char       *ickP2pGetVersion( int *major, int *minor );
const char       *ickP2pGitVersion( void );

ickErrcode_t      ickP2pInit( const char *deviceName, const char *deviceUuid, int liveTime, long bootId, long configId );
ickErrcode_t      ickP2pEnd( ickP2pEndCb_t callback );
ickErrcode_t      ickP2pSuspend( ickP2pSuspendCb_t callback  );
ickErrcode_t      ickP2pResume( void );
ickErrcode_t      ickP2pSetDeviceName( const char *deviceName );
const char       *ickP2pGetDeviceName( void );
const char       *ickP2pGetDeviceUuid( void );
ickP2pLibState_t  ickP2pGetState( void );
int               ickP2pGetLiveTime( void );
long              ickP2pGetBootId( void );
long              ickP2pGetConfigId( void );
const char       *ickP2pGetOsName( void );
ickErrcode_t      ickP2pRegisterDiscoveryCallback( ickDiscoveryDeviceCb_t callback );
ickErrcode_t      ickP2pRemoveDiscoveryCallback( ickDiscoveryDeviceCb_t callback );

ickDiscovery_t   *ickP2pDiscoveryInit( const char *interface, int port, const char *upnpFolder, ickDiscoveryEndCb_t callback, ickErrcode_t *error );
ickErrcode_t      ickP2pDiscoveryEnd( ickDiscovery_t *dh );
ickErrcode_t      ickP2pDiscoveryRegisterMessageCallback( ickDiscovery_t *dh, ickDiscoveryMessageCb_t callback );
ickErrcode_t      ickDeviceRemoveMessageCallback( ickDiscovery_t *dh, ickDiscoveryMessageCb_t callback );
ickErrcode_t      ickP2pDiscoveryAddService( ickDiscovery_t *dh, ickP2pServicetype_t type );
ickErrcode_t      ickDiscoveryRemoveService( ickDiscovery_t *dh, ickP2pServicetype_t type );
const char       *ickP2pDiscoveryGetIf( const ickDiscovery_t *dh );
int               ickP2pDiscoveryGetPort( const ickDiscovery_t *dh );

ickP2pServicetype_t ickP2pDiscoveryGetDeviceType( const ickDiscovery_t *dh, const char *uuid );
char             *ickP2pDiscoveryGetDeviceName( const ickDiscovery_t *dh, const char *uuid );
char             *ickP2pDiscoveryGetDeviceURL( const ickDiscovery_t *dh, const char *uuid );
int               ickP2pDiscoveryGetDevicePort( const ickDiscovery_t *dh, const char *uuid );

ickErrcode_t      ickP2pSendMsg( const ickDiscovery_t *dh, const char *uuid, ickP2pServicetype_t targetServices, ickP2pServicetype_t sourceService, const char *message );
int               ickP2pGetSendQueueLength( const ickDiscovery_t *dh );

ickErrcode_t      ickP2pDiscoverySetDebugging( int on );
char             *ickP2pGetLocalDebugInfoForDevice( const ickDiscovery_t *dh, const char *uuid );
char             *ickP2pGetLocalDebugInfo( const ickDiscovery_t *dh );
char             *ickP2pGetRemoteDebugInfoForDevice( const ickDiscovery_t *dh, const char *remoteUuid, const char uuid );
char             *ickP2pGetRemoteDebugInfo( const ickDiscovery_t *dh, const char *remoteUuid );


void            ickP2pSetLogFacility( ickP2pLogFacility_t facility );
void            ickP2pSetLogLevel( int level );

const char     *ickStrError( ickErrcode_t code );


#endif /* __ICKP2P_H */
