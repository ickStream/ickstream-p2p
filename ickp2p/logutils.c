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
  Private prototypes
\*=========================================================================*/
static void __icklog( const char *file, int line,  int prio, const char *fmt, ... );
static void _trimLogList( void );


/*=========================================================================*\
	Global symbols
\*=========================================================================*/
ickP2pLogFacility_t _icklog = &__icklog;


/*=========================================================================*\
	Private symbols
\*=========================================================================*/
static pthread_mutex_t   loggerMutex      = PTHREAD_MUTEX_INITIALIZER;
static int               ickp2pLogLevel   = LOG_WARNING;
static FILE             *ickp2pLogStream  = NULL;
static int               icpp2pLogBufSize = 0;
static ickP2pLogEntry_t *ickp2pLogBufRoot = NULL;


/*========================================================================*\
   Set or reset external log facility
\*========================================================================*/
void ickP2pSetLogFacility( ickP2pLogFacility_t facility )
{
  if( facility )
    _icklog = facility;
  else
    _icklog = &__icklog;
}


/*========================================================================*\
   Set up log level
\*========================================================================*/
void ickP2pSetLogging( int level, FILE *fp, int bufLen )
{
  ickp2pLogLevel   = level;
  ickp2pLogStream  = fp;
  icpp2pLogBufSize = bufLen;

  pthread_mutex_lock( &loggerMutex );
  _trimLogList();
  pthread_mutex_unlock( &loggerMutex );
}


/*========================================================================*\
   Get memory buffer content
     level is the maximum level to include
    returns an allocated string (caller must free) or NULL on error
\*========================================================================*/
char *ickP2pGetLogContent( int level )
{
  int               i;
  ickP2pLogEntry_t *walk;
  char             *result = NULL;
  size_t            rlen   = 0;

/*------------------------------------------------------------------------*\
    Lock mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_lock( &loggerMutex );

/*------------------------------------------------------------------------*\
    Loop over all entries
\*------------------------------------------------------------------------*/
  for( i=0,walk=ickp2pLogBufRoot; walk; walk=walk->next ) {
    int     rc;
    char   *prefix;
    char   *new;
    size_t  wlen;

    // Cut on log level
    if( walk->prio>level )
      continue;

    // Count entry
    i++;

    // Create prefix
    if( walk->file && *walk->file )
      rc = asprintf( &prefix, "%04d %.4f %d [%p] %s,%d: ", i, walk->timeStamp, walk->prio, (void*)walk->thread, walk->file, walk->line );
    else
      rc = asprintf( &prefix, "%04d %.4f %d [%p]: ", i, walk->timeStamp, walk->prio, (void*)walk->thread );
    if( rc<0 )
      goto bail;

    // Reallocate result
    wlen = strlen(prefix) + strlen(walk->text) + 1;
    new = realloc( result, rlen+wlen+1 );
    if( !new ) {
      free( prefix );
      goto bail;
    }
    result = new;

    // append new entry and new line
    strcpy( result+rlen, prefix );
    strcat( result+rlen, walk->text );
    rlen += wlen;
    strcpy( result+rlen-1, "\n" );

  }

  if( !result )
    result = strdup( "No entries\n" );

/*------------------------------------------------------------------------*\
    Unlock mutex and return result
\*------------------------------------------------------------------------*/
bail:
  pthread_mutex_unlock( &loggerMutex );
  return result;
}


/*========================================================================*\
   Default logging facility
\*========================================================================*/
static void __icklog( const char *file, int line, int prio, const char *fmt, ... )
{
  va_list a_list;
  struct timeval tv;

/*------------------------------------------------------------------------*\
   Only log messages more severe than current logging threshold
\*------------------------------------------------------------------------*/
  if( prio>ickp2pLogLevel )
    return;

/*------------------------------------------------------------------------*\
    Get timestamp, init arguments, lock mutex
\*------------------------------------------------------------------------*/
  gettimeofday( &tv, NULL );
  va_start( a_list, fmt );
  pthread_mutex_lock( &loggerMutex );
  
/*------------------------------------------------------------------------*\
   Do output to stream
\*------------------------------------------------------------------------*/
  if( ickp2pLogStream ) {
    // print timestamp and thread info
    fprintf( ickp2pLogStream, "%.4f %d [%p]", tv.tv_sec+tv.tv_usec*1E-6, prio, (void*)pthread_self() );

    // append location to message (if available)
    if( file && *file )
      fprintf( ickp2pLogStream, " %s,%d: ", file, line );
    else
      fprintf( ickp2pLogStream, ": " );

    // the message itself
    vfprintf( ickp2pLogStream, fmt, a_list );

    // New line and flush stream buffer
    fprintf( ickp2pLogStream, "\n" );
    fflush( ickp2pLogStream );
  }

/*------------------------------------------------------------------------*\
   Use in memory ring buffer?
\*------------------------------------------------------------------------*/
  if( icpp2pLogBufSize>0 ) {
    ickP2pLogEntry_t *entry;

    // Create entry for fifo buffer
    entry = malloc( sizeof(ickP2pLogEntry_t) );
    if( !entry )
      goto bail;

    // Construct the log message
    if( vasprintf(&entry->text,fmt,a_list)<0 ) {
      free( entry );
      goto bail;
    }

    // Store meta data
    entry->file      = file;
    entry->line      = line;
    entry->thread    = pthread_self();
    entry->timeStamp = tv.tv_sec+tv.tv_usec*1E-6;
    entry->prio      = prio;

    // Link entry
    entry->next      = ickp2pLogBufRoot;
    ickp2pLogBufRoot = entry;

    // Free tail of fifo list
    _trimLogList();
  }

/*------------------------------------------------------------------------*\
    Clean variable argument list, unlock mutex
\*------------------------------------------------------------------------*/
bail:
  va_end ( a_list );
  pthread_mutex_unlock( &loggerMutex );
}


/*========================================================================*\
   Trim log fifo buffer to maximum number of entries
\*========================================================================*/
static void _trimLogList( void )
{
  int               i;
  ickP2pLogEntry_t *walk, *next;

/*------------------------------------------------------------------------*\
    Delete everything?
\*------------------------------------------------------------------------*/
  if( icpp2pLogBufSize<=0 ) {
    walk = ickp2pLogBufRoot;
    ickp2pLogBufRoot = NULL;
  }

/*------------------------------------------------------------------------*\
    Find last element we want to keep
\*------------------------------------------------------------------------*/
  else {
    for( i=1,walk=ickp2pLogBufRoot; i<icpp2pLogBufSize&&walk; i++,walk=walk->next )
      ;

    // Cut list here
    if( walk ) {
      next       = walk->next;
      walk->next = NULL;
      walk       = next;
    }
  }

/*------------------------------------------------------------------------*\
    Delete all following list elements
\*------------------------------------------------------------------------*/
  while( walk ) {
    next = walk->next;
    if( walk->text )
      free( walk->text );
    free( walk );
    walk = next;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
