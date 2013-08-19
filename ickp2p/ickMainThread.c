/*$*********************************************************************\

Source File     : ickP2p.c

Description     : implement ickp2p API calls

Comments        : -

Called by       : User code

Calls           : Internal functions

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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#include "ickP2p.h"
#include "ickP2pInternal.h"
#include "ickDiscovery.h"
#include "ickSSDP.h"
#include "logutils.h"
#include "ickMainThread.h"


/*=========================================================================*\
  Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Private definitions and symbols
\*=========================================================================*/
#define ICKDISCOVERY_HEADER_SIZE_MAX    1536


struct _ickTimer {
  ickTimer_t     *prev;
  ickTimer_t     *next;
  struct timeval  time;
  long            interval;
  int             repeatCntr;
  void           *usrPtr;
  int             usrTag;
  ickTimerCb_t    callback;
};

static ickTimer_t       *_timerList;
static pthread_mutex_t   _timerListMutex = PTHREAD_MUTEX_INITIALIZER;


/*=========================================================================*\
  Private prototypes
\*=========================================================================*/
static void _ickTimerLink( ickTimer_t *timer );
static int _ickTimerUnlink( ickTimer_t *timer );


/*=========================================================================*\
  Ickstream main communication thread
\*=========================================================================*/
void *_ickMainThread( void *arg )
{
  _ickP2pLibContext_t *icklib = *(_ickP2pLibContext_t**)arg;
  char                *buffer;

  debug( "ickp2p main thread: starting..." );
  PTHREADSETNAME( "ickP2P" );

/*------------------------------------------------------------------------*\
    Allocate buffer
\*------------------------------------------------------------------------*/
  buffer = malloc( ICKDISCOVERY_HEADER_SIZE_MAX );
  if( !buffer ) {
    logerr( "ickp2p main thread: out of memory" );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    We're up and running!
\*------------------------------------------------------------------------*/
  icklib->state = ICKLIB_RUNNING;
  pthread_cond_signal( &icklib->condIsReady );

/*------------------------------------------------------------------------*\
    Run while not terminating
\*------------------------------------------------------------------------*/
  while( icklib->state<ICKLIB_TERMINATING ) {
    struct sockaddr address;
    socklen_t       addrlen = sizeof(address);
    ickDiscovery_t *walk;
    int             maxsock;
    struct timeval  timeout;
    fd_set          readSet;
    int             retval;
    ssize_t         rcv_size;

/*------------------------------------------------------------------------*\
    Execute all pending timers
\*------------------------------------------------------------------------*/
    _ickTimerListLock();
    while( _timerList ) {
      struct timeval  now;
      // Note: timer list might get modified by the callback
      ickTimer_t     *timer = _timerList;

      // Break if first timer is in future
      gettimeofday( &now, NULL );
      if( timer->time.tv_sec>now.tv_sec )
        break;
      if( timer->time.tv_sec==now.tv_sec && timer->time.tv_usec>now.tv_usec )
        break;

      // Execute timer callback
      debug( "ickp2p main thread: executing timer %p", timer );
      timer->callback( timer, timer->usrPtr, timer->usrTag );

      // Last execution of cyclic timer?
      if( timer->repeatCntr==1 ) {
        _ickTimerDelete( timer );
        continue;
      }

      // Decrement repeat counter
      else if( timer->repeatCntr>0 )
        timer->repeatCntr--;

      // Update timer with interval
      debug( "ickp2p main thread: rescheduling timer %p (%.3fs)",
             timer, timer->interval/1000.0 );
      _ickTimerUpdate( timer, timer->interval, timer->repeatCntr );
    }

/*------------------------------------------------------------------------*\
    Calculate time interval to next timer
\*------------------------------------------------------------------------*/
    timeout.tv_sec  = ICKMAINLOOP_TIMEOUT_MS/1000;
    timeout.tv_usec = (ICKMAINLOOP_TIMEOUT_MS%1000)*1000;
    if( _timerList ) {
      long   ds, dus;
      struct timeval now;
      gettimeofday( &now, NULL );

      // Do some portable high resolution time algebra
      ds  = _timerList->time.tv_sec-now.tv_sec;
      dus = _timerList->time.tv_usec-now.tv_usec;
      if( dus<0 ) {
        dus += 1000000L;
        ds  -= 1;
      }
      // debug( "ickp2p main thread: timer: %ld, %ld ", _timerList->time.tv_sec, _timerList->time.tv_usec);
      // debug( "ickp2p main thread:   now: %ld, %ld ", now.tv_sec, now.tv_usec);
      // debug( "ickp2p main thread: delta: %ld, %ld ", ds, dus);
      debug( "ickp2p main thread: next timer %p in %fs",
             _timerList, ds+dus/1000000.0 );
      if( ds<0 ) {
        timeout.tv_sec  = 0;
        timeout.tv_usec = 0;
      }
      if( ds*1000+dus/1000<ICKMAINLOOP_TIMEOUT_MS ) {
        timeout.tv_sec  = ds;
        timeout.tv_usec = dus;
      }
    }
    _ickTimerListUnlock();

/*------------------------------------------------------------------------*\
    Check for incoming SSDP messages on all active SSDP sockets
\*------------------------------------------------------------------------*/
    FD_ZERO( &readSet );
    maxsock = 0;
    pthread_mutex_lock( &_ickDiscoveryHandlerListMutex );
    for( walk=_ickDiscoveryHandlerList; walk; walk=walk->next ) {
      FD_SET( walk->socket, &readSet );
      if( walk->socket )
        maxsock = walk->socket;
    }
    pthread_mutex_unlock( &_ickDiscoveryHandlerListMutex );

    // Check for data
    retval = select( maxsock+1, &readSet, NULL, NULL, &timeout );
    if( retval<0 ) {
      logerr( "ickp2p main thread: select failed (%s).",
               strerror(errno) );
      break;
    }
    if( !retval ) {
      debug( "ickp2p main thread: timeout..." );
      continue;
    }
    if( icklib->state==ICKLIB_TERMINATING )
      break;

    // Process all data
    pthread_mutex_lock( &_ickDiscoveryHandlerListMutex );
    for( walk=_ickDiscoveryHandlerList; walk; walk=walk->next ) {
      if( !FD_ISSET(walk->socket,&readSet) )
        continue;

      // receive data
      memset( buffer, 0, ICKDISCOVERY_HEADER_SIZE_MAX );
      rcv_size = recvfrom( walk->socket, buffer, ICKDISCOVERY_HEADER_SIZE_MAX, 0, &address, &addrlen );
      if( rcv_size<0 ) {
        logwarn( "ickp2p main thread (%s:%d): recvfrom failed (%s).",
                   walk->interface, walk->port, strerror(errno) );
        continue;
      }
      if( !rcv_size ) {  // ?? Not possible for udp
        debug( "ickp2p main thread (%s:%d): disconnected.",
               walk->interface, walk->port );
        continue;
      }

      debug( "ickp2p main thread (%s:%d): received %ld bytes from %s:%d: \"%.*s\"",
             walk->interface, walk->port, (long)rcv_size,
             inet_ntoa(((const struct sockaddr_in *)&address)->sin_addr),
             ntohs(((const struct sockaddr_in *)&address)->sin_port),
             rcv_size, buffer );

      // Try to parse SSDP packet to internal representation
      ickSsdp_t *ssdp = _ickSsdpParse( buffer, rcv_size, &address );
      if( !ssdp )
        continue;

      // Ignore loopback messages from this instance
      if( ssdp->uuid && !strcasecmp(ssdp->uuid,icklib->deviceUuid) ) {
        _ickSsdpFree( ssdp );
        continue;
      }

      // Process data (lock handler while doing so)
      pthread_mutex_lock( &walk->mutex );
      _ickSsdpExecute( walk, ssdp );
      pthread_mutex_unlock( &walk->mutex );

      // Free internal SSDP representation
      _ickSsdpFree( ssdp );

    }
    pthread_mutex_unlock( &_ickDiscoveryHandlerListMutex );

  } //  while( icklib->state<ICKLIB_TERMINATING )
  debug( "ickp2p main thread: terminating..." );

/*------------------------------------------------------------------------*\
    Clean up
\*------------------------------------------------------------------------*/
  Sfree( buffer );

/*------------------------------------------------------------------------*\
    Clear all timer
\*------------------------------------------------------------------------*/
  _ickTimerListLock();
  while( _timerList )
    _ickTimerDelete( _timerList );
  _ickTimerListUnlock();

/*------------------------------------------------------------------------*\
    Execute callback, if requested
\*------------------------------------------------------------------------*/
  if( icklib->cbEnd )
    icklib->cbEnd();

/*------------------------------------------------------------------------*\
    Destruct context, that's all
\*------------------------------------------------------------------------*/
  _ickLibDestruct( (_ickP2pLibContext_t**)arg );
  return NULL;
}


#pragma mark -- Timer management


/*=========================================================================*\
  Lock list of timers
\*=========================================================================*/
void _ickTimerListLock( void )
{
  debug ( "_ickTimerListLock: locked" );
  pthread_mutex_lock( &_timerListMutex );
}


/*=========================================================================*\
  Unlock list of timers
\*=========================================================================*/
void _ickTimerListUnlock( void )
{
  debug ( "_ickTimerListLock: unlocked" );
  pthread_mutex_unlock( &_timerListMutex );
}


/*=========================================================================*\
  Create a timer
    interval is in millisecs
    Timer list must be locked by caller
\*=========================================================================*/
ickErrcode_t _ickTimerAdd( long interval, int repeat, ickTimerCb_t callback, void *data, int tag )
{
  ickTimer_t *timer;

/*------------------------------------------------------------------------*\
    Create and init timer structure
\*------------------------------------------------------------------------*/
  timer = calloc( 1, sizeof(ickTimer_t) );
  if( !timer ) {
    logerr( "_ickTimerAdd: out of memory" );
    return ICKERR_NOMEM;
  }

  // store user data
  timer->callback   = callback;
  timer->usrTag     = tag;
  timer->usrPtr     = data;
  timer->interval   = interval;
  timer->repeatCntr = repeat;

  // calculate execution timestamp
  gettimeofday( &timer->time, NULL );
  timer->time.tv_sec  += interval/1000;
  timer->time.tv_usec += (interval%1000)*1000;
  while( timer->time.tv_usec>1000000UL ) {
    timer->time.tv_usec -= 1000000UL;
    timer->time.tv_sec++;
  }

/*------------------------------------------------------------------------*\
    link timer to list
\*------------------------------------------------------------------------*/
  _ickTimerLink( timer );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Find a timer by vector (callback,data,tag)
    If callback is NULL, it is not used for comparison
    Timer list must be locked by caller
\*=========================================================================*/
ickTimer_t *_ickTimerFind( ickTimerCb_t callback, const void *data, int tag )
{
  ickTimer_t *walk;

/*------------------------------------------------------------------------*\
    Find list entry with same id vector
\*------------------------------------------------------------------------*/
  for( walk=_timerList; walk; walk=walk->next ) {
    if( walk->usrPtr==data && walk->usrTag==tag && (!callback||walk->callback==callback) )
      break;
  }

/*------------------------------------------------------------------------*\
    Return result
\*------------------------------------------------------------------------*/
  return walk;
}


/*=========================================================================*\
  Update timer settings
    interval is in millisecs
    Timer list must be locked by caller
\*=========================================================================*/
ickErrcode_t _ickTimerUpdate( ickTimer_t *timer, long interval, int repeat )
{

/*------------------------------------------------------------------------*\
    unlink timer
\*------------------------------------------------------------------------*/
  if( _ickTimerUnlink(timer) ) {
    logerr( "_ickTimerUpdate: invalid timer." );
    return ICKERR_INVALID;
  }

/*------------------------------------------------------------------------*\
    reset repeat counter and calculate new execution timestamp
\*------------------------------------------------------------------------*/
  timer->interval   = interval;
  timer->repeatCntr = repeat;
  gettimeofday( &timer->time, NULL );
  timer->time.tv_sec  += interval/1000;
  timer->time.tv_usec += (interval%1000)*1000;
  while( timer->time.tv_usec>1000000UL ) {
    timer->time.tv_usec -= 1000000UL;
    timer->time.tv_sec++;
  }

/*------------------------------------------------------------------------*\
    link timer to list
\*------------------------------------------------------------------------*/
  _ickTimerLink( timer );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Delete a timer
    Timer list must be locked by caller
\*=========================================================================*/
ickErrcode_t _ickTimerDelete( ickTimer_t *timer )
{

/*------------------------------------------------------------------------*\
    Unlink timer
\*------------------------------------------------------------------------*/
  if( _ickTimerUnlink(timer) ) {
    logerr( "_ickTimerUpdate: invalid timer." );
    return ICKERR_INVALID;
  }

/*------------------------------------------------------------------------*\
    Free descriptor, that's all
\*------------------------------------------------------------------------*/
  Sfree( timer );
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Delete all timers matching a search vector (callback,data,tag)
    If callback is NULL, it is not used for comparison
    Timer list must be locked by caller
\*=========================================================================*/
void _ickTimerDeleteAll( ickTimerCb_t callback, const void *data, int tag )
{
  ickTimer_t *walk, *next;

/*------------------------------------------------------------------------*\
    Find list entries with same id vector
\*------------------------------------------------------------------------*/
  for( walk=_timerList; walk; walk=next ) {
    next = walk->next;

    // no match?
    if( walk->usrPtr!=data || walk->usrTag!=tag || (callback&&walk->callback!=callback) )
      continue;

    // unlink
    if( next )
      next->prev = walk->prev;
    if( walk->prev )
      walk->prev->next = next;
    else
      _timerList = next;

    // and free
    Sfree( walk );
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
  Link a timer to list
    Timer list must be locked by caller
\*=========================================================================*/
static void _ickTimerLink( ickTimer_t *timer )
{
  ickTimer_t *walk, *last;

/*------------------------------------------------------------------------*\
    Find list entry with time stamp larger than the new one
\*------------------------------------------------------------------------*/
  for( last=NULL,walk=_timerList; walk; last=walk,walk=walk->next ) {
    if( walk->time.tv_sec>timer->time.tv_sec )
      break;
    if( walk->time.tv_sec==timer->time.tv_sec && walk->time.tv_usec>timer->time.tv_usec)
      break;
  }

/*------------------------------------------------------------------------*\
    Link new element
\*------------------------------------------------------------------------*/
  timer->next = walk;
  if( walk )
    walk->prev = timer;
  timer->prev = last;
  if( last )
    last->next = timer;
  else
    _timerList = timer;

  // fixme: signal main thread to terminate select()

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
  Unlink a timer from list
    Timer list must be locked by caller, the timer descriptor is not freed
    Returns 0 on success
\*=========================================================================*/
static int _ickTimerUnlink( ickTimer_t *timer )
{
  ickTimer_t *walk;

/*------------------------------------------------------------------------*\
    Be defensive: check it timer is list element
\*------------------------------------------------------------------------*/
  for( walk=_timerList; walk&&walk!=timer; walk=walk->next );
  if( !walk )
    return -1;

/*------------------------------------------------------------------------*\
    Unlink
\*------------------------------------------------------------------------*/
  if( timer->next )
    timer->next->prev = timer->prev;
  if( timer->prev )
    timer->prev->next = timer->next;
  else
    _timerList = timer->next;

/*------------------------------------------------------------------------*\
    That's all (No need to inform main loop in this case...)
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
