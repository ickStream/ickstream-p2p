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
    return 0 on success or error code (errno)
\*=========================================================================*/
int _ickIpBind( int socket, in_addr_t addr, int port )
{
  struct sockaddr_in  sockname;
  socklen_t           sockname_len = sizeof(struct sockaddr_in);
  int                 rc;

/*------------------------------------------------------------------------*\
    Fill sockaddr structure
\*------------------------------------------------------------------------*/
  memset( &sockname, 0, sockname_len );
  sockname.sin_family      = AF_INET;
  sockname.sin_addr.s_addr = htonl( addr );
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
    return 0 on success or error code (errno)
\*=========================================================================*/
int _ickIpAddMcast( int socket, in_addr_t ifaddr, in_addr_t maddr )
{
  struct ip_mreq  mgroup;
  int             rc;

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
  Get address of an interface
    return 0 on success or error code (errno)
\*=========================================================================*/
in_addr_t _ickIpGetIfAddr( const char *ifname )
{
  in_addr_t      inaddr;
  struct ifreq   ifr;
  int            ifrlen, s;
  int            rc;

/*------------------------------------------------------------------------*\
    Is this already a valid IP address?
\*------------------------------------------------------------------------*/
  inaddr = inet_addr( ifname );
  if( inaddr!=INADDR_NONE )
    return inaddr;

/*------------------------------------------------------------------------*\
    Get IP address via a virtual socket
\*------------------------------------------------------------------------*/
  s = socket( PF_INET, SOCK_DGRAM, 0 );
  if( s<0 ) {
    logerr( "_ickIpGetIfAddr: could not get socket (%s)", strerror(errno) );
    return INADDR_NONE;
  }

  memset( &ifr, 0, sizeof(struct ifreq) );
  strncpy( ifr.ifr_name, ifname, IFNAMSIZ );

  rc = ioctl( s, SIOCGIFADDR, &ifr, &ifrlen );
  if( rc<0 ) {
    logerr( "_ickIpGetIfAddr: ioctl(SIOCGIFADDR) failes (%s)", strerror(errno) );
    close(s);
    return INADDR_NONE;
  }
  close( s );

  return ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr;
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
  if( ifname )
    sockaddr.sin_addr.s_addr = _ickIpGetIfAddr( ifname );
  else
    sockaddr.sin_addr.s_addr = INADDR_ANY;
  if( sockaddr.sin_addr.s_addr==INADDR_NONE ) {
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
int _ickIpGetSocketPort( int s )
{
  struct sockaddr_in sockaddr;
  socklen_t          sockaddr_len = sizeof(struct sockaddr_in);

/*------------------------------------------------------------------------*\
    Get port assigned by OS
\*------------------------------------------------------------------------*/
  if( getsockname(s,(struct sockaddr *)&sockaddr,&sockaddr_len) ) {
    logerr( "_ickIpGetSocketPort: could not get socket name (%s)", strerror(errno) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Return port that got assigned to the probe socket
\*------------------------------------------------------------------------*/
  return ntohs( sockaddr.sin_port );
}

/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/
