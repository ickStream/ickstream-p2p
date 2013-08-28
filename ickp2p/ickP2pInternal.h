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

#ifndef __ICKP2PINTERNAL_H
#define __ICKP2PINTERNAL_H


/*=========================================================================*\
  Includes required by definitions from this file
\*=========================================================================*/
#include <poll.h>
#include <pthread.h>
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
  void            *callback;
};

// The context descriptor
struct _ickP2pContext {
  ickP2pLibState_t             state;
  ickErrcode_t                 error;
  pthread_mutex_t              mutex;

  char                        *osName;      // strong
  char                        *deviceName;  // strong
  char                        *hostName;    // strong

  struct _cblist              *discoveryCbs;
  pthread_mutex_t              discoveryCbsMutex;
  struct _cblist              *messageCbs;
  pthread_mutex_t              messageCbsMutex;

  // Main thread and timer
  pthread_t                    thread;
  pthread_cond_t               condIsReady;
  int                          pollBreakPipe[2];
  ickTimer_t                  *timers;
  pthread_mutex_t              timersMutex;

  // Upnp/Ssdp layer
  char                        *deviceUuid;  // strong
  char                        *upnpFolder;  // strong
  int                          liveTime;
  long                         upnpBootId;
  long                         upnpConfigId;
  char                        *interface;      // strong
  int                          upnpPort;
  int                          upnpSocket;
  char                        *locationRoot;   // strong

  // List of remote devices seen by this interface
  ickDevice_t                 *deviceList;
  pthread_mutex_t              deviceListMutex;

  // List of local services offered to the world
  ickP2pServicetype_t          ickServices;

  // Web socket layer
  ickP2pConnectMatrixCb_t      lwsConnectMatrixCb;
  struct libwebsocket_context *lwsContext;
  int                          lwsPort;
  ickPolllist_t                lwsPolllist;

  ickWGetContext_t            *wGetters;
  pthread_mutex_t              wGettersMutex;


  ickP2pEndCb_t                cbEnd;
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
#define PTHREADSETNAME( name )  prctl( PR_SET_NAME, (name) )
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

void _ickLibDeviceListLock( ickP2pContext_t *ictx );
void _ickLibDeviceListUnlock( ickP2pContext_t *ictx );
void _ickLibDeviceAdd( ickP2pContext_t *ictx, ickDevice_t *device );
void _ickLibDeviceRemove( ickP2pContext_t *ictx, ickDevice_t *device );
ickDevice_t *_ickLibDeviceFindByUuid( ickP2pContext_t *ictx, const char *uuid );
ickDevice_t *_ickLibDeviceFindByWsi( ickP2pContext_t *ictx,struct libwebsocket *wsi );


void _ickLibExecDiscoveryCallback( ickP2pContext_t *ictx,
             const ickDevice_t *dev, ickP2pDiscoveryCommand_t change, ickP2pServicetype_t type );

void _ickLibWGettersLock( ickP2pContext_t *ictx );
void _ickLibWGettersUnlock( ickP2pContext_t *ictx  );
void _ickLibWGettersAdd( ickP2pContext_t *ictx , ickWGetContext_t *wget );
void _ickLibWGettersRemove( ickP2pContext_t *ictx , ickWGetContext_t *wget );



#endif /* __ICKP2PINTERNAL_H */
