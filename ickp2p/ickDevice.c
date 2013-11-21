/*$*********************************************************************\

Name            : -

Source File     : ickDevice.c

Description     : implements a descriptor for ickstream devices

Comments        : -

Called by       : -

Calls           : -

Error Messages  : -

Date            : 25.08.2013

Updates         : -

Author          : //MAF

Remarks         : refactored from ickDiscoverRegistry

*************************************************************************
 * Copyright (c) 2013, ickStream GmbH
 * All rights reserved.
\************************************************************************/


#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#ifdef ICK_P2PDEBUGCALLS
#include <jansson.h>
#endif

#include "ickP2p.h"
#include "ickP2pInternal.h"
#include "logutils.h"
#include "ickDevice.h"


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
    Create an new device
\*=========================================================================*/
ickDevice_t *_ickDeviceNew( const char *uuid )
{
  ickDevice_t *device;
  debug( "_ickDeviceNew: \"%s\"", uuid );

/*------------------------------------------------------------------------*\
    Allocate and initialize descriptor
\*------------------------------------------------------------------------*/
  device = calloc( 1, sizeof(ickDevice_t) );
  if( !device ) {
    logerr( "_ickDeviceNew: out of memory" );
    return NULL;
  }
  pthread_mutex_init( &device->mutex, NULL );
  device->uuid = strdup( uuid );
  if( !device->uuid ) {
    logerr( "_ickDeviceNew: out of memory" );
    return NULL;
  }
  device->tCreation = _ickTimeNow();

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return device;
}


/*=========================================================================*\
  Free memory for a device descriptor
    this does no unlinking or timer deletion
    caller should lock device
\*=========================================================================*/
void _ickDeviceFree( ickDevice_t *device )
{
  debug( "_ickDeviceFree: uuid=\"%s\"", device->uuid );

/*------------------------------------------------------------------------*\
    Delete mutex
\*------------------------------------------------------------------------*/
  pthread_mutex_destroy( &device->mutex );

/*------------------------------------------------------------------------*\
    Clean up message queues
\*------------------------------------------------------------------------*/
  _ickDevicePurgeMessages( device );

/*------------------------------------------------------------------------*\
    Free memory
\*------------------------------------------------------------------------*/
  Sfree( device->uuid );
  Sfree( device->location );
  Sfree( device->friendlyName );
  Sfree( device );
}


/*=========================================================================*\
  Clean up message queues
\*=========================================================================*/
void _ickDevicePurgeMessages( ickDevice_t *device )
{
  ickMessage_t *msg, *next;
  int           num;

  debug( "_ickDevicePurgeMessages: uuid=\"%s\"", device->uuid );

/*------------------------------------------------------------------------*\
    Delete unsent messages
\*------------------------------------------------------------------------*/
  if( device->outQueue ) {
    for( num=0,msg=device->outQueue; msg; msg=next ) {
      next = msg->next;
      Sfree( msg->payload );
      Sfree( msg )
      num++;
    }
    device->outQueue = NULL;
    loginfo( "_ickDevicePurgeMessages: Deleted %d unsent messages in outQueue.", num );
  }

/*------------------------------------------------------------------------*\
    Delete undelivered messages
\*------------------------------------------------------------------------*/
  if( device->inQueue ) {
    for( num=0,msg=device->inQueue; msg; msg=next ) {
      next = msg->next;
      Sfree( msg->payload );
      Sfree( msg )
      num++;
    }
    device->inQueue = NULL;
    loginfo( "_ickDevicePurgeMessages: Deleted %d undelivered messages in inQueue.", num );
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
  Lock a device for access or modification
\*=========================================================================*/
void _ickDeviceLock( ickDevice_t *device )
{
  debug ( "_ickDeviceLock (%s): locking...", device->uuid );
  pthread_mutex_lock( &device->mutex );
  debug ( "_ickDeviceLock (%s): locked", device->uuid );
}


/*=========================================================================*\
  Unlock a device
\*=========================================================================*/
void _ickDeviceUnlock( ickDevice_t *device )
{
  debug ( "_ickDeviceUnlock (%s): unlocked", device->uuid );
  pthread_mutex_unlock( &device->mutex );
}


/*=========================================================================*\
  Set device location string
    caller should lock the device
\*=========================================================================*/
ickErrcode_t _ickDeviceSetLocation( ickDevice_t *device, const char *location )
{
  char *str = NULL;
  if( location ) {
    str = strdup( location );
    if( !str ) {
      logerr( "_ickDeviceSetLocation: out of memory" );
      return ICKERR_NOMEM;
    }
  }
  Sfree( device->location );
  device->location = str;
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Set device name, uses uuid if name is NULL
    caller should lock the device
\*=========================================================================*/
ickErrcode_t _ickDeviceSetName( ickDevice_t *device, const char *name )
{
  char *str = name ? strdup(name) : strdup(device->uuid);
  if( !str ) {
    logerr( "_ickDeviceSetLocation: out of memory" );
    return ICKERR_NOMEM;
  }
  Sfree( device->friendlyName );
  device->friendlyName = str;
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Add a message container to the output queue
    caller should lock the device
    container needs to be allocated and will be freed when the message is destroyed
    container must including LWS padding, while size does NOT include LWS padding
    caller should free the payload in case an error code is returned
\*=========================================================================*/
ickErrcode_t _ickDeviceAddOutMessage( ickDevice_t *device, void *container, size_t size )
{
  ickMessage_t *message;
  ickMessage_t *walk;
  debug ( "_ickDeviceAddOutMessage (%s): %ld bytes", device->uuid, (long)size );

/*------------------------------------------------------------------------*\
    Allocate and initialize message descriptor
\*------------------------------------------------------------------------*/
  message = calloc( 1, sizeof(ickMessage_t) );
  if( !message ) {
    logerr( "_ickDeviceAddOutMessage: out of memory" );
    return ICKERR_NOMEM;
  }
  message->tCreated = _ickTimeNow();
  message->payload  = container;
  message->size     = size;
  message->issued   = 0;

/*------------------------------------------------------------------------*\
    Link to end of output queue
\*------------------------------------------------------------------------*/
  if( !device->outQueue )
    device->outQueue = message;
  else {
    for( walk=device->outQueue; walk->next; walk=walk->next );
    message->prev = walk;
    walk->next    = message;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  debug( "_ickDeviceAddOutMessage (%s): output queue has now %d entries (%ld bytes)",
         device->uuid, _ickDevicePendingOutMessages(device), _ickDevicePendingOutBytes(device) );
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Remove a message container from the output queue
    caller should lock the device
    caller needs also to free the message
\*=========================================================================*/
ickErrcode_t _ickDeviceUnlinkOutMessage( ickDevice_t *device, ickMessage_t *message )
{
  ickMessage_t *walk;
  debug( "_ickDeviceUnlinkOutMessage (%s): message %p (%ld bytes)",
         device->uuid, message, message->size );

/*------------------------------------------------------------------------*\
    Check if member
\*------------------------------------------------------------------------*/
  for( walk=device->outQueue; walk->next; walk=walk->next )
    if( walk==message )
      break;
  if( !walk ) {
    logerr( "_ickDeviceUnlinkOutMessage (%s): message not member of output queue",
            device->uuid );
    return ICKERR_NOMEMBER;
  }

/*------------------------------------------------------------------------*\
    Unlink
\*------------------------------------------------------------------------*/
  if( message->next )
    message->next->prev = message->prev;
  if( message->prev )
    message->prev->next = message->next;
  else
    device->outQueue = message->next;

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Add a message container to the input queue
    caller should lock the device
    container needs to be allocated and will be freed when the message is destroyed
    caller should free the payload in case an error code is returned
\*=========================================================================*/
ickErrcode_t _ickDeviceAddInMessage( ickDevice_t *device, void *container, size_t size )
{
  ickMessage_t *message;
  ickMessage_t *walk;
  debug ( "_ickDeviceAddInMessage (%s): %ld bytes", device->uuid, (long)size );

/*------------------------------------------------------------------------*\
    Allocate and initialize message descriptor
\*------------------------------------------------------------------------*/
  message = calloc( 1, sizeof(ickMessage_t) );
  if( !message ) {
    logerr( "_ickDeviceAddInMessage: out of memory" );
    return ICKERR_NOMEM;
  }
  message->tCreated = _ickTimeNow();
  message->payload  = container;
  message->size     = size;
  message->issued   = 0;

/*------------------------------------------------------------------------*\
    Link to end of input queue
\*------------------------------------------------------------------------*/
  if( !device->inQueue )
    device->inQueue = message;
  else {
    for( walk=device->inQueue; walk->next; walk=walk->next );
    message->prev = walk;
    walk->next    = message;
  }

/*------------------------------------------------------------------------*\
    That's it
\*------------------------------------------------------------------------*/
  debug( "_ickDeviceAddInMessage (%s): input queue has now %d entries (%ld bytes)",
         device->uuid, _ickDevicePendingInMessages(device), _ickDevicePendingInBytes(device) );
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Remove a message container from the input queue
    caller should lock the device
    caller needs also to free the message
\*=========================================================================*/
ickErrcode_t _ickDeviceUnlinkInMessage( ickDevice_t *device, ickMessage_t *message )
{
  ickMessage_t *walk;
  debug( "_ickDeviceUnlinkInMessage (%s): message %p (%ld bytes)",
         device->uuid, message, message->size );

/*------------------------------------------------------------------------*\
    Check if member
\*------------------------------------------------------------------------*/
  for( walk=device->inQueue; walk->next; walk=walk->next )
    if( walk==message )
      break;
  if( !walk ) {
    logerr( "_ickDeviceUnlinkInMessage (%s): message not member of input queue",
            device->uuid );
    return ICKERR_NOMEMBER;
  }

/*------------------------------------------------------------------------*\
    Unlink
\*------------------------------------------------------------------------*/
  if( message->next )
    message->next->prev = message->prev;
  if( message->prev )
    message->prev->next = message->next;
  else
    device->inQueue = message->next;

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Free a message container
\*=========================================================================*/
void _ickDeviceFreeMessage( ickMessage_t *message )
{
  debug( "_ickDeviceFreeMessage: message %p (%ld bytes)",
         message, message->size );

/*------------------------------------------------------------------------*\
    Free payload and descriptor
\*------------------------------------------------------------------------*/
  Sfree( message->payload );
  Sfree( message );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
}


/*=========================================================================*\
  Get root of output queue
    caller should lock the device
\*=========================================================================*/
ickMessage_t *_ickDeviceOutQueue( ickDevice_t *device )
{
  return device->outQueue;
}


/*=========================================================================*\
  Get output queue length of a device
    caller should lock the device
\*=========================================================================*/
int _ickDevicePendingOutMessages( ickDevice_t *device )
{
  ickMessage_t *message;
  int           size;

/*------------------------------------------------------------------------*\
    Loop over output queue and count entries
\*------------------------------------------------------------------------*/
  for( size=0,message=device->outQueue; message; message=message->next )
    size++;

/*------------------------------------------------------------------------*\
    Return result
\*------------------------------------------------------------------------*/
  return size;
}


/*=========================================================================*\
  Get accumulated output queue size of a device
    caller should lock the device
\*=========================================================================*/
size_t _ickDevicePendingOutBytes( ickDevice_t *device )
{
  ickMessage_t *message;
  size_t        size;

/*------------------------------------------------------------------------*\
    Loop over output queue and count bytes
\*------------------------------------------------------------------------*/
  for( size=0,message=device->outQueue; message; message=message->next )
    size += message->size;

/*------------------------------------------------------------------------*\
    Return result
\*------------------------------------------------------------------------*/
  return size;
}


/*=========================================================================*\
  Get input queue length of a device
    caller should lock the device
\*=========================================================================*/
int _ickDevicePendingInMessages( ickDevice_t *device )
{
  ickMessage_t *message;
  int           size;

/*------------------------------------------------------------------------*\
    Loop over output queue and count entries
\*------------------------------------------------------------------------*/
  for( size=0,message=device->inQueue; message; message=message->next )
    size++;

/*------------------------------------------------------------------------*\
    Return result
\*------------------------------------------------------------------------*/
  return size;
}


/*=========================================================================*\
  Get accumulated input queue size of a device
    caller should lock the device
\*=========================================================================*/
size_t _ickDevicePendingInBytes( ickDevice_t *device )
{
  ickMessage_t *message;
  size_t        size;

/*------------------------------------------------------------------------*\
    Loop over output queue and count bytes
\*------------------------------------------------------------------------*/
  for( size=0,message=device->inQueue; message; message=message->next )
    size += message->size;

/*------------------------------------------------------------------------*\
    Return result
\*------------------------------------------------------------------------*/
  return size;
}


/*=========================================================================*\
  Convert SSDP state to string
\*=========================================================================*/
const char *_ickDeviceSsdpState2Str( ickDeviceSsdpState_t state )
{
  switch( state ) {
    case ICKDEVICE_SSDPUNSEEN:  return "not yet seen";
    case ICKDEVICE_SSDPALIVE:   return "alive";
    case ICKDEVICE_SSDPBYEBYE:  return "byebye";
    case ICKDEVICE_SSDPEXPIRED: return "expired";
  }

  return "Invalid SSDP state";
}


/*=========================================================================*\
  Convert connection state to string
\*=========================================================================*/
const char *_ickDeviceConnState2Str( ickDeviceConnState_t state )
{
  switch( state ) {
    case ICKDEVICE_NOTCONNECTED:      return "not connected";
    case ICKDEVICE_LOOPBACK:          return "loopback (connected)";
    case ICKDEVICE_CLIENTCONNECTING:  return "client connecting";
    case ICKDEVICE_ISCLIENT:          return "client (connected)";
    case ICKDEVICE_SERVERCONNECTING:  return "server (connecting)";
    case ICKDEVICE_ISSERVER:          return "server (connected)";
  }

  return "Invalid connection state";
}


/*=========================================================================*\
                                    END OF FILE
\*=========================================================================*/

