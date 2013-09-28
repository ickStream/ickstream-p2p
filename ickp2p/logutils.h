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
\************************************************************************/


#ifndef __LOGUTILS_H
#define __LOGUTILS_H

/*=========================================================================*\
	Includes needed by definitions from this file
\*=========================================================================*/
#include <syslog.h>
#include <pthread.h>
#include "ickP2p.h"


/*=========================================================================*\
       Macro and type definitions 
\*=========================================================================*/

//
// An entry in the in-memory ring buffer
//
struct _ickP2pLogEntry;
typedef struct _ickP2pLogEntry ickP2pLogEntry_t;
struct _ickP2pLogEntry {
  ickP2pLogEntry_t    *next;
  const char          *file;        // weak
  int                  line;
  double               timeStamp;
  int                  prio;
  pthread_t            thread;      // weak
  char                *text;        // strong
};


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

