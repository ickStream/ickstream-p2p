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
