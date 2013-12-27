/*$*********************************************************************\

Name            : -

Source File     : ickSSDP.c

Description     : implements upnp discovery protocol (essentially SSDP)

Comments        : -

Called by       : -

Calls           : -

Error Messages  : -

Date            : 09.08.2013

Updates         : -

Author          : //MAF

Remarks         : refactored from ickDiscoverRegistry

*************************************************************************
 * Copyright (c) 2013, ickStream GmbH
 * All rights reserved.
\************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>



#include "ickP2p.h"
#include "ickP2pInternal.h"
#include "logutils.h"
#include "ickIpTools.h"
#include "ickDevice.h"
#include "ickDescription.h"
#include "ickMainThread.h"
#include "ickWGet.h"
#include "ickSSDP.h"
#include "ickP2pCom.h"


/*=========================================================================*\
  Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Private definitions and symbols
\*=========================================================================*/

//
// Type and level of upnp notifications
//
typedef enum {
  SSDPMSGLEVEL_GLOBAL,
  SSDPMSGLEVEL_ROOT,
  SSDPMSGLEVEL_UUID,
  SSDPMSGLEVEL_DEVICEORSERVICE
} ssdpMsgLevel_t;


//
// Descriptor for ssdp send timers
//
typedef struct {
  char               *message;   // strong
  struct sockaddr_in  sockname;
  size_t              socknamelen;
  int                 socket;
  int                 refCntr;
} upnp_notification_t;



/*=========================================================================*\
  Private prototypes
\*=========================================================================*/
static int          _ickDeviceAlive( ickP2pContext_t *ictx, const ickSsdp_t *ssdp );
static int          _ickDeviceRemove( ickP2pContext_t *ictx, const ickSsdp_t *ssdp );

static void         _ickSsdpAnnounceCb( const ickTimer_t *timer, void *data, int tag );
static void         _ickSsdpSearchCb( const ickTimer_t *timer, void *data, int tag );

static int          _ssdpProcessMSearch( ickP2pContext_t *ictx, const ickSsdp_t *ssdp );
static ickErrcode_t _ssdpSendInitialDiscoveryMsg( ickP2pContext_t *ictx,
                                                  const struct sockaddr *addr,
                                                  ickSsdpMsgType_t type, long delay );
static ickErrcode_t _ssdpSendDiscoveryMsg( ickP2pContext_t *ictx,
                                           const struct sockaddr *addr,
                                           ickSsdpMsgType_t type,
                                           ssdpMsgLevel_t level,
                                           int repeat, long delay );
static ickErrcode_t __ssdpSendDiscoveryMsg( ickP2pContext_t *ictx,
                                            ickInterface_t *interface,
                                            const struct sockaddr *addr,
                                            ickSsdpMsgType_t type,
                                            ssdpMsgLevel_t level,
                                            int repeat, long delay );
static void         _ickSsdpNotifyCb( const ickTimer_t *timer, void *data, int tag );

static int          _ssdpGetVersion( const char *dscr );
static int          _ssdpVercmp( const char *user, const char *adv );


/*=========================================================================*\
  Create an SSDP listener
  returns listener port or -1 on error
\*=========================================================================*/
int _ickSsdpCreateListener( in_addr_t ifaddr, int port )
{
  int sd;
  int rc;
  int opt;

#ifdef ICK_DEBUG
  char _buf[64];
  inet_ntop( AF_INET, &ifaddr, _buf, sizeof(_buf) );
  debug( "_ickSsdpCreateListener: %s:%d", _buf, port );
#endif

/*------------------------------------------------------------------------*\
    Try to create socket
\*------------------------------------------------------------------------*/
  sd = socket( PF_INET, SOCK_DGRAM, 0 );
  if( sd<0 ){
    logerr( "_ickSsdpCreateListener: could not create socket (%s).", strerror(errno) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Set non blocking
\*------------------------------------------------------------------------*/
  rc = fcntl( sd, F_GETFL );
  if( rc>=0 )
    rc = fcntl( sd, F_SETFL, rc|O_NONBLOCK );
  if( rc<0 )
    logwarn( "_ickSsdpCreateListener: could not set O_NONBLOCK on socket (%s).",
        strerror(errno) );

/*------------------------------------------------------------------------*\
    Reuse address (multiple processes will receive MCASTS)
\*------------------------------------------------------------------------*/
  opt = 1;
  rc = setsockopt( sd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt) );
  if( rc<0 )
    logwarn( "_ickSsdpCreateListener: could not set SO_REUSEADDR on socket (%s).",
        strerror(errno) );
#ifdef ICK_USE_SO_REUSEPORT
  rc = setsockopt( sd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt) );
  if( rc<0 )
    logwarn( "_ickSsdpCreateListener: could not set SO_REUSEPORT on socket (%s).",
        strerror(errno) );
#endif

/*------------------------------------------------------------------------*\
    Bind socket to requested port
\*------------------------------------------------------------------------*/
  rc = _ickIpBind( sd, INADDR_ANY, port );
  if( rc<0 ) {
    close( sd );
    logerr( "_ickSsdpCreateListener: could not bind socket to port %d (%s).",
             port, strerror(rc) );
    return -1;
  }

/*------------------------------------------------------------------------*\
  Add socket to multicast group on target interface
\*------------------------------------------------------------------------*/
  rc = _ickIpAddMcast( sd, ifaddr, inet_addr(ICKSSDP_MCASTADDR) );
  if( rc<0 ) {
    close( sd );
    logerr( "ickP2pInit: could not add mcast membership for socket (%s).",
             strerror(rc) );
    return -1;
  }

/*------------------------------------------------------------------------*\
  That's all
\*------------------------------------------------------------------------*/
  return sd;
}


#pragma mark - SSDP parsing

/*=========================================================================*\
  Parse SSDP packet
    buffer - pointer to data
    length - valid bytes in buffer
    addr   - address of peer
    port   - expected ssdp port (1900 as default)
  returns a filled ssdp descriptor or NULL on error
\*=========================================================================*/
ickSsdp_t *_ickSsdpParse( const char *buffer, size_t length, const struct sockaddr *addr, int port )
{
  ickSsdp_t  *ssdp;
  int         lineno;
  char       *line;
  char       *bufferend;
  const char *peer;

/*------------------------------------------------------------------------*\
    Get name of peer for warnings
\*------------------------------------------------------------------------*/
  peer = inet_ntoa( ((const struct sockaddr_in *)addr)->sin_addr );

/*------------------------------------------------------------------------*\
    Create header
\*------------------------------------------------------------------------*/
  ssdp = calloc( 1, sizeof(ickSsdp_t) );
  if( !ssdp ) {
    logerr( "_ickSsdpParse: out of memory" );
    return NULL;
  }

/*------------------------------------------------------------------------*\
    Copy data
\*------------------------------------------------------------------------*/
  ssdp->buffer = malloc( length+1 );
  if( !ssdp->buffer ) {
    logerr( "_ickSsdpParse: out of memory (%ld bytes)", (long)length );
    Sfree( ssdp );
    return NULL;
  }
  memcpy( ssdp->buffer, buffer, length );
  ssdp->buffer[length] = 0;
  memcpy( &ssdp->addr, addr, sizeof(struct sockaddr) );

/*------------------------------------------------------------------------*\
    Some defaults
\*------------------------------------------------------------------------*/
  ssdp->lifetime = ICKSSDP_DEFAULTLIFETIME;

/*------------------------------------------------------------------------*\
    Loop over all lines
\*------------------------------------------------------------------------*/
  line      = ssdp->buffer;
  bufferend = ssdp->buffer+length;
  for( lineno=0; line<bufferend; lineno++ ) {
    char *name;
    char *value;
    char *ptr;
    char *lineend = strpbrk( line,"\n\r" );
    if( !lineend )
      lineend = bufferend;
    *lineend = 0;
    debug( "_ickSsdpParse (%p,%s): parsing line #%d \"%s\"...", ssdp, peer, lineno, line );

/*------------------------------------------------------------------------*\
    Ignore everything after empty line
\*------------------------------------------------------------------------*/
    if( !*line ) {
      debug( "_ickSsdpParse (%p,%s): parsed %d lines", ssdp, peer, lineno );
      break;
    }

/*------------------------------------------------------------------------*\
    First line should be a known method
    See "UPnP Device Architecture 1.1": chapter 1.1.1
\*------------------------------------------------------------------------*/
    if( ssdp->method==SSDP_METHOD_UNDEFINED ) {
      if( !strcmp(line,"M-SEARCH * HTTP/1.1") )
        ssdp->method = SSDP_METHOD_MSEARCH;
      else if( !strcmp(line,"NOTIFY * HTTP/1.1") )
        ssdp->method = SSDP_METHOD_NOTIFY;
      else if( !strcmp(line,"HTTP/1.1 200 OK") )
        ssdp->method = SSDP_METHOD_REPLY;
      else {
        logwarn( "_ickSsdpParse (%s): unknown method \"%s\"", peer, line );
        _ickSsdpFree( ssdp );
        return NULL;
      }
      goto nextline;
    }

/*------------------------------------------------------------------------*\
    Process a "NAME: value" string
\*------------------------------------------------------------------------*/
    name  = line;
    value = strchr( line, ':' );

    // do some checks; need value separator and name should not contain spaces
    ptr = strpbrk( line, " \t" );
    if( !value || (ptr && ptr<value) ) {
      logwarn( "_ickSsdpParse (%s): ignoring corrupt line \"%s\"", peer, line );
      goto nextline;
    }

    // Separate value, trim spaces
    *(value++) = 0;
    while( isspace(*value) )
      value++;
    for( ptr=strchr(value,0)-1; ptr>=value&&isspace(*ptr); ptr-- )
      *ptr = 0;

    // Trim quotes
    if( *value=='"' ) {
      value++;
      ptr = strchr(value,0)-1;
      if( ptr<value || *ptr!='"' )
        logwarn( "_ickSsdpParse (%s): value for %s contains unbalanced quotes (ignored)", peer, name );
      else
        *ptr = 0;
    }

/*------------------------------------------------------------------------*\
    Interpret header fields
\*------------------------------------------------------------------------*/
    debug( "_ickSsdpParse (%p,%s): #%d name=\"%s\" value=\"%s\"", ssdp, peer, lineno, name, value );

    if( !strcasecmp(name,"nt") )
      ssdp->nt = value;

    else if( !strcasecmp(name,"st") ) {
      // for search replies, ST takes the place of NT
      if( ssdp->method==SSDP_METHOD_REPLY )
        ssdp->nt = value;
      else
        ssdp->st = value;
    }

    else if( !strcasecmp(name,"usn") ) {
      ssdp->usn = value;

      // get uuid
      if( strncmp (value,"uuid:",5) ) {
        logwarn("_ickSsdpParse (%s): Invalid USN header value \"%s\"", peer, value );
        _ickSsdpFree( ssdp );
        return NULL;
      }
      value += 5;
      ptr = strstr( value, "::" );
      if( !ptr )
        ptr = strchr( value, 0 );
      if( ptr==value ) {
        logwarn("_ickSsdpParse (%s): Invalid USN header value \"%s\"", peer, value );
        _ickSsdpFree( ssdp );
        return NULL;
      }
      ssdp->uuid = strndup( value, ptr-value );
      if( !ssdp->uuid ) {
        logerr( "_ickSsdpParse: out of memory (%ld bytes)", (long)(ptr-value) );
        _ickSsdpFree( ssdp );
        return NULL;
      }
    }

    else if( !strcasecmp(name,"nts") ) {
      if( !strcasecmp(value,"ssdp:alive") )
        ssdp->nts = SSDP_NTS_ALIVE;
      else if( !strcasecmp(value,"ssdp:update") )
        ssdp->nts = SSDP_NTS_UPDATE;
      else if( !strcasecmp(value,"ssdp:byebye") )
        ssdp->nts = SSDP_NTS_BYEBYE;
      else {
        logwarn("_ickSsdpParse (%s): Invalid NTS header value \"%s\"", peer, value );
        _ickSsdpFree( ssdp );
        return NULL;
      }
    }

    else if( !strcasecmp(name,"man") ) {
      if( strcasecmp(value,"ssdp:discover") ) {
        logwarn("_ickSsdpParse (%s): Invalid MAN header value \"%s\"", peer, value );
        _ickSsdpFree( ssdp );
        return NULL;
      }
    }

    else if( !strcasecmp(name,"location") )
      ssdp->location = value;

    else if( !strcasecmp(name,"host") ) {
      char hostval[32];
      sprintf( hostval, "%s:%d", ICKSSDP_MCASTADDR, port );
      if( strcasecmp(value,ICKSSDP_MCASTADDR) && strcasecmp(value,hostval) )
        logwarn("_ickSsdpParse (%s): Invalid HOST header value \"%s\" (expected %s)", peer, value, hostval );
    }

    else if( !strcasecmp(name,"server") )
      ssdp->server = value;

    else if( !strcasecmp(name,"mx") ) {
      ssdp->mx = strtol( value, &ptr, 10 );
      if( *ptr )
        logwarn("_ickSsdpParse (%s): MX is not a number (%s)", peer, value );
    }

    else if( !strcasecmp(name,"bootid.upnp.org") ) {
      ssdp->bootid = strtoul( value, &ptr, 10 );
      if( *ptr )
        logwarn("_ickSsdpParse (%s): BOOTID.UPNP.ORG is not a number (%s)", peer, value );
    }

    else if( !strcasecmp(name,"nextbootid.upnp.org") ) {
      if( ssdp->nts!= SSDP_NTS_UPDATE )
        logwarn("_ickSsdpParse (%s): NEXTBOOTID.UPNP.ORG seen for non update", peer );
      ssdp->nextbootid = strtoul( value, &ptr, 10 );
      if( *ptr )
        logwarn("_ickSsdpParse (%s): BOOTID.UPNP.ORG is not a number (%s)", peer, value );
    }

    else if( !strcasecmp(name,"configid.upnp.org") ) {
      ssdp->configid = strtoul( value, &ptr, 10 );
      if( *ptr )
        logwarn("_ickSsdpParse (%s): CONFIGID.UPNP.ORG is not a number (%s)", peer, value );
    }

    else if( !strcasecmp(name,"cache-control") ) {
      ptr = strchr( value, '=' );
      if( !ptr || strncmp(value,"max-age",7) )
        logwarn("_ickSsdpParse (%s): Invalid CACHE-CONTROL header value \"%s\"", peer, value );
      else {
        unsigned long lifetime = strtoul( ptr+1, &ptr, 10 );
        if( *ptr )
          logwarn("_ickSsdpParse (%s): CACHE-CONTROL:max-age is not a number (%s)", peer, value );
        else
          ssdp->lifetime = lifetime;
      }
    }

#ifdef ICK_DEBUG
    else if( !strcasecmp(name,"ext") )
      {;}

    else if( !strcasecmp(name,"date") )
      {;}

    else if( !strcasecmp(name,"accept-ranges") )
      {;}

    else if( !strcasecmp(name,"x-user-agent") )
      {;}

    else if( !strcasecmp(name,"user-agent") )
      {;}

    else
      loginfo( "_ickSsdpParse (%s): Ignoring field with unknown name \"%s\" (value: %s)",
               peer, name, value );
#endif

/*------------------------------------------------------------------------*\
    Skip to next line
\*------------------------------------------------------------------------*/
nextline:
    line = lineend+1;
    while( line<bufferend && strchr("\n\r",*line) )
      line++;
  }

/*------------------------------------------------------------------------*\
    That's all - return descriptor
\*------------------------------------------------------------------------*/
  return ssdp;
}


/*=========================================================================*\
  Free a SSDP packet descriptor
\*=========================================================================*/
void _ickSsdpFree( ickSsdp_t *ssdp )
{
  debug( "_ickSsdpFree (%p)", ssdp );

  Sfree( ssdp->buffer );
  Sfree( ssdp->uuid );
  Sfree( ssdp );
}


/*=========================================================================*\
  Interpret a SSDP packet,
  call add/modify/remove devices as needed or initiate an M-Search
    ictx - the ickstream context
    ssdp - the ssdp packet
  return values:
   -1 : error (SSDP packet corrupted)
    0 : no device removed nor added
    1 : a device was added
    2 : a device was removed
\*=========================================================================*/
int _ickSsdpExecute( ickP2pContext_t *ictx, const ickSsdp_t *ssdp )
{
  int retval = 0;
  const char *peer;

/*------------------------------------------------------------------------*\
    Get name of peer for warnings
\*------------------------------------------------------------------------*/
  peer = inet_ntoa( ((const struct sockaddr_in *)&ssdp->addr)->sin_addr );

/*------------------------------------------------------------------------*\
    Dispatch ssdp to registry or M-Search functions
\*------------------------------------------------------------------------*/
  switch( ssdp->method ) {

    case SSDP_METHOD_REPLY:
      retval = _ickDeviceAlive( ictx, ssdp );
      break;

    case SSDP_METHOD_NOTIFY:
      if( ssdp->nts==SSDP_NTS_ALIVE || ssdp->nts==SSDP_NTS_UPDATE )
        retval = _ickDeviceAlive( ictx, ssdp );
      else if( ssdp->nts==SSDP_NTS_BYEBYE )
        retval = _ickDeviceRemove( ictx, ssdp );
      else {
        logwarn( "_ickSsdpExecute (%s): Missing NTS header for NOTIFY", peer );
        return -1;
      }
      break;

    case SSDP_METHOD_MSEARCH:
      retval = _ssdpProcessMSearch( ictx, ssdp );
      break;

    default:
      logerr( "_ickSsdpExecute (%s): Undefined method", peer );
      return -1;
  }

/*------------------------------------------------------------------------*\
    That's all -- return result
\*------------------------------------------------------------------------*/
  return retval;
}


#pragma mark - Device registry

/*=========================================================================*\
  Adds or update the device to/in the list
    ictx - the ickstream context
    ssdp - a parsed SSDP packet
  return values:
   -1 : error
    0 : device was updated
    1 : device was added
\*=========================================================================*/
static int _ickDeviceAlive( ickP2pContext_t *ictx, const ickSsdp_t *ssdp )
{
  int                  retval = 0;
  ickDevice_t         *device;
  ickTimer_t          *timer;
  const char          *peer;
  ickErrcode_t         irc;

/*------------------------------------------------------------------------*\
    Get name of peer for warnings
\*------------------------------------------------------------------------*/
  peer = inet_ntoa( ((const struct sockaddr_in *)&ssdp->addr)->sin_addr );

/*------------------------------------------------------------------------*\
    Check required header fields
\*------------------------------------------------------------------------*/
  if( !ssdp->usn ) {
    logwarn( "_ickDeviceUpdate (%s): SSDP request lacks USN header.", peer );
    return -1;
  }
  if( !ssdp->location ) {
    logwarn( "_ickDeviceUpdate (%s): SSDP request lacks LOCATION header.", peer );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Ignore all non ickstream services...
\*------------------------------------------------------------------------*/
  if( !strstr(ssdp->usn,ICKDEVICE_TYPESTR_ROOT) ) {
    debug( "_ickDeviceUpdate (%s): No ickstream device or service (%s).", peer, ssdp->usn );
    return 0;
  }

/*------------------------------------------------------------------------*\
    Lock device list and inhibit notifications sender
\*------------------------------------------------------------------------*/
  _ickLibDeviceListLock( ictx );
  _ickTimerListLock( ictx );

/*------------------------------------------------------------------------*\
    Known device?
\*------------------------------------------------------------------------*/
  device = _ickLibDeviceFindByUuid( ictx, ssdp->uuid );
  if( device ) {
    debug ( "_ickDeviceUpdate (%s): found an instance (updating or reconnecting).", ssdp->usn );

    // If this is an update, only the peer's network topology has changed
    if( ssdp->nts==SSDP_NTS_UPDATE ) {
      debug ( "_ickDeviceUpdate (%s): bootId updated %ld -> %ld.",
              ssdp->usn, device->ssdpBootId, ssdp->bootid );
      device->ssdpBootId = ssdp->bootid;

      // Actually this should not affect the existing connection.
      // However, queue a heartbeat message to terminate for dead connections
      _ickP2pSendNullMessage( ictx, device );
    }

    // New boot Id with SSDP_NTS_ALIVE: the device was disconnected
    else if( device->ssdpBootId!=ssdp->bootid ) {
      debug ( "_ickDeviceUpdate (%s): bootId changed (reconnecting) %ld -> %ld.",
              ssdp->usn, device->ssdpBootId, ssdp->bootid );
      device->ssdpBootId = ssdp->bootid;

      // If there is a dangling connection, we need to reset it
      if( device->wsi /* device->connectionState==ICKDEVICE_ISCLIENT ||
          device->connectionState==ICKDEVICE_ISSERVER */ ) {

        // Trigger wsi destruction, detach wsi from device
        libwebsocket_callback_on_writable( ictx->lwsContext, device->wsi );
        device->wsi = NULL;

        // Reset device state and notify delegates
        device->connectionState = ICKDEVICE_NOTCONNECTED;
        device->ssdpState       = ICKDEVICE_SSDPUNSEEN;
        device->tDisconnect     = _ickTimeNow();
        debug( "_ickDeviceAlive (%s): device state now \"%s\"",
               device->uuid, _ickDeviceConnState2Str(device->connectionState) );
        _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_DISCONNECTED, device->services );
        device->tXmlComplete    = 0.0;

        // Remove pending messages
        _ickDevicePurgeMessages( device );

        // Location changed?
        if( strcmp(device->location,ssdp->location) ) {
          debug ( "_ickDeviceUpdate (%s): updating location (\"%s\"->\"%s\".",
                  ssdp->usn, device->location, ssdp->location );
          char *ptr = strdup( ssdp->location );
          if( !ptr ) {
            logerr( "_ickDeviceUpdate: out of memory" );
            retval = -1;
            goto bail;
          }
          Sfree( device->location );
          device->location = ptr;
        }
      }
    }
  }

/*------------------------------------------------------------------------*\
    New device!
\*------------------------------------------------------------------------*/
  else {
    debug ( "_ickDeviceUpdate (%s): adding new ickstream device (%s)",
            ssdp->usn, ssdp->location );

    // Allocate and initialize descriptor
    device = _ickDeviceNew( ssdp->uuid );
    if( !device ) {
      logerr( "_ickDeviceUpdate: out of memory" );
      retval = -1;
      goto bail;
    }
    device->services       = ICKP2P_SERVICE_GENERIC;
    device->ickUpnpVersion = _ssdpGetVersion( ssdp->usn );
    device->location       = strdup( ssdp->location );
    device->ssdpBootId     = ssdp->bootid;
    device->ssdpConfigId   = ssdp->configid;

    if( !device->location ) {
      logerr( "_ickDeviceUpdate: out of memory" );
      _ickDeviceFree( device );
      retval = -1;
      goto bail;
    }

    // Link device to discovery handler
    _ickLibDeviceAdd( ictx, device );

    // Loop back?
    if( !strcasecmp(ssdp->uuid,ictx->deviceUuid) ) {

      // Complete device description
      device->friendlyName = strdup( ictx->deviceName );
      device->ickP2pLevel  = ICKP2PLEVEL_SUPPORTED;
      device->services     = ictx->ickServices;
      device->lifetime     = ictx->lifetime;
      if( !device->friendlyName ) {
        logerr( "_ickDeviceUpdate: out of memory" );
        _ickDeviceFree( device );
        retval = -1;
        goto bail;
      }

      //Evaluate connection matrix
      device->doConnect = 1;
      if( ictx->lwsConnectMatrixCb )
        device->doConnect = ictx->lwsConnectMatrixCb( ictx, ictx->ickServices, device->services );
      debug( "_ickDeviceUpdate (%s): %s need to connect", device->uuid, device->doConnect?"Do":"No" );
      if( device->doConnect ) {
        device->connectionState = ICKDEVICE_LOOPBACK;
        debug( "_ickDeviceAlive (%s): device state now \"%s\"",
               device->uuid, _ickDeviceConnState2Str(device->connectionState) );
      }

      // Set timestamp and set pointer to loopback device
      device->tXmlComplete = _ickTimeNow();
      ictx->deviceLoopback = device;

      // Signal device readiness to user code
      _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_INITIALIZED, device->services );
      if( device->doConnect )
        _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_CONNECTED, device->services );

    }

    // Return code is 1 (a device was added)
    retval = 1;
  }

/*------------------------------------------------------------------------*\
    Execute callbacks on first discovery
\*------------------------------------------------------------------------*/
  if( device->ssdpState!=ICKDEVICE_SSDPALIVE ) {
    _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_DISCOVERED, device->services );
    device->ssdpState = ICKDEVICE_SSDPALIVE;
  }

/*------------------------------------------------------------------------*\
    Crate or update expiration timer
\*------------------------------------------------------------------------*/
  device->lifetime = ssdp->lifetime;
  timer = _ickTimerFind( ictx, _ickDeviceExpireTimerCb, device, 0 );
  if( timer )
    _ickTimerUpdate( ictx, timer, device->lifetime*1000, 1 );
  else {
    irc = _ickTimerAdd( ictx, device->lifetime*1000, 1, _ickDeviceExpireTimerCb, device, 0 );
    if( irc ) {
      logerr( "_ickDeviceUpdate (%s): could not create expiration timer (%s)",
          device->uuid, ickStrError(irc) );
      retval = -1;
      goto bail;
    }
  }

/*------------------------------------------------------------------------*\
    New device? -> Start xml getter which will do the connection
\*------------------------------------------------------------------------*/
  if( device->tXmlComplete==0.0 ) {
    debug( "_ickDeviceUpdate (%s): need to get XML device descriptor", device->uuid );

    // Don't start wget twice
    if( device->wget ) {
      loginfo( "_ickDeviceUpdate (%s): xml retriever already exists \"%s\".",
          device->uuid, device->location );
      goto bail;
    }

    // Start retrieval of unpn descriptor
    device->wget = _ickWGetInit( ictx, device->location, _ickWGetXmlCb, device, &irc );
    if( !device->wget ) {
      logerr( "_ickDeviceUpdate (%s): could not start xml retriever \"%s\" (%s).",
          device->uuid, device->location, ickStrError(irc) );
      retval = -1;
      goto bail;
    }

    // Link to list of getters
    _ickLibWGettersLock( ictx );
    _ickLibWGettersAdd( ictx, device->wget );
    _ickLibWGettersUnlock( ictx );
  }

/*------------------------------------------------------------------------*\
    If the device is complete, but not connected,
    reinitiate web socket connection with new location
\*------------------------------------------------------------------------*/
  else if( !device->wsi && device->doConnect && device->connectionState!=ICKDEVICE_LOOPBACK ) {
    debug( "_ickDeviceUpdate (%s): trying to reconnect", device->uuid );
    if( _ickDeviceSetLocation(device,ssdp->location) ) {
      retval = -1;
      goto bail;
    }

    // This might be rejected if a connection was initiated by peer
    _ickWebSocketOpen( ictx->lwsContext, device );
  }

/*------------------------------------------------------------------------*\
    Release all locks and return result
\*------------------------------------------------------------------------*/
bail:
  _ickLibDeviceListUnlock( ictx );
  _ickTimerListUnlock( ictx );
  return retval;
}


/*=========================================================================*\
  Remove a device from a discovery context
    ictx - the ickstream context
    ssdp - a parsed SSDP packet
  return values:
   -1 : error
    0 : no device removed
    2 : device was removed
\*=========================================================================*/
static int _ickDeviceRemove( ickP2pContext_t *ictx, const ickSsdp_t *ssdp )
{
  ickDevice_t         *device;
  ickWGetContext_t    *wget, *wgetNext;

/*------------------------------------------------------------------------*\
    Need USN header field
\*------------------------------------------------------------------------*/
  if( !ssdp->usn ) {
    logwarn( "_ickDeviceRemove: SSDP request lacks USN header." );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Ignore non ickstream devices...
\*------------------------------------------------------------------------*/
  if( !strstr(ssdp->usn,ICKDEVICE_TYPESTR_ROOT) )
    return 0;

/*------------------------------------------------------------------------*\
    Lock device list and inhibit timer handling.
\*------------------------------------------------------------------------*/
  _ickLibDeviceListLock( ictx );
  _ickTimerListLock( ictx );

/*------------------------------------------------------------------------*\
    Find matching device entry
\*------------------------------------------------------------------------*/
  device = _ickLibDeviceFindByUuid( ictx, ssdp->uuid );
  if( !device ) {
    logwarn( "_ickDeviceRemove (%s): no instance found.", ssdp->usn );
    _ickLibDeviceListUnlock( ictx );
    _ickTimerListUnlock( ictx );
    return 0;
  }
  device->ssdpState = ICKDEVICE_SSDPBYEBYE;

/*------------------------------------------------------------------------*\
   Execute callback with all registered services
\*------------------------------------------------------------------------*/
  _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_BYEBYE, device->services );

/*------------------------------------------------------------------------*\
    Device still connected?
\*------------------------------------------------------------------------*/
  if( device->wsi ) {
    debug( "_ickDeviceRemove (%s): still connected.", ssdp->usn );
    _ickLibDeviceListUnlock( ictx );
    _ickTimerListUnlock( ictx );
    return 0;
  }

/*------------------------------------------------------------------------*\
   Get rid of device
\*------------------------------------------------------------------------*/

  // Unlink from device list
  _ickLibDeviceRemove( ictx, device );

  // Remove expiration handler for this device
  _ickTimerDeleteAll( ictx, _ickDeviceExpireTimerCb, device, 0 );

  // Remove heartbeat handler for this device
  _ickTimerDeleteAll( ictx, _ickHeartbeatTimerCb, device, 0 );

  // Find and remove HTTP clients for this device
  _ickLibWGettersLock( ictx );
  for( wget=ictx->wGetters; wget; wget=wgetNext ) {
    wgetNext = wget->next;
    if( _ickWGetUserData(wget)==device ) {
      // unlink from list of getters and destroy
      _ickLibWGettersRemove( ictx, wget );
      _ickWGetDestroy( wget );
    }
  }
  _ickLibWGettersUnlock( ictx );

  // Free instance
  _ickDeviceFree( device );

/*------------------------------------------------------------------------*\
    Release all remaining locks
\*------------------------------------------------------------------------*/
  _ickLibDeviceListUnlock( ictx );
  _ickTimerListUnlock( ictx );

/*------------------------------------------------------------------------*\
    That's all (code 2 indicates that a device or service was removed)
\*------------------------------------------------------------------------*/
  return 2;
}


#pragma mark - Management of local devices and services


/*=========================================================================*\
  A new discovery was started
\*=========================================================================*/
ickErrcode_t _ickSsdpNewDiscovery( ickP2pContext_t *ictx )
{
  ickErrcode_t irc;

/*------------------------------------------------------------------------*\
    Lock timer list for queuing messages
\*------------------------------------------------------------------------*/
  _ickTimerListLock( ictx );

/*------------------------------------------------------------------------*\
    Schedule initial advertisements with some inital delay to avoid
    race conditions with two-way connects
\*------------------------------------------------------------------------*/
  irc = _ssdpSendInitialDiscoveryMsg( ictx, NULL, SSDPMSGTYPE_ALIVE, ICKSSDP_INITIALDELAY );
  if( irc ) {
    _ickTimerListUnlock( ictx );
    return irc;
  }

/*------------------------------------------------------------------------*\
    Request immediate advertisements from all reachable upnp devices
\*------------------------------------------------------------------------*/
  _ickLibInterfaceListLock( ictx );
  irc = _ssdpSendDiscoveryMsg( ictx, NULL, SSDPMSGTYPE_MSEARCH, SSDPMSGLEVEL_GLOBAL,
                               ICKSSDP_REPEATS, 0 );
  _ickLibInterfaceListUnlock( ictx );
  if( irc ) {
    _ickTimerListUnlock( ictx );
    return irc;
  }

/*------------------------------------------------------------------------*\
    Setup announcement timer
\*------------------------------------------------------------------------*/
  long interval = ictx->lifetime/ICKSSDP_ANNOUNCEDIVIDOR;
  if( interval<=0 )
    interval = ICKSSDP_DEFAULTLIFETIME/ICKSSDP_ANNOUNCEDIVIDOR;
  _ickTimerAdd( ictx, interval*1000, 0, _ickSsdpAnnounceCb, (void*)ictx, 0 );

/*------------------------------------------------------------------------*\
    Setup periodic search timer
\*------------------------------------------------------------------------*/
  _ickTimerAdd( ictx, ICKSSDP_SEARCHINTERVAL*1000, 0, _ickSsdpSearchCb, (void*)ictx, 0 );

/*------------------------------------------------------------------------*\
    Unlock timer list, that's all
\*------------------------------------------------------------------------*/
  _ickTimerListUnlock( ictx );
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  A discovery is shut down
\*=========================================================================*/
void _ickSsdpEndDiscovery( ickP2pContext_t *ictx )
{

/*------------------------------------------------------------------------*\
    Lock timer list for queuing messages
\*------------------------------------------------------------------------*/
  _ickTimerListLock( ictx );

/*------------------------------------------------------------------------*\
    Delete all timers related to this discovery handler
\*------------------------------------------------------------------------*/
  _ickTimerDeleteAll( ictx, _ickSsdpAnnounceCb, ictx, 0 );

/*------------------------------------------------------------------------*\
    Immediately announce all devices and services as terminated
\*------------------------------------------------------------------------*/
  _ickLibInterfaceListLock( ictx );
  _ssdpSendDiscoveryMsg( ictx, NULL, SSDPMSGTYPE_BYEBYE, SSDPMSGLEVEL_ROOT,
                         0, 0 );
  _ssdpSendDiscoveryMsg( ictx, NULL, SSDPMSGTYPE_BYEBYE, SSDPMSGLEVEL_UUID,
                         0, 0 );
  _ssdpSendDiscoveryMsg( ictx, NULL, SSDPMSGTYPE_BYEBYE, SSDPMSGLEVEL_DEVICEORSERVICE,
                         0, 0 );
  _ickLibInterfaceListUnlock( ictx );

/*------------------------------------------------------------------------*\
    Terminate all known device
\*------------------------------------------------------------------------*/
  _ickLibDeviceListLock( ictx );
  while( ictx->deviceList ) {
    ickDevice_t *device = ictx->deviceList;
    if( device->connectionState==ICKDEVICE_LOOPBACK )
      _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_DISCONNECTED, device->services );
    _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_TERMINATE, device->services );
    _ickLibDeviceRemove( ictx, device );
    _ickDeviceFree( device );
  }
  _ickLibDeviceListUnlock( ictx );

/*------------------------------------------------------------------------*\
    Unlock timer list, that's it
\*------------------------------------------------------------------------*/
  _ickTimerListUnlock( ictx );
}

/*=========================================================================*\
  Execute frequent announcements of offered services
    timer list is already locked for callbacks
\*=========================================================================*/
static void _ickSsdpAnnounceCb( const ickTimer_t *timer, void *data, int tag )
{
  ickP2pContext_t *ictx = data;
  ickErrcode_t     irc;

  debug( "_ickSsdpAnnounceCb (%p): sending periodic announcements...", ictx );

/*------------------------------------------------------------------------*\
    Schedule advertisements
\*------------------------------------------------------------------------*/
  irc = _ssdpSendInitialDiscoveryMsg( ictx, NULL, SSDPMSGTYPE_ALIVE, 0 );
  if( irc ) {
    logerr( "_ickSsdpAnnounceCb: could not send alive announcements (%s).",
        ickStrError(irc) );
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
  Execute frequent searches for ickstream devices
    timer list is already locked for callbacks
\*=========================================================================*/
static void _ickSsdpSearchCb( const ickTimer_t *timer, void *data, int tag )
{
  ickP2pContext_t *ictx = data;
  ickErrcode_t     irc;

  debug( "_ickSsdpSearchCb: executing periodic M-Search..." );

/*------------------------------------------------------------------------*\
    Schedule an M-Search for ickstream root devices
\*------------------------------------------------------------------------*/
  _ickLibInterfaceListLock( ictx );
  irc = _ssdpSendDiscoveryMsg( ictx, NULL, SSDPMSGTYPE_MSEARCH, SSDPMSGLEVEL_DEVICEORSERVICE,
                               1 /*ICKSSDP_REPEATS*/, 0 );
  _ickLibInterfaceListUnlock( ictx );
  if( irc ) {
    logerr( "_ickSsdpSearchCb: could not send alive announcements (%s).",
        ickStrError(irc) );
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
}


#pragma mark - M-Search processing, SSDP sender, Notifications


/*=========================================================================*\
  Process M-SEARCH requests
    See "UPnP Device Architecture 1.1": chapter 1.3.3
    ictx - the ickstream context
    ssdp - the ssdp packet
  returns -1 on error, 0 on success
\*=========================================================================*/
static int _ssdpProcessMSearch( ickP2pContext_t *ictx, const ickSsdp_t *ssdp )
{
  int                  retcode = 0;

  debug( "_ssdpProcessMSearch: from %s:%d ST:%s",
         inet_ntoa(((const struct sockaddr_in *)&ssdp->addr)->sin_addr),
         ntohs(((const struct sockaddr_in *)&ssdp->addr)->sin_port), ssdp->st );

/*------------------------------------------------------------------------*\
    Need ST header fields
\*------------------------------------------------------------------------*/
  if( !ssdp->st ) {
    logwarn( "_ssdpProcessMSearch: SSDP request lacks ST header." );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Lock timer list for queuing messages and interface list
\*------------------------------------------------------------------------*/
  _ickTimerListLock( ictx );
  _ickLibInterfaceListLock( ictx );

/*------------------------------------------------------------------------*\
    Search for all devices and services
\*------------------------------------------------------------------------*/
  if( !strcmp(ssdp->st,"ssdp:all") ) {
    _ssdpSendInitialDiscoveryMsg( ictx, &ssdp->addr, SSDPMSGTYPE_MRESPONSE, 0 );
  }

/*------------------------------------------------------------------------*\
    Search for root device
\*------------------------------------------------------------------------*/
  else if( !strcmp(ssdp->st,"upnp:rootdevice") ) {
    _ssdpSendDiscoveryMsg( ictx, &ssdp->addr, SSDPMSGTYPE_MRESPONSE, SSDPMSGLEVEL_ROOT,
                           ICKSSDP_REPEATS, 0 );
  }

/*------------------------------------------------------------------------*\
    Search for a device with specific UUID
\*------------------------------------------------------------------------*/
  else if( !strncmp(ssdp->st,"uuid:",5) ) {
    if( !strcasecmp(ssdp->st+5,ictx->deviceUuid) ) {
      _ssdpSendDiscoveryMsg( ictx, &ssdp->addr, SSDPMSGTYPE_MRESPONSE, SSDPMSGLEVEL_UUID,
                             ICKSSDP_REPEATS, 0 );
    }
  }

/*------------------------------------------------------------------------*\
    Specific search for a ickstream device
\*------------------------------------------------------------------------*/
  else if( !_ssdpVercmp(ssdp->st,ICKDEVICE_TYPESTR_ROOT) )
    _ssdpSendDiscoveryMsg( ictx, &ssdp->addr, SSDPMSGTYPE_MRESPONSE, SSDPMSGLEVEL_DEVICEORSERVICE,
                           ICKSSDP_REPEATS, 0 );

/*------------------------------------------------------------------------*\
    Unlock timer and interface list, that's all
\*------------------------------------------------------------------------*/
  _ickTimerListUnlock( ictx );
  _ickLibInterfaceListUnlock( ictx );
  return retcode;
}


/*=========================================================================*\
  Send initial set of advertisements
    (also used for M-Search answers to "ssdp:all")
    See "UPnP Device Architecture 1.1": chapter 1.2.2 and 1.3.3
    ictx    - the ickstream context
    addr    - NULL for mcast,
              else unicast target in network byte order (for M-Search responses)
    type    - message type (alive or m-search response)
              don't use for updates, use _ssdpNewInterface() instead!
    Caller should lock timer list (which is already the case in timer callbacks)
    Caller should lock interface list
\*=========================================================================*/
static ickErrcode_t _ssdpSendInitialDiscoveryMsg( ickP2pContext_t *ictx,
                                                  const struct sockaddr *addr,
                                                  ickSsdpMsgType_t type, long delay )
{
  ickErrcode_t irc;
  debug( "_ssdpSendInitialDiscoveryMsg (%p): %d", ictx, type );

/*------------------------------------------------------------------------*\
    Advertise UPNP root
\*------------------------------------------------------------------------*/
  irc = _ssdpSendDiscoveryMsg( ictx, addr, type, SSDPMSGLEVEL_ROOT,
                               ICKSSDP_REPEATS, delay );
  if( irc )
    return irc;

/*------------------------------------------------------------------------*\
    Advertise UUID
\*------------------------------------------------------------------------*/
  irc = _ssdpSendDiscoveryMsg( ictx, addr, type, SSDPMSGLEVEL_UUID,
                               ICKSSDP_REPEATS, delay );
  if( irc )
    return irc;

/*------------------------------------------------------------------------*\
    Advertise ickStream root device
\*------------------------------------------------------------------------*/
  irc = _ssdpSendDiscoveryMsg( ictx, addr, type, SSDPMSGLEVEL_DEVICEORSERVICE,
                               ICKSSDP_REPEATS, delay );
  if( irc )
    return irc;

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Send update messages (if an interface was added)
    See "UPnP Device Architecture 1.1": chapter 1.2.4
    ictx    - the ickstream context
    Caller should lock timer list (which is already the case in timer callbacks)
    Caller should lock interface list
\*=========================================================================*/
ickErrcode_t _ssdpNewInterface( ickP2pContext_t *ictx )
{
  ickInterface_t *interface;
  ickErrcode_t    irc = ICKERR_SUCCESS;
  debug( "_ssdpNewInterface (%p)", ictx );

/*------------------------------------------------------------------------*\
    Set nextbootid to be announced
\*------------------------------------------------------------------------*/
  ictx->upnpNextBootId = ictx->upnpBootId+1;

/*------------------------------------------------------------------------*\
    Loop over all interfaces and send SSDP updates
\*------------------------------------------------------------------------*/
  for( interface=ictx->interfaces; interface; interface=interface->next ) {

    // Ignore new interface for updates
    if( !interface->announcedBootId )
      continue;

    // Advertise UPNP root
    irc = __ssdpSendDiscoveryMsg( ictx, interface, NULL, SSDPMSGTYPE_UPDATE, SSDPMSGLEVEL_ROOT, 1, 0 );
    if( irc )
      break;

    // Advertise UUID
    irc = __ssdpSendDiscoveryMsg( ictx, interface, NULL, SSDPMSGTYPE_UPDATE, SSDPMSGLEVEL_UUID, 1, 0 );
    if( irc )
      break;

    // Advertise ickStream root device
    irc = __ssdpSendDiscoveryMsg( ictx, interface, NULL, SSDPMSGTYPE_UPDATE, SSDPMSGLEVEL_DEVICEORSERVICE, 1, 0 );
    if( irc )
      break;
  }

/*------------------------------------------------------------------------*\
    Use new bootid from now on
\*------------------------------------------------------------------------*/
  ictx->upnpBootId = ictx->upnpNextBootId;

/*------------------------------------------------------------------------*\
    Now reannounce existing and new interfaces
\*------------------------------------------------------------------------*/
  irc = _ssdpSendInitialDiscoveryMsg( ictx, NULL, SSDPMSGTYPE_ALIVE, 0 );

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return irc;
}


/*=========================================================================*\
  Send byebye messages (if an interface is to be removed)
    See "UPnP Device Architecture 1.1": chapter 1.2.3
    ictx      - the ickstream context
    interface - interface to be removed (might NOt be NULL)
  Note: sending byebyes on interfaces that are already down will result in
        error messages
  Note: this will not remove the interface
\*=========================================================================*/
ickErrcode_t _ssdpByebyeInterface( ickP2pContext_t *ictx, ickInterface_t *interface )
{
  ickErrcode_t irc, retcode = ICKERR_SUCCESS;
  debug( "_ssdpByebyeInterface (%p): \"%s\"", ictx, interface->name );

/*------------------------------------------------------------------------*\
    Immediately send byebye without any delay
\*------------------------------------------------------------------------*/
  irc = __ssdpSendDiscoveryMsg( ictx, interface, NULL, SSDPMSGTYPE_BYEBYE, SSDPMSGLEVEL_ROOT, 0, 0 );
  if( irc )
    retcode = irc;
  irc = __ssdpSendDiscoveryMsg( ictx, interface, NULL, SSDPMSGTYPE_BYEBYE, SSDPMSGLEVEL_UUID, 0, 0 );
  if( irc )
    retcode = irc;
  irc = __ssdpSendDiscoveryMsg( ictx, interface, NULL, SSDPMSGTYPE_BYEBYE, SSDPMSGLEVEL_DEVICEORSERVICE, 0, 0 );
  if( irc )
    retcode = irc;

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return retcode;
}


/*=========================================================================*\
  Queue an outgoing discovery message
    ictx    - the ickstream context to use
    addr    - NULL for mcast (on all interfaces),
              else unicast target in network byte order (for M-Search responses)
    type    - the message type (alive,byebye,M-Search,Response)
    level   - the message type (global,root,uuid,service)
    repeat  - number of repetitions (randomly distributed in time)
              if <=0 one message is sent immediately
    delay   - initial delay of first message (in ms), if repeat>0
    returns -1 on error, 0 on success
    Caller should lock timer list (which is already the case in timer callbacks)
    Caller should lock interface list
\*=========================================================================*/
static ickErrcode_t _ssdpSendDiscoveryMsg( ickP2pContext_t *ictx,
                                           const struct sockaddr *addr,
                                           ickSsdpMsgType_t type,
                                           ssdpMsgLevel_t level,
                                           int repeat, long delay )
{
  ickInterface_t *interface;
  ickErrcode_t    irc = ICKERR_SUCCESS;

/*------------------------------------------------------------------------*\
    In broadcast mode loop over all interfaces
\*------------------------------------------------------------------------*/
  if( !addr ) {
    for( interface=ictx->interfaces; interface; interface=interface->next ) {
      interface->announcedBootId = ictx->upnpBootId;
      irc = __ssdpSendDiscoveryMsg( ictx, interface, NULL, type, level, repeat, delay );
      if( irc )
        break;
    }
    return irc;
  }

/*------------------------------------------------------------------------*\
    In unicast mode find interface matching for target address
\*------------------------------------------------------------------------*/
  interface = _ickLibInterfaceForAddr( ictx, ((const struct sockaddr_in *)addr)->sin_addr.s_addr );
  if( !interface ) {
    loginfo( "_ssdpSendDiscoveryMsg: no interface found for \"%s\"",
             inet_ntoa(((const struct sockaddr_in *)addr)->sin_addr) );
    irc = ICKERR_NOINTERFACE;
  }

/*------------------------------------------------------------------------*\
    Do the unicasts
\*------------------------------------------------------------------------*/
  else {
    interface->announcedBootId = ictx->upnpBootId;
    irc = __ssdpSendDiscoveryMsg( ictx, interface, addr, type, level, repeat, delay );
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return irc ;
}

static ickErrcode_t __ssdpSendDiscoveryMsg( ickP2pContext_t *ictx,
                                            ickInterface_t *interface,
                                            const struct sockaddr *addr,
                                            ickSsdpMsgType_t type,
                                            ssdpMsgLevel_t level,
                                            int repeat, long delay )
{

  upnp_notification_t *note;
  struct sockaddr_in   sockname;
  socklen_t            addr_len = sizeof( struct sockaddr_in );
  char                 addrstr[64];
  time_t               now;
  ickErrcode_t         irc = ICKERR_SUCCESS;
  char                 timestr[80];
  char                *nst = NULL;
  char                *usn = NULL;
  char                *sstr = NULL;
  char                *message = NULL;

  debug( "__ssdpSendDiscoveryMsg (%p): level=%d, type=%d, repeat=%d",
         ictx, level, type, repeat );

/*------------------------------------------------------------------------*\
    Use multicast?
\*------------------------------------------------------------------------*/
  if( !addr ) {
    memset( &sockname, 0, sizeof(struct sockaddr_in) );
    sockname.sin_family      = AF_INET;
    sockname.sin_addr.s_addr = inet_addr( ICKSSDP_MCASTADDR );
    sockname.sin_port        = htons( ictx->upnpListenerPort );
    addr = (const struct sockaddr*)&sockname;
  }
  snprintf( addrstr, sizeof(addrstr), "%s:%d",
         inet_ntoa(((const struct sockaddr_in *)addr)->sin_addr),
         ntohs(((const struct sockaddr_in *)addr)->sin_port));
  debug( "__ssdpSendDiscoveryMsg (%p): target \"%s\"", ictx, addrstr );

/*------------------------------------------------------------------------*\
    Create NT/ST and USN according to level and service
    See tables 1-1 and 1-3 in "UPnP Device Architecture 1.1"
\*------------------------------------------------------------------------*/
  switch( level ) {
    case SSDPMSGLEVEL_GLOBAL:
      // Only possible for M-Searches
      if( type!=SSDPMSGTYPE_MSEARCH ) {
        logerr( "__ssdpSendDiscoveryMsg: bad message type (%d) for GLOBAL level.", type );
        return ICKERR_INVALID;
      }
      sstr = ICKDEVICE_STRING_ROOT;
      nst = strdup( "ssdp:all" );
      usn = strdup( "ssdp:all" );
      break;

    case SSDPMSGLEVEL_ROOT:
      sstr = ICKDEVICE_STRING_ROOT;
      nst = strdup( "upnp:rootdevice" );
      asprintf( &usn, "uuid:%s::upnp:rootdevice", ictx->deviceUuid );
      break;

    case SSDPMSGLEVEL_UUID:
      sstr = ICKDEVICE_STRING_ROOT;
      asprintf( &nst, "uuid:%s", ictx->deviceUuid );
      asprintf( &usn, "uuid:%s", ictx->deviceUuid );
      break;

    case SSDPMSGLEVEL_DEVICEORSERVICE:
      sstr = ICKDEVICE_STRING_ROOT;
      nst  = ICKDEVICE_TYPESTR_ROOT;
      asprintf( &usn, "uuid:%s::%s", ictx->deviceUuid, nst );
      nst = strdup( nst );
      break;

    default:
      logerr( "__ssdpSendDiscoveryMsg: bad message level (%d)", level );
      return ICKERR_INVALID;
  }
  if( !nst || !usn ) {
    Sfree( nst );
    Sfree( usn );
    logerr( "__ssdpSendDiscoveryMsg: out of memory" );
    return ICKERR_NOMEM;
  }
  debug( "__ssdpSendDiscoveryMsg (%p): sstr=\"%s\" nst=\"%s\" usn=\"%s\"",
         ictx, sstr, nst, usn );

/*------------------------------------------------------------------------*\
    Create message according to type
\*------------------------------------------------------------------------*/
  switch( type ) {

    // See "UPnP Device Architecture 1.1": chapter 1.2.2
    case SSDPMSGTYPE_ALIVE:
      asprintf( &message,
        "NOTIFY * HTTP/1.1\r\n"
        "HOST: %s:%d\r\n"
        "CACHE-CONTROL: max-age=%d\r\n"
        "LOCATION: http://%s:%d/%s.xml\r\n"
        "NT: %s\r\n"
        "NTS: ssdp:alive\r\n"
        "SERVER: %s UPnP/%d.%d %s\r\n"
        "USN: %s\r\n"
        "BOOTID.UPNP.ORG: %ld\r\n"
        "CONFIGID.UPNP.ORG: %ld\r\n"
        "\r\n",
        ICKSSDP_MCASTADDR, ictx->upnpListenerPort, ictx->lifetime,
        interface->hostname, ictx->lwsPort, sstr,
        nst, ictx->osName,
        ICKDEVICE_UPNP_MAJOR, ICKDEVICE_UPNP_MINOR,
        ickUpnpNames.productAndVersion, usn,
        ictx->upnpBootId, ictx->upnpConfigId );
      break;

    // See "UPnP Device Architecture 1.1": chapter 1.2.3
    case SSDPMSGTYPE_BYEBYE:
      asprintf( &message,
        "NOTIFY * HTTP/1.1\r\n"
        "HOST: %s:%d\r\n"
        "NT: %s\r\n"
        "NTS: ssdp:byebye\r\n"
        "USN: %s\r\n"
        "BOOTID.UPNP.ORG: %ld\r\n"
        "CONFIGID.UPNP.ORG: %ld\r\n"
        "\r\n",
        ICKSSDP_MCASTADDR, ictx->upnpListenerPort, nst, usn,
        ictx->upnpBootId, ictx->upnpConfigId );
      break;

      // See "UPnP Device Architecture 1.1": chapter 1.2.4
      case SSDPMSGTYPE_UPDATE:
        asprintf( &message,
          "NOTIFY * HTTP/1.1\r\n"
          "HOST: %s:%d\r\n"
          "LOCATION: http://%s:%d/%s.xml\r\n"
          "NT: %s\r\n"
          "NTS: ssdp:update\r\n"
          "USN: %s\r\n"
          "BOOTID.UPNP.ORG: %ld\r\n"
          "CONFIGID.UPNP.ORG: %ld\r\n"
          "NEXTBOOTID.UPNP.ORG: %ld\r\n"
          "\r\n",
          ICKSSDP_MCASTADDR, ictx->upnpListenerPort,
          interface->hostname, ictx->lwsPort, sstr,
          nst, usn,
          ictx->upnpBootId, ictx->upnpConfigId, ictx->upnpNextBootId );
        break;

      // See "UPnP Device Architecture 1.1": chapter 1.3.2
    case SSDPMSGTYPE_MSEARCH:
        asprintf( &message,
          "M-SEARCH * HTTP/1.1\r\n"
          "HOST: %s:%d\r\n"
          "MAN: \"ssdp:discover\"\r\n"
          "MX: %d\r\n"
          "ST: %s\r\n"
          "USER-AGENT: %s UPnP/%d.%d %s\r\n"
          "\r\n",
          ICKSSDP_MCASTADDR, ictx->upnpListenerPort, ICKSSDP_MSEARCH_MX,
          nst, ictx->osName,
          ICKDEVICE_UPNP_MAJOR, ICKDEVICE_UPNP_MINOR,
          ickUpnpNames.productAndVersion );
        break;

    // See "UPnP Device Architecture 1.1": chapter 1.3.3
    case SSDPMSGTYPE_MRESPONSE:

      // Create RFC1123 timestamp
      time( &now );
      memset( timestr, 0, sizeof(timestr) );
      strftime( timestr, sizeof(timestr)-1, "%a, %d %b %Y %H:%M:%S GMT", localtime(&now) );

      // construct message
      asprintf( &message,
        "HTTP/1.1 200 OK\r\n"
        "CACHE-CONTROL: max-age=%d\r\n"
        "DATE: %s\r\n"
        "EXT:\r\n"
        "LOCATION: http://%s:%d/%s.xml\r\n"
        "SERVER: %s UPnP/%d.%d %s\r\n"
        "ST: %s\r\n"
        "USN: %s\r\n"
        "BOOTID.UPNP.ORG: %ld\r\n"
        "CONFIGID.UPNP.ORG: %ld\r\n"
        "\r\n",
        ictx->lifetime, timestr,
        interface->hostname, ictx->lwsPort, sstr, ictx->osName,
        ICKDEVICE_UPNP_MAJOR, ICKDEVICE_UPNP_MINOR,
        ickUpnpNames.productAndVersion, nst, usn,
        ictx->upnpBootId, ictx->upnpConfigId );
      break;

    default:
      Sfree( nst );
      Sfree( usn );
      logerr( "__ssdpSendDiscoveryMsg: bad message type (%d)", type );
      return ICKERR_INVALID;
  }
  debug( "__ssdpSendDiscoveryMsg (%p): msg=\"%s\"", ictx, message );
  Sfree( nst );
  Sfree( usn );
  if( !message ) {
    logerr( "__ssdpSendDiscoveryMsg: out of memory" );
    return ICKERR_NOMEM;
  }

/*------------------------------------------------------------------------*\
    Immediate submission?
\*------------------------------------------------------------------------*/
  if( repeat<=0 ) {
    ssize_t n;
    size_t  len = strlen( message );
    debug( "__ssdpSendDiscoveryMsg: immediately sending %ld bytes to %s: \"%s\"",
           (long)len, addrstr, message );

    // Try to send packet via communication socket
    n = sendto( interface->upnpComSocket, message, len, 0, addr, addr_len );

    // Any error ?
    if( n<0 ) {
      logerr( "__ssdpSendDiscoveryMsg: could not send to %s (%s)",
              addrstr, strerror(errno) );
      irc = ICKERR_GENERIC;
    }
    else if( n<len ) {
      logerr( "__ssdpSendDiscoveryMsg: could not send all data to %s (%d of %d)",
              addrstr, n, len );
      irc = ICKERR_GENERIC;
    }

    // free message, that's all
    Sfree( message );
    return ICKERR_SUCCESS;
  }

/*------------------------------------------------------------------------*\
    No immediate transmission: prepare timers
\*------------------------------------------------------------------------*/
  debug( "__ssdpSendDiscoveryMsg: enqueing %d transmissions of %ld bytes to %s: \"%s\"",
         repeat, (long)strlen(message), addrstr, message );

/*------------------------------------------------------------------------*\
    Create and init notification
\*------------------------------------------------------------------------*/
  note = calloc( 1, sizeof(upnp_notification_t) );
  if( !note ) {
    Sfree( message );
    logerr( "__ssdpSendDiscoveryMsg: out of memory" );
    return ICKERR_NOMEM;
  }
  note->message  = message;
  note->socket   = interface->upnpComSocket;
  note->refCntr  = repeat>0 ? repeat : 1;
  note->socknamelen = addr_len;
  memcpy( &note->sockname, addr, addr_len );

/*------------------------------------------------------------------------*\
    Queue instances, randomly delay transmissions after initial delay
\*------------------------------------------------------------------------*/
  while( repeat-- ) {
    delay += random() % ICKSSDP_RNDDELAY;
    irc = _ickTimerAdd( ictx, delay, 1, _ickSsdpNotifyCb, note, 0 );
    if( irc ) {
      _ickTimerDeleteAll( ictx, _ickSsdpNotifyCb, note, 0 );
      Sfree( note->message );
      Sfree( note );
      return irc;
    }
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  if( irc ) {
    Sfree( note->message );
    Sfree( note );
  }
  return irc;
}


#pragma mark -- Timer callbacks


/*=========================================================================*\
  Send a notification: remove from a discovery context
    timer list is already locked
\*=========================================================================*/
static void _ickSsdpNotifyCb( const ickTimer_t *timer, void *data, int tag )
{
  upnp_notification_t *note = data;
  ssize_t              n;
  size_t               len = strlen( note->message );
  char                 addrstr[64];

  // Get peer name for debugging
  snprintf( addrstr, sizeof(addrstr), "%s:%d",
         inet_ntoa(((const struct sockaddr_in *)&note->sockname)->sin_addr),
         ntohs(((const struct sockaddr_in *)&note->sockname)->sin_port));

  debug( "_ickSsdpNotifyCb: sending %ld bytes to %s: \"%s\"",
         (long)len, addrstr, note->message );

/*------------------------------------------------------------------------*\
    Try to send packet via discovery socket
\*------------------------------------------------------------------------*/
  n = sendto( note->socket, note->message, len, 0, (struct sockaddr *)&note->sockname, note->socknamelen );
  if( n<0 ) {
    logerr( "_ickSsdpNotifyCb: could not send to %s (%s)",
            addrstr, strerror(errno) );
  }
  else if( n<len ) {
    logerr( "_ickSsdpNotifyCb: could not send all data to %s (%d of %d)",
            addrstr, n, len );
  }

/*------------------------------------------------------------------------*\
    Release descriptor if no further instance is used
\*------------------------------------------------------------------------*/
  note->refCntr--;
  if( !note->refCntr ) {
    Sfree( note->message );
    Sfree( note );
  }
}


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
  device->ssdpState = ICKDEVICE_SSDPEXPIRED;

/*------------------------------------------------------------------------*\
    Execute callback with all registered services
\*------------------------------------------------------------------------*/
  _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_EXPIRED, device->services );

/*------------------------------------------------------------------------*\
    Get rid of device descriptor if not connected
\*------------------------------------------------------------------------*/
  if( device->connectionState==ICKDEVICE_NOTCONNECTED ) {

    // Remove heartbeat handler for this device
    _ickTimerDeleteAll( ictx, _ickHeartbeatTimerCb, device, 0 );

    // Unlink from device list and free instance
    _ickLibDeviceRemove( ictx, device );
    _ickDeviceFree( device );
  }

/*------------------------------------------------------------------------*\
    Release device list locks
\*------------------------------------------------------------------------*/
  _ickLibDeviceListUnlock( ictx );
}


#pragma mark -- Tools


/*=========================================================================*\
  Get version of a upnp service/device descriptor
    returns -1 on error and ignores minor version
\*=========================================================================*/
static int _ssdpGetVersion( const char *dscr )
{
  char  *ptr;
  int    version;
  debug( "_ssdpGetVersion: \"%s\"", dscr );

/*------------------------------------------------------------------------*\
  Get string
\*------------------------------------------------------------------------*/
  ptr = strrchr( dscr, ':' );
  if( !ptr ) {
    logwarn( "_ssdpGetVersion: found no version (%s).", dscr );
    return -1;
  }
  version = strtol( ptr+1, &ptr, 10 );
  if( *ptr=='.' )
    logwarn( "_ssdpGetVersion: ignoring minor version (%s).", dscr );
  else if( *ptr )
    logwarn( "_ssdpGetVersion: malformed descriptor (%s).", dscr );

/*------------------------------------------------------------------------*\
  That's compatible...
\*------------------------------------------------------------------------*/
  return version;
}


/*=========================================================================*\
  compare device/service descriptor including version
    See "UPnP Device Architecture 1.1": chapter 1.2.2 (p. 20), chapter 2 (p. 39)
    returns 0 if the service descriptor matches and the requested version is
    lower or equal to the advertised version
\*=========================================================================*/
static int _ssdpVercmp( const char *req, const char *adv )
{
  char  *ptr;
  size_t len;
  int    rVersion, aVersion;
  debug( "_ssdpVercmp: req=\"%s\" adv=\"%s\".", req, adv );

/*------------------------------------------------------------------------*\
  Get requested version string and prefix length
\*------------------------------------------------------------------------*/
  ptr = strrchr( req, ':' );
  if( !ptr ) {
    logwarn( "_ssdpVercmp: found no version in requested service (%s).", req );
    return strcmp( req, adv );
  }
  len = ptr - req;
  rVersion = strtol( ptr+1, &ptr, 10 );
  if( *ptr=='.' )
    logwarn( "_ssdpVercmp: ignoring minor version in requested service (%s).", req );
  else if( *ptr )
    logwarn( "_ssdpVercmp: malformed requested service (%s).", req );

/*------------------------------------------------------------------------*\
  Get advertised version string
\*------------------------------------------------------------------------*/
  ptr = strrchr( adv, ':' );
  if( !ptr ) {
    logwarn( "_ssdpVercmp: found no version in advertised service (%s).", adv );
    return -1;
  }

/*------------------------------------------------------------------------*\
  Check match of prefix
\*------------------------------------------------------------------------*/
  if( ptr-adv!=len || strncmp(adv,req,len+1) )
    return -1;

/*------------------------------------------------------------------------*\
  Get advertised versions
\*------------------------------------------------------------------------*/
  aVersion = strtol( ptr+1, &ptr, 10 );
  if( *ptr=='.' )
    logwarn( "_ssdpVercmp: ignoring minor version in advertised service (%s).", req );
  else if( *ptr )
    logwarn( "_ssdpVercmp: malformed advertised service (%s).", req );

/*------------------------------------------------------------------------*\
  Requested version must be lower or equal to advertised
\*------------------------------------------------------------------------*/
  if( rVersion>aVersion )
    return -1;

/*------------------------------------------------------------------------*\
  That's compatible...
\*------------------------------------------------------------------------*/
  return 0;
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/

