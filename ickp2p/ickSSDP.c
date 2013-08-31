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
  SSDPMSGLEVEL_SERVICE
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
static int   _ickDeviceAlive( ickP2pContext_t *ictx, const ickSsdp_t *ssdp );
static int   _ickDeviceRemove( ickP2pContext_t *ictx, const ickSsdp_t *ssdp );

static void         _ickSsdpAnnounceCb( const ickTimer_t *timer, void *data, int tag );
static void         _ickSsdpSearchCb( const ickTimer_t *timer, void *data, int tag );

static int          _ssdpProcessMSearch( ickP2pContext_t *ictx, const ickSsdp_t *ssdp );
static ickErrcode_t _ssdpSendInitialDiscoveryMsg( ickP2pContext_t *ictx, const struct sockaddr *addr );
static ickErrcode_t _ssdpSendDiscoveryMsg( ickP2pContext_t *ictx, const struct sockaddr *addr, ickSsdpMsgType_t type, ssdpMsgLevel_t level, ickP2pServicetype_t service, int repeat );
static void         _ickSsdpNotifyCb( const ickTimer_t *timer, void *data, int tag );

static int                 _ssdpGetVersion( const char *dscr );
static int                 _ssdpVercmp( const char *user, const char *adv );





/*=========================================================================*\
  Create an SSDP listener
  returns listener port or -1 on error
\*=========================================================================*/
int _ickSsdpCreateListener( in_addr_t ifaddr, int port )
{
  int sd;
  int rc;
  int opt;

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
  returns a filled ssdp descriptor or NULL on error
\*=========================================================================*/
ickSsdp_t *_ickSsdpParse( const char *buffer, size_t length, const struct sockaddr *addr )
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
      if( strcasecmp(value,"239.255.255.250") &&
          strcasecmp(value,"239.255.255.250:1900") ) {
        logwarn("_ickSsdpParse (%s): Invalid HOST header value \"%s\"", peer, value );
        _ickSsdpFree( ssdp );
        return NULL;
      }
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
    dh   - the discovery handler to be used
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
      if( ssdp->nts==SSDP_NTS_ALIVE )
        retval = _ickDeviceAlive( ictx, ssdp );
      else if( ssdp->nts==SSDP_NTS_UPDATE )
        retval = 0;                             // fixme: should check new boot id and remove devices if not equal
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
    ssdp - a parsed SSDP packet
    dh   - the related discovery handler
  return values:
   -1 : error
    0 : device was updated
    1 : device was added
\*=========================================================================*/
static int _ickDeviceAlive( ickP2pContext_t *ictx, const ickSsdp_t *ssdp )
{
  ickDevice_t         *device;
  ickTimer_t          *timer;
  ickWGetContext_t    *wget;
  const char          *peer;
  ickErrcode_t         irc;
  ickP2pServicetype_t  stype;

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
    Ignore non ickstream services...
\*------------------------------------------------------------------------*/
  stype = _ickDescrFindServiceByUsn( ssdp->usn );
  if( stype==ICKP2P_SERVICE_NONE ) {
    debug( "_ickDeviceUpdate (%s): No ickstream device or service (%s).", peer, ssdp->usn );
    return 0;
  }

/*------------------------------------------------------------------------*\
    Lock device list and inhibit notifications sender
\*------------------------------------------------------------------------*/
  _ickLibDeviceListLock( ictx );
  _ickTimerListLock( ictx );

/*------------------------------------------------------------------------*\
   New device?
\*------------------------------------------------------------------------*/
  device = _ickLibDeviceFindByUuid( ictx, ssdp->uuid );
  if( !device ) {
    debug ( "_ickDeviceUpdate (%s): adding new device (%s).", ssdp->usn, ssdp->location );

/*
    // Fixme: This is for backward compatibility
    // add new devices only via root descriptor
    if( stype!=ICKP2P_SERVICE_GENERIC ) {
      _ickDiscoveryDeviceListUnlock( dh );
      _ickTimerListUnlock( icklib );
       return 0;
    }
*/

    // Allocate and initialize descriptor
    device = _ickDeviceNew( ssdp->uuid, ICKDEVICE_SSDP );
    if( !device ) {
      logerr( "_ickDeviceUpdate: out of memory" );
      _ickLibDeviceListUnlock( ictx );
      _ickTimerListUnlock( ictx );
      return -1;
    }
    device->services       = stype;
    device->lifetime       = ssdp->lifetime;
    device->ickUpnpVersion = _ssdpGetVersion( ssdp->usn );

    //fixme: Ver.1 devices only answer to root requests
    if( device->ickUpnpVersion==1 ) {
      char *ptr=strrchr(ssdp->location,'/');
      if( ptr )
        asprintf( &device->location, "%.*s/Root.xml", ptr-ssdp->location, ssdp->location );
    }
    if( !device->location )
      device->location   = strdup( ssdp->location );
    if( !device->location ) {
      logerr( "_ickDeviceUpdate: out of memory" );
      _ickDeviceFree( device );
      _ickLibDeviceListUnlock( ictx );
      _ickTimerListUnlock( ictx );
      return -1;
    }

    // Start retrieval of unpn descriptor
    wget = _ickWGetInit( ictx, device->location, _ickWGetXmlCb, device, &irc );
    if( !wget ) {
      logerr( "_ickDeviceUpdate (%s): could not start xml retriever for SSDP update on \"%s\" (%s).",
          device->uuid, device->location, ickStrError(irc) );
      _ickDeviceFree( device );
      _ickLibDeviceListUnlock( ictx );
      _ickTimerListUnlock( ictx );
      return -1;
    }

    // Link to list of getters
    _ickLibWGettersLock( ictx );
    _ickLibWGettersAdd( ictx, wget );
    _ickLibWGettersUnlock( ictx );

    // Create expiration timer
    irc = _ickTimerAdd( ictx, device->lifetime*1000, 1, _ickDeviceExpireTimerCb, device, 0 );
    if( irc ) {
      logerr( "_ickDeviceUpdate (%s): could not create expiration timer (%s)",
          device->uuid, ickStrError(irc) );
      _ickDeviceFree( device );
      _ickLibDeviceListUnlock( ictx );
      _ickTimerListUnlock( ictx );
      return -1;
    }

    //Link device to discovery handler
    _ickLibDeviceAdd( ictx, device );

    // Execute callbacks registered with discovery handler
    // this is done by the xml handler after the device is full initialzed
    // _ickDiscoveryExecDiscoveryCallback( dh, device, ICKP2P_NEW, stype );

    // Release all locks and return code 1 (a device was added)
    _ickLibDeviceListUnlock( ictx );
    _ickTimerListUnlock( ictx );
    return 1;
  }

/*------------------------------------------------------------------------*\
   Update a known device
\*------------------------------------------------------------------------*/
  debug ( "_ickDeviceUpdate (%s): found an instance (updating).", ssdp->usn );

  // Update timestamp for expiration time
  device->lifetime = ssdp->lifetime;

  // Find and modify expiration handler for this device
  timer = _ickTimerFind( ictx, _ickDeviceExpireTimerCb, device, 0 );
  if( timer )
    _ickTimerUpdate( ictx, timer, device->lifetime*1000, 1 );
  else {
    if( device->type==ICKDEVICE_SSDP )
      logerr( "_ickDeviceUpdate: could not find expiration timer." );
    irc = _ickTimerAdd( ictx, device->lifetime*1000, 1, _ickDeviceExpireTimerCb, device, 0 );
    if( irc ) {
      logerr( "_ickDeviceUpdate (%s): could not create expiration timer (%s)",
          device->uuid, ickStrError(irc) );
      _ickDeviceFree( device );
      _ickLibDeviceListUnlock( ictx );
      _ickTimerListUnlock( ictx );
      return -1;
    }
  }

  // Check protocol version, should be same on one UUID
  if( device->ickUpnpVersion!=_ssdpGetVersion(ssdp->usn) ) {
    if( device->type==ICKDEVICE_SSDP )
      logwarn( "_ickDeviceUpdate (%s): version mismatch for USN \"%s\" (expected %d).",
               device->uuid, ssdp->usn, device->ickUpnpVersion );
    else
      device->ickUpnpVersion =_ssdpGetVersion( ssdp->usn );
  }

/*
  // New location?
  if( strcmp(device->location,ssdp->location) ) {
    debug ( "_ickDeviceUpdate (%s): updating location (\"%s\"->\"%s\".",
        ssdp->usn, device->location, ssdp->location );
    char *ptr = strdup( ssdp->location );
    if( !ptr ) {
      logerr( "_ickDeviceUpdate: out of memory" );
      _ickDiscoveryDeviceListUnlock( dh );
      pthread_mutex_unlock( &_sendListMutex );
      return -1;
    }
    Sfree( device->location );
    device->location = ptr;
  }
*/

  // New service: Execute callbacks registered with discovery handler
  if( (stype&~device->services) ) {
    device->services |= stype;
    _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_NEW, stype );
  }

  // Release all locks and return code 0 (no device added)
  _ickLibDeviceListUnlock( ictx );
  _ickTimerListUnlock( ictx );
  return 0;
}


/*=========================================================================*\
  Remove a device or service from a discovery context
    ssdp - a parsed SSDP packet
    dh   - the related discovery handler
  return values:
   -1 : error
    0 : no device removed
    2 : device was removed
\*=========================================================================*/
static int _ickDeviceRemove( ickP2pContext_t *ictx, const ickSsdp_t *ssdp )
{
  ickDevice_t         *device;
  ickWGetContext_t    *wget, *wgetNext;
  ickP2pServicetype_t  stype;

/*------------------------------------------------------------------------*\
    Need USN header field
\*------------------------------------------------------------------------*/
  if( !ssdp->usn ) {
    logwarn( "_ickDeviceRemove: SSDP request lacks USN header." );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Ignore non ickstream services...
\*------------------------------------------------------------------------*/
  stype = _ickDescrFindServiceByUsn( ssdp->usn );
  if( stype==ICKP2P_SERVICE_NONE )
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
    loginfo( "_ickDeviceRemove (%s): no instance found.", ssdp->usn );
    _ickLibDeviceListUnlock( ictx );
    _ickTimerListUnlock( ictx );
    return 0;
  }
  _ickDeviceLock( device );

/*------------------------------------------------------------------------*\
    A service is terminated
\*------------------------------------------------------------------------*/
  if( (device->services&stype) || stype!=ICKP2P_SERVICE_GENERIC ) {
    device->services &= ~stype;
    _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_REMOVED, stype );
  }

/*------------------------------------------------------------------------*\
    Is the root device disappearing?
\*------------------------------------------------------------------------*/
  if( stype==ICKP2P_SERVICE_GENERIC ) {

    // Execute callback with all registered services
    _ickLibExecDiscoveryCallback( ictx, device, ICKP2P_REMOVED, device->services );

    // Unlink from device list
    _ickLibDeviceRemove( ictx, device );

    // Remove expiration handler for this device
    _ickTimerDeleteAll( ictx, _ickDeviceExpireTimerCb, device, 0 );

    // Remove heartbeat handler for this device
    _ickTimerDeleteAll( ictx, _ickDeviceHeartbeatTimerCb, device, 0 );

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
  }
  else
    _ickDeviceUnlock( device );

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
    Schedule initial advertisements
\*------------------------------------------------------------------------*/
  irc = _ssdpSendInitialDiscoveryMsg( ictx, NULL );
  if( irc ) {
    _ickTimerListUnlock( ictx );
    return irc;
  }

/*------------------------------------------------------------------------*\
    Request advertisements from all reachable upnp devices
\*------------------------------------------------------------------------*/
  irc = _ssdpSendDiscoveryMsg( ictx, NULL, SSDPMSGTYPE_MSEARCH, SSDPMSGLEVEL_GLOBAL,
                               ICKP2P_SERVICE_NONE, ICKSSDP_REPEATS );
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
  _ssdpSendDiscoveryMsg( ictx, NULL, SSDPMSGTYPE_BYEBYE, SSDPMSGLEVEL_ROOT,
                         ICKP2P_SERVICE_NONE, 0 );
  _ssdpSendDiscoveryMsg( ictx, NULL, SSDPMSGTYPE_BYEBYE, SSDPMSGLEVEL_UUID,
                         ICKP2P_SERVICE_NONE, 0 );
  _ssdpSendDiscoveryMsg( ictx, NULL, SSDPMSGTYPE_BYEBYE, SSDPMSGLEVEL_SERVICE,
                         ICKP2P_SERVICE_GENERIC, 0 );
  if( ictx->ickServices&ICKP2P_SERVICE_PLAYER )
    _ssdpSendDiscoveryMsg( ictx, NULL, SSDPMSGTYPE_BYEBYE, SSDPMSGLEVEL_SERVICE,
                           ICKP2P_SERVICE_PLAYER, 0 );
  if( ictx->ickServices&ICKP2P_SERVICE_CONTROLLER )
    _ssdpSendDiscoveryMsg( ictx, NULL, SSDPMSGTYPE_BYEBYE, SSDPMSGLEVEL_SERVICE,
                           ICKP2P_SERVICE_CONTROLLER, 0 );
  if( ictx->ickServices&ICKP2P_SERVICE_SERVER_GENERIC )
    _ssdpSendDiscoveryMsg( ictx, NULL, SSDPMSGTYPE_BYEBYE, SSDPMSGLEVEL_SERVICE,
                           ICKP2P_SERVICE_SERVER_GENERIC, 0 );
  if( ictx->ickServices&ICKP2P_SERVICE_DEBUG )
    _ssdpSendDiscoveryMsg( ictx, NULL, SSDPMSGTYPE_BYEBYE, SSDPMSGLEVEL_SERVICE,
                           ICKP2P_SERVICE_DEBUG, 0 );

/*------------------------------------------------------------------------*\
    Terminate all known device
\*------------------------------------------------------------------------*/
  _ickLibDeviceListLock( ictx );
  while( ictx->deviceList ) {
    ickDevice_t *device = ictx->deviceList;
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
  Services were registerd or terminated
\*=========================================================================*/
ickErrcode_t _ickSsdpAnnounceServices( ickP2pContext_t *ictx, ickP2pServicetype_t services, ickSsdpMsgType_t mtype )
{

/*------------------------------------------------------------------------*\
    Be defensive
\*------------------------------------------------------------------------*/
  if( mtype!=SSDPMSGTYPE_ALIVE && SSDPMSGTYPE_BYEBYE ) {
    logerr( "_ickSsdpAnnounceServices: invalid message type (%d)", mtype );
    return ICKERR_INVALID;
  }

/*------------------------------------------------------------------------*\
    Lock timer list for queuing messages
\*------------------------------------------------------------------------*/
  _ickTimerListLock( ictx );

/*------------------------------------------------------------------------*\
    Announce all new or terminated services
\*------------------------------------------------------------------------*/
  if( services&ICKP2P_SERVICE_PLAYER )
    _ssdpSendDiscoveryMsg( ictx, NULL, mtype, SSDPMSGLEVEL_SERVICE,
                           ICKP2P_SERVICE_PLAYER, ICKSSDP_REPEATS );
  if( services&ICKP2P_SERVICE_CONTROLLER )
    _ssdpSendDiscoveryMsg( ictx, NULL, mtype, SSDPMSGLEVEL_SERVICE,
                           ICKP2P_SERVICE_CONTROLLER, ICKSSDP_REPEATS );
  if( services&ICKP2P_SERVICE_SERVER_GENERIC )
    _ssdpSendDiscoveryMsg( ictx, NULL, mtype, SSDPMSGLEVEL_SERVICE,
                           ICKP2P_SERVICE_SERVER_GENERIC, ICKSSDP_REPEATS );
  if( services&ICKP2P_SERVICE_DEBUG )
    _ssdpSendDiscoveryMsg( ictx, NULL, mtype, SSDPMSGLEVEL_SERVICE,
                           ICKP2P_SERVICE_DEBUG, ICKSSDP_REPEATS );

/*------------------------------------------------------------------------*\
    Unlock timer list, that's all
\*------------------------------------------------------------------------*/
  _ickTimerListUnlock( ictx );
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Execute frequent announcements of offered services
    timer list is already locked for callbacks
\*=========================================================================*/
static void _ickSsdpAnnounceCb( const ickTimer_t *timer, void *data, int tag )
{
  ickP2pContext_t *ictx = data;
  ickErrcode_t     irc;

  debug( "_ickSsdpAnnounceCb: sending periodic announcements..." );

/*------------------------------------------------------------------------*\
    Schedule initial advertisements
\*------------------------------------------------------------------------*/
  irc = _ssdpSendInitialDiscoveryMsg( ictx, NULL );
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
  irc = _ssdpSendDiscoveryMsg( ictx, NULL, SSDPMSGTYPE_MSEARCH, SSDPMSGLEVEL_SERVICE,
                               ICKP2P_SERVICE_GENERIC, 1 /*ICKSSDP_REPEATS*/ );
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
    ssdp - the ssdp packet
    dh   - the discovery handler to be used
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
    Lock timer list for queuing messages
\*------------------------------------------------------------------------*/
  _ickTimerListLock( ictx );

/*------------------------------------------------------------------------*\
    Search for all devices and services
\*------------------------------------------------------------------------*/
  if( !strcmp(ssdp->st,"ssdp:all") ) {
    _ssdpSendInitialDiscoveryMsg( ictx, &ssdp->addr );
  }

/*------------------------------------------------------------------------*\
    Search for root device
\*------------------------------------------------------------------------*/
  else if( strcmp(ssdp->st,"upnp:rootdevice") ) {
    _ssdpSendDiscoveryMsg( ictx, &ssdp->addr, SSDPMSGTYPE_MRESPONSE, SSDPMSGLEVEL_ROOT,
                          ICKP2P_SERVICE_NONE, ICKSSDP_REPEATS );
  }

/*------------------------------------------------------------------------*\
    Search for a device with specific UUID
\*------------------------------------------------------------------------*/
  else if( strncmp(ssdp->st,"uuid:",5) ) {
    if( !strcasecmp(ssdp->st+5,ictx->deviceUuid) ) {
      _ssdpSendDiscoveryMsg( ictx, &ssdp->addr, SSDPMSGTYPE_MRESPONSE, SSDPMSGLEVEL_UUID,
                             ICKP2P_SERVICE_NONE, ICKSSDP_REPEATS );
    }
  }

/*------------------------------------------------------------------------*\
    Search for a specific device or service
\*------------------------------------------------------------------------*/
  else {

    if( !_ssdpVercmp(ssdp->st,ICKDEVICE_TYPESTR_ROOT) )
      _ssdpSendDiscoveryMsg( ictx, &ssdp->addr, SSDPMSGTYPE_MRESPONSE, SSDPMSGLEVEL_SERVICE,
                             ICKP2P_SERVICE_GENERIC, ICKSSDP_REPEATS );

#ifdef ICKP2P_DYNAMICSERVICES
    if( (ictx->ickServices&ICKP2P_SERVICE_PLAYER) && !_ssdpVercmp(ssdp->st,ICKSERVICE_TYPESTR_PLAYER) )
      _ssdpSendDiscoveryMsg( ictx, &ssdp->addr, SSDPMSGTYPE_MRESPONSE, SSDPMSGLEVEL_SERVICE,
                             ICKP2P_SERVICE_PLAYER, ICKSSDP_REPEATS );
    if( (ictx->ickServices&ICKP2P_SERVICE_CONTROLLER) && !_ssdpVercmp(ssdp->st,ICKSERVICE_TYPESTR_CONTROLLER) )
      _ssdpSendDiscoveryMsg( ictx, &ssdp->addr, SSDPMSGTYPE_MRESPONSE, SSDPMSGLEVEL_SERVICE,
                             ICKP2P_SERVICE_CONTROLLER, ICKSSDP_REPEATS );
    if( (ictx->ickServices&ICKP2P_SERVICE_SERVER_GENERIC) && !_ssdpVercmp(ssdp->st,ICKSERVICE_TYPESTR_SERVER) )
      _ssdpSendDiscoveryMsg( ictx, &ssdp->addr, SSDPMSGTYPE_MRESPONSE, SSDPMSGLEVEL_SERVICE,
                             ICKP2P_SERVICE_SERVER_GENERIC, ICKSSDP_REPEATS );
    if( (ictx->ickServices&ICKP2P_SERVICE_DEBUG) && !_ssdpVercmp(ssdp->st,ICKSERVICE_TYPESTR_DEBUG) )
      _ssdpSendDiscoveryMsg( ictx, &ssdp->addr, SSDPMSGTYPE_MRESPONSE, SSDPMSGLEVEL_SERVICE,
                             ICKP2P_SERVICE_DEBUG, ICKSSDP_REPEATS );
#endif
  }

/*------------------------------------------------------------------------*\
    Unlock timer list, that's all
\*------------------------------------------------------------------------*/
  _ickTimerListUnlock( ictx );
  return retcode;
}


/*=========================================================================*\
  Send initial set of advertisements
    (also used for M-Search answers to "ssdp:all")
    See "UPnP Device Architecture 1.1": chapter 1.2.2 and 1.3.3
    dh      - the discovery handler (interface) to use
    addr    - NULL for mcast, else unicast target (for M-Search responses)
    Caller should lock timer list (which is already the case in timer callbacks)
\*=========================================================================*/
static ickErrcode_t _ssdpSendInitialDiscoveryMsg( ickP2pContext_t *ictx,
                                                  const struct sockaddr *addr )
{
  ickErrcode_t irc;

/*------------------------------------------------------------------------*\
    Advertise UPNP root
\*------------------------------------------------------------------------*/
  irc = _ssdpSendDiscoveryMsg( ictx, addr, SSDPMSGTYPE_MRESPONSE, SSDPMSGLEVEL_ROOT,
                               ICKP2P_SERVICE_NONE, ICKSSDP_REPEATS );
  if( irc )
    return irc;

/*------------------------------------------------------------------------*\
    Advertise UUID
\*------------------------------------------------------------------------*/
  irc = _ssdpSendDiscoveryMsg( ictx, addr, SSDPMSGTYPE_MRESPONSE, SSDPMSGLEVEL_UUID,
                               ICKP2P_SERVICE_NONE, ICKSSDP_REPEATS );
  if( irc )
    return irc;

/*------------------------------------------------------------------------*\
    Advertise ickstream root device
\*------------------------------------------------------------------------*/
  irc = _ssdpSendDiscoveryMsg( ictx, addr, SSDPMSGTYPE_MRESPONSE, SSDPMSGLEVEL_SERVICE,
                               ICKP2P_SERVICE_GENERIC, ICKSSDP_REPEATS );
  if( irc )
    return irc;

/*------------------------------------------------------------------------*\
    Advertise ickstream services
\*------------------------------------------------------------------------*/
  if( ictx->ickServices&ICKP2P_SERVICE_PLAYER ) {
    irc = _ssdpSendDiscoveryMsg( ictx, addr, SSDPMSGTYPE_MRESPONSE, SSDPMSGLEVEL_SERVICE,
                                ICKP2P_SERVICE_PLAYER, ICKSSDP_REPEATS );
    if( irc )
      return irc;
  }

  if( ictx->ickServices&ICKP2P_SERVICE_CONTROLLER ) {
    irc = _ssdpSendDiscoveryMsg( ictx, addr, SSDPMSGTYPE_MRESPONSE, SSDPMSGLEVEL_SERVICE,
                                 ICKP2P_SERVICE_CONTROLLER, ICKSSDP_REPEATS );
    if( irc )
      return irc;
  }

  if( ictx->ickServices&ICKP2P_SERVICE_SERVER_GENERIC ) {
    irc = _ssdpSendDiscoveryMsg( ictx, addr, SSDPMSGTYPE_MRESPONSE, SSDPMSGLEVEL_SERVICE,
                           ICKP2P_SERVICE_SERVER_GENERIC, ICKSSDP_REPEATS );
    if( irc )
      return irc;
  }

  if( ictx->ickServices&ICKP2P_SERVICE_DEBUG ) {
    irc = _ssdpSendDiscoveryMsg( ictx, addr, SSDPMSGTYPE_MRESPONSE, SSDPMSGLEVEL_SERVICE,
                                 ICKP2P_SERVICE_DEBUG, ICKSSDP_REPEATS );
    if( irc )
      return irc;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Queue an outgoing discovery message
    ictx    - the ickstream context to use
    addr    - NULL for mcast, else unicast target (for M-Search responses)
    type    - the message type (alive,byebye,M-Search,Response)
    level   - the message type (global,root,uuid,service)
    service - the ickstream service to announce (for type=SSDPMSG_SERVICE)
              with ICKP2P_SERVICE_GENERIC the ickstream root device is announced
    repeat  - number of repetitions (randomly distributed in time)
              if <=0 one message is sent immediately
    returns -1 on error, 0 on success
    Caller should lock timer list (which is already the case in timer callbacks)
\*=========================================================================*/
static ickErrcode_t _ssdpSendDiscoveryMsg( ickP2pContext_t *ictx,
                                           const struct sockaddr *addr,
                                           ickSsdpMsgType_t type,
                                           ssdpMsgLevel_t level,
                                           ickP2pServicetype_t service,
                                           int repeat )
{
  upnp_notification_t *note;
  struct sockaddr_in   sockname;
  socklen_t            addr_len = sizeof( struct sockaddr_in );
  char                 addrstr[64];
  time_t               now;
  ickErrcode_t         irc = ICKERR_SUCCESS;
  long                 delay;
  char                 timestr[80];
  char                *nst = NULL;
  char                *usn = NULL;
  char                *sstr = NULL;
  char                *message = NULL;

/*------------------------------------------------------------------------*\
    Use multicast?
\*------------------------------------------------------------------------*/
  if( !addr ) {
    memset( &sockname, 0, sizeof(struct sockaddr_in) );
    sockname.sin_family      = AF_INET;
    sockname.sin_addr.s_addr = inet_addr( ICKSSDP_MCASTADDR );
    sockname.sin_port        = htons( ICKSSDP_MCASTPORT );
    addr = (const struct sockaddr*)&sockname;
  }
  snprintf( addrstr, sizeof(addrstr), "%s:%d",
         inet_ntoa(((const struct sockaddr_in *)addr)->sin_addr),
         ntohs(((const struct sockaddr_in *)addr)->sin_port));

/*------------------------------------------------------------------------*\
    Create NT/ST and USN according to level and service
    See tables 1-1 and 1-3 in "UPnP Device Architecture 1.1"
\*------------------------------------------------------------------------*/
  switch( level ) {
    case SSDPMSGLEVEL_GLOBAL:
      // Only possible for M-Searches
      if( type!=SSDPMSGTYPE_MSEARCH ) {
        logerr( "_ssdpSendDiscoveryMsg: bad message type (%d) for GLOBAL level.", type );
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

    case SSDPMSGLEVEL_SERVICE:
#ifdef ICKP2P_DYNAMICSERVICES
      switch( service ) {
        case ICKP2P_SERVICE_GENERIC:
          sstr = ICKDEVICE_STRING_ROOT;
          nst  = ICKDEVICE_TYPESTR_ROOT;
          break;
        case ICKP2P_SERVICE_PLAYER:
          sstr = ICKSERVICE_STRING_PLAYER;
          nst  = ICKSERVICE_TYPESTR_PLAYER;
          break;
        case ICKP2P_SERVICE_CONTROLLER:
          sstr = ICKSERVICE_STRING_SERVER;
          nst  = ICKSERVICE_TYPESTR_SERVER;
          break;
        case ICKP2P_SERVICE_SERVER_GENERIC:
          sstr = ICKSERVICE_STRING_CONTROLLER;
          nst  = ICKSERVICE_TYPESTR_CONTROLLER;
          break;
        case ICKP2P_SERVICE_DEBUG:
          sstr = ICKSERVICE_STRING_DEBUG;
          nst  = ICKSERVICE_TYPESTR_DEBUG;
          break;
        default:
          logerr( "_ssdpSendDiscoveryMsg: bad service type (%d)", service );
          return ICKERR_INVALID;
      }
#else
      sstr = ICKDEVICE_STRING_ROOT;
      nst  = ICKDEVICE_TYPESTR_ROOT;
#endif
      asprintf( &usn, "uuid:%s::%s", ictx->deviceUuid, nst );
      nst = strdup( nst );
      break;

    default:
      logerr( "_ssdpSendDiscoveryMsg: bad message level (%d)", level );
      return ICKERR_INVALID;
  }
  if( !nst || !usn ) {
    Sfree( nst );
    Sfree( usn );
    logerr( "_ssdpSendDiscoveryMsg: out of memory" );
    return ICKERR_NOMEM;
  }

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
        "LOCATION: %s/%s.xml\r\n"
        "NT: %s\r\n"
        "NTS: ssdp:alive\r\n"
        "SERVER: %s\r\n"
        "USN: %s\r\n"
        "BOOTID.UPNP.ORG: %ld\r\n"
        "CONFIGID.UPNP.ORG: %ld\r\n"
        "\r\n",
        ICKSSDP_MCASTADDR, ictx->upnpPort, ictx->lifetime,
        ictx->locationRoot, sstr,
        nst, ictx->osName, usn,
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
        ICKSSDP_MCASTADDR, ictx->upnpPort, nst, usn,
        ictx->upnpBootId, ictx->upnpConfigId );
      break;

      // See "UPnP Device Architecture 1.1": chapter 1.3.2
    case SSDPMSGTYPE_MSEARCH:
        asprintf( &message,
          "M-SEARCH * HTTP/1.1\r\n"
          "HOST: %s:%d\r\n"
          "MAN: \"ssdp:discover\"\r\n"
          "MX: %d\r\n"
          "ST: %s\r\n"
          "USER-AGENT: %s\r\n"
          "\r\n",
          ICKSSDP_MCASTADDR, ictx->upnpPort, ICKSSDP_MSEARCH_MX,
          nst, ictx->osName );
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
        "LOCATION: %s/%s.xml\r\n"
        "SERVER: %s\r\n"
        "ST: %s\r\n"
        "USN: %s\r\n"
        "BOOTID.UPNP.ORG: %ld\r\n"
        "CONFIGID.UPNP.ORG: %ld\r\n"
        "\r\n",
        ictx->lifetime, timestr,
        ictx->locationRoot, sstr,
        ictx->osName, nst, usn,
        ictx->upnpBootId, ictx->upnpConfigId );
      break;

    default:
      Sfree( nst );
      Sfree( usn );
      logerr( "_ssdpSendDiscoveryMsg: bad message type (%d)", type );
      return ICKERR_INVALID;
  }
  Sfree( nst );
  Sfree( usn );
  if( !message ) {
    logerr( "_ssdpSendDiscoveryMsg: out of memory" );
    return ICKERR_NOMEM;
  }

/*------------------------------------------------------------------------*\
    Immediate submission?
\*------------------------------------------------------------------------*/
  if( repeat<=0 ) {
    ssize_t n;
    size_t  len = strlen( message );
    debug( "_ssdpSendDiscoveryMsg: immediately sending %ld bytes to %s: \"%s\"",
           (long)len, addrstr, message );

    // Try to send packet via discovery socket
    n = sendto( ictx->upnpSocket, message, len, 0, addr, addr_len );

    // Any error ?
    if( n<0 ) {
      logerr( "_ssdpSendDiscoveryMsg: could not send to %s (%s)",
              addrstr, strerror(errno) );
      irc = ICKERR_GENERIC;
    }
    else if( n<len ) {
      logerr( "_ssdpSendDiscoveryMsg: could not send all data to %s (%d of %d)",
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
  debug( "_ssdpSendDiscoveryMsg: enqueing %d transmissions of %ld bytes to %s: \"%s\"",
         repeat, (long)strlen(message), addrstr, message );

/*------------------------------------------------------------------------*\
    Create and init notification
\*------------------------------------------------------------------------*/
  note = calloc( 1, sizeof(upnp_notification_t) );
  if( !note ) {
    Sfree( message );
    logerr( "_ssdpSendDiscoveryMsg: out of memory" );
    return ICKERR_NOMEM;
  }
  note->message  = message;
  note->socket   = ictx->upnpSocket;
  note->refCntr  = repeat>0 ? repeat : 1;
  note->socknamelen = addr_len;
  memcpy( &note->sockname, addr, addr_len );

/*------------------------------------------------------------------------*\
    Queue instances, randomly delay transmissions
\*------------------------------------------------------------------------*/
  delay = 0;
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
  n = sendto( note->socket, note->message, len, 0, &note->sockname, note->socknamelen );
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
  else if( ptr )
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
  else if( ptr )
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

