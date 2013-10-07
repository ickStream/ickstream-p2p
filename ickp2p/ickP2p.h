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
\************************************************************************/

#ifndef __ICKP2P_H
#define __ICKP2P_H


/*=========================================================================*\
  Includes required by definitions from this file
\*=========================================================================*/
#include <stdio.h>
#include <stddef.h>

/*=========================================================================*\
  Definition of constants
\*=========================================================================*/
#define ICK_P2PENABLEDEBUGAPI


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
  ICKERR_NOMEMBER,
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
  ICKLIB_CREATED,
  ICKLIB_RUNNING,
  ICKLIB_SUSPENDED,
  ICKLIB_TERMINATING
} ickP2pLibState_t;

// Modes of device discovery callback
typedef enum {
  ICKP2P_INITIALIZED = 1,
  ICKP2P_CONNECTED,
  ICKP2P_DISCONNECTED,
  ICKP2P_DISCOVERED,
  ICKP2P_BYEBYE,
  ICKP2P_EXPIRED,
  ICKP2P_TERMINATE,
  ICKP2P_INVENTORY,
  ICKP2P_ERROR
} ickP2pDeviceState_t;

// Service types
typedef enum {
  ICKP2P_SERVICE_NONE              = -1,
  ICKP2P_SERVICE_GENERIC           = 0,
  ICKP2P_SERVICE_PLAYER            = 0x1,
  ICKP2P_SERVICE_CONTROLLER        = 0x2,
  ICKP2P_SERVICE_SERVER_GENERIC    = 0x4,
  ICKP2P_SERVICE_DEBUG             = 0x8,
  ICKP2P_SERVICE_MAX               = ICKP2P_SERVICE_DEBUG,
  ICKP2P_SERVICE_ANY               = 2*ICKP2P_SERVICE_MAX-1
} ickP2pServicetype_t;

// Flags for messages (used in message callback)
typedef enum {
  ICKP2P_MESSAGEFLAG_NONE         = 0,
  ICKP2P_MESSAGEFLAG_STRING       = 0x01,
  ICKP2P_MESSAGEFLAG_NOTIFICATION = 0x02
} ickP2pMessageFlag_t;

// Names used in upnp descriptions
typedef struct {
  const char *manufacturer;
  const char *manufacturerUrl;
  const char *modelDescriptor;
  const char *modelName;
  const char *productAndVersion;
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
typedef void  (*ickP2pEndCb_t)( ickP2pContext_t *ictx );
typedef void  (*ickP2pDiscoveryCb_t)( ickP2pContext_t *ictx, const char *uuid, ickP2pDeviceState_t change, ickP2pServicetype_t type );
typedef void  (*ickP2pMessageCb_t)( ickP2pContext_t *ictx, const char *sourceUuid, ickP2pServicetype_t sourceService, ickP2pServicetype_t targetServices, const char *message, size_t mSize, ickP2pMessageFlag_t mFlags );
typedef int   (*ickP2pConnectMatrixCb_t)( ickP2pContext_t *ictx, ickP2pServicetype_t localServices, ickP2pServicetype_t remoteServices );
typedef void  (*ickP2pLogFacility_t)( const char *file, int line, int prio, const char * format, ... );


/*=========================================================================*\
  Public global symbols
\*=========================================================================*/

// Names used in upnp descriptions. Might be set by application prior to ickP2pInit();
extern ickUpnpNames_t ickUpnpNames;


/*=========================================================================*\
  Public prototypes
\*=========================================================================*/

const char          *ickP2pGetVersion( int *major, int *minor );
const char          *ickP2pGitVersion( void );
void                 ickP2pSetLogFacility( ickP2pLogFacility_t facility );
void                 ickP2pSetLogging( int level, FILE *fp, int bufLen );
char                *ickP2pGetLogContent( int level );
const char          *ickStrError( ickErrcode_t code );
const char          *ickLibDeviceState2Str( ickP2pDeviceState_t change );

// Context lifecycle
ickP2pContext_t     *ickP2pCreate( const char *deviceName, const char *deviceUuid,
                                   const char *upnpFolder, int lifetime, int port,
                                   ickP2pServicetype_t services, ickErrcode_t *error );
ickErrcode_t         ickP2pResume( ickP2pContext_t *ictx );
ickErrcode_t         ickP2pSuspend( ickP2pContext_t *ictx );
ickErrcode_t         ickP2pEnd( ickP2pContext_t *ictx, ickP2pEndCb_t callback );

// Context configuration
ickErrcode_t         ickP2pAddInterface( ickP2pContext_t *ictx, const char *ifname, const char *hostname );
ickErrcode_t         ickP2pUpnpLoopback( ickP2pContext_t *ictx, int enable );
ickErrcode_t         ickP2pSetConnectMatrix( ickP2pContext_t *ictx, ickP2pConnectMatrixCb_t matrixCb );
ickErrcode_t         ickP2pRegisterDiscoveryCallback( ickP2pContext_t *ictx, ickP2pDiscoveryCb_t callback );
ickErrcode_t         ickP2pRemoveDiscoveryCallback( ickP2pContext_t *ictx, ickP2pDiscoveryCb_t callback );
ickErrcode_t         ickP2pRegisterMessageCallback( ickP2pContext_t *ictx, ickP2pMessageCb_t callback );
ickErrcode_t         ickP2pRemoveMessageCallback( ickP2pContext_t *ictx,ickP2pMessageCb_t callback );


// Get context features
ickP2pLibState_t     ickP2pGetState( const ickP2pContext_t *ictx );
const char          *ickLibState2Str( ickP2pLibState_t state );
const char          *ickP2pGetOsName( const ickP2pContext_t *ictx );
const char          *ickP2pGetName( const ickP2pContext_t *ictx );
const char          *ickP2pGetDeviceUuid( const ickP2pContext_t *ictx );
const char          *ickP2pGetUpnpFolder( const ickP2pContext_t *ictx );
int                  ickP2pGetLifetime( const ickP2pContext_t *ictx );
int                  ickP2pGetLwsPort( const ickP2pContext_t *ictx );
int                  ickP2pGetUpnpPort( const ickP2pContext_t *ictx );
int                  ickP2pGetUpnpLoopback( const ickP2pContext_t *ictx );
long                 ickP2pGetBootId( const ickP2pContext_t *ictx );
long                 ickP2pGetConfigId( const ickP2pContext_t *ictx );
ickP2pServicetype_t  ickP2pGetServices( const ickP2pContext_t *ictx );

// Get device features (allowed only in callbacks!)
char                *ickP2pGetDeviceName( const ickP2pContext_t *ictx, const char *uuid );
ickP2pServicetype_t  ickP2pGetDeviceServices( const ickP2pContext_t *ictx, const char *uuid );
char                *ickP2pGetDeviceLocation( const ickP2pContext_t *ictx, const char *uuid );
int                  ickP2pGetDeviceLifetime( const ickP2pContext_t *ictx, const char *uuid );
int                  ickP2pGetDeviceUpnpVersion( const ickP2pContext_t *ictx, const char *uuid );
int                  ickP2pGetDeviceConnect( const ickP2pContext_t *ictx, const char *uuid );
int                  ickP2pGetDeviceMessagesPending( const ickP2pContext_t *ictx, const char *uuid );
int                  ickP2pGetDeviceMessagesSent( const ickP2pContext_t *ictx, const char *uuid );
int                  ickP2pGetDeviceMessagesReceived( const ickP2pContext_t *ictx, const char *uuid );
double               ickP2pGetDeviceTimeCreated( const ickP2pContext_t *ictx, const char *uuid );
double               ickP2pGetDeviceTimeConnected( const ickP2pContext_t *ictx, const char *uuid );

// Messaging
ickErrcode_t         ickP2pSendMsg( ickP2pContext_t *ictx, const char *uuid, ickP2pServicetype_t targetServices,
                                    ickP2pServicetype_t sourceService, const char *payload, size_t pSize );

// Debugging API - needs to be build in at compile time
ickErrcode_t         ickP2pSetHttpDebugging( ickP2pContext_t *ictx, int enable );
char                *ickP2pGetDebugInfo( ickP2pContext_t *ictx, const char *uuid );
char                *ickP2pGetDebugPath( ickP2pContext_t *ictx, const char *uuid );

// Default connection matrix
int                 ickP2pDefaultConnectMatrixCb( ickP2pContext_t *ictx, ickP2pServicetype_t localServices, ickP2pServicetype_t remoteServices );


#endif /* __ICKP2P_H */
