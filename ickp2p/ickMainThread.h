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
ickErrcode_t  _ickMainThreadBreak( ickP2pContext_t *ictx, char flag );


ickErrcode_t  _ickMainThreadAddWGet( ickWGetContext_t *ickWGet );

void          _ickTimerListLock( ickP2pContext_t *ictx );
void          _ickTimerListUnlock( ickP2pContext_t *ictx );
ickErrcode_t  _ickTimerAdd( ickP2pContext_t *ictx, long interval, int repeat, ickTimerCb_t callback, void *data, int tag );
ickTimer_t   *_ickTimerFind( ickP2pContext_t *ictx, ickTimerCb_t callback, const void *data, int tag );
ickErrcode_t  _ickTimerUpdate( ickP2pContext_t *ictx, ickTimer_t *timer, long interval, int repeat );
ickErrcode_t  _ickTimerDelete( ickP2pContext_t *ictx, ickTimer_t *timer );
void          _ickTimerDeleteAll( ickP2pContext_t *ictx, ickTimerCb_t callback, const void *data, int tag );


#endif /* __ICKMAINTHREAD_H */
