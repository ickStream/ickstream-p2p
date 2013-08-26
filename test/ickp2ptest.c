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
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <uuid/uuid.h>

#include "ickP2p.h"


/*=========================================================================*\
  Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Private definitions and symbols
\*=========================================================================*/
#define LOGLEVEL    7
#define DEVICENAME  "ickp2plibtester"
#define UUID        "3fe109ad-aa4c-4b0e-a089-3e30f5fc1afe"
#define IFNAME      "wlan0"

static volatile int stop_signal;


/*=========================================================================*\
  Private prototypes
\*=========================================================================*/
static void sigHandler( int sig, siginfo_t *siginfo, void *context );
static void ickDeviceCb( ickP2pContext_t *ictx, const char *uuid, ickP2pDeviceCommand_t change, ickP2pServicetype_t type );



/*=========================================================================*\
  main
\*=========================================================================*/
int main( int argc, char *argv[] )
{
  ickErrcode_t     irc;
  ickP2pContext_t *ictx;
  uuid_t           uuid;
  char             uuidStr[37];


/*------------------------------------------------------------------------*\
    Hello world and setting loglevel
\*------------------------------------------------------------------------*/
  printf( "%s: starting (pid %d)...\n", argv[0], (int)getpid() );
  printf( "ickP2pSetLogLevel: %d\n", LOGLEVEL );
  ickP2pSetLogLevel( LOGLEVEL );

/*------------------------------------------------------------------------*\
    Use random uuid
\*------------------------------------------------------------------------*/
  uuid_generate( uuid );
  uuid_unparse( uuid, uuidStr );

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
    Init library
\*------------------------------------------------------------------------*/
  ictx = ickP2pInit( DEVICENAME, uuidStr, "./httpFolder", 100, -1, -1, NULL, IFNAME, 1900, ickDeviceCb, NULL, &irc  );
  if( !ictx ) {
    fprintf( stderr, "ickP2pInit: %s\n", ickStrError(irc) );
    return -1;
  }
  printf( "ickP2pGetOsName:     \"%s\"\n", ickP2pGetOsName(ictx) );
  printf( "ickP2pGetDeviceName: \"%s\"\n", ickP2pGetName(ictx) );
  printf( "ickP2pGetDeviceUuid: \"%s\"\n", ickP2pGetDeviceUuid(ictx) );
  printf( "ickP2pGetState:      %d\n",     ickP2pGetState(ictx) );
  printf( "ickP2pGetBootId:     %ld\n",    ickP2pGetBootId(ictx) );
  printf( "ickP2pGetConfigId:   %ld\n",    ickP2pGetConfigId(ictx) );
  printf( "ickP2pGetUpnpPort:   %d\n",     ickP2pGetUpnpPort(ictx) );
  printf( "ickP2pGetLwsPort:    %d\n",     ickP2pGetLwsPort(ictx) );
  printf( "ickP2pGetHostname:   %s\n",     ickP2pGetHostname(ictx) );

/*------------------------------------------------------------------------*\
    Main loop: wait for termination
\*------------------------------------------------------------------------*/
  while( !stop_signal ) {
    sleep( 1 );
  }

/*------------------------------------------------------------------------*\
    Terminate library
\*------------------------------------------------------------------------*/
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
    Caled when devices or services are found or terminated
\*=========================================================================*/
static void ickDeviceCb( ickP2pContext_t *ictx, const char *uuid, ickP2pDeviceCommand_t change, ickP2pServicetype_t type )
{
  char *cstr = "???";
  char  tstr[128];

  switch( change ) {
    case   ICKP2P_ADD:
      cstr = "adding";
      break;
    case ICKP2P_REMOVE:
      cstr = "removing";
      break;
    case ICKP2P_EXPIRED:
      cstr = "expiring";
      break;
    case ICKP2P_TERMINATE:
      cstr = "terminating";
      break;
  }

  *tstr = 0;
  if( type==ICKP2P_SERVICE_GENERIC )
    strcpy( tstr, "root device" );
  else {
    strcpy( tstr, "services:" );
    if( type&ICKP2P_SERVICE_PLAYER )
      strcat( tstr, " player");
    if( type&ICKP2P_SERVICE_CONTROLLER )
      strcat( tstr, " controller" );
    if( type&ICKP2P_SERVICE_SERVER_GENERIC )
      strcat( tstr, " generic_server" );
    if( type&ICKP2P_SERVICE_DEBUG )
      strcat( tstr, " debugging" );
  }

  printf( "%s: %s -- %s %s\n", ickP2pGetIf(ictx), uuid, cstr, tstr );
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


