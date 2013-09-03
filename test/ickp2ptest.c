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
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
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
//#define IFNAME      "wlan0"
#define IFNAME      "en1"
#define IF1NAME     "lo0"

#define VLP_SIZE    23456L
#define VLP_MODUL   251

static volatile int stop_signal;


/*=========================================================================*\
  Private prototypes
\*=========================================================================*/
static void sigHandler( int sig, siginfo_t *siginfo, void *context );
static void ickDiscoverCb( ickP2pContext_t *ictx, const char *uuid, ickP2pDiscoveryCommand_t change, ickP2pServicetype_t type );
static void ickMessageCb( ickP2pContext_t *ictx, const char *sourceUuid, ickP2pServicetype_t sourceService, ickP2pServicetype_t targetServices, const char* message, size_t mSize );



/*=========================================================================*\
  main
\*=========================================================================*/
int main( int argc, char *argv[] )
{
  ickErrcode_t         irc;
  ickP2pContext_t     *ictx;
  uuid_t               uuid;
  char                 uuidStr[37];
  ickP2pServicetype_t  service;
  int                  cntr;
  char                *vlp;
  long                 i;


/*------------------------------------------------------------------------*\
    Hello world and setting loglevel
\*------------------------------------------------------------------------*/
  printf( "%s: starting (pid %d)...\n", argv[0], (int)getpid() );
  printf( "ickP2pSetLogLevel: %d\n", LOGLEVEL );
  ickP2pSetLogLevel( LOGLEVEL );

/*------------------------------------------------------------------------*\
    Use random uuid and service
\*------------------------------------------------------------------------*/
  srandom( (unsigned int)time(NULL) );
  uuid_generate( uuid );
  uuid_unparse( uuid, uuidStr );
  service = 1<<random()%4;

/*------------------------------------------------------------------------*\
    Create a very large payload message
\*------------------------------------------------------------------------*/
  vlp = malloc( VLP_SIZE );
  if( !vlp ) {
    printf( "%s: could not allocate %ld bytes\n", argv[0], VLP_SIZE );
    return -1;
  }
  for( i=0; i<VLP_SIZE; i++ )
    vlp[i] = (char)(i%VLP_MODUL);

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
  ictx = ickP2pCreate( DEVICENAME, uuidStr, "./httpFolder", 100, NULL, IFNAME, 1900, service, &irc  );
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
    Add interface
\*------------------------------------------------------------------------*/
#ifdef IF1NAME
  irc = ickP2pAddInterface( ictx, IF1NAME );
  if( irc ) {
    printf( "ickP2pAddInterface: %s\n", ickStrError(irc) );
    goto end;
  }
#endif

/*------------------------------------------------------------------------*\
    Allow remote access to debuggig information
\*------------------------------------------------------------------------*/
  irc = ickP2pRemoteDebugApi( ictx, 1 );
  if( irc ) {
    printf( "ickP2pRemoteDebugApi: %s\n", ickStrError(irc) );
    goto end;
  }

/*------------------------------------------------------------------------*\
    Set loopback mode
\*------------------------------------------------------------------------*/
  irc = ickP2pUpnpLoopback( ictx, 1 );
  if( irc ) {
    printf( "ickP2pUpnpLoopback: %s\n", ickStrError(irc) );
    goto end;
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
  printf( "ickP2pGetOsName:        \"%s\"\n", ickP2pGetOsName(ictx) );
  printf( "ickP2pGetDeviceName:    \"%s\"\n", ickP2pGetName(ictx) );
  printf( "ickP2pGetDeviceUuid:    \"%s\"\n", ickP2pGetDeviceUuid(ictx) );
  printf( "ickP2pGetState:         %d\n",     ickP2pGetState(ictx) );
  printf( "ickP2pGetBootId:        %ld\n",    ickP2pGetBootId(ictx) );
  printf( "ickP2pGetConfigId:      %ld\n",    ickP2pGetConfigId(ictx) );
  printf( "ickP2pGetUpnpPort:      %d\n",     ickP2pGetUpnpPort(ictx) );
  printf( "ickP2pGetLwsPort:       %d\n",     ickP2pGetLwsPort(ictx) );
  printf( "ickP2pGetHostname:      %s\n",     ickP2pGetHostname(ictx) );
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
    printf( "Sending test message #%03d...\n", cntr );
    irc = ickP2pSendMsg( ictx, NULL, ICKP2P_SERVICE_ANY, service, buffer, 0 );
    if( irc ) {
      printf( "ickP2pSendMsg: %s\n", ickStrError(irc) );
      goto end;
    }

    if( !(cntr%10) ) {
      printf( "Sending very large payload (%ld bytes) message #%03d...\n", (long)VLP_SIZE, ++cntr );
      irc = ickP2pSendMsg( ictx, NULL, ICKP2P_SERVICE_ANY, service, vlp, VLP_SIZE );
      if( irc ) {
        printf( "ickP2pSendMsg: %s\n", ickStrError(irc) );
        goto end;
      }
    }

    // wait a random time [0-9s]
    sleep( random()%10 );

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
    Called when devices or services are found or terminated
\*=========================================================================*/
static void ickDiscoverCb( ickP2pContext_t *ictx, const char *uuid, ickP2pDiscoveryCommand_t change, ickP2pServicetype_t type )
{
  char *cstr = "???";
  char  tstr[128];

  switch( change ) {
    case   ICKP2P_LEGACY:
      cstr = "adding (legacy)";
      break;
    case   ICKP2P_NEW:
      cstr = "adding (new)";
      break;
    case ICKP2P_REMOVED:
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
    Called for incoming messages
\*=========================================================================*/
static void ickMessageCb( ickP2pContext_t *ictx, const char *sourceUuid,
                          ickP2pServicetype_t sourceService, ickP2pServicetype_t targetServices,
                          const char* message, size_t mSize )
{
  long i;

/*------------------------------------------------------------------------*\
    Print meta data and message snippet
\*------------------------------------------------------------------------*/
  printf( "%s: message from %s,0x%02x -> 0x%02x \"%.30s%s\" (%ld bytes)\n",
          ickP2pGetIf(ictx), sourceUuid, sourceService, targetServices, message,
          mSize>30?"...":"", (long)mSize );

/*------------------------------------------------------------------------*\
    Check integrity of VLP
\*------------------------------------------------------------------------*/
  if( mSize>VLP_SIZE-10 ) {
    for( i=0; i<VLP_SIZE; i++ )
      if( message[i] != (char)(i%VLP_MODUL) )
        break;
    if( i<VLP_SIZE )
      printf( "VLP is corrupt at position %ld", i );
  }
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


