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

