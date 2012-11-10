//
//  ickP2PComm.c
//  ickStreamProto
//
//  Created by Jörg Schwieder on 06.03.12.
//  Copyright (c) 2012 Du!Business GmbH. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libwebsockets.h"

#include "ickDiscovery.h"
#include "ickDiscoveryInternal.h"


#pragma mark - message handling

static ickDiscovery_t * _ick_p2pDiscovery = NULL;

struct _ick_message_struct {
    struct _ick_message_struct * next;
    unsigned char * paddedData;
    size_t size;
    
};


/*static struct _ick_device_struct * __ickGetDevice4socket(struct libwebsocket * wsi) {
    if (!wsi) return NULL;
    
    struct _ick_device_struct * iDev = _ickDeviceGet(NULL);
    while (iDev) {
        if (iDev->wsi == wsi)
            return iDev;
        iDev = iDev->next;
    }
    return NULL;
}*/

static struct _ick_message_struct * __ickGetFirstMessage(struct _ick_device_struct * device) {
    pthread_mutex_lock(device->messageMutex);
    struct _ick_message_struct * message = device->messageOut;
    pthread_mutex_unlock(device->messageMutex);
    return message;
}

static int __ickInsertMessage(struct _ick_device_struct * device, struct _ick_message_struct * newMessage) {
    pthread_mutex_lock(device->messageMutex);
    struct _ick_message_struct * message = device->messageOut;
    newMessage->next = NULL;
    if (!message)
        device->messageOut = newMessage;
    else {
        while (message->next) 
            message = message->next;
        message->next = newMessage;
    }
    
    // insert empty dummy message
    struct _ick_message_struct * emptyMessage = malloc(sizeof(struct _ick_message_struct));
    if (!emptyMessage)
        return 0;
    unsigned char * dummydata = malloc(LWS_SEND_BUFFER_PRE_PADDING + 1 + LWS_SEND_BUFFER_POST_PADDING);
    if (!dummydata) {
        free(emptyMessage);
        return 0;
    }
    dummydata[LWS_SEND_BUFFER_PRE_PADDING] = 0;
    emptyMessage->paddedData = dummydata;
    emptyMessage->next = NULL;
    emptyMessage->size = 1;
    newMessage->next = emptyMessage;

    pthread_mutex_unlock(device->messageMutex);
    return 0;
}

// 0: success
// -1: not found. Still will be deleted!
static int __ickDeleteMessage(struct _ick_device_struct * device, struct _ick_message_struct * oldMessage) {
    pthread_mutex_lock(device->messageMutex);
    struct _ick_message_struct * message = device->messageOut;
    if (message == oldMessage)
        device->messageOut = oldMessage->next;
    else {
        while ((message->next != oldMessage) && (message != NULL)) 
            message = message->next;
        if (message)
            message->next = oldMessage->next;
    }
    pthread_mutex_unlock(device->messageMutex);
    free (oldMessage->paddedData);
    free (oldMessage);
    return (message != NULL) ? 0 : -1;
}


#pragma mark - callback registration

// callback registration for registration

struct _ickMessageCallbacks {
    struct _ickMessageCallbacks * next;
    ickDevice_message_callback_t callback;
};

static struct _ickMessageCallbacks * _ick_MessageCallbacks = NULL;

int ickDeviceRegisterMessageCallback(ickDevice_message_callback_t callback) {
    struct _ickMessageCallbacks * cbTemp = _ick_MessageCallbacks;
    
    while (cbTemp)
        if (cbTemp->callback == callback)
            return -1;
    
    cbTemp = malloc(sizeof(struct _ickMessageCallbacks));
    cbTemp->next = _ick_MessageCallbacks;
    cbTemp->callback = callback;
    _ick_MessageCallbacks = cbTemp;
    return 0;
}

static int _ick_execute_MessageCallback (struct _ick_device_struct * device, void * data, size_t size, enum ickMessage_communicationstate state) {
    struct _ickMessageCallbacks * cbTemp = _ick_MessageCallbacks;
    
    while (cbTemp) {
        cbTemp->callback(device->UUID, data, size, state);
        cbTemp = cbTemp->next;
    }
    return 0;
}





#pragma mark - server

static int _ick_serve_xml_file(struct libwebsocket *wsi) {
	char buf[512];
	char *p = buf;
    
    int l = 0;
    char * xmlfile = NULL;

    l = asprintf(&xmlfile, 
                 "<root>\r\n"
                 " <specVersion>\r\n"
                 "  <major>1</major>\r\n"
                 "  <minor>0</minor>\r\n"
                 " </specVersion>\r\n"
                 " <device>\r\n"
                 "  <deviceType>" ICKDEVICE_TYPESTR_ROOT "</deviceType>\r\n"
                 "  <friendlyName>%s</friendlyName>\r\n"    // name
                 "  <manufacturer>ickStream</manufacturer>\r\n"
                 "  <manufacturerURL>http://ickstream.com</manufacturerURL>\r\n"
                 "  <modelDescription>ickStreamDevice</modelDescription>\r\n"
                 "  <modelName>ickStreamDevice</modelName>\r\n"
                 "  <UDN>uuid:%s</UDN>\r\n"                      // uuid
                 //                 "  <presentationURL>%s</presentationURL>\r\n"  // url
                 " </device>\r\n"
                 "</root>",
                 _ick_p2pDiscovery->friendlyName,
                 _ick_p2pDiscovery->UUID
                 //                 _ick_p2pDiscovery->interface
                 );
    
    if (l < 0) {
        return 1;
    }

    
	p += sprintf(p, "HTTP/1.0 200 OK\x0d\x0a"
                 "Server: libwebsockets\x0d\x0a"
                 "Content-Type: %s\x0d\x0a"
                 "Content-Length: %u\x0d\x0a"
                 "\x0d\x0a", "text/xml", l);
    
	libwebsocket_write(wsi, (unsigned char *)buf, p - buf, LWS_WRITE_HTTP);
    
    libwebsocket_write(wsi, (unsigned char *)xmlfile, l, LWS_WRITE_HTTP);
    
	return 0;
}


enum ick_server_protocols {
	/* always first */
	PROTOCOL_HTTP = 0,
    
    ICK_PROTOCOL_P2PJSON,

	/* always last */
	ICK_PROTOCOL_COUNT
};

/*  this protocol server (always the first one) just knows how to do HTTP
    from libwebsocket sample server
    No functionality, yet, but should be used to serve up user data
 */

static int callback_http(struct libwebsocket_context * context,
                         struct libwebsocket *wsi,
                         enum libwebsocket_callback_reasons reason, void *user,
                         void *in, size_t len)
{
	char client_name[128];
	char client_ip[128];
    
	switch (reason) {
        case LWS_CALLBACK_HTTP:
            // HTTP server would go here. Just put everything to be served up in a folder "LOCAL_RESOURCE_PATH" and probably do some sanity check for the request
            // First application: load XML resource file..... need to change the port in the XML description link since that's currently the ignore port.
            // TODO: Probably want to use the discovery struct to communicate port since it's what's known to P2Pdiscovery
            //
            // We probably want to add stuff like default UPnP icons etc. here
            //
            fprintf(stderr, "serving HTTP URI %s\n", (char *)in);
            
            if (in && ((strcmp(in, "/Root.xml") == 0) ||
                       (strcmp(in, "/Player.xml") == 0) ||
                       (strcmp(in, "/Controller.xml") == 0))) {
                _ick_serve_xml_file(wsi);
                break;
            }

            /*            
            if (in && strcmp(in, "/favicon.ico") == 0) {
                if (libwebsockets_serve_http_file(wsi,
                                                  LOCAL_RESOURCE_PATH"/favicon.ico", "image/x-icon"))
                    fprintf(stderr, "Failed to send favicon\n");
                break;
            }
            ....*/
            
            break;
            
            /*
             * callback for confirming to continue with client IP appear in
             * protocol 0 callback since no websocket protocol has been agreed
             * yet.  You can just ignore this if you won't filter on client IP
             * since the default uhandled callback return is 0 meaning let the
             * connection continue.
             */
            
        case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
            
            // we don't filter, but some logging is always nice....
            
            libwebsockets_get_peer_addresses((int)(long)user, client_name,
                                             sizeof(client_name), client_ip, sizeof(client_ip));
            
            fprintf(stderr, "Received network connect from %s (%s)\n",
                    client_name, client_ip);
            
            // if we returned non-zero from here, we kill the connection
            break;
            
        default:
            break;
	}
    
	return 0;
}

struct __p2p_server_session_data {
    struct _ick_device_struct * device;
    char * bufIn;
    size_t bufLen;
};


// forward declaration
static void *_ickReOpenWebsocket(void * UUID);

//
// Maximum reconnect delay in ms
// 500ms
#define ICK_RECONNECT_DELAY 500

static int
_ick_callback_p2p_server(struct libwebsocket_context * context,
                    struct libwebsocket *wsi,
                    enum libwebsocket_callback_reasons reason,
                    void *user, void *in, size_t len)
{
    int n;
    struct __p2p_server_session_data * pss = user;
    struct _ick_device_struct * device = pss->device;
    
	switch (reason) {
            
        case LWS_CALLBACK_ESTABLISHED:
        case LWS_CALLBACK_CLIENT_ESTABLISHED: {
            char ipbuf[21];
            char namebuf[255];
            libwebsockets_get_peer_addresses(libwebsocket_get_socket_fd(wsi), namebuf, 255, ipbuf, 21);
            if (reason == LWS_CALLBACK_ESTABLISHED)
                fprintf(stderr, "ick_callback_p2p_server: LWS_CALLBACK_ESTABLISHED\n");
            else 
                fprintf(stderr, "ick_callback_p2p_server: LWS_CALLBACK_CLIENT_ESTABLISHED\n");
            pss->bufIn = NULL;
            pss->bufLen = 0;
            pss->device = _ickDevice4wsi(wsi);
            if (!pss->device)
                fprintf(stderr, "LWS_CALLBACK_X_ESTABLISHED, no device found on %s, %s\n", namebuf, ipbuf);
        }
            break;
            
        case LWS_CALLBACK_CLIENT_WRITEABLE: {
            if (_ick_discovery_locked(_ick_p2pDiscovery) == ICK_DISCOVERY_QUIT)
                break;
            
            //            device = __ickGetDevice4socket(wsi);
            if (!device)
                break;
            struct _ick_message_struct * message = __ickGetFirstMessage(device);
            if (message) {
                n = libwebsocket_write(wsi, message->paddedData +
                                       LWS_SEND_BUFFER_PRE_PADDING,
                                       message->size,
                                       LWS_WRITE_TEXT);
                if (n < 0) {
                    fprintf(stderr, "ERROR writing to socket\n");
                }
                __ickDeleteMessage(device, message);
                libwebsocket_callback_on_writable(context, wsi);
            }
        }
            break;
            
        case LWS_CALLBACK_SERVER_WRITEABLE: {
            if (_ick_discovery_locked(_ick_p2pDiscovery) == ICK_DISCOVERY_QUIT)
                break;
            
            //            device = __ickGetDevice4socket(wsi);
            if (!device)
                break;
            struct _ick_message_struct * message = __ickGetFirstMessage(device);
            if (message) {
                n = libwebsocket_write(wsi, message->paddedData +
                                       LWS_SEND_BUFFER_PRE_PADDING,
                                       message->size,
                                       LWS_WRITE_TEXT);
                if (n < 0) {
                    fprintf(stderr, "ERROR writing to socket\n");
                }
                __ickDeleteMessage(device, message);
                libwebsocket_callback_on_writable(context, wsi);
            }
        }
            break;
            
        case LWS_CALLBACK_BROADCAST:
            // no broadcasts right now
/*            n = libwebsocket_write(wsi, in, len, LWS_WRITE_TEXT);
            if (n < 0)
                fprintf(stderr, "mirror write failed\n");*/
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE:
        case LWS_CALLBACK_RECEIVE: {
            //            device = __ickGetDevice4socket(wsi);
            if (!device) {  // might be the looback server socket
                if (_ick_p2pDiscovery && (wsi == _ick_p2pDiscovery->wsi))
                    device = _ickDeviceGet(_ick_p2pDiscovery->UUID);
                if (!device) // still no device found? can't process
                    break;
            }
            // old packet part present?
            if (pss->bufIn) {
                void * tmp = pss->bufIn;
                pss->bufIn = realloc(pss->bufIn, pss->bufLen + len);
                if (!pss->bufIn) {
                    pss->bufLen = 0;
                    free(tmp);
                    break;
                }
                memcpy(pss->bufIn + pss->bufLen, in, len);
                pss->bufLen += len;
            }
            // packet not yet complete?
            size_t r = libwebsockets_remaining_packet_payload (wsi);
            if (r) {
                if (!pss->bufIn) {
                    pss->bufIn = malloc(len);
                    if (!pss->bufIn)
                        break;
                    memcpy(pss->bufIn, in, len);
                    pss->bufLen = len;
                }
            } else {    // complete? call callback
                if (pss->bufIn) {    // we have a concatenated packet?
                    if (pss->bufLen == 1)
                        if (pss->bufIn[0] == 0) // ignore empty packets
                            pss->bufLen = 0;
                    if (pss->bufLen)
                        _ick_execute_MessageCallback(device, pss->bufIn, pss->bufLen, ICKMESSAGE_INCOMING_DATA);
                    free(pss->bufIn);
                    pss->bufIn = NULL;
                    pss->bufLen = 0;
                } else {  // this should be the usual case: return packet in one
                    if (len == 1)
                        if (((unsigned char *)in)[0] == 0)
                            len = 0;
                    if (len)
                        _ick_execute_MessageCallback(device, in, len, ICKMESSAGE_INCOMING_DATA);
                }
                // try to write packet since we assume that the application will at least try to send an acknowledge packet.
                libwebsocket_callback_on_writable(context, wsi);
            }
        }
            break;
            /*
             * this just demonstrates how to use the protocol filter. If you won't
             * study and reject connections based on header content, you don't need
             * to handle this callback
             */
            
        case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION: {
            struct lws_tokens * tokens = (struct lws_tokens *)user;
            // No origin? Can't identify client -> deny
            if (tokens[WSI_TOKEN_SWORIGIN].token == NULL)
                return 1;
            char * UUID = malloc(tokens[WSI_TOKEN_SWORIGIN].token_len + 1);
            strncpy(UUID, tokens[WSI_TOKEN_SWORIGIN].token, tokens[WSI_TOKEN_SWORIGIN].token_len);
            UUID[tokens[WSI_TOKEN_SWORIGIN].token_len] = 0;
            device = _ickDeviceGet(UUID);
            int is_self = !strcmp(UUID, _ick_p2pDiscovery->UUID);
            // we do already have a connecting client or a connected server for this UUID and it's not a loopback then don't connect.
            if (device && device->wsi && !is_self)
                return 1;
            // if this is a loopback, the server uses the discovery struct to store the wsi; need the device strcut for the client side of the connection
            if (is_self) {
                if (_ick_p2pDiscovery->wsi)
                    return 1;
                else
                    _ick_p2pDiscovery->wsi = wsi;
            } else if (device && !device->wsi) // we did find the device but it doesn't have a wsi yet, use it. Should be a bit uncommon, we should usually connect a client when we find a device so I'd expect this to be either occupied by a wsi or the device not yet found....
                device->wsi = wsi;
            else {      // otherwise create a device for this connection
                char * URL = NULL;
                if (tokens[WSI_TOKEN_HOST].token_len) {
                    URL = malloc(tokens[WSI_TOKEN_HOST].token_len + 1);
                    strncpy(URL, tokens[WSI_TOKEN_HOST].token, tokens[WSI_TOKEN_HOST].token_len);
                    URL[tokens[WSI_TOKEN_HOST].token_len] = 0;
                }
                device = _ickDeviceCreateNew(UUID, URL, NULL, 0, wsi);
            }
        }
            break;
        
        case LWS_CALLBACK_CLOSED: {
            // can't process input buffer anymore
            if (pss->bufIn) {
                free(pss->bufIn);
                pss->bufIn = NULL;
                pss->bufLen = 0;
            }
            
            if (device)
                fprintf(stderr, "LWS_CALLBACK_CLOSED device: %s on %s\n", device->UUID, device->URL);
            else {
                fprintf(stderr, "LWS_CALLBACK_CLOSED loopback?\n");
                if (_ick_p2pDiscovery && (wsi == _ick_p2pDiscovery->wsi)) {
                    device = _ickDeviceGet(_ick_p2pDiscovery->UUID);
                    _ick_p2pDiscovery->wsi = NULL;
                }
                if (!device) // still no device found? can't process
                    break;
            }
            // A bit risky... but this is how we cover loopback for now: delete the wsi whenever one of the two ends fails
            // should not be any issue in other cases
            //            if (wsi == device->wsi)
                device->wsi = NULL;
            // OK, try to re-establish as long as the device is still there
            // and wait 
            // forget about this socket
            // let's hope these pointer operations are really atomic....
            device->wsi = NULL;
            char * UUID = strdup(device->UUID);
            pthread_t mythread;
            pthread_create(&mythread, NULL, _ickReOpenWebsocket, UUID);
        }
            break;
            
        default:
            debug("connection code %d\n", reason);
            break;
	}
    
	return 0;
}

/* list of supported protocols and callbacks */

static struct libwebsocket_protocols __protocols[] = {
	/* first protocol must always be HTTP handler */
    
	{
		"http-only",		/* name */
		callback_http,		/* callback */
		0			/* per_session_data_size */
	},
	{
		"ickstream-p2p-message-protocol",
		_ick_callback_p2p_server,
		sizeof(struct __p2p_server_session_data)
	},
	{
		NULL, NULL, 0		/* End of list */
	}
};

static struct libwebsocket_context * __context = NULL;
static int __quit_thread = 0;
static pthread_t __service_thread;
static int __port;

// 0.2ms
#define ICK_SLEEP_INTERVAL 200

static void * __ickP2PServiceThread(void * dummy) {
    while (!__quit_thread) {
        libwebsocket_service(__context, 100);
        libwebsocket_callback_on_writable_all_protocol(&(__protocols[ICK_PROTOCOL_P2PJSON]));
        usleep(ICK_SLEEP_INTERVAL);
    }
    __quit_thread = 0;
    if (_ick_p2pDiscovery->exitCallback)
        _ick_p2pDiscovery->exitCallback();
    return NULL;
}


static void __ickOpenWebsocket(struct _ick_device_struct * device) {
    char * URL = NULL;
    unsigned short port = __port;
    // loopback?
    if (strcmp(device->UUID, _ick_p2pDiscovery->UUID)) {
        // no - use data
        URL = strdup(device->URL);
        port = device->port;
        if (!port)
            port = WEBSOCKET_PORT;
    } else
        URL = strdup("127.0.0.1");
    device->wsi = libwebsocket_client_connect(__context, 
                                              URL,                                                      
                                              port,
                                              0,
                                              "/",
                                              _ick_p2pDiscovery->location,
                                              _ick_p2pDiscovery->UUID,
                                              __protocols[ICK_PROTOCOL_P2PJSON].name,
                                              -1);
    
    free(URL);    
}

//
// this will re-connect a broken websocket.
// wait a random time between 50-100ms to avoid concurrency in case of temporary network outages
// runs in a separate thread

static void *_ickReOpenWebsocket(void * UUID) {
    // us but half the time...
    long delay = ICK_RECONNECT_DELAY * 500;
    delay += (random() % delay);
    usleep(delay);
    
    // device still there and not connected? Reconnect
    struct _ick_device_struct * device = _ickDeviceGet(UUID);
    if (device && !device->wsi)
        __ickOpenWebsocket(device);
    free(UUID);
    return NULL;
}

static int _wantToConnect(enum ickDevice_servicetype myType, enum ickDevice_servicetype otherType) {
    // I'm a controller, so I want to connect to servers and players, not other controllers
    if (myType & ICKDEVICE_CONTROLLER) {
        if (otherType & ICKDEVICE_SERVER_GENERIC)
            return 1;
        if (otherType & ICKDEVICE_PLAYER)
            return 1;
    }
    // I'm a player, so I want to connect to controllers and servers
    if (myType & ICKDEVICE_PLAYER) {
        if (otherType & ICKDEVICE_SERVER_GENERIC)
            return 1;
        if (otherType & ICKDEVICE_CONTROLLER)
            return 1;
    }
    // I'm a server, so I want to connect to players and controllers
    // TBD: DO we also want to connect to other servers?
    if (myType & ICKDEVICE_SERVER_GENERIC) {
        if (otherType & ICKDEVICE_PLAYER)
            return 1;
        if (otherType & ICKDEVICE_CONTROLLER)
            return 1;
    }
        
    // I idn't find a reason to connect? So don't...
    return 0;
}


static void _ickOpenDeviceWebsocket(const char * UUID, enum ickDiscovery_command change, enum ickDevice_servicetype type) {
    // This is too simplicstic Controllers should connect to players but not other players
#ifdef SUPPORT_ICK_SERVERS
    if (!_wantToConnect(_ick_p2pDiscovery->services, type))
        return;
#else
    if (!(type & ICKDEVICE_PLAYER) ||
        !(_ick_p2pDiscovery->services & ICKDEVICE_CONTROLLER))
        return;
#endif
    switch (change) {
        case ICKDISCOVERY_ADD_DEVICE: {
            struct _ick_device_struct * device = _ickDeviceGet(UUID);
            if (!device || device->wsi)
                break;
            __ickOpenWebsocket(device);
        }
            break;
            
        default:
            break;
    }
}

//
// this connects all known players
// needed in situations where we only become a controller after the players are already detected 
void _ickConnectUnconnectedPlayers(void) {
    struct _ick_device_struct * device = _ickDeviceGet(NULL);
    while (device) {
#ifdef SUPPORT_ICK_SERVERS
        if (((device->type & ICKDEVICE_PLAYER) || (device->type & ICKDEVICE_SERVER_GENERIC)) && (device->wsi == NULL))
#else
            if ((device->type & ICKDEVICE_PLAYER) && (device->wsi == NULL))
#endif
        device = device->next;
    }
}

static unsigned short __findFreePort(void) {
    int serverfd;
    serverfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in channel;
    memset(&channel, 0, sizeof(channel));
    channel.sin_family = AF_INET;
    channel.sin_addr.s_addr = INADDR_ANY;
    bind(serverfd, (struct sockaddr *) &channel, sizeof(channel));
    
    socklen_t channellen;
    getsockname(serverfd, (struct sockaddr *) &channel, &channellen);
    unsigned short rport = ntohs(channel.sin_port);

    int yes = 1;
	setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, (void *)&yes, sizeof(yes));
/*    struct linger linger;
    linger.l_onoff = 1;
    linger.l_linger = 5;
    setsockopt(serverfd, SOL_SOCKET, SO_LINGER, &linger, sizeof(linger));*/
    
    shutdown(serverfd, SHUT_RDWR);
    close(serverfd);
    if (!rport)
        rport = WEBSOCKET_PORT;
    return rport;
}

int _ickInitP2PComm (struct _ick_discovery_struct * disc, int port) {
    _ick_p2pDiscovery = disc;
        
    __port = (port) ? port: __findFreePort();
    _ick_p2pDiscovery->websocket_port = __port;
    __context = libwebsocket_create_context(__port, NULL, __protocols, libwebsocket_internal_extensions, NULL, NULL, -1, -1, 0);
    if (__context == NULL)
        return -1;
    
    if (pthread_create(&__service_thread, NULL, __ickP2PServiceThread, NULL)) {
		debug("error creating serviceing thread\n");
		return -1;
    }
    ickDeviceRegisterDeviceCallback(&_ickOpenDeviceWebsocket);
    
    return 0;
}

int _ickCloseP2PComm(int wait) {
    __quit_thread = 1;
    if (wait)
        pthread_join(__service_thread, NULL);
    libwebsocket_context_destroy(__context);
    return 0;
}

// send a message to device UUID
enum ickMessage_communicationstate ickDeviceSendMsg(const char * UUID, const void * message, const size_t message_size) {
    
    struct _ick_device_struct * device = _ickDeviceGet(UUID);
    if (!device)
        return ICKMESSAGE_UNKNOWN_TARGET;
    if (UUID && strcmp(UUID, device->UUID))
        return ICKMESSAGE_UNKNOWN_TARGET;
    
    while (device) {
        struct _ick_message_struct * newMessage = malloc(sizeof(struct _ick_message_struct));
        if (!newMessage)
            return ICKMESSAGE_COULD_NOT_SEND;
        unsigned char * data = malloc(LWS_SEND_BUFFER_PRE_PADDING + message_size + LWS_SEND_BUFFER_POST_PADDING);
        if (!data) {
            free(newMessage);
            return ICKMESSAGE_COULD_NOT_SEND;
        }
        newMessage->paddedData = data;
        memcpy(newMessage->paddedData + LWS_SEND_BUFFER_PRE_PADDING, message, message_size);
        newMessage->next = NULL;
        newMessage->size = message_size;
        
        __ickInsertMessage(device, newMessage);
        
        if (UUID)
            return ICKMESSAGE_SUCCESS;
        device = device->next;
    }
    return ICKMESSAGE_SUCCESS;
}




