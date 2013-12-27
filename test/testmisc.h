/*$*********************************************************************\

Name            : -

Source File     : testmisc.h

Description     : Definitions for testmisc.c

Comments        : -

Date            : 27.12.2013

Updates         : -

Author          : //MAF 

Remarks         : -


*************************************************************************
 * Copyright (c) 2013, ickStream GmbH
 * All rights reserved.
\************************************************************************/

#ifndef __TESTMISC_H
#define __TESTMISC_H

/*========================================================================*\
   Definitions
\*========================================================================*/

#define VLP_SIZE    23456L
#define VLP_MODUL   251

/*========================================================================*\
   Prototypes
\*========================================================================*/

/*------------------------------------------------------------------------*\
   Very large message
\*------------------------------------------------------------------------*/
char *createVlp( void );
int   checkVlp( const char *msg, size_t len );

/*------------------------------------------------------------------------*\
   Callbacks
\*------------------------------------------------------------------------*/
void ickDiscoverCb( ickP2pContext_t *ictx, const char *uuid, ickP2pDeviceState_t change, ickP2pServicetype_t type );
void ickMessageCb( ickP2pContext_t *ictx, const char *sourceUuid, ickP2pServicetype_t sourceService, ickP2pServicetype_t targetServices, const char* message, size_t mSize, ickP2pMessageFlag_t mFlags );


#endif  /* __TESTMISC_H */


/*========================================================================*\
                                 END OF FILE
\*========================================================================*/

