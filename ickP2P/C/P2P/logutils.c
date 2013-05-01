/*$*********************************************************************\

Name            : -

Source File     : logutils.c

Description     : default logging facility

Comments        : Posix log levels:
                   LOG_EMERG    0   system is unusable
                   LOG_ALERT    1   action must be taken immediately
                   LOG_CRIT     2   critical conditions
                   LOG_ERR      3   error conditions
                   LOG_WARNING  4   warning conditions
                   LOG_NOTICE   5   normal but significant condition
                   LOG_INFO     6   informational
                   LOG_DEBUG    7   debug-level messages

Called by       : - 

Calls           : stdout functions

Error Messages  : -
  
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

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/time.h>

#include "logutils.h"


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
static void __icklog( const char *file, int line,  int prio, const char *fmt, ... );
ickDiscovery_log_facility_t *_icklog = &__icklog;


/*=========================================================================*\
	Private symbols
\*=========================================================================*/
static pthread_mutex_t loggerMutex = PTHREAD_MUTEX_INITIALIZER;


/*========================================================================*\
   Logging facility
\*========================================================================*/
static void __icklog( const char *file, int line,  int prio, const char *fmt, ... )
{

/*------------------------------------------------------------------------*\
    Init arguments, lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &loggerMutex );
  va_list a_list;
  va_start( a_list, fmt );
  
/*------------------------------------------------------------------------*\
   Log everything more severe than to stderr
\*------------------------------------------------------------------------*/
  if( prio<=LOG_WARNING ) {
    struct timeval tv;


    // print timestamp and thread info
    gettimeofday( &tv, NULL );
    fprintf( stderr, "%.4f [%p]", tv.tv_sec+tv.tv_usec*1E-6, (void*)pthread_self() );

    // prepend location to message (if available)
    if( file && *file )
      fprintf( stderr, " %s,%d: ", file, line );
    else
      fprintf( stderr, ": " );

    // the message itself
    vfprintf( stderr, fmt, a_list );
  
    // New line and flush stream buffer
    fprintf( stderr, "\n" );
    fflush( stderr );
  }

/*------------------------------------------------------------------------*\
    Clean variable argument list, unlock mutex
\*------------------------------------------------------------------------*/
  va_end ( a_list );
  pthread_mutex_unlock( &loggerMutex );
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
