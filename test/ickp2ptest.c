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

#include "config.h"
#include "ickP2p.h"


/*=========================================================================*\
  Global symbols
\*=========================================================================*/
// none


/*=========================================================================*\
  Private definitions and symbols
\*=========================================================================*/
#define TESTVERSION "0.2"
#define DEVICENAME  "ickp2plibtester"
#define IFNAME      "wlan0"

#define VLP_SIZE    23456L
#define VLP_MODUL   251

static volatile int stop_signal;


/*=========================================================================*\
  Private prototypes
\*=========================================================================*/
static void sigHandler( int sig, siginfo_t *siginfo, void *context );
static void ickDiscoverCb( ickP2pContext_t *ictx, const char *uuid, ickP2pDeviceState_t change, ickP2pServicetype_t type );
static void ickMessageCb( ickP2pContext_t *ictx, const char *sourceUuid, ickP2pServicetype_t sourceService, ickP2pServicetype_t targetServices, const char* message, size_t mSize, ickP2pMessageFlag_t mFlags );



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
  long                 i;

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
    Called when devices or services are found or terminated
\*=========================================================================*/
static void ickDiscoverCb( ickP2pContext_t *ictx, const char *uuid, ickP2pDeviceState_t change, ickP2pServicetype_t type )
{
  char *cstr = "???";
  char  tstr[128];

  switch( change ) {
    case   ICKP2P_CONNECTED:
      cstr = "connected";
      break;
    case   ICKP2P_DISCONNECTED:
      cstr = "disconnected";
      break;
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
    case ICKP2P_ERROR:
      cstr = "error";
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

  printf( "+++ %s -- %s %s\n", uuid, cstr, tstr );
  printf( "+++ %s -- Location: %s\n", uuid, ickP2pGetDeviceLocation(ictx,uuid) );
}


/*=========================================================================*\
    Called for incoming messages
\*=========================================================================*/
static void ickMessageCb( ickP2pContext_t *ictx, const char *sourceUuid,
                          ickP2pServicetype_t sourceService, ickP2pServicetype_t targetServices,
                          const char* message, size_t mSize, ickP2pMessageFlag_t mFlags )
{
  long i;

/*------------------------------------------------------------------------*\
    Print meta data and message snippet
\*------------------------------------------------------------------------*/
  printf( ">>> message from %s,0x%02x -> 0x%02x \"%.30s%s\" (%ld bytes, flags 0x%02x)\n",
          sourceUuid, sourceService, targetServices, message,
          mSize>30?"...":"", (long)mSize, mFlags );

/*------------------------------------------------------------------------*\
    Respond to messages
\*------------------------------------------------------------------------*/
  if( !strncmp(message,"Message #",strlen("Message #")) ) {
    char *response;
    ickErrcode_t irc;
    asprintf( &response, "Response from %s for %s", ickP2pGetDeviceUuid(ictx), message );

    // Broadcast message as string
    printf( "Sending %s\n", response );
    irc = ickP2pSendMsg( ictx, sourceUuid, ICKP2P_SERVICE_ANY, targetServices, response, 0 );
    if( irc ) {
      printf( "ickP2pSendMsg: %s\n", ickStrError(irc) );
      return;
    }
    free( response );
  }

/*------------------------------------------------------------------------*\
    Check integrity of strings
\*------------------------------------------------------------------------*/
  if( (mFlags&ICKP2P_MESSAGEFLAG_STRING) && mSize!=strlen(message)+1 ) {
    printf( "Mismatch in string message size (mSize is %ld, strlen+1 is %ld)",
        (long)mSize, (long)(strlen(message)+1) );
  }

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


