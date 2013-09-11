/*$*********************************************************************\

Source File     : ickIpTools.c

Description     : IP related tool functions

Comments        : -

Called by       : Internal functions

Calls           : standard posix network functions

Date            : 15.08.2013

Updates         : -

Author          : JS, //MAF

Remarks         : -

*************************************************************************
 * Copyright (c) 2013, ickStream GmbH
 * All rights reserved.
\************************************************************************/

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ickP2p.h"
#include "ickP2pInternal.h"
#include "logutils.h"
#include "ickIpTools.h"


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
  Bind a socket to an addr:port
    addr - IP address (network byte order)
    port - port to bind to or 0 for free port selection
    return 0 on success or error code (errno)
\*=========================================================================*/
int _ickIpBind( int socket, in_addr_t addr, int port )
{
  struct sockaddr_in  sockname;
  socklen_t           sockname_len = sizeof(struct sockaddr_in);
  int                 rc;

#ifdef ICK_DEBUG
  char _buf[64];
  inet_ntop( AF_INET, &addr, _buf, sizeof(_buf) );
  debug( "_ickIpBind (%d): %s:%d", socket, _buf, port );
#endif

/*------------------------------------------------------------------------*\
    Fill sockaddr structure
\*------------------------------------------------------------------------*/
  memset( &sockname, 0, sockname_len );
  sockname.sin_family      = AF_INET;
  sockname.sin_addr.s_addr = addr;
  sockname.sin_port        = htons( port );

/*------------------------------------------------------------------------*\
    Bind socket to addr:port
\*------------------------------------------------------------------------*/
  rc = bind( socket, (struct sockaddr *)&sockname, sockname_len );

/*------------------------------------------------------------------------*\
    Return error code
\*------------------------------------------------------------------------*/
  return rc<0 ? errno : 0;
}


/*=========================================================================*\
  Add a socket to a multicast group
    ifaddr - interface
    maddr  - multicast group
    ifaddr and maddr are in network byte order
    return 0 on success or error code (errno)
\*=========================================================================*/
int _ickIpAddMcast( int socket, in_addr_t ifaddr, in_addr_t maddr )
{
  struct ip_mreq  mgroup;
  int             rc;

#ifdef ICK_DEBUG
  char _buf1[64], _buf2[64];
  inet_ntop( AF_INET, &ifaddr, _buf1, sizeof(_buf1) );
  inet_ntop( AF_INET, &maddr,  _buf2, sizeof(_buf2) );
  debug( "_ickIpAddMcast (%d): in: %s mc: %s", socket, _buf1, _buf2 );
#endif

/*------------------------------------------------------------------------*\
    Construct request
\*------------------------------------------------------------------------*/
  mgroup.imr_multiaddr.s_addr = maddr;
  mgroup.imr_interface.s_addr = ifaddr;

/*------------------------------------------------------------------------*\
    Try to set socket option
\*------------------------------------------------------------------------*/
  rc = setsockopt( socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mgroup, sizeof(mgroup) );

/*------------------------------------------------------------------------*\
    Return error code
\*------------------------------------------------------------------------*/
  return rc<0 ? errno : 0;
}


/*=========================================================================*\
  Get address and network mask of an interface
    ifname  - be interface name or address
    addr    - pointer to addr of interface (might be NULL)
    netmask - pointer to network mask of interface (might be NULL)
    name    - pointer to name of interface, will be an allocated string (might be NULL)
    *addr and *netmask are in network byte order
    return 0 on success or error code
\*=========================================================================*/
ickErrcode_t _ickIpGetIfAddr( const char *ifname, in_addr_t *addr, in_addr_t *netmask, char **name )
{
  char          *buffer;
  struct ifconf  ifc;
  size_t         pos;
  in_addr_t      ifaddr;
  int            sd;
  int            rc;
  ickErrcode_t   irc = ICKERR_NOINTERFACE;

/*------------------------------------------------------------------------*\
    Create virtual socket
\*------------------------------------------------------------------------*/
  sd = socket( PF_INET, SOCK_DGRAM, 0 );
  if( sd<0 ) {
    logerr( "_ickIpGetIfAddr: could not get socket (%s)", strerror(errno) );
    return ICKERR_NOSOCKET;
  }

/*------------------------------------------------------------------------*\
    Is this already a valid IP address?
\*------------------------------------------------------------------------*/
  ifaddr = inet_addr( ifname );

/*------------------------------------------------------------------------*\
    Allocate buffer
\*------------------------------------------------------------------------*/
  buffer = calloc( 1, ICK_IFRBUFFERSIZE );

/*------------------------------------------------------------------------*\
    Get all known interfaces
\*------------------------------------------------------------------------*/
  ifc.ifc_len = ICK_IFRBUFFERSIZE;
  ifc.ifc_req = (struct ifreq *)buffer;
  if( ioctl(sd,SIOCGIFCONF,&ifc)<0 ) {
    close( sd );
    logerr( "_ickIpGetIfAddr: ioctl(SIOCGIFCONF) failed (%s)", strerror(errno) );
    return ICKERR_NOSOCKET;
  }

/*------------------------------------------------------------------------*\
    Loop over interface list
\*------------------------------------------------------------------------*/
  size_t len;
  for( pos=0; pos<ifc.ifc_len; pos+=len ) {

    // Get current interface descriptor
    struct ifreq *ifr= (struct ifreq *)(buffer + pos );
    debug( "_ickIpGetIfAddr: testing interface \"%s\"", ifr->ifr_name );

    // Get actual size of interface descriptor
#ifdef linux
    len = sizeof( struct ifreq );
#else
    len = IFNAMSIZ + ifr->ifr_addr.sa_len;
#endif

    // We are looking for an internet interface
    if( ifr->ifr_addr.sa_family!=AF_INET )
      continue;

    // If ifname is no address the interface name must match
    if( ifaddr==INADDR_NONE && strcasecmp(ifname,ifr->ifr_name) )
      continue;

    // Get interface address
    rc = ioctl( sd, SIOCGIFADDR, ifr );
    if( rc<0 ) {
      logerr( "_ickIpGetIfAddr: ioctl(SIOCGIFADDR) failed (%s)", strerror(errno) );
      irc = ICKERR_NOINTERFACE;
      break;
    }

    // If ifname is an address it must match
    if( ifaddr!=INADDR_NONE && ifaddr!=((struct sockaddr_in *)&ifr->ifr_addr)->sin_addr.s_addr )
      continue;

    // Store address
    if( addr )
      *addr = ((struct sockaddr_in *)&ifr->ifr_addr)->sin_addr.s_addr;

    // Get and store interface network mask
    if( netmask ) {
      rc = ioctl( sd, SIOCGIFNETMASK, ifr );
      if( rc<0 ) {
        logerr( "_ickIpGetIfAddr: ioctl(SIOCGIFNETMASK) failed (%s)", strerror(errno) );
        irc = ICKERR_NOINTERFACE;
        break;
      }
      *netmask = ((struct sockaddr_in *)(&ifr->ifr_addr))->sin_addr.s_addr;
    }

    // Duplicate real interface name
    if( name ) {
      *name = strdup( ifr->ifr_name );
      if( !*name ) {
        logerr( "_ickIpGetIfAddr: out of memory" );
        irc = ICKERR_NOMEM;
        break;
      }
    }

    // Found it!
    debug( "_ickIpGetIfAddr (%s): found interface \"%s\"", ifname, ifr->ifr_name );
    irc = ICKERR_SUCCESS;
    break;
  }

/*------------------------------------------------------------------------*\
    Clean up and return result
\*------------------------------------------------------------------------*/
  Sfree( buffer );
  close( sd );
  return irc;
}


/*=========================================================================*\
  Find a free TCP/IP port
    If ifname is NULL, INADDR_ANY is used (all interfaces)
    This will return a port out of the range 49,152 to 65,535
    The use of this function is deprecated, use bind with port 0 instead.
    Only used for libwebsocket, since that does not allow port 0.
    return port on success or -1 on error
\*=========================================================================*/
int _ickIpGetFreePort( const char *ifname )
{
  int                s;
  int                port;
  struct sockaddr_in sockaddr;

/*------------------------------------------------------------------------*\
    Create virtual help socket
\*------------------------------------------------------------------------*/
  s = socket( AF_INET, SOCK_STREAM, 0 );
  if( s<0 ) {
    logerr( "_ickIpGetFreePort: could not get socket (%s)", strerror(errno) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Set address to interface, port 0
\*------------------------------------------------------------------------*/
  memset( &sockaddr, 0, sizeof(sockaddr) );
  sockaddr.sin_family      = AF_INET;
  sockaddr.sin_port        = 0;
  if( !ifname )
    sockaddr.sin_addr.s_addr = INADDR_ANY;
  else if( _ickIpGetIfAddr(ifname,&sockaddr.sin_addr.s_addr,NULL,NULL) ) {
    logerr( "_ickIpGetFreePort: no address for interface \"%s\".", ifname?ifname:"(nil)" );
    close( s );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Try to bind the port
\*------------------------------------------------------------------------*/
  if( bind(s,(struct sockaddr *)&sockaddr,sizeof(sockaddr))<0 ) {
    logerr( "_ickIpGetFreePort: could not bind socket (%s)", strerror(errno) );
    close( s );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Get port assigned by OS
\*------------------------------------------------------------------------*/
  port = _ickIpGetSocketPort( s );

/*------------------------------------------------------------------------*\
    Don't need the virtual help socket any more
\*------------------------------------------------------------------------*/
  // int opt = 1;
  // setsockopt( s, SOL_SOCKET, SO_REUSEADDR, (void *)&opt, sizeof(opt) );
  if( close(s)<0 ) {
    logerr( "_ickIpGetFreePort: could not close socket (%s)", strerror(errno) );
    close( s );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Return port that got assigned to the probe socket
\*------------------------------------------------------------------------*/
  debug( "_ickIpGetFreePort (%s): found port %d", ifname, port );
  return port;
}


/*=========================================================================*\
  Get port a socket is bound to
\*=========================================================================*/
int _ickIpGetSocketPort( int sd )
{
  struct sockaddr_in sockaddr;
  socklen_t          sockaddr_len = sizeof(struct sockaddr_in);
  int                port;

/*------------------------------------------------------------------------*\
    Get port assigned by OS
\*------------------------------------------------------------------------*/
  if( getsockname(sd,(struct sockaddr *)&sockaddr,&sockaddr_len) ) {
    logerr( "_ickIpGetSocketPort: could not get socket name (%s)", strerror(errno) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Return port that got assigned to the probe socket
\*------------------------------------------------------------------------*/
  port = ntohs( sockaddr.sin_port );
  debug( "_ickIpGetSocketPort (%d): %d", sd, port );
  return port;
}

/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
