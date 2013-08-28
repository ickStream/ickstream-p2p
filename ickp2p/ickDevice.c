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


#include <stdlib.h>
#include <string.h>
#include <pthread.h>

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
ickDevice_t *_ickDeviceNew( const char *uuid, ickDeviceType_t type )
{
  ickDevice_t *device;
  debug( "_ickDeviceNew: uuid=\"%s\" type %d", uuid, type );

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
    Delete unsent messages
\*------------------------------------------------------------------------*/
  if( device->outQueue ) {
    ickMessage_t *msg, *next;
    int           num;

    //Loop over output queue, free and count entries
    for( num=0,msg=device->outQueue; msg; msg=next ) {
      next = msg->next;
      Sfree( msg->payload );
      Sfree( msg )
      num++;
    }
    device->outQueue = NULL;
    loginfo( "_ickDeviceFree: Deleted %d unsent messages.", num );
  }

/*------------------------------------------------------------------------*\
    Free memory
\*------------------------------------------------------------------------*/
  Sfree( device->uuid );
  Sfree( device->location );
  Sfree( device->friendlyName );
  Sfree( device );
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
ickErrcode_t _ickDeviceAddMessage( ickDevice_t *device, void *container, size_t size )
{
  ickMessage_t *message;
  ickMessage_t *walk;
  debug ( "_ickDeviceAddMessage (%s): %ld bytes", device->uuid, (long)size );

/*------------------------------------------------------------------------*\
    Allocate and initialize message descriptor
\*------------------------------------------------------------------------*/
  message = calloc( 1, sizeof(ickMessage_t) );
  if( !message ) {
    logerr( "_ickDeviceAddMessage: out of memory" );
    return ICKERR_NOMEM;
  }
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
  debug( "_ickDeviceAddMessage (%s): output queue has now %d entries (%ld bytes)",
         device->uuid, _ickDevicePendingMessages(device), _ickDevicePendingBytes(device) );
  return ICKERR_SUCCESS;
}


/*=========================================================================*\
  Remove a message container from the output queue
    caller should lock the device
    This will also free the message
\*=========================================================================*/
ickErrcode_t _ickDeviceRemoveAndFreeMessage( ickDevice_t *device, ickMessage_t *message )
{
  ickMessage_t *walk;
  debug( "_ickDeviceRemoveAndFreeMessage (%s): message %p (%ld bytes)",
         device->uuid, message, message->size );

/*------------------------------------------------------------------------*\
    Check if member
\*------------------------------------------------------------------------*/
  for( walk=device->outQueue; walk->next; walk=walk->next )
    if( walk==message )
      break;
  if( !walk ) {
    logerr( "_ickDeviceRemoveMessage (%s): message not member of output queue",
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
    Free payload and descriptor
\*------------------------------------------------------------------------*/
  Sfree( message->payload );
  Sfree( message );

/*------------------------------------------------------------------------*\
    That's all
\*------------------------------------------------------------------------*/
  return ICKERR_SUCCESS;
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
int _ickDevicePendingMessages( ickDevice_t *device )
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
size_t _ickDevicePendingBytes( ickDevice_t *device )
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
                                    END OF FILE
\*=========================================================================*/

