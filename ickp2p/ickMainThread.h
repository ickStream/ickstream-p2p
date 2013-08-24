/*$*********************************************************************\

Header File     : ickMainThread.h

Description     : Internal include file for mainthread functions

Comments        : -

Date            : 15.08.2013

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

#ifndef __ICKMAINTHREAD_H
#define __ICKMAINTHREAD_H


/*=========================================================================*\
  Includes required by definitions from this file
\*=========================================================================*/
// none


/*=========================================================================*\
  Definition of constants
\*=========================================================================*/
#define ICKMAINLOOP_TIMEOUT_MS 1000


/*=========================================================================*\
  Macro and type definitions
\*=========================================================================*/



/*------------------------------------------------------------------------*\
  Macros
\*------------------------------------------------------------------------*/


/*------------------------------------------------------------------------*\
  Signatures for function pointers
\*------------------------------------------------------------------------*/
typedef void (*ickTimerCb_t)( const ickTimer_t *timer, void *data, int tag );


/*=========================================================================*\
  Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Internal prototypes
\*=========================================================================*/
void         *_ickMainThread( void *arg );

ickErrcode_t  _ickMainThreadAddWGet( ickWGetContext_t *ickWGet );

void          _ickTimerListLock( _ickP2pLibContext_t *icklib );
void          _ickTimerListUnlock( _ickP2pLibContext_t *icklib );
ickErrcode_t  _ickTimerAdd( _ickP2pLibContext_t *icklib, long interval, int repeat, ickTimerCb_t callback, void *data, int tag );
ickTimer_t   *_ickTimerFind( _ickP2pLibContext_t *icklib, ickTimerCb_t callback, const void *data, int tag );
ickErrcode_t  _ickTimerUpdate( _ickP2pLibContext_t *icklib, ickTimer_t *timer, long interval, int repeat );
ickErrcode_t  _ickTimerDelete( _ickP2pLibContext_t *icklib, ickTimer_t *timer );
void          _ickTimerDeleteAll( _ickP2pLibContext_t *icklib, ickTimerCb_t callback, const void *data, int tag );


#endif /* __ICKMAINTHREAD_H */
