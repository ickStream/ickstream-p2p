/*$*********************************************************************\

Source File     : ssdplog.c

Description     : log ssdp mcast activity

Comments        : -

Called by       : OS

Calls           : network functions

Date            : 07.09.2013

Updates         : -

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
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "config.h"


/*=========================================================================*\
  Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Private definitions and symbols
\*=========================================================================*/
#define VERSION     "0.2"
#define IFNAME      "wlan0"
#define SSDPPORT    "1900"
#define SSDPADDR    "239.255.255.250"
#define BUFFERSIZE  2048

struct _addrlist {
  struct _addrlist *next;
  in_addr_t         addr;
};

static volatile int stop_signal;


/*=========================================================================*\
  Private prototypes
\*=========================================================================*/
static void sigHandler( int sig, siginfo_t *siginfo, void *context );


/*=========================================================================*\
  main
\*=========================================================================*/
int main( int argc, char *argv[] )
{
  int                  help_flag   = 0;
  int                  vers_flag   = 0;
  const char          *cfg_fname   = NULL;
  const char          *ifname      = IFNAME;
  const char          *port_arg    = SSDPPORT;
  const char          *addr_arg    = SSDPADDR;
  char                *ignore_arg  = NULL;
  in_addr_t            ifaddr;
  char                *buffer;
  int                  port;
  struct _addrlist    *ignore_list = NULL;
  int                  sd;
  int                  rc;
  int                  opt;
  struct sockaddr_in   sockname;
  socklen_t            sockname_len = sizeof(struct sockaddr_in);
  struct ip_mreq       mgroup;
  int                  cntr = 0;
  int                  igncntr = 0;

/*-------------------------------------------------------------------------*\
        Set up command line switches
        (leading * flags availability in config file)
\*-------------------------------------------------------------------------*/
  addarg( "help",      "-?",  &help_flag,   NULL,            "Show this help message and quit" );
  addarg( "version",   "-V",  &vers_flag,   NULL,            "Show version and quit" );
  addarg( "config",    "-c",  &cfg_fname,   "filename",      "Set name of configuration file" );
  addarg( "*idev",     "-i",  &ifname,      "interface",     "Network interface" );
  addarg( "*port",     "-p",  &port_arg,    "port",          "Listening port" );
  addarg( "*maddr",    "-m",  &addr_arg,    "address",       "Mcast group address" );
  addarg( "*ignore",   "-I",  &ignore_arg,  "address[,...]", "Ignore senders" );

/*-------------------------------------------------------------------------*\
        Parse the arguments
\*-------------------------------------------------------------------------*/
  if( getargs(argc,argv) ) {
    usage( argv[0], 1 );
    return 1;
  }

/*-------------------------------------------------------------------------*\
    Show version and/or help
\*-------------------------------------------------------------------------*/
  if( vers_flag ) {
    printf( "%s version %s\n", argv[0], VERSION );
    printf( "<c> 2013 by //MAF, ickStream GmbH\n\n" );
    return 0;
  }
  if( help_flag ) {
    usage( argv[0], 0 );
    return 0;
  }

/*------------------------------------------------------------------------*\
    Read configuration
\*------------------------------------------------------------------------*/
  if( cfg_fname && readconfig(cfg_fname) )
    return -1;

/*------------------------------------------------------------------------*\
    Get port
\*------------------------------------------------------------------------*/
  char *eptr;
  port = (int)strtol( port_arg, &eptr, 10 );
  while( isspace(*eptr) )
    eptr++;
  if( *eptr || port<0 || port>65365 ) {
    fprintf( stderr, "Bad port number: '%s'\n", port_arg );
    return 1;
  }

/*------------------------------------------------------------------------*\
    Get IP address from interface
\*------------------------------------------------------------------------*/
  ifaddr = inet_addr( ifname );
  if( ifaddr==INADDR_NONE ) {
    struct ifreq   ifr;
    int            ifrlen, s;

    s = socket( PF_INET, SOCK_DGRAM, 0 );
    if( s<0 ) {
      fprintf( stderr, "Could not get socket (%s)\n", strerror(errno) );
      return -1;
    }

    memset( &ifr, 0, sizeof(struct ifreq) );
    strncpy( ifr.ifr_name, ifname, IFNAMSIZ );

    rc = ioctl( s, SIOCGIFADDR, &ifr, &ifrlen );
    close( s );
    if( rc<0 ) {
      // fprintf( stderr, "ioctl(SIOCGIFADDR) failed (%s)\n", strerror(errno) );
      ifaddr = INADDR_NONE;
    }
    else
      ifaddr = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr;
  }
  if( ifaddr==INADDR_NONE ) {
    fprintf( stderr, "Could not get IP address of interface \"%s\"\n",
             ifname );
    return 1;
  }

/*------------------------------------------------------------------------*\
    Create list of ignored sources
\*------------------------------------------------------------------------*/
  if( ignore_arg ) {
    char *ptr;
    for( ptr=strtok(ignore_arg,",;"); ptr; ptr=strtok(NULL,",;") ) {
      struct _addrlist *new;
      in_addr_t         addr;
      addr = inet_addr( ptr );
      if( addr==INADDR_NONE ) {
        fprintf( stderr, "Bad filter address \"%s\"\n", ptr );
        return 1;
      }
      new = calloc( 1, sizeof(struct _addrlist) );
      if( !new ) {
        fprintf( stderr, "Out of memory\n" );
        return 1;
      }
      new->next   = ignore_list;
      new->addr   = addr;
      ignore_list = new;
      igncntr++;
    }
  }

/*-------------------------------------------------------------------------*\
    Allocate buffer
\*-------------------------------------------------------------------------*/
  buffer = malloc( BUFFERSIZE );
  if( !buffer ) {
    fprintf( stderr, "Out of memory\n" );
    return 1;
  }

/*-------------------------------------------------------------------------*\
    Report interface address
\*-------------------------------------------------------------------------*/
  inet_ntop( AF_INET, &ifaddr, buffer, BUFFERSIZE );
  printf( "##### Using addr %s for interface \"%s\"\n", buffer, ifname );
  if( igncntr )
    printf( "##### Ignoring %d sources\n", igncntr );
  igncntr = 0;

/*------------------------------------------------------------------------*\
    OK, from here on we catch some terminating signals and ignore others
\*------------------------------------------------------------------------*/
  struct sigaction act;
  memset( &act, 0, sizeof(act) );
  act.sa_sigaction = &sigHandler;
  act.sa_flags     = SA_SIGINFO;
  sigaction( SIGINT, &act, NULL );
  sigaction( SIGTERM, &act, NULL );

/*------------------------------------------------------------------------*\
    Try to create socket
\*------------------------------------------------------------------------*/
  sd = socket( PF_INET, SOCK_DGRAM, 0 );
  if( sd<0 ){
    free( buffer );
    fprintf( stderr,  "Could not create socket (%s)\n", strerror(errno) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Set non blocking
\*------------------------------------------------------------------------*/
  rc = fcntl( sd, F_GETFL );
  if( rc>=0 )
    rc = fcntl( sd, F_SETFL, rc|O_NONBLOCK );
  if( rc<0 )
    fprintf( stderr, "Could not set O_NONBLOCK on socket (%s)", strerror(errno) );

/*------------------------------------------------------------------------*\
    Reuse address (multiple processes will receive MCASTS)
\*------------------------------------------------------------------------*/
  opt = 1;
  rc = setsockopt( sd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt) );
  if( rc<0 )
    fprintf( stderr, "Could not set SO_REUSEADDR on socket (%s) \n", strerror(errno) );

/*------------------------------------------------------------------------*\
    Bind socket to requested port
\*------------------------------------------------------------------------*/
  memset( &sockname, 0, sockname_len );
  sockname.sin_family      = AF_INET;
  sockname.sin_addr.s_addr = htonl( INADDR_ANY );
  sockname.sin_port        = htons( port );
  rc = bind( sd, (struct sockaddr *)&sockname, sockname_len );
  if( rc<0 ) {
    free( buffer );
    close( sd );
    fprintf( stderr, "Could not bind socket to port %d (%s).", port, strerror(errno) );
    return -1;
  }

/*------------------------------------------------------------------------*\
  Add socket to multicast group on target interface
\*------------------------------------------------------------------------*/
  mgroup.imr_multiaddr.s_addr = inet_addr( addr_arg );
  mgroup.imr_interface.s_addr = ifaddr;
  rc = setsockopt( sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mgroup, sizeof(mgroup) );
  if( rc<0 ) {
    free( buffer );
    close( sd );
    fprintf( stderr, "Could not add mcast membership \"%s\" for interface %s (%s).",
             addr_arg, ifname, strerror(errno) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Main loop: wait for termination
\*------------------------------------------------------------------------*/
  while( !stop_signal ) {
    fd_set            readSet;
    int               retval;
    ssize_t           rcv_size;
    struct timeval    timeout;
    struct sockaddr   address;
    socklen_t         addrlen = sizeof(address);
    struct _addrlist *walk;
    struct timeval    tv;

/*------------------------------------------------------------------------*\
    Set timeout to 500ms
\*------------------------------------------------------------------------*/
    timeout.tv_sec  = 0;
    timeout.tv_usec = 500*1000;

/*------------------------------------------------------------------------*\
    Check for incoming SSDP messages on socket
\*------------------------------------------------------------------------*/
    FD_ZERO( &readSet );
    FD_SET( sd, &readSet );
    retval = select( sd+1, &readSet, NULL, NULL, &timeout );
    if( retval<0 ) {
      if( errno!=EINTR )
        fprintf( stderr, "select() failed (%s)\n", strerror(errno) );
      break;
    }
    if( !retval )
      continue;
    gettimeofday( &tv, NULL );

/*------------------------------------------------------------------------*\
    Receive data
\*------------------------------------------------------------------------*/
//    memset( buffer, 0, BUFFERSIZE );
    rcv_size = recvfrom( sd, buffer, BUFFERSIZE, 0, &address, &addrlen );
    if( rcv_size<0 ) {
      fprintf( stderr, "recvfrom() failed (%s)\n", strerror(errno) );
      continue;
    }
    if( !rcv_size ) {  // ?? Not possible for udp
      fprintf( stderr, "disconnected\n" );
      break;
    }

/*------------------------------------------------------------------------*\
    Ignore source?
\*------------------------------------------------------------------------*/
    for( walk=ignore_list; walk; walk=walk->next )
      if( walk->addr==((const struct sockaddr_in *)&address)->sin_addr.s_addr )
        break;
    if( walk ) {
      printf( "." );
      fflush( stdout );
      igncntr++;
      continue;
    }

/*------------------------------------------------------------------------*\
    Report data
\*------------------------------------------------------------------------*/
    printf( "\n>>>>> %.4f >>>>> %ld bytes from %s:%d >>>>>\n%.*s<<<<<\n",
           tv.tv_sec+tv.tv_usec*1E-6, (long)rcv_size,
           inet_ntoa(((const struct sockaddr_in *)&address)->sin_addr),
           ntohs(((const struct sockaddr_in *)&address)->sin_port),
           rcv_size, buffer );
    cntr++;

  }

/*------------------------------------------------------------------------*\
    Clean up
\*------------------------------------------------------------------------*/
  free( buffer );
  close( sd );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  printf( "\n##### Received %d packets, %d ignored\n", cntr, igncntr );
  return 0;
}



/*=========================================================================*\
        Handle signals
\*=========================================================================*/
static void sigHandler( int sig, siginfo_t *siginfo, void *context )
{

/*------------------------------------------------------------------------*\
    What sort of signal is to be processed ?
\*------------------------------------------------------------------------*/
  switch( sig ) {

/*------------------------------------------------------------------------*\
    A normal termination request
\*------------------------------------------------------------------------*/
    case SIGINT:
    case SIGTERM:
      stop_signal = sig;
      break;

/*------------------------------------------------------------------------*\
    Ignore broken pipes
\*------------------------------------------------------------------------*/
    case SIGPIPE:
      break;
  }

/*------------------------------------------------------------------------*\
    That's it.
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/


