/*$*********************************************************************\

Source File     : ickMainThread.c

Description     : implement ickp2p main thread

Comments        : -

Called by       : API wrapper

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
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#include <libwebsockets.h>

#include "ickP2p.h"
#include "ickP2pInternal.h"
#include "logutils.h"
#include "ickIpTools.h"
#include "ickDevice.h"
#include "ickSSDP.h"
#include "ickDescription.h"
#include "ickP2pCom.h"
#include "ickWGet.h"
#include "ickMainThread.h"


/*=========================================================================*\
  Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Private definitions and symbols
\*=========================================================================*/
#define ICKDISCOVERY_HEADER_SIZE_MAX    1536
#define ICKPOLLIST_INITSIZE             10
#define ICKPOLLIST_INCEMENT             10


//
// A timer descriptor
//
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

//
// Data per libwebsockets HTTP session
//
typedef struct {
  char   *payload;
  size_t  psize;
  char   *nextptr;
} _ickLwsHttpData_t;


/*=========================================================================*\
  Private prototypes
\*=========================================================================*/

static int  _ickPolllistInit( ickPolllist_t *plist, int size, int increment );
static void _ickPolllistClear( ickPolllist_t *plist );
static void _ickPolllistFree( ickPolllist_t *plist );
static int  _ickPolllistAdd( ickPolllist_t *plist, int fd, int events );
static int  _ickPolllistRemove( ickPolllist_t *plist, int fd );
static int  _ickPolllistAppend( ickPolllist_t *plist, const ickPolllist_t *plista );
static int  _ickPolllistSet( ickPolllist_t *plist, int fd, int events );
static int  _ickPolllistUnset( ickPolllist_t *plist, int fd, int events );
static int  _ickPolllistCheck( const ickPolllist_t *plist, int fd, int events );
static int  _ickPolllistGetIndex( const ickPolllist_t *plist, int fd );

static struct libwebsocket_context *_ickCreateLwsContext( ickP2pContext_t *ictx, const char *ifname, int *port );
static int  _lwsHttpCb( struct libwebsocket_context *context,
                        struct libwebsocket *wsi,
                        enum libwebsocket_callback_reasons reason, void *user,
                        void *in, size_t len );
static void _ickTimerLink( ickP2pContext_t *ictx, ickTimer_t *timer );
static int  _ickTimerUnlink( ickP2pContext_t *ictx, ickTimer_t *timer );


/*=========================================================================*\
  Private symbols
\*=========================================================================*/

//
// Protocols handled by libwebsocket
//
static struct libwebsocket_protocols _lwsProtocols[] = {
  // first protocol must always be HTTP handler
  {
    "http-only",
    _lwsHttpCb,
    sizeof( _ickLwsHttpData_t )
  },

  // the icktream protocol
  {
    ICKP2P_WS_PROTOCOLNAME,
    _lwsP2pCb,
    sizeof( _ickLwsP2pData_t )
  },

  // End of list
  {
    NULL, NULL, 0
  }
};


/*=========================================================================*\
  Ickstream main communication thread
\*=========================================================================*/
void *_ickMainThread( void *arg )
{
  ickP2pContext_t     *ictx = *(ickP2pContext_t**)arg;
  int                  rc;
  ickPolllist_t        plist;
  ickWGetContext_t    *wget, *wgetNext;
  char                *buffer;

  debug( "ickp2p main thread: starting..." );
  PTHREADSETNAME( "ickP2P" );

/*------------------------------------------------------------------------*\
    Reset error state
\*------------------------------------------------------------------------*/
  ictx->error = ICKERR_SUCCESS;

/*------------------------------------------------------------------------*\
    Allocate buffers
\*------------------------------------------------------------------------*/
  buffer = malloc( ICKDISCOVERY_HEADER_SIZE_MAX );
  if( !buffer ) {
    logerr( "ickp2p main thread: out of memory" );
    ictx->error = ICKERR_NOMEM;
    pthread_cond_signal( &ictx->condIsReady );
    return NULL;
  }
  if( _ickPolllistInit(&plist,ICKPOLLIST_INITSIZE,ICKPOLLIST_INCEMENT) ) {
    logerr( "ickp2p main thread: out of memory" );
    Sfree( buffer );
    ictx->error = ICKERR_NOMEM;
    pthread_cond_signal( &ictx->condIsReady );
    return NULL;
  }
  if( _ickPolllistInit(&ictx->lwsPolllist,ICKPOLLIST_INITSIZE,ICKPOLLIST_INCEMENT) ) {
    logerr( "ickp2p main thread: out of memory" );
    Sfree( buffer );
    _ickPolllistFree( &plist );
    ictx->error = ICKERR_NOMEM;
    pthread_cond_signal( &ictx->condIsReady );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Init libwebsocket server
\*------------------------------------------------------------------------*/
  ictx->lwsContext = _ickCreateLwsContext( ictx, ictx->interface, &ictx->lwsPort );
  if( !ictx->lwsContext ) {
    Sfree( buffer );
    _ickPolllistFree( &plist );
    _ickPolllistFree( &ictx->lwsPolllist );
    ictx->error = ICKERR_LWSERR;
    pthread_cond_signal( &ictx->condIsReady );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Construct root of location url
\*------------------------------------------------------------------------*/
  rc = asprintf( &ictx->locationRoot, "http://%s:%d", ictx->hostName, ictx->lwsPort );
  if( rc<0 ) {
    logerr( "ickP2pDiscoveryInit: out of memory." );
    libwebsocket_context_destroy( ictx->lwsContext );
    Sfree( buffer );
    _ickPolllistFree( &plist );
    _ickPolllistFree( &ictx->lwsPolllist );
    ictx->error = ICKERR_NOMEM;
    pthread_cond_signal( &ictx->condIsReady );
    return NULL;
  }
  debug( "ickP2pDiscoveryInit: Using location root \"%s\".", ictx->locationRoot );

/*------------------------------------------------------------------------*\
    We're up and running!
\*------------------------------------------------------------------------*/
  ictx->state = ICKLIB_RUNNING;
  pthread_cond_signal( &ictx->condIsReady );

/*------------------------------------------------------------------------*\
    Run while not terminating
\*------------------------------------------------------------------------*/
  while( ictx->state<ICKLIB_TERMINATING ) {
    struct sockaddr   address;
    socklen_t         addrlen = sizeof(address);
    int               timeout;
    int               retval;
    int               i;

/*------------------------------------------------------------------------*\
    Execute all pending timers
\*------------------------------------------------------------------------*/
    _ickTimerListLock( ictx );
    while( ictx->timers ) {
      struct timeval  now;
      // Note: timer list might get modified by the callback
      ickTimer_t     *timer = ictx->timers;

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
        _ickTimerDelete( ictx, timer );
        continue;
      }

      // Decrement repeat counter
      else if( timer->repeatCntr>0 )
        timer->repeatCntr--;

      // Update timer with interval
      debug( "ickp2p main thread: rescheduling timer %p (%.3fs)",
             timer, timer->interval/1000.0 );
      _ickTimerUpdate( ictx, timer, timer->interval, timer->repeatCntr );
    }

/*------------------------------------------------------------------------*\
    Calculate time interval to next timer
\*------------------------------------------------------------------------*/
    timeout  = ICKMAINLOOP_TIMEOUT_MS;
    if( ictx->timers ) {
      struct timeval now;
      gettimeofday( &now, NULL );

      timeout = (ictx->timers->time.tv_sec-now.tv_sec)*1000 +
                (ictx->timers->time.tv_usec-now.tv_usec)/1000;
      // debug( "ickp2p main thread: timer: %ld, %ld ", _timerList->time.tv_sec, _timerList->time.tv_usec);
      // debug( "ickp2p main thread:   now: %ld, %ld ", now.tv_sec, now.tv_usec);
      debug( "ickp2p main thread: next timer %p in %.3fs",
             ictx->timers, timeout/1000.0 );
      if( timeout<0 )
        timeout = 0;
      else if( timeout>ICKMAINLOOP_TIMEOUT_MS )
        timeout  = ICKMAINLOOP_TIMEOUT_MS;
    }
    _ickTimerListUnlock( ictx );

/*------------------------------------------------------------------------*\
    First poll descriptor is always the help pipe to break the poll on timer updates
\*------------------------------------------------------------------------*/
    _ickPolllistClear( &plist );
    _ickPolllistAdd( &plist, ictx->pollBreakPipe[0], POLLIN );

/*------------------------------------------------------------------------*\
    Add SSDP socket
\*------------------------------------------------------------------------*/
    _ickPolllistAdd( &plist, ictx->upnpSocket, POLLIN );

/*------------------------------------------------------------------------*\
    Collect all http client instances from discovery handlers
\*------------------------------------------------------------------------*/
#if 0
    _ickLibWGettersLock( ictx );
    for( wget=ictx->wGetters; wget; wget=wget->next ) {
      if( _ickPolllistAdd(&plist,_ickWGetSocket(dh),POLLIN) )
        break;
    }
    _ickLibWGettersUnock( ictx );
    if( wget ) {
      logerr( "ickp2p main thread: out of memory." );
      break;
    }
#endif

/*------------------------------------------------------------------------*\
    Merge sockets managed by libwebsockets
\*------------------------------------------------------------------------*/
    if( _ickPolllistAppend(&plist,&ictx->lwsPolllist) ) {
      logerr( "ickp2p main thread: out of memory." );
      break;
    }

/*------------------------------------------------------------------------*\
    Do the polling...
\*------------------------------------------------------------------------*/
    debug( "ickp2p main thread: polling %d sockets (%d lws), timeout %.3fs...",
            plist.nfds, ictx->lwsPolllist.nfds, timeout/1000.0 );
    for( i=0; i<plist.nfds; i++ )
      debug( "ickp2p main thread: poll list element #%d - %d (event mask 0x%02x)",
             i, plist.fds[i].fd, plist.fds[i].events );
    retval = poll( plist.fds, plist.nfds, timeout );
    if( retval<0 ) {
      logerr( "ickp2p main thread: poll failed (%s).", strerror(errno) );
      break;
    }
    if( !retval ) {
      debug( "ickp2p main thread: timed out." );
      continue;
    }
    for( i=0; i<plist.nfds; i++ )
      debug( "ickp2p main thread: poll list element #%d - %d (revent mask 0x%02x)",
             i, plist.fds[i].fd, plist.fds[i].revents );

/*------------------------------------------------------------------------*\
    Was there a break request?
\*------------------------------------------------------------------------*/
    if( plist.fds[0].revents&POLLIN ) {
      ssize_t len = read( plist.fds[0].fd, buffer, ICKDISCOVERY_HEADER_SIZE_MAX );
      if( len<0 )
        logerr( "ickp2p main thread: Unable to read break request pipe: %s",
                 strerror(errno) );
      else
        debug( "ickp2p main thread: received break requests (\"%.*s\")", (int)len, buffer );
      if( retval==1 )
        continue;
    }
    if( ictx->state==ICKLIB_TERMINATING )
      break;

/*------------------------------------------------------------------------*\
    Process incoming data from SSDP socket
\*------------------------------------------------------------------------*/
    _ickLibLock( ictx );
    do {
      ssize_t len;

      // Is this socket readable?
      if( _ickPolllistCheck(&plist,ictx->upnpSocket,POLLIN)<=0 )
        continue;

      // receive data
      memset( buffer, 0, ICKDISCOVERY_HEADER_SIZE_MAX );
      len = recvfrom( ictx->upnpSocket, buffer, ICKDISCOVERY_HEADER_SIZE_MAX, 0, &address, &addrlen );
      if( len<0 ) {
        logwarn( "ickp2p main thread: recvfrom failed (%s).", strerror(errno) );
        continue;
      }
      if( !len ) {  // ?? Not possible for udp
        debug( "ickp2p main thread: disconnected." );
        continue;
      }

      debug( "ickp2p main thread: received %ld bytes from %s:%d: \"%.*s\"",
             (long)len,
             inet_ntoa(((const struct sockaddr_in *)&address)->sin_addr),
             ntohs(((const struct sockaddr_in *)&address)->sin_port),
             len, buffer );

      // Try to parse SSDP packet to internal representation
      ickSsdp_t *ssdp = _ickSsdpParse( buffer, len, &address );
      if( !ssdp )
        continue;

      // Ignore loop back messages from ourself
      if( ssdp->uuid && !strcasecmp(ssdp->uuid,ictx->deviceUuid) ) {
        debug( "ickp2p main thread: ignoring message from myself" );
        _ickSsdpFree( ssdp );
        continue;
      }

      // Process data (lock handler while doing so)
      _ickSsdpExecute( ictx, ssdp );

      // Free internal SSDP representation
      _ickSsdpFree( ssdp );

    } while( 0 );
    _ickLibUnlock( ictx );

/*------------------------------------------------------------------------*\
    Process http client sockets
\*------------------------------------------------------------------------*/
    _ickLibWGettersLock( ictx );
    for( wget=ictx->wGetters; wget; wget=wgetNext ) {
      wgetNext = wget->next;
/*    int fd = _ickWGetSocket( wget );

      i = _ickPolllistGetIndex( &plist, fd );
      if( i<0 ) {
        logwarn( "ickp2p main thread: wget socket %d not in plist", fd );
        continue;
      }
      debug( "ickp2p main thread: servicing wget socket %d (event mask 0x%02x)",
             plist.fds[i].fd, plist.fds[i].revents );
      irc = _ickWGetServiceFd( wget, &plist.fds[i] );
*/    // For the time beeing just check the state of the separate thread
      if( _ickWGetServiceFd(wget,NULL) ) {
        ickDevice_t *device = _ickWGetUserData( wget );

        // unlink HTTP client from list of getters and destroy
        _ickLibWGettersRemove( ictx, wget );
        _ickWGetDestroy( wget );

        // If the device is complete, initiate web socket connection
        if( device->friendlyName && !device->wsi )
          _ickWebSocketOpen( ictx->lwsContext, device );
      }
    }
    _ickLibWGettersUnlock( ictx );

/*------------------------------------------------------------------------*\
    Service libwebsockets descriptors
\*------------------------------------------------------------------------*/
    for( i=0; i<plist.nfds; i++ ) {
      // Is this member of the lws set?
      if( _ickPolllistGetIndex(&ictx->lwsPolllist,plist.fds[i].fd)<0 )
        continue;
      debug( "ickp2p main thread: servicing lws socket %d (event mask 0x%02x)",
             plist.fds[i].fd, plist.fds[i].revents );
      if( libwebsocket_service_fd(ictx->lwsContext, &plist.fds[i])<0 ) {
        logerr( "ickp2p main thread: libwebsocket_service_fd returned an error." );
        break;
      }
    }

  } //  while( icklib->state<ICKLIB_TERMINATING )
  debug( "ickp2p main thread: terminating..." );

/*------------------------------------------------------------------------*\
    Stop SSDP services and announce termination
\*------------------------------------------------------------------------*/
  _ickSsdpEndDiscovery( ictx );

/*------------------------------------------------------------------------*\
    Get rid of libwebsocket context
\*------------------------------------------------------------------------*/
  libwebsocket_context_destroy( ictx->lwsContext );

/*------------------------------------------------------------------------*\
    Clean up
\*------------------------------------------------------------------------*/
  Sfree( buffer );
  _ickPolllistFree( &plist );
  _ickPolllistFree( &ictx->lwsPolllist );

/*------------------------------------------------------------------------*\
    Clear all timer
\*------------------------------------------------------------------------*/
  _ickTimerListLock( ictx );
  while( ictx->timers )
    _ickTimerDelete( ictx, ictx->timers );
  _ickTimerListUnlock( ictx );

/*------------------------------------------------------------------------*\
    Destroy all open http clients
\*------------------------------------------------------------------------*/
  _ickLibWGettersLock( ictx );
  while( ictx->wGetters )
    _ickLibWGettersRemove( ictx, ictx->wGetters );
  _ickLibWGettersUnlock( ictx );

/*------------------------------------------------------------------------*\
    Execute callback, if requested
\*------------------------------------------------------------------------*/
  if( ictx->cbEnd )
    ictx->cbEnd( ictx );

/*------------------------------------------------------------------------*\
    Destruct context, that's all
\*------------------------------------------------------------------------*/
  _ickLibDestruct( ictx );
  return NULL;
}


/*=========================================================================*\
  Create a libwebsocket instance
\*=========================================================================*/
ickErrcode_t _ickMainThreadBreak( ickP2pContext_t *ictx, char flag )
{
  debug( "_ickMainThreadBreak (%p): sending break request '%c'", ictx, flag );

/*------------------------------------------------------------------------*\
    Try to send flag
\*------------------------------------------------------------------------*/
  if( write(ictx->pollBreakPipe[1],&flag,1)<0 ) {
    logerr( "_ickMainThreadBreak: Unable to write to poll break pipe: %s", strerror(errno) );
    return ICKERR_GENERIC;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
}


#pragma mark -- Libwebsocket init and HTTP handler


/*=========================================================================*\
  Create a libwebsocket instance
\*=========================================================================*/
static struct libwebsocket_context *_ickCreateLwsContext( ickP2pContext_t *ictx, const char *ifname, int *port )
{
  struct libwebsocket_context *lwsContext;
  struct lws_context_creation_info info;
  memset( &info, 0, sizeof(info) );

/*------------------------------------------------------------------------*\
    Try to guess a free port
      fixme: libwebsockets should be patched to accept port=-1 for
             autoselecting a port
\*------------------------------------------------------------------------*/
#ifdef CONTEXT_PORT_CHOOSE_FREE
  info.port = CONTEXT_PORT_CHOOSE_FREE;
#else
  info.port = _ickIpGetFreePort( ifname );
  if( info.port<0 )
    info.port = 49152 + random()%10000;
  *port = info.port;
#endif

/*------------------------------------------------------------------------*\
    Setup rest of configuration vector
\*------------------------------------------------------------------------*/
  info.iface                    = ifname;
  info.protocols                = _lwsProtocols;
  info.ssl_cert_filepath        = NULL;
  info.ssl_private_key_filepath = NULL;
  info.gid                      = -1;
  info.uid                      = -1;
  info.options                  = 0;
  info.user                     = ictx;

/*------------------------------------------------------------------------*\
    Try to create the context
\*------------------------------------------------------------------------*/
  lwsContext = libwebsocket_create_context( &info );
  if( !lwsContext )
    logerr( "_ickCreateLwsContext: Could not get LWS context (%s:%d)", ifname, info.port );

/*------------------------------------------------------------------------*\
    Get port in patched library version
\*------------------------------------------------------------------------*/
#ifdef CONTEXT_PORT_CHOOSE_FREE
  else
    *port = libwebsocket_get_listen_port( lwsContext );
#endif

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return lwsContext;
}


/*=========================================================================*\
  Handle HTTP requests
\*=========================================================================*/
static int _lwsHttpCb( struct libwebsocket_context *context,
                       struct libwebsocket *wsi,
                       enum libwebsocket_callback_reasons reason, void *user,
                       void *in, size_t len )
{
  ickP2pContext_t      *ictx = libwebsocket_context_user( context );
  _ickLwsHttpData_t    *psd = (_ickLwsHttpData_t*) user;
  ickP2pServicetype_t   stype;
  int                   retval = 0;
  int                   fd, socket;
  size_t                remain;
  int                   sent;
  char                  clientName[160];
  char                  clientIp[128];


/*------------------------------------------------------------------------*\
    What to do?
\*------------------------------------------------------------------------*/
  switch( reason ) {

/*------------------------------------------------------------------------*\
    Init protocol
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_PROTOCOL_INIT:
      debug( "_lwsHttpCb: init" );
      break;

/*------------------------------------------------------------------------*\
    Shut down protocol
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_PROTOCOL_DESTROY:
      debug( "_lwsHttpCb: shutdown" );
      break;

/*------------------------------------------------------------------------*\
    Serve http content
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_HTTP:
      socket = libwebsocket_get_socket_fd( wsi );
      debug( "_lwsHttpCb %d: requesting \"%s\"", socket, in );

      // reset session specific user data
      memset( psd, 0, sizeof(_ickLwsHttpData_t) );

      // Handle UPNP description requests
      _ickLibLock( ictx );
      stype = _ickDescrFindServiceByUrl( ictx, in );
      if( stype!=ICKP2P_SERVICE_NONE ) {
        debug( "_lwsHttpCb: found matching service %d", stype );
        psd->payload = _ickDescrGetDeviceDescr( ictx, wsi, stype );
        _ickLibUnlock( ictx );
        if( !psd->payload )
          return -1;
        psd->psize   = strlen( psd->payload );
        psd->nextptr = psd->payload;
        debug( "_lwsHttpCb %d: sending upnp descriptor \"%s\"", socket, psd->payload );

        // Enqueue a LWS_CALLBACK_HTTP_WRITEABLE callback for the real work
        libwebsocket_callback_on_writable( context, wsi );
        break;
      }
      else
        _ickLibUnlock( ictx );

      // We don't serve root or files if no folder is set
      if( !ictx->upnpFolder || !strcmp(in, "/") ) {
        void *response = HTTP_404;
        libwebsocket_write( wsi, response, strlen(response), LWS_WRITE_HTTP );
        retval = -1;
        break;
      }

      // Serve file from folder
      else {
        char        *resource  = malloc( strlen(ictx->upnpFolder)+strlen(in)+1 );
        char        *ext       = strrchr( in, '.' );
        struct stat  sbuffer;
        char        *mime;

        // Get full path
        sprintf( resource, "%s%s", ictx->upnpFolder, (char*)in );
        debug( "_lwsHttpCb %d: resource path is \"%s\"", socket, resource );

        if( stat(resource,&sbuffer) ) {
          void *response = HTTP_404;
          libwebsocket_write( wsi, response, strlen(response), LWS_WRITE_HTTP );
          retval = -1;
          break;
        }

        // choose mime type based on the file extension
        if( !ext )
          mime = "text/plain";
        else if( !strcmp(ext,".png") )
          mime = "image/png";
        else if( !strcmp(ext,".jpg") )
          mime = "image/jpg";
        else if( !strcmp(ext,".gif") )
          mime = "image/gif";
        else if( !strcmp(ext,".ico") )
          mime = "image/x-icon";
        else if( !strcmp(ext,".html") )
          mime = "text/html";
        else if( !strcmp(ext,".css") )
          mime = "text/css";
        else
          mime = "text/plain";

        // through completion or error, close the socket
        if( libwebsockets_serve_http_file(context,wsi,resource,mime) )
          retval = -1;
      }
      break;

/*------------------------------------------------------------------------*\
    Receiver is writable for payload chunk transmission
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_HTTP_WRITEABLE:
      socket = libwebsocket_get_socket_fd( wsi );
      remain = psd->psize - (psd->nextptr-psd->payload);
      debug( "_lwsHttpCb %d: writable, %d bytes remaining", socket, remain );

      // Try to send a chunk
      sent = libwebsocket_write( wsi, (unsigned char*)psd->nextptr, remain, LWS_WRITE_HTTP );
      if( sent<0 ) {
        logerr( "_lwsHttpCb %d: lws write failed", socket );
        retval = -1;
        break;
      }

      // Everything transmitted?
      if( sent==remain ) {
        retval = 1;
        break;
      }

      // Not ready: enqueue a new callback for leftover
      debug( "_lwsHttpCb %d: truncated lws write (%d / %d / %d)", socket, len, remain, psd->psize );
      psd->nextptr += sent;
      libwebsocket_callback_on_writable( context, wsi );
      break;

/*------------------------------------------------------------------------*\
    File transfer is complete
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_HTTP_FILE_COMPLETION:
      socket = libwebsocket_get_socket_fd( wsi );
      debug( "_lwsHttpCb %d: file complete", socket );
      // kill the connection after we sent one file
      retval = -1;
      break;

/*------------------------------------------------------------------------*\
    Http connection was closed
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_CLOSED_HTTP:
      socket = libwebsocket_get_socket_fd( wsi );
      debug( "_lwsHttpCb %d: connection closed", socket);
      Sfree( psd->payload );
      break;

/*------------------------------------------------------------------------*\
    Do IP filtering and logging
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
      socket = libwebsocket_get_socket_fd( wsi );
      libwebsockets_get_peer_addresses( context, wsi, (int)(long)in,
               clientName, sizeof(clientName), clientIp, sizeof(clientIp) );
      loginfo( "_lwsHttpCb %d: Received network connect from \"%s\" (%s)",
               socket, clientName, clientIp);
      // Accept all connections
      retval = 0;
      break;

/*------------------------------------------------------------------------*\
    Append headers (not used)
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
      socket = libwebsocket_get_socket_fd( wsi );
      debug( "_lwsHttpCb %d: append handshake header", socket );
      break;

/*------------------------------------------------------------------------*\
    Add a socket to poll list
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_ADD_POLL_FD:
      fd = (int)(long)in;
      debug( "_lwsHttpCb: adding socket %d (mask 0x%02x)",
             fd, (int)(long)len );
      if( _ickPolllistAdd(&ictx->lwsPolllist,fd,(int)(long)len) )
        retval = 1;
      break;

/*------------------------------------------------------------------------*\
    Remove a socket from poll list
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_DEL_POLL_FD:
      fd = (int)(long)in;
      debug( "_lwsHttpCb: removing socket %d", fd );
      if( _ickPolllistRemove(&ictx->lwsPolllist,fd) )
        retval = 1;
      break;

/*------------------------------------------------------------------------*\
    Set mode for poll list entry
\*------------------------------------------------------------------------*/
    case LWS_CALLBACK_SET_MODE_POLL_FD:
      fd = (int)(long)in;
      debug( "_lwsHttpCb: set events for socket %d (mask 0x%02x)", fd, (int)(long)len );
      if( _ickPolllistSet(&ictx->lwsPolllist,fd,(int)(long)len) )
        retval = 1;
      break;

    case LWS_CALLBACK_CLEAR_MODE_POLL_FD:
      fd = (int)(long)in;
      debug( "_lwsHttpCb: clear events for socket %d (mask 0x%02x)", fd, (int)(long)len );
      if( _ickPolllistUnset(&ictx->lwsPolllist,fd,(int)(long)len) )
        retval = 1;
      break;

/*------------------------------------------------------------------------*\
    Unknown/unhandled request
\*------------------------------------------------------------------------*/
    default:
      logerr( "_lwsHttpCb: unknown/unhandled reason (%d)", reason );
      break;
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return retval;
}


#pragma mark -- Timer callbacks


/*=========================================================================*\
  A device has expired: remove from a discovery context
    timer list is already locked as this is a timer callback
\*=========================================================================*/
void _ickDeviceExpireTimerCb( const ickTimer_t *timer, void *data, int tag )
{
  ickDevice_t     *device = data;
  ickP2pContext_t *ictx   = device->ictx;

  debug( "_ickDeviceExpireCb: %s", device->uuid );

/*------------------------------------------------------------------------*\
    Lock device list
\*------------------------------------------------------------------------*/
  _ickLibDeviceListLock( ictx );

/*------------------------------------------------------------------------*\
    Execute callback with all registered services
\*------------------------------------------------------------------------*/
  _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_EXPIRED, device->services );

/*------------------------------------------------------------------------*\
    Remove heartbeat handler for this device
\*------------------------------------------------------------------------*/
  _ickTimerDeleteAll( ictx, _ickDeviceHeartbeatTimerCb, device, 0 );

/*------------------------------------------------------------------------*\
    Unlink from device list and free instance
\*------------------------------------------------------------------------*/
  _ickLibDeviceRemove( ictx, device );
  _ickDeviceFree( device );

/*------------------------------------------------------------------------*\
    Release device list locks
\*------------------------------------------------------------------------*/
  _ickLibDeviceListUnlock( ictx );
}


/*=========================================================================*\
  Send heart bet on a LWS connection. This is done to reset the expiration
    timer in case a ws connection exists but SSDP is not routed.
    timer list is already locked as this is a timer callback
\*=========================================================================*/
void _ickDeviceHeartbeatTimerCb( const ickTimer_t *timer, void *data, int tag )
{
  ickDevice_t     *device = data;
  ickP2pContext_t *ictx   = device->ictx;

  debug( "_ickDeviceHeartbeatTimerCb: %s", device->uuid );

/*------------------------------------------------------------------------*\
    Queue heartbeat message
\*------------------------------------------------------------------------*/
  _ickP2pSendNullMessage( ictx, device );

}


#pragma mark -- Manage polling list


/*=========================================================================*\
  Init poll list
    returns o on success or -1 on error (out of memory)
\*=========================================================================*/
static int _ickPolllistInit( ickPolllist_t *plist, int size, int increment )
{

/*------------------------------------------------------------------------*\
    Init array
\*------------------------------------------------------------------------*/
  plist->fds = calloc( size, sizeof(struct pollfd) );
  if( !plist->fds ) {
    logerr( "_ickPolllistInit: out of memory" );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Init indexes
\*------------------------------------------------------------------------*/
  plist->nfds      = 0;
  plist->size      = size;
  plist->increment = increment;

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
  Clear poll list (remove all members)
\*=========================================================================*/
static void _ickPolllistClear( ickPolllist_t *plist )
{

/*------------------------------------------------------------------------*\
    reset index
\*------------------------------------------------------------------------*/
  plist->nfds      = 0;

}


/*=========================================================================*\
  Free poll list (do not use afterwards)
\*=========================================================================*/
static void _ickPolllistFree( ickPolllist_t *plist )
{

/*------------------------------------------------------------------------*\
    Free array
\*------------------------------------------------------------------------*/
  Sfree( plist->fds );

}


/*=========================================================================*\
  Add a socket to a poll list
    returns -1 on error (out of memory)
\*=========================================================================*/
static int _ickPolllistAdd( ickPolllist_t *plist, int fd, int events )
{
  int idx;
  debug( "_ickPolllistAdd (%p): fd %d mask 0x%02x", plist, fd, events );

/*------------------------------------------------------------------------*\
    Avoid duplicates
\*------------------------------------------------------------------------*/
  idx = _ickPolllistGetIndex( plist, fd );
  if( idx>=0 ) {
    logwarn( "_ickPolllistAdd: fd %d already member", fd );
    plist->fds[idx].events = events;
    return 0;
  }

/*------------------------------------------------------------------------*\
    Need to extend array?
\*------------------------------------------------------------------------*/
  if( plist->nfds>=plist->size ) {
    struct pollfd *fds;
    fds = realloc( plist->fds, (plist->size+plist->increment) *sizeof(struct pollfd) );
    if( !fds ) {
      logerr( "_ickPolllistAdd: out of memory" );
      return -1;
    }
    plist->fds   = fds;
    plist->size += plist->increment;
  }

/*------------------------------------------------------------------------*\
    Append poll descriptor to end
\*------------------------------------------------------------------------*/
  plist->fds[plist->nfds].fd      = fd;
  plist->fds[plist->nfds].events  = events;
  plist->fds[plist->nfds].revents = 0;
  plist->nfds++;

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
  Remove a socket from poll list
    returns -1 on error (not member)
\*=========================================================================*/
static int _ickPolllistRemove( ickPolllist_t *plist, int fd )
{
  int idx;
  debug( "_ickPolllistRemove (%p): fd %d", plist, fd );

/*------------------------------------------------------------------------*\
    Get list entry
\*------------------------------------------------------------------------*/
  idx = _ickPolllistGetIndex( plist, fd );
  if( idx<0 ) {
    logwarn( "_ickPolllistRemove: fd %d not element", fd );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Replace this entry with the last one
\*------------------------------------------------------------------------*/
  if( idx!=plist->nfds-1 )
    memcpy( &plist->fds[idx], &plist->fds[plist->nfds-1], sizeof(struct pollfd) );
  plist->nfds--;

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
  Append a polling list to another one
    returns -1 on error (out of memory)
\*=========================================================================*/
static int _ickPolllistAppend( ickPolllist_t *plist, const ickPolllist_t *plista )
{
  int i;
  debug( "_ickPolllistAppend (%p): %p (%d elements)", plist, plista, plista->nfds );

/*------------------------------------------------------------------------*\
    Loop over all append list members
\*------------------------------------------------------------------------*/
  for( i=0; i<plista->nfds; i++ ) {
    if( _ickPolllistAdd(plist,plista->fds[i].fd,plista->fds[i].events) )
      return -1;
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
  Set event type for poll list element
    Note: the bits in event are ored to the bits already set
    returns -1 on error (fd not member)
\*=========================================================================*/
static int _ickPolllistSet( ickPolllist_t *plist, int fd, int events )
{
  int idx;

/*------------------------------------------------------------------------*\
    Get list entry
\*------------------------------------------------------------------------*/
  idx = _ickPolllistGetIndex( plist, fd );
  if( idx<0 ) {
    logwarn( "_ickPolllistSet: fd %d not element", fd );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Set requested bits
\*------------------------------------------------------------------------*/
  plist->fds[idx].events |= events;

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
  Unset event type for poll list element
    Note: only the bits in event are reset
    returns -1 on error (fd not member)
\*=========================================================================*/
static int _ickPolllistUnset( ickPolllist_t *plist, int fd, int events )
{
  int idx;

/*------------------------------------------------------------------------*\
    Get list entry
\*------------------------------------------------------------------------*/
  idx = _ickPolllistGetIndex( plist, fd );
  if( idx<0 ) {
    logwarn( "_ickPolllistUnset: fd %d not element", fd );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Set requested bists
\*------------------------------------------------------------------------*/
  plist->fds[idx].events &= ~events;

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return 0;
}



/*=========================================================================*\
  Check a socket of a poll list for one or more condition
    returns -1 on error (fd not member)
             Bit vector of conditions met
\*=========================================================================*/
static int _ickPolllistCheck( const ickPolllist_t *plist, int fd, int events )
{
  int idx;

/*------------------------------------------------------------------------*\
    Get list entry
\*------------------------------------------------------------------------*/
  idx = _ickPolllistGetIndex( plist, fd );
  if( idx<0 ) {
    logwarn( "_ickPolllistCheck: fd %d not element", fd );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Return result
\*------------------------------------------------------------------------*/
  return plist->fds[idx].revents & events;
}


/*=========================================================================*\
  Search a socket descriptor in list
    return index if found, -1 otherwhise
\*=========================================================================*/
static int _ickPolllistGetIndex( const ickPolllist_t *plist, int fd )
{
  int i;

/*------------------------------------------------------------------------*\
    Check all members
\*------------------------------------------------------------------------*/
  for( i=0; i<plist->nfds; i++ ) {
    if( plist->fds[i].fd==fd )
      return i;
  }

/*------------------------------------------------------------------------*\
    Not found
\*------------------------------------------------------------------------*/
  return -1;
}


#pragma mark -- Timer management


/*=========================================================================*\
  Lock list of timers
\*=========================================================================*/
void _ickTimerListLock( ickP2pContext_t *ictx )
{
  debug ( "_ickTimerListLock (%p): locking...", ictx );
  pthread_mutex_lock( &ictx->timersMutex );
  debug ( "_ickTimerListLock (%p): locked", ictx );
}


/*=========================================================================*\
  Unlock list of timers
\*=========================================================================*/
void _ickTimerListUnlock( ickP2pContext_t *ictx )
{
  debug ( "_ickTimerListLock (%p): unlocked", ictx );
  pthread_mutex_unlock( &ictx->timersMutex );
}


/*=========================================================================*\
  Create a timer
    interval is in millisecs
    Timer list must be locked by caller
\*=========================================================================*/
ickErrcode_t _ickTimerAdd( ickP2pContext_t *ictx, long interval, int repeat, ickTimerCb_t callback, void *data, int tag )
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
  _ickTimerLink( ictx, timer );

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
ickTimer_t *_ickTimerFind( ickP2pContext_t *ictx, ickTimerCb_t callback, const void *data, int tag )
{
  ickTimer_t *walk;

/*------------------------------------------------------------------------*\
    Find list entry with same id vector
\*------------------------------------------------------------------------*/
  for( walk=ictx->timers; walk; walk=walk->next ) {
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
ickErrcode_t _ickTimerUpdate( ickP2pContext_t *ictx, ickTimer_t *timer, long interval, int repeat )
{

/*------------------------------------------------------------------------*\
    unlink timer
\*------------------------------------------------------------------------*/
  if( _ickTimerUnlink(ictx,timer) ) {
    logerr( "_ickTimerUpdate (%p): invalid timer.", ictx );
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
  _ickTimerLink( ictx, timer );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Delete a timer
    Timer list must be locked by caller
\*=========================================================================*/
ickErrcode_t _ickTimerDelete( ickP2pContext_t *ictx, ickTimer_t *timer )
{

/*------------------------------------------------------------------------*\
    Unlink timer
\*------------------------------------------------------------------------*/
  if( _ickTimerUnlink(ictx,timer) ) {
    logerr( "_ickTimerDelete (%p): invalid timer.", ictx );
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
void _ickTimerDeleteAll( ickP2pContext_t *ictx, ickTimerCb_t callback, const void *data, int tag )
{
  ickTimer_t *walk, *next;

/*------------------------------------------------------------------------*\
    Find list entries with same id vector
\*------------------------------------------------------------------------*/
  for( walk=ictx->timers; walk; walk=next ) {
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
      ictx->timers = next;

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
static void _ickTimerLink( ickP2pContext_t *ictx, ickTimer_t *timer )
{
  ickTimer_t *walk, *last;

/*------------------------------------------------------------------------*\
    Find list entry with time stamp larger than the new one
\*------------------------------------------------------------------------*/
  for( last=NULL,walk=ictx->timers; walk; last=walk,walk=walk->next ) {
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
  else {
    ictx->timers = timer;

    // If root was changed write to help pipe to break main loop poll timer
    _ickMainThreadBreak( ictx, 'T' );
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
  Unlink a timer from list
    Timer list must be locked by caller, the timer descriptor is not freed
    Returns 0 on success
\*=========================================================================*/
static int _ickTimerUnlink( ickP2pContext_t *ictx, ickTimer_t *timer )
{
  ickTimer_t *walk;

/*------------------------------------------------------------------------*\
    Be defensive: check it timer is list element
\*------------------------------------------------------------------------*/
  for( walk=ictx->timers; walk&&walk!=timer; walk=walk->next );
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
    ictx->timers = timer->next;

/*------------------------------------------------------------------------*\
    That's all (No need to inform main loop in this case...)
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
