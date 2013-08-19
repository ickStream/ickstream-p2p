/*$*********************************************************************\

Name            : -

Source File     : utils.h

Description     : Main include file for logutils.c

Comments        : Posix log levels:
                   LOG_EMERG    0   system is unusable
                   LOG_ALERT    1   action must be taken immediately
                   LOG_CRIT     2   critical conditions
                   LOG_ERR      3   error conditions
                   LOG_WARNING  4   warning conditions
                   LOG_NOTICE   5   normal but significant condition
                   LOG_INFO     6   informational    
                   LOG_DEBUG    7   debug-level messages

Date            : 08.03.2013 

Updates         : 21.04.2013 adapted for ickstream-p2p    //MAF

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


#ifndef __LOGUTILS_H
#define __LOGUTILS_H

/*=========================================================================*\
	Includes needed by definitions from this file
\*=========================================================================*/
#include <syslog.h>
#include "ickP2p.h"

/*=========================================================================*\
       Macro and type definitions 
\*=========================================================================*/

// Define the debug macro. This could also used in libwebsockets...
#ifdef ICK_DEBUG
 #define debug( args... ) ((*_icklog)( __FILE__, __LINE__, LOG_DEBUG, args ))

/*static inline void debug(const char *format, ...)
{
//  va_list ap;
//  va_start(ap, format); vfprintf(stderr, format, ap); va_end(ap);
  (*_icklog)( __FILE__, __LINE__, LOG_DEBUG, format )
} */

#else
#define debug( args... ) { ;}

/*static inline
 void debug(const char *format, ...)
 {
 }*/
#endif

// Some convenience macros
#define logerr( args... )     ((*_icklog)( __FILE__, __LINE__, LOG_ERR, args ))
#define logwarn( args... )    ((*_icklog)( __FILE__, __LINE__, LOG_WARNING, args ))
#define lognotice( args... )  ((*_icklog)( __FILE__, __LINE__, LOG_NOTICE, args ))
#define loginfo( args... )    ((*_icklog)( __FILE__, __LINE__, LOG_INFO, args ))

// Define location macros if not supplied by preprocessor
#ifndef __LINE__
#define __LINE__ 0
#endif

#ifndef __FILE__
#define __FILE__ NULL
#endif


/*=========================================================================*\
       Global symbols 
\*=========================================================================*/
extern ickP2pLogFacility_t _icklog;


/*========================================================================*\
   Prototypes
\*========================================================================*/
void __icksrvlog( const char *file, int line, int prio, const char *fmt, ... );


#endif  /* __LOGUTILS_H */


/*========================================================================*\
                                 END OF FILE
\*========================================================================*/

