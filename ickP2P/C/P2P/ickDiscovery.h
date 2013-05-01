//
//  ickDiscovery.h
//  ickStreamP2P
//
//  ========
//  Copyright (c) 2013 ickStream GmbH.
//  All rights reserved.
//  ========

#ifndef ickStreamProto_ickDiscovery_h
#define ickStreamProto_ickDiscovery_h

#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif
    
    // Re-announce interval
    // this is actually not the interval by which the ickstream discovery re-announces devices but the maximum time between notifications
    // The real value will be a random figure between 50% and 100% of this value
    // Added here so that implementations can change it.
    //
    // UPnP's recommended value is 1,800s but that may be too much for moble players that disconnect/reconnect/lose connection easily because it means we'll expect devices to be valid for this time
    // If the device has been active all the time it's also a low-cost reannouncement, the full cycle is a maximum of 14 UDP packets for a controller and player
    //
#define ICKDISCOVERY_ANNOUNCE_INTERVAL  150
        
    // Search interval. The interval between search requests to the whole network.
    // The search interval works in a similar way as the announce interval but potentially causes more traffic since all devices are supposed to answer.
#define ICKDISCOVERY_SEARCH_INTERVAL    150

    // Define expected maximum response delay UPnP says this needs to be between 1 and 5, let's chose 2 for now.
#define ICKDISCOVERY_SEARCH_DELAY       2
    
    // Default Websocket port
    // ickStreamP2P will usually try to find a free port to use for it's server functionality but if that doesn't work, it will fall back to a default port.
    // This default port can be changed at compile time using this define
    // Can be any port that is usually unused on the target platform, the port number will be communicated through the discovery process
#define WEBSOCKET_PORT  7862
    
    
    typedef enum ickDiscovery_result {
        ICKDISCOVERY_SUCCESS            = 0,
        ICKDISCOVERY_RUNNING            = 1,
        ICKDISCOVERY_SOCKET_ERROR       = 2,
        ICKDISCOVERY_THREAD_ERROR       = 3,
        ICKDISCOVERY_MEMORY_ERROR       = 4
    } ickDiscoveryResult_t;
    
    typedef enum ICKDISCOVERY_SSDP_TYPES {
        ICKDISCOVERY_TYPE_NOTIFY,
        ICKDISCOVERY_TYPE_SEARCH,
        ICKDISCOVERY_TYPE_RESPONSE,
        
        ICKDISCOVERY_SSDP_TYPES_COUNT
    } ickDiscoverySSDPTypes_t;
    
    typedef enum ickDiscovery_command {
        ICKDISCOVERY_ADD_DEVICE,
        ICKDISCOVERY_REMOVE_DEVICE,
        ICKDISCOVERY_UPDATE_DEVICE
    } ickDiscoveryCommand_t;
    
    typedef enum ickDevice_servicetype {
        ICKDEVICE_GENERIC           = 0,
        ICKDEVICE_PLAYER            = 0x1,
        ICKDEVICE_CONTROLLER        = 0x2,
        ICKDEVICE_SERVER_GENERIC    = 0x4,
        ICKDEVICE_DEBUG             = 0x8,
        ICKDEVICE_MAX               = 0x8,
        ICKDEVICE_ANY               = (ICKDEVICE_MAX * 2) - 1,
        ICKDEVICE_NONE              = -1
    } ickDeviceServicetype_t;
    
    struct _ick_discovery_struct;
    typedef struct _ick_discovery_struct ickDiscovery_t;
    
    typedef void (* ickDiscovery_discovery_exit_callback_t)();
    
    // Communication setup commands
    
    // Start the P2P component by initiating the discovery handler. Without it, no connections are being built
    //
    // UUID is a valid UPnP UUID, 
    // interface is the IP address or interface of the network interface to be used (e.g. "192.168.0.19" or "en0"). Using an interface ("en0") is recommended right now.
    // No Multi-homing right now.
    //
    // Spawns a communication handling thread and is reentrant but will not do anything on the second or subsequent call (other than returning ICKDISCOVERY_RUNNING
    //
    ickDiscoveryResult_t ickInitDiscovery(const char * UUID,
                                              const char * interface,
                                              ickDiscovery_discovery_exit_callback_t exitCallback);
    
    // Stop the P2P component. "wait" is a synchronicity flag, it makes the function block until the discovery thread has really ended
    // Currently, using "wait" is recommended. 
    // Using "wait" will usually cause the execution of this function to take at least 100ms for shutting down the sender thread
    // May only be called from the main thread
    //
    // ickInitDiscovery and ickEndDiscovery MUST be paired, that is, ickEndDiscovery is expected to be called before the application goes to sleep or exits and ickEndDiscovery MUST be called before another call to ickInitDiscovery can bve made, anything else will cause a fatal error.
    //
    void ickEndDiscovery(int wait);
    
    //
    // This is a prototype hack for players and controllers only
    // In the long run, we will want to have a full-scale service registry including hooks for the actual service
    // Players and controllers don't need this, though, both are fully being driven through the ickStream connection
    // this currently just ORs a capability into the type enum and sends out an updated capability discovery message
    //
    // Since the capabilities are bound to the discovery object, all Services have to be re-added after an ickEndDiscovery. We should consider whether we want an "ickDiscoverySuspend and ickDiscoveryResume to keep the object alive while shutting down the actual operation.
    //
    int ickDiscoveryAddService(ickDeviceServicetype_t type);
        
    //
    // Remove a capability. We probably want to be able to correctly shut down a player.
    // Not needed befor ickEndDiscovery since this will announce the shutdown of the whole device.
    //
    int ickDiscoveryRemoveService(ickDeviceServicetype_t type);

    //
    // This is another configuration function. 
    // We probably eventually want to replace all these with a common initializer but since it's currently optional I believe it's OK.
    // This sets up a prefferred device name (default name) and a folder from which to serve up any kind of information.
    // ickP2P will serve any content in this folder to any client asking on the right port (to be determined over UPnP Discovery), essentially acting as an http server.
    // No idea what happens to huge files in here (performance wise) so we'd better not have them.
    // But we'd have to make sure this is an extra-folder and not the root of a flat folder structure containing program code.
    //
    // There are notable exceptions. Files named "Player.xml", "Server.xml" or "Controller.xml" will not be served, at least if player, server and controller type is set for the p2p instance.
    // The reason is that these file names are being used as palceholders and the XML files defining the player, server or controller characteristics are internally created
    //
    // One thing that _is_ expected to be served from here are icons.
    // icons can be configured by adding an "iconList.xml" file to this folder, and the icons files, too, of course.
    // iconList.xml specifies the available icon files are expected by UPnP e.g.:
    // <iconList>
    //         <icon>
    //              <mimetype>image/png</mimetype>
    //              <width>114</width>
    //              <height>114</height>
    //              <depth>24</depth>
    //              <url>/icon114.png</url>
    //         </icon>
    // .... more icons ...
    // </iconList>
    // ickP2P will read this xml file but not check for the availability of the icon files!
    //
    // There might be more info added to this folder in the future
    //
    // This function should be called before services are being added or a remote conbtroller might miss the device name
    // Also, deviceinformation is supposed to be static so changing it after a device has been detected will let the change go unnoticed until the device is completely unregistered.
    // Names are supposed to be UTF-8 encoded
    //
    ickDiscoveryResult_t ickDiscoverySetupConfigurationData(const char * defaultDeviceName,
                                                            const char * dataFolder);
    

    /* These are the main commands to communicate to the P2P Core for player/controller communication.
     Service communication would be somewhat similar but uses different protocols. 
     The main difference will probably be that a service has an address/service prefix where a player/controller has a UUID.
     
     Currently, there is NO protocol handling whatsoever in this, all protocol components have to be embedded in the JSON message(s) being sent, this includes request IDs.
     
     General Syntax:
     return value:
     0 - success;
     negative values: errors
     TBD: meaningful error values and an error details retrieval system, currently, only -1 is being returned on any error.
     TBD: define a results enum to cpature this.
     
     char * UUID:       the unique identifier of the device you want to communicate with. For services this would be the service prefix.
     void * message:    the message to be sent in raw, encoded format (that is: a valid, encoded JSON/RPC command.
                        TBD: If we want to allow bundling of several commands in an array, we need to define that on the application layer level; here, the encoded array would be supplied
     size_t size:       the size (in bytes) of the encoded message. Must nox exceed 64KB. TBD: Is this enough? I assume 16 bit size parameters in the protocol, causing this limitation. We probably don't want chunks that are all too big.
          
     TBD: Define whether to also support synchronous calls that wait for a reply.
     TODO: Currently there is no way to de-register callbacks other than to stop the whole ickDiscovery
     */
    
    //
    // List available devices
    // Return value is an array of char * UUID values
    // type valie is a value of ORed together types, all of which have to match the type criteria of a device to be returned
    // NOTE: this array (not the UUIDs) needs to be freed after use by the caller!
    //
    char ** ickDeviceList(ickDeviceServicetype_t type);
    
    //
    //  get the type of device UUID
    //  TBD: we might want to return a pointer to the whole service definition once we've defined service definitions....
    //
    ickDeviceServicetype_t ickDeviceType(const char * UUID);
    
    //
    //  temporary hack: get device URL and port
    //
    char * ickDeviceURL(const char * UUID);
    unsigned short ickDevicePort(const char * UUID);
    
    // get suggested device name
    //
    char * ickDeviceName(const char * UUID);
    
    //
    // Callback function type for callback that is being called whenever a device gets added or removed
    //
    typedef void (* ickDiscovery_device_callback_t)(const char * device, ickDiscoveryCommand_t change, ickDeviceServicetype_t type);
    
    //
    // register device list callback
    // TBD: We might want to expose the internal registration callback that also gets notifications for UPnP devices but required a whole set of additional type definitions because there's more information required for these than just a UUID.
    //
    int ickDeviceRegisterDeviceCallback(ickDiscovery_device_callback_t callback);
    
    //
    // remove device callback
    //
    int ickDeviceRemoveDeviceCallback(ickDiscovery_device_callback_t callback);

    
    typedef enum ickMessage_communicationstate {
        ICKMESSAGE_SUCCESS          = 0,
        ICKMESSAGE_INCOMING_DATA    = 0x1,
        ICKMESSAGE_OUTGOING_DATA    = 0x2,
        
        ICKMESSAGE_DATA_INCOMPLETE  = 0x100,
        ICKMESSAGE_COULD_NOT_SEND   = 0x200,
        ICKMESSAGE_UNKNOWN_TARGET    = 0x400,
        
        ICKMESSAGE_CONNECTION_LOST  = 0x20000
    } ickMessageCommunicationstate_t;
    
    //
    // send a message to device UUID
    // Return value is "ICKMESSAGE_SUCCESS" as long as the target device is known and did not formally disconnect
    // Actual message sending is asynchronous and buffered, in case of disconnects the messages stay queued unless either a reconnect can be established or the target device is formally de-registered (either through a timeout of the device validity or a disconnect notification).
    // TBD: Do we need a timeout to determine when we want ickP2P to stop to try sending messages?
    //
    // Parameters:
    //      targetUUID: the UUID to send the message to. If NULL, the message will be broadcast to all known devices
    //      message: the message to be sent. JSON object encoded as a UTF-8 encoded C-string.
    //      message_size: the size of the message. if 0, the C string will be evaluated, otherwise,
    //                    even non-0-terminated substrings may be sent
    //      service_type: If a device hosts several services, this parameter allows you to target only a subset of them.
    //                    It's an OR-mask of the services to be addressed. The message will still be sent to all callbacks,
    //                    it's up to the callback to decide whether the message is relevant for it
    //      sourceUUID: If a device hosts subdevices (with separate UUIDs), this can be used to tell the target,
    //                  which UUID the message is originating from. '0' means the root UUID will be used.
    //                  This feature is not yet supported, the value is reserved and should default to the UUID of the
    //                  source where known.
    //
    ickMessageCommunicationstate_t ickDeviceSendMsg(const char * targetUUID,
                                                    const char * message,
                                                    const size_t message_size);
    ickMessageCommunicationstate_t ickDeviceSendTargetedMsg(const char * targetUUID,
                                                            const char * message,
                                                            const size_t message_size,
                                                            ickDeviceServicetype_t service_type,
                                                            const char * sourceUUID);
    
    //
    // Callback function type for callback that is being called whenever a message comes in
    // NOTE: since ickStreamP2P currently is completely asynchronous, both notifications and replies are being retrievd through this.
    // TBD: we should define whether we want to separate notifications and replies but this requires P2P to inspect the content
    //
    // NOTE: To use the data handed over, you MUST copy it and you MUST NOT free it. More than one callback can be registered and all receive the same data block, after this it's being freed or reused and overwritten with new data.
    //
    //  NOTE: The target UUID is reserved and will currently always contain the UUID of the root device of the current client
    //
    typedef void (* ickDevice_message_callback_t)(const char * sourceUUID, const char * message, size_t message_size, ickMessageCommunicationstate_t state, ickDeviceServicetype_t service_type, const char * targetUUID);
    
    //
    // register message callback
    //
    int ickDeviceRegisterMessageCallback(ickDevice_message_callback_t callback);
    
    //
    // remove message callback
    //
    int ickDeviceRemoveMessageCallback(ickDevice_message_callback_t callback);
    
    
    // debug commands
    
    // get debug info for a device
    // output is JSON string
    // output format is a dictionary containing the device information
    char * ickDeviceGetLocalDebugInfoForDevice(char * UUID);

    // get debug info for all devices
    // output is JSON string
    // output format is an array containing all device information
    char * ickDeviceGetLocalDebugInfo();

    // get debug info for a remote device
    // output is JSON string
    // output format is a an array of dictionaries containing the device information
    // Note: this is a blocking call synchronously retrieving data from a remote device!
    // Call it from a separate thread to avoid blocking. Use is thread safe.
    char * ickDeviceGetRemoteDebugInfoForDevice(char * UUID);
    
    // get debug info for a remote device
    // output is JSON string
    // output format is a dictionary with debug information about the state of debugUUID as seen on UUID
    // Note: this is a blocking call synchronously retrieving data from a remote device!
    // Call it from a separate thread to avoid blocking. Use is thread safe.
    char * ickDeviceGetRemoteDebugInfoForDeviceQueryDevice(char * UUID, char * debugUUID);
    
    // enable debug mirroring and broadcasting
    void enableDebugCallback(int on);
    
    // set logging callback
    typedef void (ickDiscovery_log_facility_t)( const char *file, int line, int prio, const char * format, ... );
    void ickDiscoverySetLogFacility(ickDiscovery_log_facility_t * function);
    
    // get version information. Returns version information in major/minor ints and returns version string
    char * ickDiscoveryGetVersion(int *major, int *minor);
    // return git version of build
    char * ickDiscoveryGetGitVersion();
    
    
#ifdef __cplusplus
}
#endif
    
#endif
