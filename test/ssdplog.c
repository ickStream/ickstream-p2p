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
#include <sys/utsname.h>


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
  const char          *msearch_arg = NULL;
  in_addr_t            ifaddr;
  in_addr_t            mcaddr;
  char                *buffer;
  int                  port;
  struct _addrlist    *ignore_list = NULL;
  int                  minterval   = 0;
  char                *eptr;
  struct utsname       utsname;
  char                *msearchstr  = NULL;
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
  addarg( "help",       "-?",  &help_flag,   NULL,            "Show this help message and quit" );
  addarg( "version",    "-V",  &vers_flag,   NULL,            "Show version and quit" );
  addarg( "config",     "-c",  &cfg_fname,   "filename",      "Set name of configuration file" );
  addarg( "*idev",      "-i",  &ifname,      "interface",     "Network interface" );
  addarg( "*port",      "-p",  &port_arg,    "port",          "Listening port" );
  addarg( "*maddr",     "-m",  &addr_arg,    "address",       "Mcast group address" );
  addarg( "*ignore",    "-I",  &ignore_arg,  "address[,...]", "Ignore senders" );
  addarg( "*broadcast", "-b",  &msearch_arg, "interval",      "Broadcast M-Search packets (dafault: off)" );

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
  port = (int)strtol( port_arg, &eptr, 10 );
  while( isspace(*eptr) )
    eptr++;
  if( *eptr || port<0 || port>65365 ) {
    fprintf( stderr, "Bad port number: '%s'\n", port_arg );
    return 1;
  }

/*------------------------------------------------------------------------*\
    Get msearch interval (if any)
\*------------------------------------------------------------------------*/
  if( msearch_arg ) {
    minterval = (int)strtol( msearch_arg, &eptr, 10 );
    while( isspace(*eptr) )
      eptr++;
    if( *eptr || minterval<30 ) {
      fprintf( stderr, "Bad interval: '%s' (should be >=30)\n", msearch_arg );
      return 1;
    }
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
    Get multicast address
\*------------------------------------------------------------------------*/
  mcaddr = inet_addr( addr_arg );
  if( mcaddr==INADDR_NONE ) {
    fprintf( stderr, "Bad multicast address \"%s\"\n", addr_arg );
    return 1;
  }

/*------------------------------------------------------------------------*\
    Get msearch string
\*------------------------------------------------------------------------*/
  if( minterval ) {
    char buf[64];
    if( uname(&utsname) ) {
      fprintf( stderr, "Could not get uname (%s)", strerror(errno) );
      return 1;
    }
    inet_ntop( AF_INET, &mcaddr, buf, sizeof(buf) );
    rc = asprintf( &msearchstr,
                    "M-SEARCH * HTTP/1.1\r\n"
                    "HOST: %s:%d\r\n"
                    "MAN: \"ssdp:discover\"\r\n"
                    "MX: %d\r\n"
                    "ST: %s\r\n"
                    "USER-AGENT: %s/%s UPnP/1.1 ssdplog/1.0\r\n"
                    "\r\n",
                    buf, port, 1, "ssdp:all",
                    utsname.sysname, utsname.release );
    if( rc<0 ) {
      fprintf( stderr, "Out of memory\n" );
      return 1;
    }
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
    Reuse address (multiple processes will receive MCASTs)
\*------------------------------------------------------------------------*/
  opt = 1;
  rc = setsockopt( sd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt) );
  if( rc<0 )
    fprintf( stderr, "Could not set SO_REUSEADDR on socket (%s) \n", strerror(errno) );
#ifdef ICK_USE_SO_REUSEPORT
  rc = setsockopt( sd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt) );
  if( rc<0 )
    fprintf( stderr, "Could not set SO_REUSEPORT on socket (%s) \n", strerror(errno) );
#endif

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
    fprintf( stderr, "Could not bind socket to port %d (%s)\n", port, strerror(errno) );
    return -1;
  }

/*------------------------------------------------------------------------*\
  Add socket to multicast group on target interface
\*------------------------------------------------------------------------*/
  mgroup.imr_multiaddr.s_addr = mcaddr;
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
  long              last_msearch = 0;
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
    Need to send M-Search?
\*------------------------------------------------------------------------*/
    gettimeofday( &tv, NULL );
    if( minterval && tv.tv_sec>last_msearch ) {
      struct sockaddr_in addr;
      ssize_t            n;
      size_t             len = strlen( msearchstr );

      memset( &addr, 0, sizeof(struct sockaddr_in) );
      addr.sin_family      = AF_INET;
      addr.sin_addr.s_addr = mcaddr;
      addr.sin_port        = htons( port );

      printf( "\n***** %.4f ***** %ld bytes to %s:%d *****\n%s*****\n",
             tv.tv_sec+tv.tv_usec*1E-6, (long)len,
             inet_ntoa(addr.sin_addr), port, msearchstr );
      fflush( stdout );

      // Try to send packet via discovery socket
      n = sendto( sd, msearchstr, len, 0, (const struct sockaddr*)&addr, sizeof(addr) );

      // Any error ?
      if( n<0 ) {
        fprintf( stderr, "Could not send to %s:%d (%s)",
                 inet_ntoa(addr.sin_addr), port, strerror(errno) );
        break;
      }
      else if( n<len ) {
        fprintf( stderr, "Could not send all data to %s:%d (%ld of %ld)",
                 inet_ntoa(addr.sin_addr), port, (long)n, (long)len );
        break;
      }

      last_msearch = tv.tv_sec+minterval;
    }

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
           (int)rcv_size, buffer );
    fflush( stdout );
    cntr++;

  }

/*------------------------------------------------------------------------*\
    Clean up
\*------------------------------------------------------------------------*/
  free( buffer );
  if( msearchstr )
    free( msearchstr );
  close( sd );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  printf( "\n##### Received %d packets, %d ignored\n", cntr, igncntr );
  fflush( stdout );

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


