/*$*********************************************************************\

Source File     : testmisc.c

Description     : Miscellaneous functions for test suite

Comments        : -

Called by       : test code and ickstream p2p library

Calls           : icstream p2p library

Date            : 24.12.2013

Updates         : -

Author          : //MAF

Remarks         : -

*************************************************************************
 * Copyright (c) 2013, ickStream GmbH
 * All rights reserved.
\************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ickP2p.h"
#include "testmisc.h"


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
    Create a very large payload message
\*=========================================================================*/
char *createVlp( void )
{
  char *vlp;
  long  i;

  vlp = malloc( VLP_SIZE );
  if( !vlp ) {
    printf( "Could not allocate %ld bytes\n", VLP_SIZE );
    return NULL;
  }
  for( i=0; i<VLP_SIZE; i++ )
    vlp[i] = (char)(i%VLP_MODUL);

  return vlp;
}


/*=========================================================================*\
    Check very large payload message
\*=========================================================================*/
int checkVlp( const char *msg, size_t len )
{
  long i;

  for( i=0; i<VLP_SIZE&&i<len; i++ )
    if( msg[i] != (char)(i%VLP_MODUL) )
      break;

   if( i<VLP_SIZE ) {
     printf( "VLP is corrupt at position %ld", i );
     return -1;
   }

   return 0;
}


/*=========================================================================*\
    Called when devices or services are found or terminated
\*=========================================================================*/
void ickDiscoverCb( ickP2pContext_t *ictx, const char *uuid, ickP2pDeviceState_t change, ickP2pServicetype_t type )
{
  char  tstr[128];

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

  // Print discovery event
  printf( "+++ %s: %s -- %s -- %s\n", ickP2pGetDeviceUuid(ictx),
           uuid, ickLibDeviceState2Str(change), tstr );
  printf( "+++ %s: %s -- Location: %s\n", ickP2pGetDeviceUuid(ictx),
           uuid, ickP2pGetDeviceLocation(ictx,uuid) );

  // For new connections send hello
  if( change==ICKP2P_CONNECTED ) {
    ickErrcode_t irc;
    // Broadcast message as string
    printf( "+++ %s: %s -- Sending Hello!\n", ickP2pGetDeviceUuid(ictx), uuid );
    irc = ickP2pSendMsg( ictx, uuid, ICKP2P_SERVICE_ANY, type, "Hello!", 0 );
    if( irc ) {
      printf( "ickP2pSendMsg: %s\n", ickStrError(irc) );
      return;
    }
  }
}


/*=========================================================================*\
    Called for incoming messages
\*=========================================================================*/
void ickMessageCb( ickP2pContext_t *ictx, const char *sourceUuid,
                          ickP2pServicetype_t sourceService, ickP2pServicetype_t targetServices,
                          const char* message, size_t mSize, ickP2pMessageFlag_t mFlags )
{

/*------------------------------------------------------------------------*\
    Print meta data and message snippet
\*------------------------------------------------------------------------*/
  printf( ">>> %s: message from %s,0x%02x -> 0x%02x \"%.30s%s\" (%ld bytes, flags 0x%02x)\n",
      ickP2pGetDeviceUuid(ictx) , sourceUuid, sourceService, targetServices, message,
          mSize>30?"...":"", (long)mSize, mFlags );

/*------------------------------------------------------------------------*\
    Respond to messages
\*------------------------------------------------------------------------*/
  if( !strncmp(message,"Message #",strlen("Message #")) ) {
    char *response;
    ickErrcode_t irc;
    asprintf( &response, "Response for %s", message );

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
  if( mSize>VLP_SIZE-10 )
    checkVlp( message, mSize );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/


