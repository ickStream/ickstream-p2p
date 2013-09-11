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
