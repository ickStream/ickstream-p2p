/*$*********************************************************************\

Source File     : ickP2p.c

Description     : Test suite for ickp2p API calls

Comments        : -

Called by       : User code

Calls           : Internal functions

Date            : 11.08.2013

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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, 
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
\************************************************************************/


#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <syslog.h>
#include <uuid/uuid.h>
#include <sys/select.h>
#include <sys/time.h>

#include "ickP2p.h"
#include "config.h"
#include "testmisc.h"


/*=========================================================================*\
  Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Private definitions and symbols
\*=========================================================================*/
#define TESTVERSION "0.3"
#define DEVICENAME  "ickp2plibtester"
#define IFNAME      "wlan0"

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
  int                  loop_flag   = 0;
  const char          *cfg_fname   = NULL;
  char                *uuidStr     = NULL;
  const char          *name        = DEVICENAME;
  const char          *ifname      = IFNAME;
  const char          *port_arg    = "1900";
  const char          *verb_arg    = "4";
  const char          *wait_arg    = "10000";
  const char          *service_arg = NULL;

  ickErrcode_t         irc;
  ickP2pContext_t     *ictx;
  uuid_t               uuid;
  ickP2pServicetype_t  service;
  int                  port        = 1900;
  int                  waitspan    = 10000;
  int                  cntr;
  char                *vlp;

/*-------------------------------------------------------------------------*\
        Set up command line switches
        (leading * flags availability in config file)
\*-------------------------------------------------------------------------*/
  addarg( "help",      "-?",  &help_flag,   NULL,        "Show this help message and quit" );
  addarg( "version",   "-V",  &vers_flag,   NULL,        "Show version and quit" );
  addarg( "config",    "-c",  &cfg_fname,   "filename",  "Set name of configuration file" );
  addarg( "*uuid",     "-u",  &uuidStr,     "uuid",      "Use UUID for this device (default: random)" );
  addarg( "*name",     "-n",  &name,        "name",      "Use Name for this device" );
  addarg( "*port",     "-p",  &port_arg,    "port",      "SSDP listener port" );
  addarg( "*idev",     "-i",  &ifname,      "interface", "Main network interface" );
  addarg( "*loopback", "-l",  &loop_flag,   NULL,        "Enable UPnP self discovery" );
  addarg( "*services", "-s",  &service_arg, "bitvector", "Announce services (default: random)" );
  addarg( "*wait",     "-w",  &wait_arg,    "millisecs", "Maximum wait time between two messages" );
  addarg( "*verbose",  "-v",  &verb_arg,    "level",     "Set ickp2p logging level (0-7)" );

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
    printf( "%s version %s\n", argv[0], TESTVERSION );
    printf( "ickstream p2p library version %s\n", ickP2pGetVersion(NULL,NULL) );
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
    Set verbosity level
\*------------------------------------------------------------------------*/
  if( verb_arg ) {
    char *eptr;
    int   level = (int)strtol( verb_arg, &eptr, 10 );
    while( isspace(*eptr) )
      eptr++;
    if( *eptr || level<0 || level>7 ) {
      fprintf( stderr, "Bad verbosity level: '%s'\n", verb_arg );
      return 1;
    }
    ickP2pSetLogging( level, stderr, 500 );
#ifndef ICK_DEBUG
    if( level>=LOG_DEBUG ) {
       fprintf( stderr, "%s: binary not compiled for debugging, loglevel %d might be too high!\n",
                        argv[0], level );
    }
#endif
  }

/*------------------------------------------------------------------------*\
    Use random uuid?
\*------------------------------------------------------------------------*/
  if( !uuidStr ) {
    uuidStr = malloc( 37 );
    srandom( (unsigned int)time(NULL) );
    uuid_generate( uuid );
    uuid_unparse( uuid, uuidStr );
  }

/*------------------------------------------------------------------------*\
    Use random service?
\*------------------------------------------------------------------------*/
  if( service_arg ) {
    char *eptr;
    service = (int)strtol( service_arg, &eptr, 0 );
    while( isspace(*eptr) )
      eptr++;
    if( *eptr || service<0 || service>ICKP2P_SERVICE_ANY ) {
      fprintf( stderr, "Bad service vector: '%s'\n", service_arg );
      return 1;
    }
  }
  else
    service = 1<<random()%4;

/*------------------------------------------------------------------------*\
    Get SSDP listener port
\*------------------------------------------------------------------------*/
  if( port_arg ) {
    char *eptr;
    port = (int)strtol( port_arg, &eptr, 0 );
    while( isspace(*eptr) )
      eptr++;
    if( *eptr || port<0 || port>65535 ) {
      fprintf( stderr, "Bad port: '%s'\n", port_arg );
      return 1;
    }
  }

/*------------------------------------------------------------------------*\
    Get wait time windows
\*------------------------------------------------------------------------*/
  if( wait_arg ) {
    char *eptr;
    waitspan = (int)strtol( wait_arg, &eptr, 0 );
    while( isspace(*eptr) )
      eptr++;
    if( *eptr || waitspan<0 ) {
      fprintf( stderr, "Bad wait time: '%s'\n", port_arg );
      return 1;
    }
  }

/*------------------------------------------------------------------------*\
    Create a very large payload message
\*------------------------------------------------------------------------*/
  vlp = createVlp();
  if( !vlp )
    return -1;

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
    Create context
\*------------------------------------------------------------------------*/
  ictx = ickP2pCreate( name, uuidStr, "./httpFolder", 100, port, service, &irc  );
  if( !ictx ) {
    fprintf( stderr, "ickP2pCreate: %s\n", ickStrError(irc) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    Add callbacks
\*------------------------------------------------------------------------*/
  irc = ickP2pRegisterDiscoveryCallback( ictx, ickDiscoverCb );
  if( irc ) {
    printf( "ickP2pRegisterDiscoveryCallback: %s\n", ickStrError(irc) );
    goto end;
  }
  irc = ickP2pRegisterMessageCallback( ictx, ickMessageCb );
  if( irc ) {
    printf( "ickP2pRegisterMessageCallback: %s\n", ickStrError(irc) );
    goto end;
  }

/*------------------------------------------------------------------------*\
    Add interfaces
\*------------------------------------------------------------------------*/
  irc = ickP2pAddInterface( ictx, ifname, NULL );
  if( irc ) {
    printf( "ickP2pAddInterface (%s): %s\n", ifname, ickStrError(irc) );
    goto end;
  }
#ifdef IF1NAME
  irc = ickP2pAddInterface( ictx, IF1NAME, NULL );
  if( irc ) {
    printf( "ickP2pAddInterface (%s): %s\n", ifname, ickStrError(irc) );
    goto end;
  }
#endif

/*------------------------------------------------------------------------*\
    Allow remote access to debuggig information
\*------------------------------------------------------------------------*/
  irc = ickP2pSetHttpDebugging( ictx, 1 );
  if( irc ) {
    printf( "ickP2pRemoteDebugApi: %s\n", ickStrError(irc) );
    goto end;
  }

/*------------------------------------------------------------------------*\
    Set loopback mode
\*------------------------------------------------------------------------*/
  if( loop_flag ) {
    irc = ickP2pUpnpLoopback( ictx, 1 );
    if( irc ) {
      printf( "ickP2pUpnpLoopback: %s\n", ickStrError(irc) );
      goto end;
    }
  }

/*------------------------------------------------------------------------*\
    Startup
\*------------------------------------------------------------------------*/
  irc = ickP2pResume( ictx );
  if( irc ) {
    printf( "ickP2pResume: %s\n", ickStrError(irc) );
    goto end;
  }

/*------------------------------------------------------------------------*\
    Report status
\*------------------------------------------------------------------------*/
  printf( "%s: starting pid %d\n", argv[0], (int)getpid() );
  printf( "ickP2pGetOsName:        \"%s\"\n", ickP2pGetOsName(ictx) );
  printf( "ickP2pGetDeviceName:    \"%s\"\n", ickP2pGetName(ictx) );
  printf( "ickP2pGetDeviceUuid:    \"%s\"\n", ickP2pGetDeviceUuid(ictx) );
  printf( "ickP2pGetState:         %d\n",     ickP2pGetState(ictx) );
  printf( "ickP2pGetBootId:        %ld\n",    ickP2pGetBootId(ictx) );
  printf( "ickP2pGetConfigId:      %ld\n",    ickP2pGetConfigId(ictx) );
  printf( "ickP2pGetUpnpPort:      %d\n",     ickP2pGetUpnpPort(ictx) );
  printf( "ickP2pGetLwsPort:       %d\n",     ickP2pGetLwsPort(ictx) );
  printf( "ickP2pGetServices:      0x%02x\n", ickP2pGetServices(ictx) );
  printf( "ickP2pGetUpnpLoopback:  %d\n",     ickP2pGetUpnpLoopback(ictx) );

/*------------------------------------------------------------------------*\
    Main loop: wait for termination
\*------------------------------------------------------------------------*/
  for( cntr=1; !stop_signal; cntr++ ) {
    char buffer [256];

    // construct message
    sprintf( buffer, "Message #%03d from %s - hello ickstream world!", cntr, uuidStr );

    // Broadcast message as string
    printf( "Sending test message #%03d (%ld bytes) ...\n", cntr, (long)strlen(buffer)+1 );
    irc = ickP2pSendMsg( ictx, NULL, ICKP2P_SERVICE_ANY, service, buffer, 0 );
    if( irc ) {
      printf( "ickP2pSendMsg: %s\n", ickStrError(irc) );
      goto end;
    }

    // Send large payload message
    if( !(cntr%10) ) {
      printf( "Sending very large payload (%ld bytes) message #%03d...\n", (long)VLP_SIZE, ++cntr );
      irc = ickP2pSendMsg( ictx, NULL, ICKP2P_SERVICE_ANY, service, vlp, VLP_SIZE );
      if( irc ) {
        printf( "ickP2pSendMsg: %s\n", ickStrError(irc) );
        goto end;
      }
    }

    // wait a random time
    if( waitspan ) {
      struct timeval timeout;
      long           waitms = random()%(waitspan+1);
      timeout.tv_sec  = waitms/1000;
      timeout.tv_usec = (waitms%1000)*1000;
      select( 0, NULL, NULL, NULL, &timeout);
    }

  }

/*------------------------------------------------------------------------*\
    Terminate library
\*------------------------------------------------------------------------*/
end:
  irc = ickP2pEnd( ictx, NULL );
  if( irc ) {
    printf( "ickP2pEnd: %s\n", ickStrError(irc) );
    return -1;
  }

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  printf( "%s: main thread terminated\n", argv[0] );
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


