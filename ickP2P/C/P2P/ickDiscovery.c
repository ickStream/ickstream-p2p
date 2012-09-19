//
//  ickDiscovery.c
//  ickStreamP2P
//
//  Created by Jörg Schwieder on 16.01.12.
//  Copyright (c) 2012 Du!Business GmbH. All rights reserved.
//
//  Basic device discovery handling. The code in this file sets up a discovery socket and handles incoming messages. 
//  The messages are then dispatched to callback functions, currently going into the Registry in ickDiscoveryRegistry
//  There are also interface functions to send discoevry messages and queries.
//

#include <sys/utsname.h>

#include "ickDiscovery.h"
#include "ickDiscoveryInternal.h"
#include "openssdpsocket.h"
#include "upnputils.h"



static int ickDiscoveryInitService(void);


/*enum ickDiscovery_result {
    ICKDISCOVERY_SUCCESS            = 0,
    ICKDISCOVERY_RUNNING            = 1,
    ICKDISCOVERY_SOCKET_ERROR       = 2,
    ICKDISCOVERY_THREAD_ERROR       = 3
};*/
/*enum ICKDISCOVERY_SSDP_TYPES {
    ICKDISCOVERY_TYPE_NOTIFY,
    ICKDISCOVERY_TYPE_SEARCH,
    ICKDISCOVERY_TYPE_RESPONSE,
 
    ICKDISCOVERY_SSDP_TYPES_COUNT
};*/


// Discovery thread setup


// the discovery handler is fully configurable but currently only a singleton instance for default discovery handling is being used
/*struct _ick_discovery_struct {
    int         lock;
    pthread_t   thread;
    int         socket;
    
    char *      UUID;
    
    receive_callback_t * receive_callbacks;
};*/

//
// Create and open discovery thread.
// Shall only be called from main thread
//

void * _ickDiscovery_poll_thread(void * _disc);

static enum ickDiscovery_result _ickInitDiscovery(ickDiscovery_t * discovery) {
    // discovery already active
    if (_ick_discovery_locked(discovery) == ICK_DISCOVERY_LOCKED)
        return ICKDISCOVERY_RUNNING;
    _ick_lock_discovery(discovery);
    
    int udpSocket;
    
/*    udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == -1) {
        _ick_unlock_discovery(discovery);
        return ICKDISCOVERY_SOCKET_ERROR;
    }
    struct sockaddr_in inaddr;
    memset((char *) &inaddr, 0, sizeof(inaddr));
    inaddr.sin_family = AF_INET;
    inaddr.sin_port = htons(1900);
    inaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    int yes = 1;
	int err1 = setsockopt(udpSocket, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(int));
	setsockopt(udpSocket, SOL_SOCKET, SO_REUSEADDR, (void *)&yes, sizeof(yes));
	setsockopt(udpSocket, SOL_SOCKET, SO_NOSIGPIPE, (void *)&yes, sizeof(int));
    
    if (err1) {
        debug("ickDiscovery: error setting broadcast sockopt: %d", err1);
        _ick_unlock_discovery(discovery);
        return ICKDISCOVERY_SOCKET_ERROR;
    }
    
    struct in_addr mc_if;
    mc_if.s_addr = htonl(INADDR_ANY);
    //    inaddr.sin_addr.s_addr = mc_if.s_addr;
    if(setsockopt(udpSocket, IPPROTO_IP, IP_MULTICAST_IF, (const char *)&mc_if, sizeof(mc_if)) < 0)
    {
        debug("ickDiscovery: error setting multicast sockopt", err1);
        _ick_unlock_discovery(discovery);
        return ICKDISCOVERY_SOCKET_ERROR;
    }

    if (bind(udpSocket, &inaddr, sizeof(inaddr))) {
        debug("ickDiscovery: error binding socket");
        _ick_unlock_discovery(discovery);
        return ICKDISCOVERY_SOCKET_ERROR;        
    }*/
    
    const char * interfaces[2] = { discovery->interface, LOCALHOST_ADDR };
    
    udpSocket = OpenAndConfSSDPReceiveSocket(2, interfaces, 0);
    if (udpSocket <= 0) {
        debug("ickDiscovery: error setting broadcast sockopt");
        _ick_unlock_discovery(discovery);
        return ICKDISCOVERY_SOCKET_ERROR;        
    }

    discovery->socket = udpSocket;
    
    if (pthread_create(&(discovery->thread), NULL, _ickDiscovery_poll_thread, discovery)) {
        _ick_unlock_discovery(discovery);
        return ICKDISCOVERY_THREAD_ERROR;
    }
    
    return ICKDISCOVERY_SUCCESS;
}

// use singleton
/*struct _ick_discovery_struct {
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
};*/


static ickDiscovery_t _ick_discovery = {    
    0,  // lock
    0,  // thread
    0,  // socket
    0,  // websocket_port
    
    NULL, // UUID
    NULL, // interface
    NULL, // location
    NULL, // osname
    ICKDEVICE_GENERIC, // services
    
    NULL,   // friendlyName
    NULL,   // serverFolder
    
    NULL,   // wsi
    
    NULL,   // receive_callbacks
    NULL    // exitCallback
};

enum ickDiscovery_result ickInitDiscovery(const char * UUID, const char * interface, ickDiscovery_discovery_exit_callback_t exitCallback) {
    _ick_discovery.exitCallback = exitCallback;
    _ick_discovery.UUID = malloc(strlen(UUID) + 1);
    _ick_discovery.interface = malloc(strlen(interface) + 1);
    _ick_discovery.receive_callbacks = malloc(sizeof(struct _ick_callback_list));
    _ick_discovery.friendlyName = strdup("ickStreamDevice");
    _ick_discovery.serverFolder = NULL;
    if (!_ick_discovery.interface) {
        free (_ick_discovery.UUID); _ick_discovery.UUID = NULL;
        free (_ick_discovery.interface); _ick_discovery.interface = NULL;
        free (_ick_discovery.receive_callbacks); _ick_discovery.receive_callbacks = NULL;
        free (_ick_discovery.location); _ick_discovery.location = NULL;
        free (_ick_discovery.osname); _ick_discovery.osname = NULL;
        
        return ICKDISCOVERY_MEMORY_ERROR;
    }
    strcpy(_ick_discovery.UUID, UUID);
    strcpy(_ick_discovery.interface, interface);
                                      
    // add default callbacks here...    
    _ick_discovery.receive_callbacks->callback = _ick_receive_notify;
    _ick_discovery.receive_callbacks->next = NULL;
    
    // This _should_ be called only once...
    if (ickDiscoveryInitService())
        return ICKDISCOVERY_MEMORY_ERROR;
    
    return _ickInitDiscovery(&_ick_discovery);
}


//
// shut down discovery
// if wait is nonzero ickEndDiscovery will block until thread finishes
// Shall only be called from main thread!
//

static void _ickEndDiscovery(ickDiscovery_t *discovery, int wait) {
    if (!discovery)
        return;
    //    if (!_ick_discovery_locked(discovery))
    //        return;
    _ick_quit_discovery(discovery);
    
    // the discovery thread does block on receiving messages from the socket, so we need to close it to make sure the thread ends
    shutdown(discovery->socket, 2);
    close(discovery->socket);
    
    if (!discovery->thread)
        return;
    
    // wait for discovery thread to end.
    if (wait)
        pthread_join(discovery->thread, NULL);
}

// use singleton

void ickEndDiscovery(int wait) {
    _ick_close_discovery_registry(wait);
    _ickCloseP2PComm(wait);
    
    _ickEndDiscovery(&_ick_discovery, wait);
    free(_ick_discovery.UUID);
    _ick_discovery.UUID = NULL;
    free(_ick_discovery.interface);
    _ick_discovery.interface = NULL;
}


//
// Discovery poll thread
// Reads incoming information and executes protocol handlers
//      - answers to device queries
//      - device announcement messages
//      - ickStream discovery instance initialization requests
//      - DLNA server announcements/query responses
//      - disconnect messages
//

#define ICKDISCOVERY_HEADER_SIZE_MAX 1536

/*static char * _ickDiscovery_headlines[ICKDISCOVERY_SSDP_TYPES_COUNT] = {
    "NOTIFY * HTTP/1.1\r\n",    // ICKDISCOVERY_TYPE_NOTOFY
    "M-SEARCH * HTTP/1.1\r\n",  // ICKDISCOVERY_TYPE_SEARCH
    "HTTP/1.1 200 OK\r\n"       // ICKDISCOVERY_TYPE_RESPONSE
};*/

#define _DEBUG_STRLEN   20

void * _ickDiscovery_poll_thread (void * _disc) {
    ickDiscovery_t * discovery = _disc;
    char *buffer = malloc(ICKDISCOVERY_HEADER_SIZE_MAX);
    if (!buffer)
        return NULL;
    struct sockaddr address;
    socklen_t addrlen;
    //    char addrstr[_DEBUG_STRLEN];
    //    memset(addrstr, 0, _DEBUG_STRLEN);
    ssize_t rcv_size = 0;
    
    while (_ick_discovery_locked(discovery) != ICK_DISCOVERY_QUIT) {
        addrlen = sizeof(address);
        //        debug("\nstart receiving Data\n");
        memset(buffer, 0, ICKDISCOVERY_HEADER_SIZE_MAX);
        
        struct timeval timeout = {1, 0};  // make select() return once per second
        
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(discovery->socket, &readSet);
        
        if (select(discovery->socket+1, &readSet, NULL, NULL, &timeout) >= 0)
        {
            if (FD_ISSET(discovery->socket, &readSet))
            {
                rcv_size = recvfrom(discovery->socket, buffer, ICKDISCOVERY_HEADER_SIZE_MAX, 0, &address, &addrlen);
                ParseSSDPPacket(discovery, buffer, rcv_size, &address);
            }
        }
        usleep(200);
        
        // receive callbacks might be added dynamically later. So they might initially be NULL, in this case, ignore the message
/*        if (!discovery->receive_callbacks)
            continue;
        
        sockaddr_to_string(&address, addrstr, _DEBUG_STRLEN);
        debug("Message:\n%s from \n %s\n", buffer, addrstr);

        for (enum ICKDISCOVERY_SSDP_TYPES e = ICKDISCOVERY_TYPE_NOTIFY; e < ICKDISCOVERY_SSDP_TYPES_COUNT; e++) {
            if (!strncmp(buffer, _ickDiscovery_headlines[e], strlen(_ickDiscovery_headlines[e]))) {
                // Do we have a callback for this message (again... dynamically...)
                if (discovery->receive_callbacks[e])
                    discovery->receive_callbacks[e](buffer, &address, addrlen);
                break;
            }
        }*/
    }
    
    close(discovery->socket);
    
    if (discovery->exitCallback)
        discovery->exitCallback();
    
    return NULL;
}



static int ickDiscoveryInitService(void) {
    if ((_ick_discovery.UUID == NULL) || (_ick_discovery.interface == NULL))
        return -1;
    
    // this is ugly.... get an IP address string from the interface...
    // OK, let's de-uglyfy it a bit by first checking whether it maybe already _is_ an IP string...
    char * inaddr_s = NULL;
    in_addr_t inaddr = inet_addr(_ick_discovery.interface);
	if(inaddr != INADDR_NONE) {
		inaddr_s = _ick_discovery.interface;
	} else {
        inaddr = GetIfAddrIPv4(_ick_discovery.interface);
        //        if (inaddr == INADDR_NONE) 
        //            return -1;
        struct in_addr s_inaddr;
        s_inaddr.s_addr = inaddr;
        inaddr_s = inet_ntoa(s_inaddr);
    }
    if (!inaddr_s)
        inaddr_s = "0.0.0.0";
    //        return -1;
    
    _ick_discovery.location = malloc(strlen(inaddr_s) + 1);
    if (!_ick_discovery.location)
        return -1;
    strcpy(_ick_discovery.location, inaddr_s);
    
    struct utsname name;
	if(!uname(&name))
        asprintf(&_ick_discovery.osname, "%s/%s", name.sysname, name.release);
    if (!_ick_discovery.osname)
        asprintf(&_ick_discovery.osname, "Generic/1.0");
    
    _ickInitP2PComm(&_ick_discovery, 0); // WEBSOCKET_PORT
    _ick_init_discovery_registry(&_ick_discovery);
    return 0;
}

//
// Add/remove capabilities
// Adds embedded devices or services to the current root device
//
// TBD: define services, currently only device types for player and controller are defined

int ickDiscoveryAddService(enum ickDevice_servicetype type) {
    if ((_ick_discovery.UUID == NULL) || (_ick_discovery.interface == NULL) ||
        (_ick_discovery.location == NULL) || (_ick_discovery.osname == NULL))
        return -1;

    char * server_name;
    asprintf(&server_name, ICKDEVICE_TYPESTR_SERVERSTRING, _ick_discovery.osname);
    
    char * location;
    char * usn;
    
    // Add player - only if not already present
    if (!(_ick_discovery.services & ICKDEVICE_PLAYER) && (type & ICKDEVICE_PLAYER)) {
        asprintf(&location, ICKDEVICE_TYPESTR_LOCATION, _ick_discovery.location, _ick_discovery.websocket_port, ICKDEVICE_STRING_PLAYER);
        asprintf(&usn, ICKDEVICE_TYPESTR_USN, _ick_discovery.UUID, ICKDEVICE_TYPESTR_PLAYER);
        _ick_add_service(ICKDEVICE_TYPESTR_PLAYER, usn, server_name, location);
        free(location);
        free(usn);
    }
    if (!(_ick_discovery.services & ICKDEVICE_SERVER_GENERIC) && (type & ICKDEVICE_SERVER_GENERIC)) {
        asprintf(&location, ICKDEVICE_TYPESTR_LOCATION, _ick_discovery.location, _ick_discovery.websocket_port, ICKDEVICE_STRING_SERVER);
        asprintf(&usn, ICKDEVICE_TYPESTR_USN, _ick_discovery.UUID, ICKDEVICE_TYPESTR_SERVER);
        _ick_add_service(ICKDEVICE_TYPESTR_SERVER, usn, server_name, location);
        free(location);
        free(usn);
    }    
    
    //  OK, let's not add controllers - no reason to do so.
    // But registering a controller should make us connect to known players...
    // Add controller - only if not already present
/*    if (!(_ick_discovery.services & ICKDEVICE_CONTROLLER) && (type & ICKDEVICE_CONTROLLER)) {
        asprintf(&location, ICKDEVICE_TYPESTR_LOCATION, inaddr_s, ICKDEVICE_STRING_CONTROLLER);
        asprintf(&usn, ICKDEVICE_TYPESTR_USN, _ick_discovery.UUID, ICKDEVICE_TYPESTR_CONTROLLER);
        _ick_add_service(ICKDEVICE_TYPESTR_CONTROLLER, usn, server_name, location);
        free(location);
        free(usn);
    }*/
    _ick_discovery.services |= type;
    
    free(server_name);
    
    return 0;
}

int ickDiscoveryRemoveService(enum ickDevice_servicetype type) {
    // Remove player
    if (type & ICKDEVICE_PLAYER)
        _ick_remove_service(ICKDEVICE_TYPESTR_PLAYER, true);
#ifdef SUPPORT_ICK_SERVERS
    // Remove server
    if (type & ICKDEVICE_SERVER_GENERIC)
        _ick_remove_service(ICKDEVICE_TYPESTR_SERVER, true);
#endif
    // Remove controller
    if (type & ICKDEVICE_CONTROLLER)
        _ick_remove_service(ICKDEVICE_TYPESTR_CONTROLLER, true);
    _ick_discovery.services &= ~type;
    
    return 0;
}

enum ickDiscovery_result ickDiscoverySetupConfigurationData(const char * defaultDeviceName, const char * dataFolder) {
    if (defaultDeviceName) {
        if (_ick_discovery.friendlyName)
            free(_ick_discovery.friendlyName);
        _ick_discovery.friendlyName = strdup(defaultDeviceName);
        if (!_ick_discovery.friendlyName)
            return ICKDISCOVERY_MEMORY_ERROR;
    }
    if (dataFolder) {
        if (_ick_discovery.serverFolder)
            free(_ick_discovery.serverFolder);
        _ick_discovery.serverFolder = strdup(dataFolder);
        if (!_ick_discovery.serverFolder)
            return ICKDISCOVERY_MEMORY_ERROR;
    }
    if (defaultDeviceName || dataFolder)
        _ick_notifications_send(ICK_SEND_CMD_NOTIFY_ADD, NULL);
    return ICKDISCOVERY_SUCCESS;
}




