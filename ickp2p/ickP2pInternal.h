/*$*********************************************************************\

Header File     : ickP2pInternal.h

Description     : Internal include file for libickp2p

Comments        : -

Date            : 11.08.2013

Updates         : -

Author          : JS, //MAF

Remarks         : -

*************************************************************************
 * Copyright (c) 2013, ickStream GmbH
 * All rights reserved.
\************************************************************************/

#ifndef __ICKP2PINTERNAL_H
#define __ICKP2PINTERNAL_H


/*=========================================================================*\
  Includes required by definitions from this file
\*=========================================================================*/
#include <poll.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <libwebsockets.h>
#include "ickP2p.h"


/*=========================================================================*\
  Definition of constants
\*=========================================================================*/

// Protocol version
#define ICK_VERSION_MAJOR 1
#define ICK_VERSION_MINOR 0


/*=========================================================================*\
  Macro and type definitions
\*=========================================================================*/

// Flags for interface shutdown
typedef enum {
  ICKP2P_INTSHUTDOWN_NONE     = 0,
  ICKP2P_INTSHUTDOWN_EXPOST,
  ICKP2P_INTSHUTDOWN_PROACTIVE
} ickInterfaceShutdown_t;

// An interface  (from ickP2p.c)
struct _ickInterface;
typedef struct _ickInterface ickInterface_t;
struct  _ickInterface {
  ickInterface_t        *next;
  ickInterface_t        *prev;
  char                  *name;          // strong
  in_addr_t              addr;          // network byte order
  in_addr_t              netmask;       // network byte order
  char                  *hostname;      // strong
  long                   announcedBootId;
  ickInterfaceShutdown_t shutdownMode;
  int                    upnpComSocket;
  int                    upnpComPort;
};

// A timer managed by ickp2p (from ickMainThread.c)
struct _ickTimer;
typedef struct _ickTimer ickTimer_t;

// List of polling descriptors (from ickMainThread.c)
typedef struct  {
  struct pollfd *fds;
  nfds_t         nfds;
  nfds_t         size;
  nfds_t         increment;
} ickPolllist_t;


// A wget instance (from ickWGet.c)
struct _ickWGetContext;
typedef struct _ickWGetContext ickWGetContext_t;

// An ickstream device (from ickDevice.c)
struct _ickDevice;
typedef struct _ickDevice ickDevice_t;

// Elements in linked list of callbacks
struct _cblist {
  struct _cblist  *next;
  struct _cblist  *prev;
  int              isNew;
  void            *callback;
};

// The context descriptor
struct _ickP2pContext {
  ickP2pLibState_t               state;
  ickErrcode_t                   error;
  pthread_mutex_t                mutex;

  double                         tCreation;
  double                         tResume;

#ifdef ICK_P2PENABLEDEBUGAPI
  int                            debugApiEnabled;
#endif

  char                          *osName;             // strong
  char                          *deviceName;         // strong

  struct _cblist                *discoveryCbs;       // strong
  pthread_mutex_t                discoveryCbsMutex;
  struct _cblist                *messageCbs;         // strong
  pthread_mutex_t                messageCbsMutex;

  // Main thread and timer
  pthread_t                      thread;
  pthread_cond_t                 condIsReady;
  int                            pollBreakPipe[2];
  ickTimer_t                    *timers;            // strong
  pthread_mutex_t                timersMutex;

  // Networking
  ickInterface_t                *interfaces;        // strong
  pthread_mutex_t                interfaceListMutex;

  // Upnp/Ssdp layer
  char                          *deviceUuid;        // strong
  char                          *upnpFolder;        // strong
  int                            lifetime;
  long                           upnpBootId;
  long                           upnpNextBootId;
  long                           upnpConfigId;
  int                            upnpListenerPort;
  int                            upnpListenerSocket;
  int                            upnpLoopback;

  // List of remote devices seen by this interface
  ickDevice_t                   *deviceList;        // strong
  ickDevice_t                   *deviceLoopback;
  pthread_mutex_t                deviceListMutex;

  // List of local services offered to the world
  ickP2pServicetype_t            ickServices;

  // Web socket layer
  ickP2pConnectMatrixCb_t        lwsConnectMatrixCb;
  struct libwebsocket_context   *lwsContext;        // strong
  struct libwebsocket_protocols *lwsProtocols;      // strong
  int                            lwsPort;
  ickPolllist_t                  lwsPolllist;

  ickWGetContext_t              *wGetters;          // strong
  pthread_mutex_t                wGettersMutex;

  ickP2pEndCb_t                  cbEnd;
};


/*------------------------------------------------------------------------*\
  Macros
\*------------------------------------------------------------------------*/

// Conservative free
#define Sfree(p) {if(p)free(p); (p)=NULL;}

// Set name of threads in debugging mode
#ifdef ICK_DEBUG
#ifdef __linux__
#include <sys/prctl.h>
#define PTHREADSETNAME( name )  prctl( PR_SET_NAME, (unsigned long)(name), 0, 0, 0 )
#endif
#endif
#ifndef PTHREADSETNAME
#define PTHREADSETNAME( name )  { ;}
#endif

// Get rid of warnings about "#pragma mark"
#ifdef __linux__
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#endif

/*------------------------------------------------------------------------*\
  Signatures for function pointers
\*------------------------------------------------------------------------*/


/*------------------------------------------------------------------------*\
  Internal globals
\*------------------------------------------------------------------------*/
// none

/*=========================================================================*\
  Internal prototypes
\*=========================================================================*/
void _ickLibLock( ickP2pContext_t *ictx );
void _ickLibUnlock( ickP2pContext_t *ictx );
void _ickLibDestruct( ickP2pContext_t *ictx );

ickErrcode_t    _ickLibInterfaceUnlink( ickP2pContext_t *ictx, ickInterface_t *interface );
void            _ickLibInterfaceDestruct( ickInterface_t *interface );
ickInterface_t *_ickLibInterfaceByName( const ickP2pContext_t *ictx, const char *ifname );
ickInterface_t *_ickLibInterfaceForAddr( const ickP2pContext_t *ictx, in_addr_t addr );
ickInterface_t *_ickLibInterfaceForHost( const ickP2pContext_t *ictx, const char *hostname, in_addr_t *addr );
void            _ickLibInterfaceListLock( ickP2pContext_t *ictx );
void            _ickLibInterfaceListUnlock( ickP2pContext_t *ictx );

void _ickLibDeviceListLock( ickP2pContext_t *ictx );
void _ickLibDeviceListUnlock( ickP2pContext_t *ictx );
void _ickLibDeviceAdd( ickP2pContext_t *ictx, ickDevice_t *device );
void _ickLibDeviceRemove( ickP2pContext_t *ictx, ickDevice_t *device );
ickDevice_t *_ickLibDeviceFindByUuid( const ickP2pContext_t *ictx, const char *uuid );
ickDevice_t *_ickLibDeviceFindByWsi( const ickP2pContext_t *ictx,struct libwebsocket *wsi );


void _ickLibExecDiscoveryCallback( ickP2pContext_t *ictx,
             const ickDevice_t *dev, ickP2pDeviceState_t change, ickP2pServicetype_t type );

void _ickLibWGettersLock( ickP2pContext_t *ictx );
void _ickLibWGettersUnlock( ickP2pContext_t *ictx  );
void _ickLibWGettersAdd( ickP2pContext_t *ictx , ickWGetContext_t *wget );
void _ickLibWGettersRemove( ickP2pContext_t *ictx , ickWGetContext_t *wget );

double      _ickTimeNow( void );



#endif /* __ICKP2PINTERNAL_H */
