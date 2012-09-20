//
//  ickDiscoveryRegistry.c
//  ickStreamProto
//
//  Created by Jörg Schwieder on 21.01.12.
//  Copyright (c) 2012 Du!Business GmbH. All rights reserved.
//

#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <fcntl.h>

#include "ickDiscovery.h"
#include "ickDiscoveryInternal.h"

#include "libwebsockets.h"
#include "miniwget.h"
#include "minixml.h"

static struct _ick_device_struct * _ickStreamDevices = NULL;
static pthread_mutex_t _device_mutex = PTHREAD_MUTEX_INITIALIZER;
static ickDiscovery_t * _discovery = NULL;

// callback registration for registration

struct _ickDeviceCallbacks {
    struct _ickDeviceCallbacks * next;
    ickDiscovery_device_callback_t callback;
};

static struct _ickDeviceCallbacks * _ick_DeviceCallbacks = NULL;

int ickDeviceRegisterDeviceCallback(ickDiscovery_device_callback_t callback) {
    struct _ickDeviceCallbacks * cbTemp = _ick_DeviceCallbacks;
    
    while (cbTemp)
        if (cbTemp->callback == callback)
            return -1;
        else
            (cbTemp = cbTemp->next);
    
    cbTemp = malloc(sizeof(struct _ickDeviceCallbacks));
    cbTemp->next = _ick_DeviceCallbacks;
    cbTemp->callback = callback;
    _ick_DeviceCallbacks = cbTemp;
    return 0;
}


static int _ick_execute_DeviceCallback (struct _ick_device_struct * device, enum ickDiscovery_command change) {
    struct _ickDeviceCallbacks * cbTemp = _ick_DeviceCallbacks;

    while (cbTemp) {
        cbTemp->callback(device->UUID, change, device->type);
        cbTemp = cbTemp->next;
    }
    return 0;
}

struct _ick_device_struct * _ickDeviceGet(const char * UUID) {
    if (!UUID)
        return _ickStreamDevices;
    struct _ick_device_struct * iDev = _ickStreamDevices;
    
    while (iDev) {
        if (iDev->UUID)
            if (!strcasecmp(UUID, iDev->UUID))
                return iDev;
        iDev = iDev->next;
    }
    return NULL;
}

struct _ick_device_struct * _ickDevice4wsi(struct libwebsocket * wsi) {
    if (!wsi)
        return NULL;
    struct _ick_device_struct * iDev = _ickStreamDevices;
    
    while (iDev) {
        if (iDev->wsi == wsi)
            return iDev;
        iDev = iDev->next;
    }
    return NULL;
}

enum ickDevice_servicetype ickDeviceType(const char * UUID) {
    struct _ick_device_struct * iDev = _ickDeviceGet(UUID);
    if (iDev)
        return iDev->type;
    return 0;
}

char * ickDeviceURL(const char * UUID) {
    struct _ick_device_struct * iDev = _ickDeviceGet(UUID);
    if (iDev)
        return iDev->URL;
    return NULL;
}

unsigned short ickDevicePort(const char * UUID) {
    struct _ick_device_struct * iDev = _ickDeviceGet(UUID);
    if (iDev)
        return iDev->port;
    return 0;
}

char * ickDeviceName(const char * UUID) {
    struct _ick_device_struct * iDev = _ickDeviceGet(UUID);
    if (iDev)
        return iDev->name;
    return NULL;
}




struct _ick_device_struct * _ickDeviceCreateNew(char * UUID, char * URL, void * element, enum ickDevice_servicetype type, struct libwebsocket * wsi) {
    struct _ick_device_struct * device = malloc(sizeof(struct _ick_device_struct));
    
    pthread_mutex_lock(&_device_mutex);
    device->next = _ickStreamDevices;
    _ickStreamDevices = device;
    device->UUID = UUID;
    device->element = element;
    device->xmlData = NULL;
    device->name = NULL;
    device->URL = URL;
    device->wsi = wsi;
    device->type = type;
    device->messageOut = NULL;
    device->messageMutex = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(device->messageMutex, NULL);
    
    pthread_mutex_unlock(&_device_mutex);
    return device;
}


#define _ICK_DEVICE_FREE_ELEMENT(device,element) do { \
    if ((device)->element) { \
        free((device)->element); \
        (device)->element = NULL; \
    } \
} while (0)

void _ickDeviceDestroy(struct _ick_device_struct * device) {
    pthread_mutex_lock(&_device_mutex);
    _ICK_DEVICE_FREE_ELEMENT(device, UUID);
    _ICK_DEVICE_FREE_ELEMENT(device, xmlData);
    _ICK_DEVICE_FREE_ELEMENT(device, name);
    _ICK_DEVICE_FREE_ELEMENT(device, URL);
    device->element = NULL; // belongs to lower layer, needs to be freed there.
    device->wsi = NULL;     // should have been cleared by callback
    device->messageOut = NULL; //dito
    pthread_mutex_destroy(device->messageMutex);
    device->messageMutex = NULL;
    free(device);
    pthread_mutex_unlock(&_device_mutex);
}

#define _ICK_DEVICE_SET_VALUE_LOCKED(device,element,value) do { \
    pthread_mutex_lock(&_device_mutex); \
    if (!(device)->element) \
        (device)->element = (value); \
    pthread_mutex_unlock(&_device_mutex); \
} while (0)

// check whether device is an ickStream device
static enum ickDevice_servicetype _ick_isIckDevice(const struct _upnp_device * device) {
    char strtmp[512];
    char * start;
    
    strncpy(strtmp, device->headers[HEADER_USN].p, device->headers[HEADER_USN].l);
    strtmp[device->headers[HEADER_USN].l] = 0;
    start = strstr(strtmp, ICKDEVICE_TYPESTR_MISC);
    if (!start)
        return ICKDEVICE_GENERIC;
    
    if (strstr(start, ICKDEVICE_TYPESTR_PLAYER))
        return ICKDEVICE_PLAYER;
    
#ifdef SUPPORT_ICK_SERVERS
    if (strstr(start, ICKDEVICE_TYPESTR_SERVER))
        return ICKDEVICE_SERVER_GENERIC;    
#endif
    
    if (strstr(start, ICKDEVICE_TYPESTR_CONTROLLER))
        return ICKDEVICE_CONTROLLER;
    
    // No service type for root device
    //    if (strstr(start, ICKDEVICE_TYPESTR_ROOT))
    //    return 1;

    return 0;
}

#define XMLMAX  256

struct _ick_xmlparser_s {
    char * name;
    int level;
    char elt[XMLMAX];
    int writeme;
};

//
// load XML Data for device descriptions
// we only read root device information, so whenever we see we do already have XML Data we are fine and don't read it again.
// we are always reading root info
//
static void _ick_parsexml_startelt(void * data, const char * elt, int l) {
    struct _ick_xmlparser_s * ps = data;
    ps->level ++;
    if (l >= XMLMAX)
        l = XMLMAX - 2;
    memcpy(ps->elt, elt, l);
    ps->elt[l] = 0;
}

static void _ick_parsexml_endelt(void * data, const char * elt, int l) {
    struct _ick_xmlparser_s * ps = data;
    ps->level --;
}

// We expect "deviceType" always to be the first elelemt!
// and device descriptions have to precede embedded device descriptions
static void _ick_parsexml_processelt(void * data, const char * content, int l) {
    struct _ick_xmlparser_s * ps = data;
    if ((l > 33) && 
        ((ps->writeme == 0) || !ps->name) &&
        !strcmp(ps->elt, "deviceType") &&
        !memcmp(content, ICKDEVICE_TYPESTR_MISC, 33)) {  // right now we only check whether it's an ickStream device: "urn:schemas-ickstream-com:device:"
        ps->writeme = ps->level;
    } else if ((ps->writeme == ps->level) && !ps->name && !strcmp(ps->elt, "friendlyName")) {
        ps->name = malloc(l + 1);
        if (ps->name) {
            memcpy(ps->name, content, l);
            (ps->name)[l] = 0;
        }
    }
}

static void * _ick_loadxmldata_thread(void * param) {
    struct _ick_device_struct * iDev = param;
    
    char * urlString;
    int result = asprintf(&urlString, "http://%s:%d/Root.xml", iDev->URL, iDev->port);
    if (result < 1)
        return NULL;
    
    int size;
    void * data = miniwget(urlString, &size);
    free(urlString);
    
    if (size < 1)
        return NULL;
    
    struct _ick_xmlparser_s device_parser;
    device_parser.name = NULL;
    device_parser.level = 0;
    device_parser.writeme = 0;
    
    struct xmlparser parser;
	/* xmlparser object */
	parser.xmlstart = data;
	parser.xmlsize = size;
	parser.data = &device_parser;
	parser.starteltfunc = _ick_parsexml_startelt;
	parser.endeltfunc = _ick_parsexml_endelt;
	parser.datafunc = _ick_parsexml_processelt;
	parser.attfunc = 0;
	parsexml(&parser);
    
    if ((device_parser.writeme && device_parser.name) && (iDev->name == NULL)) {
        _ICK_DEVICE_SET_VALUE_LOCKED(iDev, name, device_parser.name);
        _ick_execute_DeviceCallback(iDev, ICKDISCOVERY_ADD_DEVICE);
        if (_discovery->exitCallback)
            _discovery->exitCallback();
    } else {
        if (device_parser.name)
            free(device_parser.name);
    }
    
    return NULL;
}


static void _ick_load_xml_data(struct _ick_device_struct * iDev) {
    if (!iDev || iDev->xmlData || !iDev->URL)
        return;
    pthread_t thread;
    pthread_create(&thread, NULL, _ick_loadxmldata_thread, iDev);
}



//
// notification callback - called whenever a device gets added, changed or removed
//

void _ick_receive_notify(const struct _upnp_device * device, enum ickDiscovery_command cmd) {
    enum ickDevice_servicetype devType = _ick_isIckDevice(device);
    
    if (devType == ICKDEVICE_GENERIC)
        return;
        
    struct _ick_device_struct * iDev = NULL;
    struct _ick_device_struct * iDevParent = NULL;
        
    // step 1: find UUID (if present; if not, don't add device)
    char * UUID = NULL;
    int l = 0;
    char * p = NULL;
    char * strtmp = NULL;
    
    do {
        l = device->headers[HEADER_USN].l;
        if (l <= 7)
            break;;
        strtmp = malloc(l + 1);
        memcpy(strtmp, device->headers[HEADER_USN].p, l);
        strtmp[l] = 0;
        if (strncmp(strtmp, "uuid:", 5))
            break;
        p = strstr(strtmp, "::");
        l = (p) ? (p - strtmp - 5) : (l - 5);
        UUID = malloc(l + 1);
        strncpy(UUID, strtmp + 5, l);
        UUID[l] = 0;
        free(strtmp);
    } while (0);

    
    // We need to ALWAYS look for an existing device. 
    // It could be we have created one when the websocket connected but the discovery was not yet in. 
    // In this case let's join the connections, just in case, even though we do already have all necessary connections.
    // We also need to search for both device and UUID due to this.
    pthread_mutex_lock(&_device_mutex);
    iDev = _ickStreamDevices;
    while (iDev && (iDev->element != device) && 
           UUID && (strcmp(iDev->UUID, UUID))) {
        iDevParent = iDev;
        iDev = iDev->next;
    }
    pthread_mutex_unlock(&_device_mutex);
    
    if (!iDev) {
        switch (cmd) {
                // Device to be removed not found? Nothing to do
            case ICKDISCOVERY_REMOVE_DEVICE:
                return;
                break;
                // Device to update not found? Maybe it wasn't detectable last time,... add it.
            case ICKDISCOVERY_UPDATE_DEVICE:
                cmd = ICKDISCOVERY_ADD_DEVICE;
            default:
                iDev = _ickDeviceCreateNew(UUID, NULL, NULL, 0, NULL);
                if (!iDev)
                    return;
                break;
        }
    }
    
    switch (cmd) {
        case ICKDISCOVERY_ADD_DEVICE: {
            // What do we do if the element does already exist??? For now: overwrite it. It will not be freed but handling it is the responsibility if the upnp layer anyway.
            iDev->element = (void *)device;
            iDev->type |= devType;
            _ICK_DEVICE_SET_VALUE_LOCKED(iDev, UUID, UUID);
            
            l = device->headers[HEADER_LOCATION].l;
            p = (char *)device->headers[HEADER_LOCATION].p;
            if (l > 7 && !strncasecmp(p, "http://", 7)) {   // skip http://
                p += 7;
                l -= 7;
            }
            strtmp = malloc(l + 1);
            memcpy(strtmp, p, l);
            strtmp[l] = 0;
            //            p = strtmp;
            //            if (!strncasecmp(strtmp, "http://", 7))
            //                p = strtmp + 7;
            p = strchr(strtmp, ':');
            if (p)
                *p = 0;
            _ICK_DEVICE_SET_VALUE_LOCKED(iDev, URL, strtmp);
            p++;
            unsigned short port = atoi(p);
            iDev->port = port;
                        
            _ick_execute_DeviceCallback(iDev, ICKDISCOVERY_ADD_DEVICE);
            
            // let's load the name data... This means we're probably going to send an update later...
            _ick_load_xml_data(iDev);
        }
            break;
            
        case ICKDISCOVERY_UPDATE_DEVICE: {
            /* Handle update callback to application here */
            // TODO: get ickStream-Info from Device
            
            // Device was "Generic so far"? Then we did not report it, yet.
            enum ickDiscovery_command cmd = (iDev->type == ICKDEVICE_GENERIC) ? ICKDISCOVERY_ADD_DEVICE : ICKDISCOVERY_UPDATE_DEVICE;
            debug("ICKDISCOVERY_UPDATE_DEVICE: %s\n", UUID);
            iDev->element = (void *)device;
            iDev->type |= devType;
            // OK, we only ADD capabilities, we never remove them. That's kind of in-line with UPnP which invalidates a whole root device at a time, but do we really waynt it this way for ickStream?
            _ick_execute_DeviceCallback(iDev, cmd);            
            
            // let's load the name data... This means we're probably going to send another update later...
            _ick_load_xml_data(iDev);
        }
            break;
        case ICKDISCOVERY_REMOVE_DEVICE:
            _ick_execute_DeviceCallback(iDev, ICKDISCOVERY_REMOVE_DEVICE);
            
            pthread_mutex_lock(&_device_mutex);
            if (iDevParent)
                iDevParent->next = iDev->next;
            else if (_ickStreamDevices == iDev)
                _ickStreamDevices = iDev->next;
            pthread_mutex_unlock(&_device_mutex);
            
            // bye bye
            _ickDeviceDestroy(iDev);
            
            break;
    }    
}


// command queue handling


static pthread_mutex_t _ick_sender_mutex = PTHREAD_MUTEX_INITIALIZER;

struct _ick_sender_cmd {
    enum _ick_send_cmd      command;    // what do we have to do
    struct timeval          time;       // when? in micorseconds
    void *                  data;       // some commands bring their content - in lack of a better idea.
    LIST_ENTRY(_ick_sender_cmd) entries;
};

static LIST_HEAD(__ick_sender_cmd, _ick_sender_cmd) _ick_send_cmdlisthead;

static int _ick_notification_queue(struct _ick_sender_cmd * cmd);


#pragma mark - from minisspd

//
// minisspd
// The following code is partly modiied from minisspd
// It's the underlying UPnP discovery. All UPnP devices will be discovered and kept in a link list.
// The higher level ickStream registry then uses these devices to filter for devices it can handle.
// UPnP hooks should be a straightforward implementation this way
//


#define NTS_SSDP_ALIVE	1
#define NTS_SSDP_BYEBYE	2

/* discovered device list kept in memory */
static struct _upnp_device * devlist = 0;

/* bootid and configid */
unsigned int upnp_bootid = 1;
unsigned int upnp_configid = 1;

/* updateDevice() :
 * adds or updates the device to the list.
 * return value :
 *  -1 : error
 *   0 : the device was updated
 *   1 : the device was new    */
static int
updateDevice(const struct _ick_discovery_struct * discovery, const struct _header_string * headers, time_t t)
{
	struct _upnp_device ** pp = &devlist;
	struct _upnp_device * p = *pp;	/* = devlist; */
    struct _ick_callback_list * cblist;
    pthread_mutex_lock(&_ick_sender_mutex); // need to make sure we don't remove the device while it's being updated
	while(p)
	{
		if(  p->headers[HEADER_NT].l == headers[HEADER_NT].l
           && (0==memcmp(p->headers[HEADER_NT].p, headers[HEADER_NT].p, headers[HEADER_NT].l))
           && p->headers[HEADER_USN].l == headers[HEADER_USN].l
           && (0==memcmp(p->headers[HEADER_USN].p, headers[HEADER_USN].p, headers[HEADER_USN].l)) )
		{
			p->t = t; // even if the device is up to date we need to update the lifetime...
            if (p->headers[HEADER_LOCATION].l == headers[HEADER_LOCATION].l &&
                (0 == memcmp(p->headers[HEADER_LOCATION].p, headers[HEADER_LOCATION].p, headers[HEADER_LOCATION].l))) {
                debug("device already up to date: %.*s\n", headers[HEADER_USN].l, headers[HEADER_USN].p);
                pthread_mutex_unlock(&_ick_sender_mutex);
                return 0;
            }
			//printf("found! %d\n", (int)(t - p->t));
			debug("device updated : %.*s\n", headers[HEADER_USN].l, headers[HEADER_USN].p);
			debug("device address : %.*s\n", headers[HEADER_LOCATION].l, headers[HEADER_LOCATION].p);
			/* update Location ! */
			if(headers[HEADER_LOCATION].l > p->headers[HEADER_LOCATION].l)
			{
				p = realloc(p, sizeof(struct _upnp_device)
                            + headers[0].l+headers[1].l+headers[2].l );
				if(!p)	/* allocation error */ {
                    pthread_mutex_unlock(&_ick_sender_mutex);
					return 0;
                }
				*pp = p;
			}
			memcpy(p->data + p->headers[0].l + p->headers[1].l,
			       headers[2].p, headers[2].l);
            
            cblist = discovery->receive_callbacks;
            while (cblist) {
                cblist->callback(p, ICKDISCOVERY_UPDATE_DEVICE);
                cblist = cblist->next;
            }
            
            pthread_mutex_unlock(&_ick_sender_mutex);
            return 0;
		}
		pp = &p->next;
		p = *pp;	/* p = p->next; */
	}
    pthread_mutex_unlock(&_ick_sender_mutex);
	debug("new device discovered : %.*s\n",
          headers[HEADER_USN].l, headers[HEADER_USN].p);
    debug("device address : %.*s\n", headers[HEADER_LOCATION].l, headers[HEADER_LOCATION].p);
	/* add */
	{
		char * pc;
		int i;
		p = malloc(  sizeof(struct _upnp_device)
		           + headers[0].l+headers[1].l+headers[2].l );
		if(!p) {
			debug("updateDevice(): cannot allocate memory");
			return -1;
		}
        pthread_mutex_lock(&_ick_sender_mutex); // need to make sure we don't add the device while the command queue is being used
		p->next = devlist;
		p->t = t;
		pc = p->data;
		for(i = 0; i < 3; i++)
		{
			p->headers[i].p = pc;
			p->headers[i].l = headers[i].l;
			memcpy(pc, headers[i].p, headers[i].l);
			pc += headers[i].l;
		}
		devlist = p;

        // add expiration handler
        struct _ick_sender_cmd * cmd = malloc(sizeof(struct _ick_sender_cmd));
        if (cmd) {
            cmd->command = ICK_SEND_CMD_EXPIRE_DEVICE;
            cmd->data = p;
            cmd->time.tv_sec = t;
            cmd->time.tv_usec = 0;
            _ick_notification_queue(cmd);
        }
        pthread_mutex_unlock(&_ick_sender_mutex);

        
        cblist = discovery->receive_callbacks;
        while (cblist) {
            cblist->callback(p, ICKDISCOVERY_UPDATE_DEVICE);
            cblist = cblist->next;
        }
	}
	return 1;
}

/* removeDevice() :
 * remove a device from the list
 * return value :
 *    0 : no device removed
 *   -1 : device removed */
static int
removeDevice(const struct _ick_discovery_struct * discovery, const struct _header_string * headers)
{
	struct _upnp_device ** pp = &devlist;
	struct _upnp_device * p = *pp;	/* = devlist */
    pthread_mutex_lock(&_ick_sender_mutex); // need to make sure we don't remove the device while it's being updated
	while(p)
	{
		if(  p->headers[HEADER_NT].l == headers[HEADER_NT].l
           && (0==memcmp(p->headers[HEADER_NT].p, headers[HEADER_NT].p, headers[HEADER_NT].l))
           && p->headers[HEADER_USN].l == headers[HEADER_USN].l
           && (0==memcmp(p->headers[HEADER_USN].p, headers[HEADER_USN].p, headers[HEADER_USN].l)) )
		{
			debug("remove device : %.*s\n", headers[HEADER_USN].l, headers[HEADER_USN].p);
			*pp = p->next;
            
            struct _ick_callback_list * cblist;
            cblist = discovery->receive_callbacks;
            while (cblist) {
                cblist->callback(p, ICKDISCOVERY_REMOVE_DEVICE);
                cblist = cblist->next;
            }
            
            // now remove expiration handler for this device....
            // checking all right now, just to be sure.
            struct _ick_sender_cmd * cmd = LIST_FIRST(&_ick_send_cmdlisthead);
            while (cmd) {
                struct _ick_sender_cmd * next = LIST_NEXT(cmd, entries);
                if ((cmd->command == ICK_SEND_CMD_EXPIRE_DEVICE) && (cmd->data == p)) {
                    LIST_REMOVE(cmd, entries);
                    free(cmd);
                }
                cmd = next;
            }
            
			free(p);
            pthread_mutex_unlock(&_ick_sender_mutex);
			return -1;
		}
		pp = &p->next;
		p = *pp;	/* p = p->next; */
	}
    pthread_mutex_unlock(&_ick_sender_mutex);
	debug("device not found for removing : %.*s\n", headers[HEADER_USN].l, headers[HEADER_USN].p);
	return 0;
}

/* SendSSDPMSEARCHResponse() :
 * build and send response to M-SEARCH SSDP packets. */
static void
SendSSDPMSEARCHResponse(int s, const struct sockaddr * sockname,
                        const char * st, const char * usn,
                        const char * server, const char * location)
{
	int l, n;
	char buf[512];
	socklen_t sockname_len;
	/*
	 * follow guideline from document "UPnP Device Architecture 1.0"
	 * uppercase is recommended.
	 * DATE: is recommended
	 * SERVER: OS/ver UPnP/1.0 miniupnpd/1.0
	 * - check what to put in the 'Cache-Control' header
	 *
	 * have a look at the document "UPnP Device Architecture v1.1 */
	l = snprintf(buf, sizeof(buf), "HTTP/1.1 200 OK\r\n"
                 "CACHE-CONTROL: max-age=" XSTR(ICKDISCOVERY_ANNOUNCE_INTERVAL) "\r\n"
                 /*"DATE: ...\r\n"*/
                 "ST: %s\r\n"
                 "USN: %s\r\n"
                 "EXT:\r\n"
                 "SERVER: %s\r\n"
                 "LOCATION: %s\r\n"
                 /*"OPT: \"http://schemas.upnp.org/upnp/1/0/\";\r\n" */ /* UDA v1.1 */
                 /*"01-NLS: %u\r\n" */ /* same as BOOTID. UDA v1.1 */
                 "BOOTID.UPNP.ORG: %u\r\n" /* UDA v1.1 */
                 "CONFIGID.UPNP.ORG: %u\r\n" /* UDA v1.1 */
                 "\r\n",
                 st, usn,
                 server, location,
                 /*upnp_bootid, */ upnp_bootid, upnp_configid);
#ifdef ENABLE_IPV6
	sockname_len = (sockname->sa_family == PF_INET6)
    ? sizeof(struct sockaddr_in6)
    : sizeof(struct sockaddr_in);
#else
	sockname_len = sizeof(struct sockaddr_in);
#endif
	n = sendto(s, buf, l, 0,
	           sockname, sockname_len );
	if(n < 0) {
		debug("sendto(udp): %m");
	}
}


// linked list holding service/device entries

static LIST_HEAD(servicehead, _upnp_service) servicelisthead;

/* Process M-SEARCH requests */
static void
processMSEARCH(int s, const char * st, int st_len,
               const struct sockaddr * addr)
{
	struct _upnp_service * serv;
#ifdef ENABLE_IPV6
	char buf[64];
#endif
    
	if(!st || st_len==0)
		return;
#ifdef ENABLE_IPV6
	sockaddr_to_string(addr, buf, sizeof(buf));
	debug("SSDP M-SEARCH from %s ST:%.*s\n",
          buf, st_len, st);
#else
	debug("SSDP M-SEARCH from %s:%d ST: %.*s\n",
          inet_ntoa(((const struct sockaddr_in *)addr)->sin_addr),
          ntohs(((const struct sockaddr_in *)addr)->sin_port),
          st_len, st);
#endif
	if(st_len==8 && (0==memcmp(st, "ssdp:all", 8))) {
		/* send a response for all services */
		for(serv = servicelisthead.lh_first;
		    serv;
		    serv = serv->entries.le_next) {
			SendSSDPMSEARCHResponse(s, addr,
			                        serv->st, serv->usn,
			                        serv->server, serv->location);
		}
	} else if(st_len > 5 && (0==memcmp(st, "uuid:", 5))) {
		/* find a matching UUID value */
		for(serv = servicelisthead.lh_first;
		    serv;
		    serv = serv->entries.le_next) {
			if(0 == strncmp(serv->usn, st, st_len)) {
				SendSSDPMSEARCHResponse(s, addr,
				                        serv->st, serv->usn,
				                        serv->server, serv->location);
			}
		}
	} else {
		/* find matching services */
		if(st[st_len-2]==':' && isdigit(st[st_len-1]))
			st_len -= 2;
		for(serv = servicelisthead.lh_first;
		    serv;
		    serv = serv->entries.le_next) {
			if(0 == strncmp(serv->st, st, st_len)) {
				SendSSDPMSEARCHResponse(s, addr,
				                        serv->st, serv->usn,
				                        serv->server, serv->location);
			}
		}
	}
}

/**
 * helper function.
 * reject any non ASCII or non printable character.
 */
static int
containsForbiddenChars(const unsigned char * p, int len)
{
	while(len > 0) {
		if(*p < ' ' || *p >= '\x7f')
			return 1;
		p++;
		len--;
	}
	return 0;
}

#define METHOD_MSEARCH 1
#define METHOD_NOTIFY 2
#define METHOD_REPLY 3

/* ParseSSDPPacket() :
 * parse a received SSDP Packet and call 
 * updateDevice() or removeDevice() as needed
 * return value :
 *    -1 : a device was removed
 *     0 : no device removed nor added
 *     1 : a device was added.  */
int
ParseSSDPPacket(const struct _ick_discovery_struct * discovery, const char * p, ssize_t n,
                const struct sockaddr * addr)
{
	const char * linestart;
	const char * lineend;
	const char * nameend;
	const char * valuestart;
	struct _header_string headers[3];
	int i, r = 0;
	int methodlen;
	int nts = -1;
	int method = -1;
	unsigned int lifetime = 180;	/* 3 minutes by default */
	const char * st = NULL;
	int st_len = 0;
	memset(headers, 0, sizeof(headers));
	for(methodlen = 0;
	    methodlen < n && (isalpha(p[methodlen]) || p[methodlen]=='-');
		methodlen++);
	if(methodlen==8 && 0==memcmp(p, "M-SEARCH", 8)) 
		method = METHOD_MSEARCH;
	else if(methodlen==6 && 0==memcmp(p, "NOTIFY", 6))
		method = METHOD_NOTIFY;
    else
        if(methodlen==5 && 0==memcmp(p, "REPLY * HTTP/1.1 200 OK", 23)) {
            debug("\nM-SEARCH Reply: '%.*s'",
                  methodlen, p);
            method = METHOD_REPLY;    
        }
	linestart = p;
	while(linestart < p + n - 2) {
		/* start parsing the line : detect line end */
		lineend = linestart;
		while(lineend < p + n && *lineend != '\n' && *lineend != '\r')
			lineend++;
		//printf("line: '%.*s'\n", lineend - linestart, linestart);
		/* detect name end : ':' character */
		nameend = linestart;
		while(nameend < lineend && *nameend != ':')
			nameend++;
		/* detect value */
		if(nameend < lineend)
			valuestart = nameend + 1;
		else
			valuestart = nameend;
		/* trim spaces */
		while(valuestart < lineend && isspace(*valuestart))
			valuestart++;
		/* suppress leading " if needed */
		if(valuestart < lineend && *valuestart=='\"')
			valuestart++;
		if(nameend > linestart && valuestart < lineend) {
			int l = nameend - linestart;	/* header name length */
			int m = lineend - valuestart;	/* header value length */
			/* suppress tailing spaces */
			while(m>0 && isspace(valuestart[m-1]))
				m--;
			/* suppress tailing ' if needed */
			if(m>0 && valuestart[m-1] == '\"')
				m--;
			i = -1;
			/*printf("--%.*s: (%d)%.*s--\n", l, linestart,
             m, m, valuestart);*/
			if(l==2 && 0==strncasecmp(linestart, "nt", 2))
				i = HEADER_NT;
			else if(l==3 && 0==strncasecmp(linestart, "usn", 3))
				i = HEADER_USN;
			else if(l==3 && 0==strncasecmp(linestart, "nts", 3)) {
				if(m==10 && 0==strncasecmp(valuestart, "ssdp:alive", 10))
					nts = NTS_SSDP_ALIVE;
				else if(m==11 && 0==strncasecmp(valuestart, "ssdp:byebye", 11))
					nts = NTS_SSDP_BYEBYE;
			}
			else if(l==8 && 0==strncasecmp(linestart, "location", 8))
				i = HEADER_LOCATION;
			else if(l==13 && 0==strncasecmp(linestart, "cache-control", 13)) {
				/* parse "name1=value1, name_alone, name2=value2" string */
				const char * name = valuestart;	/* name */
				const char * val;				/* value */
				int rem = m;	/* remaining bytes to process */
				while(rem > 0) {
					val = name;
					while(val < name + rem && *val != '=' && *val != ',')
						val++;
					if(val >= name + rem)
						break;
					if(*val == '=') {
						while(val < name + rem && (*val == '=' || isspace(*val)))
							val++;
						if(val >= name + rem)
							break;
						if(0==strncasecmp(name, "max-age", 7))
							lifetime = (unsigned int)strtoul(val, 0, 0);
						/* move to the next name=value pair */
						while(rem > 0 && *name != ',') {
							rem--;
							name++;
						}
						/* skip spaces */
						while(rem > 0 && (*name == ',' || isspace(*name))) {
							rem--;
							name++;
						}
					} else {
						rem -= (val - name);
						name = val;
						while(rem > 0 && (*name == ',' || isspace(*name))) {
							rem--;
							name++;
						}
					}
				}
				/*debug("**%.*s**%u", m, valuestart, lifetime);*/
			} else if(l==2 && 0==strncasecmp(linestart, "st", 2)) {
                // for search replis, ST takes the place of NT
                if (method == METHOD_REPLY)
                    i = HEADER_NT;
				st = valuestart;
				st_len = m;
			}
			if(i>=0) {
				headers[i].p = valuestart;
				headers[i].l = m;
			}
		}
		linestart = lineend;
		while((*linestart == '\n' || *linestart == '\r') && linestart < p + n)
			linestart++;
	}
#if 0
	printf("NTS=%d\n", nts);
	for(i=0; i<3; i++) {
		if(headers[i].p)
			printf("%d-'%.*s'\n", i, headers[i].l, headers[i].p);
	}
#endif
	debug("SSDP request: '%.*s' (%d) st=%.*s",
          methodlen, p, method, st_len, st);
    debug(" from %s:%d ST: %.*s\n",
          inet_ntoa(((const struct sockaddr_in *)addr)->sin_addr),
          ntohs(((const struct sockaddr_in *)addr)->sin_port),
          st_len, st);

	switch(method) {
        case METHOD_REPLY:
            if(headers[0].p && headers[1].p && headers[2].p)
                r = updateDevice(discovery, headers, time(NULL) + lifetime);                
            break;
        case METHOD_NOTIFY:
            // BYEBYE only delivers two lines
            if(headers[0].p && headers[1].p && (headers[2].p || nts==NTS_SSDP_BYEBYE)) {
                if(nts==NTS_SSDP_ALIVE) {
                    r = updateDevice(discovery, headers, time(NULL) + lifetime);
                }
                else if(nts==NTS_SSDP_BYEBYE) {
                    r = removeDevice(discovery, headers);
                }
            }
            break;
        case METHOD_MSEARCH:
            processMSEARCH(discovery->socket, st, st_len, addr);
            break;
        default:
            debug("method %.*s, don't know what to do", methodlen, p);
	}
	return r;
}


// Add a service.
// returns  0 on success
//          -1 on failure

int _ick_add_service (const char * st, const char * usn, const char * server, const char * location) {
    pthread_mutex_lock(&_ick_sender_mutex);
    
    struct _upnp_service * service = malloc(sizeof(struct _upnp_service));
    if (!service) return -1;
    service->st = malloc(strlen(st) + 1);
    if (!service->st) return -1;
    strcpy(service->st, st);
    service->usn = malloc(strlen(usn) + 1);
    if (!service->usn) return -1;
    strcpy(service->usn, usn);
    service->server = malloc(strlen(server) + 1);
    if (!service->server) return -1;
    strcpy(service->server, server);
    service->location = malloc(strlen(location) + 1);
    if (!service->location) return -1;
    strcpy(service->location, location);

    LIST_INSERT_HEAD(&servicelisthead, service, entries);
    
    // announce service device periodically
    _ick_notifications_send(ICK_SEND_CMD_NOTIFY_PERIODICADD, service);
    
    pthread_mutex_unlock(&_ick_sender_mutex);
    return 0;
}

// Remove a service
// returns  0 on success
//          -1 on failure

int _ick_remove_service(const char * st, bool lock) {
    if (lock)
        pthread_mutex_lock(&_ick_sender_mutex);
    struct _upnp_service * service = servicelisthead.lh_first;
    while (service) {
        if (!strcmp(st, service->st)) {
            // announce service removal; needs to be before actual removal, still need the data
            _ick_notifications_send(ICK_SEND_CMD_NOTIFY_REMOVE, service);
            
            LIST_REMOVE(service, entries);
            free(service->st);
            free(service->server);
            free(service->location);
            free(service->usn);
            free(service);
            if (lock)
                pthread_mutex_unlock(&_ick_sender_mutex);
            return 0;
        }
        service = service->entries.le_next;
    }
    if (lock)
        pthread_mutex_unlock(&_ick_sender_mutex);
    return -1;
}


//
// send notification messages
//
// A message for each registered service is being sent.
//
// two functions: a "creator" function for setting up the socket and notification info
// an internal function being spawned as a separate thread to do the actual sending.
//

// mutex for notification send thread

static struct {
    const char *      UUID;
    const char *      location;
    const char *      osname;
    pthread_t         thread;
    unsigned short    port;
} _ick_sender_struct;

static int _sendsock;

// create message for command and object, if more than one is needed, it's #num
// #num:
//          ADD/REMOVE
//          0 - rootmessage
//          1 - UUID
//          2 - URN

/*static void
SendSSDPNotifies(int s, const char * host, unsigned short port,
                 unsigned int lifetime)
{
	struct sockaddr_in sockname;
	int l, n, i=0;
	char bufr[512];
    
	memset(&sockname, 0, sizeof(struct sockaddr_in));
	sockname.sin_family = AF_INET;
	sockname.sin_port = htons(SSDP_PORT);
	sockname.sin_addr.s_addr = inet_addr(SSDP_MCAST_ADDR);
    
	while(known_service_types[i])
	{
		l = snprintf(bufr, sizeof(bufr),
                     "NOTIFY * HTTP/1.1\r\n"
                     "HOST: %s:%d\r\n"
                     "CACHE-CONTROL: max-age=%u\r\n"
                     "lOCATION: http://%s:%d" ROOTDESC_PATH"\r\n"
                     "SERVER: " MINIUPNPD_SERVER_STRING "\r\n"
                     "NT: %s%s\r\n"
                     "USN: %s::%s%s\r\n"
                     "NTS: ssdp:alive\r\n"
                     "OPT: \"http://schemas.upnp.org/upnp/1/0/\";\r\n"
                     "01-NLS: %u\r\n"
                     "BOOTID.UPNP.ORG: %u\r\n"
                     "CONFIGID.UPNP.ORG: %u\r\n"
                     "\r\n",
                     SSDP_MCAST_ADDR, SSDP_PORT,
                     lifetime,
                     host, port,
                     known_service_types[i], (i==0?"":"1"),
                     uuidvalue, known_service_types[i], (i==0?"":"1"),
                     upnp_bootid, upnp_bootid, upnp_configid );
		if(l>=sizeof(bufr))
		{
			syslog(LOG_WARNING, "SendSSDPNotifies(): truncated output");
			l = sizeof(bufr);
		}
		n = sendto(s, bufr, l, 0,
                   (struct sockaddr *)&sockname, sizeof(struct sockaddr_in) );
		if(n < 0)
		{

			syslog(LOG_ERR, "sendto(udp_notify=%d, %s): %m", s, host);
		}
		i++;
	}
}*/

#define ICK_NOTIFICATION_ALIVE      "ssdp:alive"
#define ICK_NOTIFICATION_BYEBYE     "ssdp:byebye"
#define ICK_NOTIFICATION_STRING     "NOTIFY * HTTP/1.1\r\n" \
                                    "HOST: " UPNP_MCAST_ADDR ":" XSTR(UPNP_PORT) "\r\n" \
                                    "CACHE-CONTROL: max-age=" XSTR(ICKDISCOVERY_ANNOUNCE_INTERVAL) "\r\n" \
                                    "LOCATION: %s\r\n" \
                                    "SERVER: %s\r\n"  \
                                    "NT: %s\r\n" \
                                    "USN: %s\r\n" \
                                    "NTS: %s\r\n" \
                                    "BOOTID.UPNP.ORG: %u\r\n" /* UDA v1.1 */ \
                                    "CONFIGID.UPNP.ORG: %u\r\n" /* UDA v1.1 */ \
                                    "\r\n"
#define ICK_NOTIFICATION_STRING_B   "NOTIFY * HTTP/1.1\r\n" \
                                    "HOST: " UPNP_MCAST_ADDR ":" XSTR(UPNP_PORT) "\r\n" \
                                    "NT: %s\r\n" \
                                    "USN: %s\r\n" \
                                    "NTS: %s\r\n" \
                                    "BOOTID.UPNP.ORG: %u\r\n" /* UDA v1.1 */ \
                                    "CONFIGID.UPNP.ORG: %u\r\n" /* UDA v1.1 */ \
                                    "\r\n"
#define ICK_SEARCH_STRING           "M-SEARCH * HTTP/1.1\r\n" \
                                    "HOST: " UPNP_MCAST_ADDR ":" XSTR(PORT) "\r\n" \
                                    "ST: %s\r\n" \
                                    "MAN: \"ssdp:discover\"\r\n" \
                                    "MX: " XSTR(ICKDISCOVERY_SEARCH_DELAY) "\r\n" \
                                    "\r\n"


static char * _ick_notification_create (enum _ick_send_cmd cmd, struct _upnp_service * device, int num) {
    char * nt = NULL;
    char * usn = NULL;
    char * result;
        
    switch (cmd) {
        case ICK_SEND_CMD_NOTIFY_ADD: {
            char * location;
            char * server;
            switch (num) {
                case 0: {
                    char * location;
                    char * server;
                    nt = "upnp:rootdevice";
                    asprintf(&usn, ICKDEVICE_TYPESTR_USN, _ick_sender_struct.UUID, ICKDEVICE_TYPESTR_ROOT);
                    asprintf(&location, ICKDEVICE_TYPESTR_LOCATION, _ick_sender_struct.location, _ick_sender_struct.port, ICKDEVICE_STRING_ROOT);
                    asprintf(&server, ICKDEVICE_TYPESTR_SERVERSTRING, _ick_sender_struct.osname);
                    asprintf(&result, ICK_NOTIFICATION_STRING, 
                             location, 
                             server,
                             nt,
                             usn,
                             ICK_NOTIFICATION_ALIVE,
                             upnp_bootid,
                             upnp_configid);
                    free(usn);
                    free(location);
                    free(server);
                    return result;
                }
                    break;
                case 1: {
                    asprintf(&nt, "uuid:%s", _ick_sender_struct.UUID);
                    if (device) {
                        location = device->location;
                        server = device->server;
                    } else {
                        asprintf(&location, ICKDEVICE_TYPESTR_LOCATION, _ick_sender_struct.location, _ick_sender_struct.port, ICKDEVICE_STRING_ROOT);
                        asprintf(&server, ICKDEVICE_TYPESTR_SERVERSTRING, _ick_sender_struct.osname);
                    }
                    asprintf(&result, ICK_NOTIFICATION_STRING, 
                             location, 
                             server,
                             nt,
                             nt,
                             ICK_NOTIFICATION_ALIVE,
                             upnp_bootid,
                             upnp_configid);
                    free(nt);
                    if (!device) {
                        free(location);
                        free(server);
                    }
                    return result;
                }
                    break;
                case 2: {
                    if (device) {
                        usn = device->usn;
                        location = device->location;
                        server = device->server;
                        nt = device->st;
                    } else {
                        nt = ICKDEVICE_TYPESTR_ROOT;
                        asprintf(&usn, ICKDEVICE_TYPESTR_USN, _ick_sender_struct.UUID, ICKDEVICE_TYPESTR_ROOT);
                        asprintf(&location, ICKDEVICE_TYPESTR_LOCATION, _ick_sender_struct.location, _ick_sender_struct.port, ICKDEVICE_STRING_ROOT);
                        asprintf(&server, ICKDEVICE_TYPESTR_SERVERSTRING, _ick_sender_struct.osname);
                    }
                    asprintf(&result, ICK_NOTIFICATION_STRING, 
                             location, 
                             server,
                             nt,
                             usn,
                             ICK_NOTIFICATION_ALIVE,
                             upnp_bootid,
                             upnp_configid);
                    if (!device) {
                        free(usn);
                        free(location);
                        free(server);
                    }
                    return result;
                }
                    break;
            }
        }
            break;
            
        case ICK_SEND_CMD_NOTIFY_REMOVE: {
            switch (num) {
                case 0: {
                    nt = "upnp:rootdevice";
                    asprintf(&usn, ICKDEVICE_TYPESTR_USN, _ick_sender_struct.UUID, ICKDEVICE_TYPESTR_ROOT);
                    asprintf(&result, ICK_NOTIFICATION_STRING_B, 
                             nt,
                             usn,
                             ICK_NOTIFICATION_BYEBYE,
                             upnp_bootid,
                             upnp_configid);
                    free(usn);
                    return result;
                }
                    break;
                case 1: {
                    asprintf(&nt, "uuid:%s", _ick_sender_struct.UUID);
                    asprintf(&result, ICK_NOTIFICATION_STRING_B, 
                             nt,
                             nt,
                             ICK_NOTIFICATION_BYEBYE,
                             upnp_bootid,
                             upnp_configid);
                    free(nt);
                    return result;
                }
                    break;
                case 2: {
                    if (device) {
                        usn = device->usn;
                        nt = device->st;
                    } else {
                        nt = ICKDEVICE_TYPESTR_ROOT;
                        asprintf(&usn, ICKDEVICE_TYPESTR_USN, _ick_sender_struct.UUID, ICKDEVICE_TYPESTR_ROOT);
                    }
                    asprintf(&result, ICK_NOTIFICATION_STRING_B, 
                             nt,
                             usn,
                             ICK_NOTIFICATION_BYEBYE,
                             upnp_bootid,
                             upnp_configid);
                    if (!device) {
                        free(usn);
                    }
                    return result;
                }
                    break;
            }
        }
            break;
            
            // This needs to be changed once we want to search for more than just players
        case ICK_SEND_CMD_SEARCH: {
            switch(num) {
                case 0:
                    asprintf(&result, ICK_SEARCH_STRING, ICKDEVICE_TYPESTR_PLAYER);
                    break;
                case 1:
                    asprintf(&result, ICK_SEARCH_STRING, ICKDEVICE_TYPESTR_SERVER);
                    break;
            }
            return result;
        }
            break;
            
        default:
            break;
    }
    return NULL;
}


// send the actual message to the UDP socket

static void _ick_notification_send_socket (char * msg) {
    static struct sockaddr_in sockname;
    static int init = 0;
    if (!init) {
        memset(&sockname, 0, sizeof(struct sockaddr_in));
        sockname.sin_family = AF_INET;
        sockname.sin_port = htons(UPNP_PORT);
        sockname.sin_addr.s_addr = inet_addr(UPNP_MCAST_ADDR);
        init = 1;
    }
    
    int l = strlen(msg) + 1;
    // Wen want to restrict packet size to 512 bytes since some IFs don't support bigger UDP packets
    if(l >= 512) {
        debug("SendSSDPNotifies(): truncated output");
        l = 512;
    }
    int n = sendto(_sendsock, msg, l, 0,
               (struct sockaddr *)&sockname, sizeof(struct sockaddr_in) );
    if(n < 0) {
        debug("error sendto(udp_notify=%d): %d", _sendsock, n);
    }

}

//
// add a command to the queue
// commands are sorted chonologically and new entries are always added AFTER entries with the same timestamp, so if you add several for immediate execution they will be executed in order
//

static int _ick_notification_queue(struct _ick_sender_cmd * cmd) {
    struct _ick_sender_cmd * entry = LIST_FIRST(&_ick_send_cmdlisthead);
    struct _ick_sender_cmd * last = NULL;
    
    while (entry) {
        last = entry;
        // new command shall be executed before this entry? Done!
        if (entry->time.tv_sec > cmd->time.tv_sec)
            break;
        if (entry->time.tv_usec == cmd->time.tv_sec) {
            if (entry->time.tv_usec > cmd->time.tv_usec) // The entries have equal time? Insert AFTER the existing entry
                break;
        }
        entry = LIST_NEXT(entry, entries);
    }
    if (entry)
        LIST_INSERT_BEFORE(entry, cmd, entries);
    else if (last)
        LIST_INSERT_AFTER(last, cmd, entries);
    else
        LIST_INSERT_HEAD(&_ick_send_cmdlisthead, cmd, entries);
    
    return 0;
}


// these are in microseconds. 
// RETRYDELAY is the maximum delay until a message is resent. All ickStream discovery messages are being resent once per interval.
// FUZZ is the maximum delay being used until an "immediate" command is actually being send. exception: "remove".
// these are in microseconds now.
#define ICKDISCOVERY_RETRYDELAY         100000  //100ms
#define ICKDISCOVERY_FUZZ               10000   //10ms


#define TIME_ADD(time,interval) do { \
long ___ms=(time).tv_usec + (interval); \
(time).tv_usec = ___ms % 1000000; \
(time).tv_sec += (long)(___ms / 1000000); \
} while (0)

//struct timeval debtv; debtv.tv_sec = (time).tv_sec; debtv.tv_usec = (time).tv_usec; \
//debug("TIME_ADD: from (%d, %d) to (%d, %d) delta %d\n", debtv.tv_sec, debtv.tv_usec, (time).tv_sec, (time).tv_usec, (___ms - debtv.tv_usec)); \

//
// notification command execution thread
// this threads will execute scheduled notifications and search requests
// the first message might be delayed up to 100ms, more urgent messages must not be sent through the command list
// a "quit" command will end the thread but may also be delayed up to 100ms.
// 

static void * _ick_notification_request_thread (void * dummy) {
    struct timespec duration = {0,0};
    struct timeval time = {0,0};
    long ms;
    char * msg[3] = {NULL, NULL, NULL};
    int i;
    struct _upnp_service * var;
    struct _upnp_service * entry;

    while (1) {
        pthread_mutex_lock(&_ick_sender_mutex);
        struct _ick_sender_cmd * cmd = LIST_FIRST(&_ick_send_cmdlisthead);
        while (cmd) {
            // there's a time for the command?
            if (cmd->time.tv_sec) {
                gettimeofday(&time, NULL);
                // commands are sorted.
                // the first command is already more than 1s in the future? We're done, go sleep.
                if ((time.tv_sec + 1) < cmd->time.tv_sec)
                    break;
                ms = (cmd->time.tv_sec - time.tv_sec) * 1000000 + (cmd->time.tv_usec - time.tv_usec);    // time to event
                if (ms > 100000)    // more than 100ms away -> go sleep
                    break;
                // time to event, but less than 100ms, sleep a bit
                if (ms > 0) {
                    duration.tv_nsec = ms * 1000; // nanoseconds
                    nanosleep(&duration, NULL);
                }
            }
            LIST_REMOVE(cmd, entries);
            switch (cmd->command) {
                case ICK_SEND_QUIT: {
                    // clear queue; We assume nobody is adding commands after a QUIT has been encountered. Periodic notifications can't because they are only processed in this thread.
                    while (cmd) {
                        if (cmd->command == ICK_SEND_CMD_NOTIFY_REMOVE)
                            free(cmd->data); // Free data for removes
                        if (cmd->command == ICK_SEND_CMD_EXPIRE_DEVICE) { // expire all devices
                            struct _upnp_device * device = cmd->data;
                            pthread_mutex_unlock(&_ick_sender_mutex); // remove will lock the command queue again
                            removeDevice(_discovery, device->headers);
                            pthread_mutex_lock(&_ick_sender_mutex); // and lock again, we need the command list again
                        }
                        free(cmd);
                        cmd = LIST_FIRST(&_ick_send_cmdlisthead);
                        if (cmd)
                            LIST_REMOVE(cmd, entries);
                    }
                    pthread_mutex_unlock(&_ick_sender_mutex);
                    close(_sendsock);
                    _sendsock = 0;
                    
                    if (_discovery->exitCallback)
                        _discovery->exitCallback();

                    return NULL;
                }
                    break;
                    
                case ICK_SEND_CMD_NOTIFY_ADD: {
                    entry = cmd->data;
                    i = 0;
                    LIST_VALIDATE_PRESENT(&servicelisthead, var, entry, entries);
                    if (!cmd->data)
                        msg[i] = _ick_notification_create(cmd->command, NULL, 0);
                    i++;
                    if (entry || !(cmd->data))
                        for (; i < 3; i++)
                            msg[i] = _ick_notification_create(cmd->command, entry, i);
                    pthread_mutex_unlock(&_ick_sender_mutex); // unlock thread while sending, we have our data
                    for (i = 0; i < 3; i++)
                        if (msg[i]) {
                            _ick_notification_send_socket(msg[i]);
                            free(msg[i]); msg[i] = NULL;
                        }
                    pthread_mutex_lock(&_ick_sender_mutex); // and lock again, we need the command list again
                    }
                    break;
                    
                case ICK_SEND_CMD_NOTIFY_REMOVE:
                    pthread_mutex_unlock(&_ick_sender_mutex); // unlock thread while sending, we have our data
                    _ick_notification_send_socket(cmd->data); // Remove brings it's own data because the entry is already removed
                    free(cmd->data);
                    pthread_mutex_lock(&_ick_sender_mutex); // and lock again, we need the command list again                    
                    break;
                    
                    // currently, search is one message since we only look for players.
                    // Will end up as an array once we also actively look for servers or root devices.
                    // Controllers and Root devices will end up in our device list even now when they announce themselves
                case ICK_SEND_CMD_SEARCH:
                    pthread_mutex_unlock(&_ick_sender_mutex); // unlock thread while sending, we have our data
                    msg[0] = _ick_notification_create(cmd->command, NULL, 0);
                    _ick_notification_send_socket(msg[0]);
#ifdef SUPPORT_ICK_SERVERS
                    msg[1] = _ick_notification_create(cmd->command, NULL, 1);
                    _ick_notification_send_socket(msg[1]);
#endif
                    pthread_mutex_lock(&_ick_sender_mutex); // and lock again, we need the command list again
                    free(msg[0]); msg[0] = NULL;
#ifdef SUPPORT_ICK_SERVERS
                    free(msg[1]); msg[1] = NULL;
#endif
                    break;
                    
                case ICK_SEND_CMD_NOTIFY_PERIODICADD:
                    entry = cmd->data;
                    LIST_VALIDATE_PRESENT(&servicelisthead, var, entry, entries);
                    if (entry || !(cmd->data)) {
                        _ick_notifications_send(ICK_SEND_CMD_NOTIFY_ADD, entry);
                        ms = ICKDISCOVERY_ANNOUNCE_INTERVAL * 500000;  // 1000 / 2 ... this is supposed to be seconds.
                                                                       //                        debug("ICK_SEND:CMD_NOTIFY_PERIODICADD interval %d\n", ms);
                        TIME_ADD(cmd->time, ms +  (random() % ms));
                        _ick_notification_queue(cmd);
                    }
                    cmd = NULL; // don't free this, we are reusing it!
                    break;
                    
                case ICK_SEND_CMD_NOTIFY_PERIODICSEARCH:
                    _ick_notifications_send(ICK_SEND_CMD_SEARCH, NULL);
                    ms = ICKDISCOVERY_SEARCH_INTERVAL * 500000;  // 1000 / 2;
                                                                 //                    debug("ICK_SEND:CMD_NOTIFY_PERIODICSEARCH interval %d\n", ms);
                    TIME_ADD(cmd->time, ms +  (random() % ms));
                    _ick_notification_queue(cmd);
                    cmd = NULL; // don't free this, we are reusing it!
                    break;
                    
                case ICK_SEND_CMD_EXPIRE_DEVICE: {
                    struct _upnp_device * device = cmd->data;
                    if (time.tv_sec >= device->t) {
                        pthread_mutex_unlock(&_ick_sender_mutex); // remove will lock the command queue again
                        removeDevice(_discovery, device->headers);
                        pthread_mutex_lock(&_ick_sender_mutex); // and lock again, we need the command list again                        
                    } else { // not expired? Got renewed. Update expiration command.
                        cmd->time.tv_sec = device->t;
                        _ick_notification_queue(cmd);
                        cmd = NULL; // don't free this, we are reusing it!                        
                    }
                    // that's it. nothing to free, data is just a reference.
                }
                    
                    break;
                    
                default:
                    break;
            }
            free(cmd);
            cmd = LIST_FIRST(&_ick_send_cmdlisthead);
        }
        pthread_mutex_unlock(&_ick_sender_mutex);
        
        // Now lets read what's coming back from M-SEARCH....
        static char _buffer [520];
        static char * buffer = NULL;
        if (!buffer) {
            memcpy(_buffer, "REPLY * ", 8);
            buffer = _buffer + 8;
        }
        struct sockaddr address;
        socklen_t addrlen;
        ssize_t rcv_size = 0;

        rcv_size = recvfrom(_sendsock, buffer, 512, 0, &address, &addrlen);
        if (rcv_size != -1) {
            debug("\npacket returned %.*s\n", rcv_size, buffer);
            ParseSSDPPacket(_discovery, _buffer, rcv_size + 8, &address);
        }
        duration.tv_nsec = 100000000; // 100ms
        nanosleep(&duration, NULL);
    }
}



//
// create, time and queue a notification command. NOT thread safe, lock has to be handled before this gets called!
// REMOVE is added fully expanded (since it's being sent when the device/service to be removed is no longer present), all others commands just refer to the service
// NULL for the service means we are referring to the root device
// QUIT it only used to end the sender thread and will NOT be repeated, however, it will be delayed by 100ms to allow REMOVEs to be sent
//
int _ick_notifications_send (enum _ick_send_cmd command, struct _upnp_service * service) {
    struct _ick_sender_cmd * cmd;
    struct timeval time;
    gettimeofday(&time, NULL);
    char * msg[3] = {NULL, NULL, NULL};
    int i, j, l;
    long delay = 0;
    
    switch (command) {
        case ICK_SEND_CMD_NOTIFY_ADD: {
            cmd = malloc(sizeof(struct _ick_sender_cmd));
            cmd->command = command;
            cmd->data = service;
            cmd->time.tv_sec = time.tv_sec;
            cmd->time.tv_usec = time.tv_usec;
            TIME_ADD(cmd->time, random() % ICKDISCOVERY_FUZZ);
            _ick_notification_queue(cmd);
            cmd = malloc(sizeof(struct _ick_sender_cmd));
            cmd->command = command;
            cmd->data = service;
            cmd->time.tv_sec = time.tv_sec;
            cmd->time.tv_usec = time.tv_usec;
            delay = ICKDISCOVERY_RETRYDELAY / 2;
            TIME_ADD(cmd->time, delay +  (random() % delay));
            _ick_notification_queue(cmd);
        }
            break;
            
        case ICK_SEND_CMD_NOTIFY_REMOVE: {
            if (!service) {
                msg[0] = _ick_notification_create(command, NULL, 0);
                msg[1] = _ick_notification_create(command, NULL, 1);
            }
            msg[2] = _ick_notification_create(command, service, 2);

            delay = 0;
            for (j = 0; j < 2; j++) {
                if (delay)
                    delay += random() % delay;
                TIME_ADD(time, delay);
                for (i = 0; i < 3; i++) {
                    if (!msg[i])
                        continue;
                    cmd = malloc(sizeof(struct _ick_sender_cmd));            
                    cmd->command = command;
                    l = strlen(msg[i]) + 1;
                    if (!j)  {
                        cmd->data = malloc(l);
                        memcpy(cmd->data, msg[i], l);
                    } else
                        cmd->data = msg[i];
                    cmd->time.tv_sec = time.tv_sec;
                    cmd->time.tv_usec = time.tv_usec;
                    _ick_notification_queue(cmd);
                }
                delay = ICKDISCOVERY_RETRYDELAY / 2;
            }
        }
            break;
            
        case ICK_SEND_CMD_SEARCH:
            delay = ICKDISCOVERY_FUZZ / 2;
            for (j = 0; j < 2; j++) {
                cmd = malloc(sizeof(struct _ick_sender_cmd));            
                cmd->command = command;
                cmd->data = NULL;
                cmd->time.tv_sec = time.tv_sec;
                cmd->time.tv_usec = time.tv_usec;
                TIME_ADD(cmd->time, delay + (random() % delay));
                _ick_notification_queue(cmd);
                delay = ICKDISCOVERY_RETRYDELAY / 2;
            }
            break;

        case ICK_SEND_CMD_NOTIFY_PERIODICADD:
        case ICK_SEND_CMD_NOTIFY_PERIODICSEARCH:
        case ICK_SEND_QUIT:
            if (command == ICK_SEND_QUIT) {
                TIME_ADD(time, ICKDISCOVERY_RETRYDELAY);    // delay to allow REMOVEs to be sent. No need to fuzzy up things.
                service = NULL;     // just to be sure :)
            }
            cmd = malloc(sizeof(struct _ick_sender_cmd));            
            cmd->command = command;
            cmd->data = service;
            cmd->time.tv_sec = time.tv_sec;
            cmd->time.tv_usec = time.tv_usec;
            _ick_notification_queue(cmd);
            break;
            
        default:
            break;
    }
    
    return 0;
}



void _ick_init_discovery_registry (ickDiscovery_t * disc) {
    _ick_sender_struct.UUID = disc->UUID;
    _ick_sender_struct.location = disc->location;
    _ick_sender_struct.osname = disc->osname;
    _ick_sender_struct.port = disc->websocket_port;
    _discovery = disc;
    LIST_INIT(&servicelisthead);
    LIST_INIT(&_ick_send_cmdlisthead);
    pthread_mutex_init(&_ick_sender_mutex, NULL);
    pthread_mutex_init(&_device_mutex, NULL);
    srandom(time(NULL));
    
    if( (_sendsock = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
		debug("error creating sendsocket (udp): %d", _sendsock);
		return;
	}
    
    int yes = 1;
	/*		int err1 = */setsockopt(_sendsock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(int));
	setsockopt(_sendsock, SOL_SOCKET, SO_REUSEADDR, (void *)&yes, sizeof(yes));
    setsockopt(_sendsock, SOL_SOCKET, MSG_NOSIGNAL, (void *)&yes, sizeof(int));	
    
    int flags = fcntl(_sendsock, F_GETFL);
    flags |= O_NONBLOCK;    
    fcntl(_sendsock, F_SETFL, flags);

    if (pthread_create(&(_ick_sender_struct.thread), NULL, _ick_notification_request_thread, NULL)) {
		debug("error creating sender thread");
		return;
    }
    pthread_mutex_lock(&_ick_sender_mutex);
    // announce root device periodically
    _ick_notifications_send(ICK_SEND_CMD_NOTIFY_PERIODICADD, NULL);
    // search for devices periodically
    _ick_notifications_send(ICK_SEND_CMD_NOTIFY_PERIODICSEARCH, NULL);
    pthread_mutex_unlock(&_ick_sender_mutex);
}

void _ick_close_discovery_registry (int wait) {
    pthread_mutex_lock(&_ick_sender_mutex);
    while (servicelisthead.lh_first) {
        _ick_remove_service(servicelisthead.lh_first->st, false);
    };
    // shutdown sender thread;
    _ick_notifications_send(ICK_SEND_QUIT, NULL);
    pthread_mutex_unlock(&_ick_sender_mutex);
    
    if (wait)
        pthread_join(_ick_sender_struct.thread, NULL);
    
    // I'm not sure this is going to work if we don't wait....
    pthread_mutex_destroy(&_device_mutex);
    pthread_mutex_destroy(&_ick_sender_mutex);
}







