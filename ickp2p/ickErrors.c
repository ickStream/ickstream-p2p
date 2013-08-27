/*$*********************************************************************\

Source File     : ickErrors.c

Description     : ickstream error descriptions

Comments        : -

Called by       : User code

Calls           : -

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
 * this SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS for A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE for ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF this SOFTWARE,
 * EVEN if ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\************************************************************************/

#include <stdio.h>

#include "ickP2p.h"


/*=========================================================================*\
  Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Private definitions and symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Private prototypes
\*=========================================================================*/
// none


/*=========================================================================*\
  Get string for error code
\*=========================================================================*/
const char *ickStrError( ickErrcode_t code )
{

/*------------------------------------------------------------------------*\
    Select string according to code
\*------------------------------------------------------------------------*/
  switch( code ) {
    case ICKERR_SUCCESS:          return "No error";
    case ICKERR_GENERIC:          return "Generic error";
    case ICKERR_NOTIMPLEMENTED:   return "Not implemented";
    case ICKERR_INVALID:          return "Invalid parameter";
    case ICKERR_UNINITIALIZED:    return "Not initialized";
    case ICKERR_INITIALIZED:      return "Already initialized";
    case ICKERR_NOMEMBER:         return "Not a member";
    case ICKERR_WRONGSTATE:       return "Library in wrong state";
    case ICKERR_NOMEM:            return "Out of memory";
    case ICKERR_NOINTERFACE:      return "Bad interface";
    case ICKERR_NOTHREAD:         return "Could not create thread";
    case ICKERR_NOSOCKET:         return "Could not create socket";
    case ICKERR_NODEVICE:         return "Unknown device";
    case ICKERR_BADURI:           return "Bad URI";
    case ICKERR_NOTCONNECTED:     return "Not connected";
    case ICKERR_ISCONNECTED:      return "Already connected";
    case ICKERR_LWSERR:           return "Libwebsockets error";

    default:                 break;
  }

/*------------------------------------------------------------------------*\
    Unknown error code
\*------------------------------------------------------------------------*/
  return "<Undefined error code>";
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
