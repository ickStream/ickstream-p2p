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

// Elements in linked list of callbacks
struct _cb_list {
  struct _cb_list        *next;
  ickDiscoveryDeviceCb_t  callback;
};


typedef struct {
  ickP2pLibState_t  state;
  pthread_mutex_t   mutex;
  pthread_cond_t    condIsReady;
  char             *osName;      // strong
  char             *deviceName;  // strong
  char             *deviceUuid;  // strong
  int               liveTime;
  long              upnpBootId;
  long              upnpConfigId;
  struct _cb_list  *deviceCbList;
  ickP2pEndCb_t     cbEnd;
} _ickP2pLibContext_t;


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
extern _ickP2pLibContext_t *_ickLib;


/*=========================================================================*\
  Internal prototypes
\*=========================================================================*/
void _ickLibDestruct( _ickP2pLibContext_t **icklibptr );

#endif /* __ICKP2PINTERNAL_H */
