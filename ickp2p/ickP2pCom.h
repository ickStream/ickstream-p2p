/*$*********************************************************************\

Header File     : ickP2pCom.h

Description     : Internal include file for communication functions

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

#ifndef __ICKP2PCOM_H
#define __ICKP2PCOM_H


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

//
// Data per libwebsockets P2p session
//
typedef struct {
  ickP2pContext_t *ictx;         // weak
  int              kill;         // close connection without modifying device
  char            *uuid;         // strong
  char            *host;         // strong
  ickDevice_t     *device;       // weak
  unsigned char   *inBuffer;     // strong;
  size_t           inBufferSize;
} _ickLwsP2pData_t;


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
ickErrcode_t _ickP2pSendNullMessage( ickP2pContext_t *ictx, ickDevice_t *device );
ickErrcode_t _ickDeliverLoopbackMessage( ickP2pContext_t *ictx );
ickErrcode_t _ickWebSocketOpen( struct libwebsocket_context *context, ickDevice_t *device );
void         _ickP2pExecMessageCallback( ickP2pContext_t *ictx, const ickDevice_t *device,
                                         const void *message, size_t mSize );

int    _lwsP2pCb( struct libwebsocket_context *context,
                  struct libwebsocket *wsi,
                  enum libwebsocket_callback_reasons reason, void *user,
                  void *in, size_t len );

void        _ickHeartbeatTimerCb( const ickTimer_t *timer, void *data, int tag );


#endif /* __ICKP2PCOM_H */
