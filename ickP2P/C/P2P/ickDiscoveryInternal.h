//
//  ickDiscoveryInternal.h
//  ickStreamProto
//
//  Created by Jörg Schwieder on 13.02.12.
//  Copyright (c) 2012 Du!Business GmbH. All rights reserved.
//

#ifndef ickStreamProto_ickDiscoveryInternal_h
#define ickStreamProto_ickDiscoveryInternal_h

#include <stdio.h>
//#include <ifaddrs.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <sys/queue.h>
#include <ctype.h>

#include "libwebsockets.h"

#ifdef DEBUG
static inline
void debug(const char *format, ...)
{
	va_list ap;
	va_start(ap, format); vfprintf(stderr, format, ap); va_end(ap);
}
#else
static inline
void debug(const char *format, ...)
{
}
#endif


//
// General utility tristate type...
//
typedef enum _ickTristate {
    ICK_UNKNOWN = -1,
    ICK_NO = 0,
    ICK_YES = 1
} ickTristate;


/*
 * Mac OSX as well as iOS do not define the MSG_NOSIGNAL flag,
 * but happily have something equivalent in the SO_NOSIGPIPE flag.
 */
#ifdef __APPLE__
#define MSG_NOSIGNAL SO_NOSIGPIPE 
#endif


// Some UPNP Standards

#define XSTR(s) STR(s)
#define STR(s) #s

#define UPNP_PORT       1900
#define UPNP_MCAST_ADDR "239.255.255.250"
#define LOCALHOST_ADDR  "127.0.0.1"
#define SUPPORT_ICK_SERVERS 1

struct _upnp_device;

// callback functions for received discovery messages have this type
typedef void (* receive_callback_t)(const struct _upnp_device * device, enum ickDiscovery_command);


struct _ick_callback_list {
    struct _ick_callback_list * next;
    receive_callback_t callback;
};


// message lsit struct for websocket communication, from ickP2PComm.c

struct _ick_message_struct;

//
// Consolidated linked list for ickDevices
// renamed from _reference_list for consistency
//
// no more redundant 3rd list managing the connections
// Contains necessary data for both client and server side connections since each ickStream device only has a client or server link except for loopback
// Will continue to work until we use more than one socket per connection, then we probably need an array or something.
// Loopback is a special case: here only the client side is stored in the reference list, server side goes to _ick_discovery_struct
//


struct _ick_device_struct {
    struct _ick_device_struct * next;
    
    enum ickDevice_servicetype type;
    char * UUID;
    char * URL;
    unsigned short port;
    
    char * name;
    
    struct libwebsocket * wsi;
    struct _ick_message_struct * messageOut;
    pthread_mutex_t * messageMutex;
        
    void *  element;
    void *  xmlData;
};

// get an element
struct _ick_device_struct * _ickDeviceGet(const char * UUID);
struct _ick_device_struct * _ickDevice4wsi(struct libwebsocket * wsi);



//
// strcut defining the discovery handler.
// consolidated to contain connection information.
// holds socket for server side loopback connection
//

struct _ick_discovery_struct {
    int         lock;
    pthread_t   thread;
    int         socket;
    unsigned short         websocket_port;
    
    char *      UUID;
    char *      interface;
    char *      location;
    char *      osname;
    enum ickDevice_servicetype services;
    
    char *      friendlyName;
    char *      serverFolder;
    
    struct libwebsocket * wsi; // server side loopback connection.
    
    struct _ick_callback_list * receive_callbacks;
    ickDiscovery_discovery_exit_callback_t exitCallback;
};


// use simple lock for quitting thread; discovery thread is a singleton right now

// May change lock handling to something more sensible if needed... change here
#define ICK_DISCOVERY_UNLOCKED  0
#define ICK_DISCOVERY_LOCKED    1
#define ICK_DISCOVERY_QUIT      -1
static inline void _ick_unlock_discovery(ickDiscovery_t * discovery) {
    discovery->lock = ICK_DISCOVERY_UNLOCKED;
}
static inline void _ick_lock_discovery(ickDiscovery_t * discovery) {
    discovery->lock = ICK_DISCOVERY_LOCKED;
}
static inline void _ick_quit_discovery(ickDiscovery_t * discovery) {
    discovery->lock = ICK_DISCOVERY_QUIT;
}
static inline int _ick_discovery_locked(ickDiscovery_t * discovery) {
    return discovery->lock;
}



//
//  Functions
//


// receiver callsbacks for discovery messages
// in ickDiscoveryRegistry.c

extern void _ick_receive_notify(const struct _upnp_device * device, enum ickDiscovery_command cmd);



// minissdp modified functions and internal maintenance

extern int ParseSSDPPacket(const struct _ick_discovery_struct * discovery, const char * p, ssize_t n, const struct sockaddr * addr);

/* 
 from minisspd
 device data structures */
struct _header_string {
	const char * p; /* string pointer */
	int l;          /* string length */
};

#define HEADER_NT	0
#define HEADER_USN	1
#define HEADER_LOCATION	2

// UPnP devices found

struct _upnp_device {
	struct _upnp_device * next;
	time_t t;                 /* validity time */
	struct _header_string headers[3]; /* NT, USN and LOCATION headers */
	char data[];
};


/* Services stored for answering to M-SEARCH
   For ickStream, devices are registered like services, but if the service is "server" more than one service per device may have to be registered */
struct _upnp_service {
	char * st;	/* Service type */
	char * usn;	/* Unique identifier */
	char * server;	/* Server string */
	char * location;	/* URL */
	LIST_ENTRY(_upnp_service) entries;
};


#define LIST_VALIDATE_PRESENT(head, iterator, object, field) do { \
iterator = LIST_FIRST(head); \
while (iterator) { \
if ((iterator) == (object)) \
break; \
(iterator) = LIST_NEXT((iterator), field); \
} \
(object) = (iterator); \
} while (0)

// device type strings

#define ICKDEVICE_TYPESTR_MISC          "urn:schemas-ickstream-com:device:"
#define ICKDEVICE_TYPESTR_ROOT          "urn:schemas-ickstream-com:device:Root:1"
#define ICKDEVICE_TYPESTR_PLAYER        "urn:schemas-ickstream-com:device:Player:1"
#define ICKDEVICE_TYPESTR_SERVER        "urn:schemas-ickstream-com:device:Server:1"
//#define ICKDEVICE_TYPESTR_PLAYER        "urn:schemas-upnp-org:device:MediaRenderer:1"
#define ICKDEVICE_TYPESTR_CONTROLLER    "urn:schemas-ickstream-com:device:Controller:1"
#define ICKDEVICE_STRING_PLAYER         "Player"
#define ICKDEVICE_STRING_SERVER         "Server"
#define ICKDEVICE_STRING_CONTROLLER     "Controller"
#define ICKDEVICE_STRING_ROOT           "Root"

#define ICKDEVICE_TYPESTR_USN           "uuid:%s::%s"       // 1st string: UUID, 2nd string: device URN
#define ICKDEVICE_TYPESTR_LOCATION      "http://%s:%d/%s.xml"       // Port 9 is "discard". We need to replace this with something sensible once we enable description XML downloads

#define ICKDEVICE_TYPESTR_SERVERSTRING  "SERVER: %s UPnP/1.1 ickStream/1.0"


/* Commands to be used to feed the notification and search message queue */

enum _ick_send_cmd {
    ICK_SEND_CMD_NONE,
    ICK_SEND_QUIT,
    ICK_SEND_CMD_SEARCH,
    ICK_SEND_CMD_NOTIFY_REMOVE,
    ICK_SEND_CMD_NOTIFY_PERIODICADD,
    ICK_SEND_CMD_NOTIFY_PERIODICSEARCH,
    ICK_SEND_CMD_NOTIFY_ADD
};



void _ick_init_discovery_registry(ickDiscovery_t * _ick_discovery);
void _ick_close_discovery_registry (int wait);

int _ick_add_service (const char * st, const char * usn, const char * server, const char * location);
int _ick_remove_service(const char * st, bool lock);
int _ick_notifications_send (enum _ick_send_cmd command, struct _upnp_service * service);

struct _ick_device_struct * _ickDeviceCreateNew(char * UUID, char * URL, void * element, enum ickDevice_servicetype type, struct libwebsocket * wsi);

int _ickInitP2PComm (struct _ick_discovery_struct * disc, int port);
int _ickCloseP2PComm(int wait);
void _ickConnectUnconnectedPlayers(void);


#endif
