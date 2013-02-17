# --------------------------------------------------------------
#
# Description     : makefile for the ickstream p2p library
#
# Comments        : -
#
# Date            : 16.02.2013
#
# Updates         : -
#
# Author          : 
#                  
# Remarks         : -
#
# Copyright (c) 2013 ickStream GmbH.
# All rights reserved.
# --------------------------------------------------------------


LIBDIR          = lib
INCLUDEDIR		= include
LIBNAME         = libickstreamp2p
#GITVERSION      = $(shell git rev-list HEAD --count)
GITVERSION      = $(shell git rev-list HEAD --max-count=1)

AR              = ar
CC              = cc
CFLAGS          = -Wall -g -rdynamic -DLWS_NO_FORK -DGIT_VERSION=$(GITVERSION) -D_GNU_SOURCE

# Source files to process
ICKP2PSRCS      = ickP2P/C/P2P/ickDiscovery.c  ickP2P/C/P2P/ickDiscoveryRegistry.c  \
                  ickP2P/C/P2P/ickP2PComm.c
MINIUPNPSRCS    = miniupnp/miniupnpc/connecthostport.c miniupnp/miniupnpc/miniwget.c \
                  miniupnp/miniupnpc/minixml.c miniupnp/miniupnpc/receivedata.c
MINISSDPDSRCS   = miniupnp/minissdpd/openssdpsocket.c miniupnp/minissdpd/upnputils.c
WEBSOCKETSSRCS  = libwebsockets/lib/libwebsockets.c libwebsockets/lib/sha-1.c \
                  libwebsockets/lib/parsers.c libwebsockets/lib/md5.c libwebsockets/lib/handshake.c \
                  libwebsockets/lib/extension.c libwebsockets/lib/base64-decode.c \
                  libwebsockets/lib/client-handshake.c libwebsockets/lib/extension-deflate-stream.c \
                  libwebsockets/lib/extension-x-google-mux.c
JANSSONSRCS     = jansson/src/value.c jansson/src/memory.c jansson/src/dump.c \
                  jansson/src/hashtable.c jansson/src/strbuffer.c jansson/src/utf.c \
                  jansson/src/pack_unpack.c jansson/src/error.c jansson/src/strconv.c

SRC             = $(ICKP2PSRCS) $(MINIUPNPSRCS) $(MINISSDPDSRCS) $(WEBSOCKETSSRCS) $(JANSSONSRCS)
OBJECTS         = $(SRC:.c=.o)


# Include directories and special headers
INTERNALINCLUDES = -IickP2P/C/P2P -Iminiupnp/minissdpd -Iminiupnp/miniupnpc -Ilibwebsockets/lib -Ijansson/src
PUBLICHEADERS    = ickP2P/C/P2P/ickDiscovery.h
GENHEADERS       = jansson/src/jansson_config.h miniupnp/miniupnpc/miniupnpcstrings.h
s

# How to compile c source files
%.o: %.c
	$(CC) $(INTERNALINCLUDES) $(INCLUDES) $(CFLAGS) $(DEBUGFLAGS) -c $< -o $@


# Default rule: make all
all:  $(GENHEADERS) $(INCLUDEDIR) $(LIBDIR)/$(LIBNAME).a 


# Provide public headers
$(INCLUDEDIR): $(PUBLICHEADERS)
	@echo '*************************************************************'
	@echo "Collecting public headers:"
	@test -d $(INCLUDEDIR) || mkdir $(INCLUDEDIR)
	cp -f $? $@


# Minimal configuration of jansson 
# TODO: probably this needs be adjusted...
jansson/src/jansson_config.h: jansson/src/jansson_config.h.squeezebox
	cp $< $@


# Minimal configuration of miniupnp
miniupnp/miniupnpc/miniupnpcstrings.h: miniupnp/miniupnpc/miniupnpcstrings.h.in
	cd miniupnp/miniupnpc;$(MAKE) miniupnpcstrings.h


# How to build the static library
$(LIBDIR)/$(LIBNAME).a: $(OBJECTS) 
	@echo '*************************************************************'
	@echo "Building library:"
	@test -d $(LIBDIR) || mkdir $(LIBDIR)

	ar cr $@ $?
	ar ts >/dev/null $@


# How to update from git
update:
	@echo '*************************************************************'
	@echo "Updating from git repository:"
	git pull --recurse-submodules
	git submodule update --recursive


# How to create dependencies
depend:
	@echo '*************************************************************'
	@echo "Creating dependencies:"
	makedepend -- $(INTERNALINCLUDES) $(INCLUDES) $(CFLAGS) -- $(SRC)


# How to clean tempoarary files
clean:
	@echo '*************************************************************'
	@echo "Deleting intermediate files:"
	rm -f $(OBJECTS)  $(GENHEADERS)


# How to clean all
cleanall: clean
	@echo '*************************************************************'
	@echo "Clean all:"
	rm -rf $(LIBDIR) $(INCLUDEDIR)

# End of Makefile -- makedepend output might follow ...

# DO NOT DELETE

ickP2P/C/P2P/ickDiscovery.o: ickP2P/C/P2P/ickDiscovery.h
ickP2P/C/P2P/ickDiscovery.o: ickP2P/C/P2P/ickDiscoveryInternal.h
ickP2P/C/P2P/ickDiscovery.o: /usr/include/stdio.h /usr/include/features.h
ickP2P/C/P2P/ickDiscovery.o: /usr/include/libio.h /usr/include/_G_config.h
ickP2P/C/P2P/ickDiscovery.o: /usr/include/wchar.h /usr/include/xlocale.h
ickP2P/C/P2P/ickDiscovery.o: /usr/include/unistd.h /usr/include/getopt.h
ickP2P/C/P2P/ickDiscovery.o: /usr/include/pthread.h /usr/include/endian.h
ickP2P/C/P2P/ickDiscovery.o: /usr/include/sched.h /usr/include/time.h
ickP2P/C/P2P/ickDiscovery.o: /usr/include/netinet/in.h /usr/include/stdint.h
ickP2P/C/P2P/ickDiscovery.o: /usr/include/netinet/tcp.h
ickP2P/C/P2P/ickDiscovery.o: /usr/include/arpa/inet.h /usr/include/stdlib.h
ickP2P/C/P2P/ickDiscovery.o: /usr/include/alloca.h /usr/include/string.h
ickP2P/C/P2P/ickDiscovery.o: /usr/include/strings.h /usr/include/ctype.h
ickP2P/C/P2P/ickDiscovery.o: libwebsockets/lib/libwebsockets.h
ickP2P/C/P2P/ickDiscovery.o: /usr/include/poll.h
ickP2P/C/P2P/ickDiscovery.o: miniupnp/minissdpd/openssdpsocket.h
ickP2P/C/P2P/ickDiscovery.o: miniupnp/minissdpd/upnputils.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/stdio.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/features.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/libio.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/_G_config.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/wchar.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/xlocale.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/pthread.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/endian.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/sched.h /usr/include/time.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/fcntl.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: ickP2P/C/P2P/ickDiscovery.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: ickP2P/C/P2P/ickDiscoveryInternal.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/unistd.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/getopt.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/netinet/in.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/stdint.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/netinet/tcp.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/arpa/inet.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/stdlib.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/alloca.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/string.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/strings.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/ctype.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: libwebsockets/lib/libwebsockets.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: /usr/include/poll.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: miniupnp/miniupnpc/miniwget.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: miniupnp/miniupnpc/declspec.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: miniupnp/miniupnpc/minixml.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: jansson/src/jansson.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: jansson/src/jansson_config.h
ickP2P/C/P2P/ickP2PComm.o: /usr/include/stdio.h /usr/include/features.h
ickP2P/C/P2P/ickP2PComm.o: /usr/include/libio.h /usr/include/_G_config.h
ickP2P/C/P2P/ickP2PComm.o: /usr/include/wchar.h /usr/include/xlocale.h
ickP2P/C/P2P/ickP2PComm.o: /usr/include/stdlib.h /usr/include/alloca.h
ickP2P/C/P2P/ickP2PComm.o: /usr/include/string.h
ickP2P/C/P2P/ickP2PComm.o: libwebsockets/lib/libwebsockets.h
ickP2P/C/P2P/ickP2PComm.o: /usr/include/poll.h ickP2P/C/P2P/ickDiscovery.h
ickP2P/C/P2P/ickP2PComm.o: ickP2P/C/P2P/ickDiscoveryInternal.h
ickP2P/C/P2P/ickP2PComm.o: /usr/include/unistd.h /usr/include/getopt.h
ickP2P/C/P2P/ickP2PComm.o: /usr/include/pthread.h /usr/include/endian.h
ickP2P/C/P2P/ickP2PComm.o: /usr/include/sched.h /usr/include/time.h
ickP2P/C/P2P/ickP2PComm.o: /usr/include/netinet/in.h /usr/include/stdint.h
ickP2P/C/P2P/ickP2PComm.o: /usr/include/netinet/tcp.h
ickP2P/C/P2P/ickP2PComm.o: /usr/include/arpa/inet.h /usr/include/strings.h
ickP2P/C/P2P/ickP2PComm.o: /usr/include/ctype.h
miniupnp/miniupnpc/connecthostport.o: /usr/include/string.h
miniupnp/miniupnpc/connecthostport.o: /usr/include/features.h
miniupnp/miniupnpc/connecthostport.o: /usr/include/xlocale.h
miniupnp/miniupnpc/connecthostport.o: /usr/include/stdio.h
miniupnp/miniupnpc/connecthostport.o: /usr/include/libio.h
miniupnp/miniupnpc/connecthostport.o: /usr/include/_G_config.h
miniupnp/miniupnpc/connecthostport.o: /usr/include/wchar.h
miniupnp/miniupnpc/connecthostport.o: /usr/include/unistd.h
miniupnp/miniupnpc/connecthostport.o: /usr/include/getopt.h
miniupnp/miniupnpc/connecthostport.o: /usr/include/errno.h
miniupnp/miniupnpc/connecthostport.o: /usr/include/netdb.h
miniupnp/miniupnpc/connecthostport.o: /usr/include/netinet/in.h
miniupnp/miniupnpc/connecthostport.o: /usr/include/stdint.h
miniupnp/miniupnpc/connecthostport.o: /usr/include/endian.h
miniupnp/miniupnpc/connecthostport.o: /usr/include/rpc/netdb.h
miniupnp/miniupnpc/connecthostport.o: /usr/include/time.h
miniupnp/miniupnpc/connecthostport.o: miniupnp/miniupnpc/connecthostport.h
miniupnp/miniupnpc/miniwget.o: /usr/include/stdio.h /usr/include/features.h
miniupnp/miniupnpc/miniwget.o: /usr/include/libio.h /usr/include/_G_config.h
miniupnp/miniupnpc/miniwget.o: /usr/include/wchar.h /usr/include/xlocale.h
miniupnp/miniupnpc/miniwget.o: /usr/include/stdlib.h /usr/include/alloca.h
miniupnp/miniupnpc/miniwget.o: /usr/include/string.h /usr/include/ctype.h
miniupnp/miniupnpc/miniwget.o: /usr/include/endian.h /usr/include/unistd.h
miniupnp/miniupnpc/miniwget.o: /usr/include/getopt.h
miniupnp/miniupnpc/miniwget.o: /usr/include/netinet/in.h
miniupnp/miniupnpc/miniwget.o: /usr/include/stdint.h /usr/include/arpa/inet.h
miniupnp/miniupnpc/miniwget.o: /usr/include/net/if.h /usr/include/netdb.h
miniupnp/miniupnpc/miniwget.o: /usr/include/rpc/netdb.h /usr/include/time.h
miniupnp/miniupnpc/miniwget.o: miniupnp/miniupnpc/miniupnpcstrings.h
miniupnp/miniupnpc/miniwget.o: miniupnp/miniupnpc/miniwget.h
miniupnp/miniupnpc/miniwget.o: miniupnp/miniupnpc/declspec.h
miniupnp/miniupnpc/miniwget.o: miniupnp/miniupnpc/connecthostport.h
miniupnp/miniupnpc/miniwget.o: miniupnp/miniupnpc/receivedata.h
miniupnp/miniupnpc/minixml.o: /usr/include/string.h /usr/include/features.h
miniupnp/miniupnpc/minixml.o: /usr/include/xlocale.h
miniupnp/miniupnpc/minixml.o: miniupnp/miniupnpc/minixml.h
miniupnp/miniupnpc/receivedata.o: /usr/include/stdio.h
miniupnp/miniupnpc/receivedata.o: /usr/include/features.h
miniupnp/miniupnpc/receivedata.o: /usr/include/libio.h
miniupnp/miniupnpc/receivedata.o: /usr/include/_G_config.h
miniupnp/miniupnpc/receivedata.o: /usr/include/wchar.h /usr/include/xlocale.h
miniupnp/miniupnpc/receivedata.o: /usr/include/unistd.h /usr/include/getopt.h
miniupnp/miniupnpc/receivedata.o: /usr/include/netinet/in.h
miniupnp/miniupnpc/receivedata.o: /usr/include/stdint.h /usr/include/endian.h
miniupnp/miniupnpc/receivedata.o: /usr/include/poll.h /usr/include/errno.h
miniupnp/miniupnpc/receivedata.o: miniupnp/miniupnpc/receivedata.h
miniupnp/minissdpd/openssdpsocket.o: miniupnp/minissdpd/config.h
miniupnp/minissdpd/openssdpsocket.o: /usr/include/string.h
miniupnp/minissdpd/openssdpsocket.o: /usr/include/features.h
miniupnp/minissdpd/openssdpsocket.o: /usr/include/xlocale.h
miniupnp/minissdpd/openssdpsocket.o: /usr/include/unistd.h
miniupnp/minissdpd/openssdpsocket.o: /usr/include/getopt.h
miniupnp/minissdpd/openssdpsocket.o: /usr/include/netinet/in.h
miniupnp/minissdpd/openssdpsocket.o: /usr/include/stdint.h
miniupnp/minissdpd/openssdpsocket.o: /usr/include/endian.h
miniupnp/minissdpd/openssdpsocket.o: /usr/include/arpa/inet.h
miniupnp/minissdpd/openssdpsocket.o: /usr/include/net/if.h
miniupnp/minissdpd/openssdpsocket.o: /usr/include/syslog.h
miniupnp/minissdpd/openssdpsocket.o: miniupnp/minissdpd/openssdpsocket.h
miniupnp/minissdpd/openssdpsocket.o: miniupnp/minissdpd/upnputils.h
miniupnp/minissdpd/upnputils.o: miniupnp/minissdpd/config.h
miniupnp/minissdpd/upnputils.o: /usr/include/stdio.h /usr/include/features.h
miniupnp/minissdpd/upnputils.o: /usr/include/libio.h /usr/include/_G_config.h
miniupnp/minissdpd/upnputils.o: /usr/include/wchar.h /usr/include/xlocale.h
miniupnp/minissdpd/upnputils.o: /usr/include/unistd.h /usr/include/getopt.h
miniupnp/minissdpd/upnputils.o: /usr/include/fcntl.h /usr/include/time.h
miniupnp/minissdpd/upnputils.o: /usr/include/netinet/in.h
miniupnp/minissdpd/upnputils.o: /usr/include/stdint.h /usr/include/endian.h
miniupnp/minissdpd/upnputils.o: /usr/include/arpa/inet.h
miniupnp/minissdpd/upnputils.o: miniupnp/minissdpd/upnputils.h
libwebsockets/lib/libwebsockets.o: libwebsockets/lib/private-libwebsockets.h
libwebsockets/lib/libwebsockets.o: /usr/include/unistd.h
libwebsockets/lib/libwebsockets.o: /usr/include/features.h
libwebsockets/lib/libwebsockets.o: /usr/include/getopt.h /usr/include/stdio.h
libwebsockets/lib/libwebsockets.o: /usr/include/libio.h
libwebsockets/lib/libwebsockets.o: /usr/include/_G_config.h
libwebsockets/lib/libwebsockets.o: /usr/include/wchar.h
libwebsockets/lib/libwebsockets.o: /usr/include/xlocale.h
libwebsockets/lib/libwebsockets.o: /usr/include/stdlib.h
libwebsockets/lib/libwebsockets.o: /usr/include/alloca.h
libwebsockets/lib/libwebsockets.o: /usr/include/string.h
libwebsockets/lib/libwebsockets.o: /usr/include/strings.h
libwebsockets/lib/libwebsockets.o: /usr/include/ctype.h /usr/include/endian.h
libwebsockets/lib/libwebsockets.o: /usr/include/errno.h /usr/include/fcntl.h
libwebsockets/lib/libwebsockets.o: /usr/include/time.h /usr/include/signal.h
libwebsockets/lib/libwebsockets.o: /usr/include/netdb.h
libwebsockets/lib/libwebsockets.o: /usr/include/netinet/in.h
libwebsockets/lib/libwebsockets.o: /usr/include/stdint.h
libwebsockets/lib/libwebsockets.o: /usr/include/rpc/netdb.h
libwebsockets/lib/libwebsockets.o: /usr/include/netinet/tcp.h
libwebsockets/lib/libwebsockets.o: /usr/include/arpa/inet.h
libwebsockets/lib/libwebsockets.o: /usr/include/poll.h
libwebsockets/lib/libwebsockets.o: libwebsockets/lib/libwebsockets.h
libwebsockets/lib/libwebsockets.o: /usr/include/net/if.h
libwebsockets/lib/sha-1.o: /usr/include/string.h /usr/include/features.h
libwebsockets/lib/sha-1.o: /usr/include/xlocale.h
libwebsockets/lib/parsers.o: libwebsockets/lib/private-libwebsockets.h
libwebsockets/lib/parsers.o: /usr/include/unistd.h /usr/include/features.h
libwebsockets/lib/parsers.o: /usr/include/getopt.h /usr/include/stdio.h
libwebsockets/lib/parsers.o: /usr/include/libio.h /usr/include/_G_config.h
libwebsockets/lib/parsers.o: /usr/include/wchar.h /usr/include/xlocale.h
libwebsockets/lib/parsers.o: /usr/include/stdlib.h /usr/include/alloca.h
libwebsockets/lib/parsers.o: /usr/include/string.h /usr/include/strings.h
libwebsockets/lib/parsers.o: /usr/include/ctype.h /usr/include/endian.h
libwebsockets/lib/parsers.o: /usr/include/errno.h /usr/include/fcntl.h
libwebsockets/lib/parsers.o: /usr/include/time.h /usr/include/signal.h
libwebsockets/lib/parsers.o: /usr/include/netdb.h /usr/include/netinet/in.h
libwebsockets/lib/parsers.o: /usr/include/stdint.h /usr/include/rpc/netdb.h
libwebsockets/lib/parsers.o: /usr/include/netinet/tcp.h
libwebsockets/lib/parsers.o: /usr/include/arpa/inet.h /usr/include/poll.h
libwebsockets/lib/parsers.o: libwebsockets/lib/libwebsockets.h
libwebsockets/lib/md5.o: /usr/include/string.h /usr/include/features.h
libwebsockets/lib/md5.o: /usr/include/xlocale.h /usr/include/stdio.h
libwebsockets/lib/md5.o: /usr/include/libio.h /usr/include/_G_config.h
libwebsockets/lib/md5.o: /usr/include/wchar.h
libwebsockets/lib/handshake.o: libwebsockets/lib/private-libwebsockets.h
libwebsockets/lib/handshake.o: /usr/include/unistd.h /usr/include/features.h
libwebsockets/lib/handshake.o: /usr/include/getopt.h /usr/include/stdio.h
libwebsockets/lib/handshake.o: /usr/include/libio.h /usr/include/_G_config.h
libwebsockets/lib/handshake.o: /usr/include/wchar.h /usr/include/xlocale.h
libwebsockets/lib/handshake.o: /usr/include/stdlib.h /usr/include/alloca.h
libwebsockets/lib/handshake.o: /usr/include/string.h /usr/include/strings.h
libwebsockets/lib/handshake.o: /usr/include/ctype.h /usr/include/endian.h
libwebsockets/lib/handshake.o: /usr/include/errno.h /usr/include/fcntl.h
libwebsockets/lib/handshake.o: /usr/include/time.h /usr/include/signal.h
libwebsockets/lib/handshake.o: /usr/include/netdb.h /usr/include/netinet/in.h
libwebsockets/lib/handshake.o: /usr/include/stdint.h /usr/include/rpc/netdb.h
libwebsockets/lib/handshake.o: /usr/include/netinet/tcp.h
libwebsockets/lib/handshake.o: /usr/include/arpa/inet.h /usr/include/poll.h
libwebsockets/lib/handshake.o: libwebsockets/lib/libwebsockets.h
libwebsockets/lib/extension.o: libwebsockets/lib/private-libwebsockets.h
libwebsockets/lib/extension.o: /usr/include/unistd.h /usr/include/features.h
libwebsockets/lib/extension.o: /usr/include/getopt.h /usr/include/stdio.h
libwebsockets/lib/extension.o: /usr/include/libio.h /usr/include/_G_config.h
libwebsockets/lib/extension.o: /usr/include/wchar.h /usr/include/xlocale.h
libwebsockets/lib/extension.o: /usr/include/stdlib.h /usr/include/alloca.h
libwebsockets/lib/extension.o: /usr/include/string.h /usr/include/strings.h
libwebsockets/lib/extension.o: /usr/include/ctype.h /usr/include/endian.h
libwebsockets/lib/extension.o: /usr/include/errno.h /usr/include/fcntl.h
libwebsockets/lib/extension.o: /usr/include/time.h /usr/include/signal.h
libwebsockets/lib/extension.o: /usr/include/netdb.h /usr/include/netinet/in.h
libwebsockets/lib/extension.o: /usr/include/stdint.h /usr/include/rpc/netdb.h
libwebsockets/lib/extension.o: /usr/include/netinet/tcp.h
libwebsockets/lib/extension.o: /usr/include/arpa/inet.h /usr/include/poll.h
libwebsockets/lib/extension.o: libwebsockets/lib/libwebsockets.h
libwebsockets/lib/extension.o: libwebsockets/lib/extension-deflate-stream.h
libwebsockets/lib/extension.o: /usr/include/zlib.h /usr/include/zconf.h
libwebsockets/lib/extension.o: libwebsockets/lib/extension-x-google-mux.h
libwebsockets/lib/base64-decode.o: /usr/include/stdio.h
libwebsockets/lib/base64-decode.o: /usr/include/features.h
libwebsockets/lib/base64-decode.o: /usr/include/libio.h
libwebsockets/lib/base64-decode.o: /usr/include/_G_config.h
libwebsockets/lib/base64-decode.o: /usr/include/wchar.h
libwebsockets/lib/base64-decode.o: /usr/include/xlocale.h
libwebsockets/lib/base64-decode.o: /usr/include/string.h
libwebsockets/lib/client-handshake.o: libwebsockets/lib/private-libwebsockets.h
libwebsockets/lib/client-handshake.o: /usr/include/unistd.h
libwebsockets/lib/client-handshake.o: /usr/include/features.h
libwebsockets/lib/client-handshake.o: /usr/include/getopt.h
libwebsockets/lib/client-handshake.o: /usr/include/stdio.h
libwebsockets/lib/client-handshake.o: /usr/include/libio.h
libwebsockets/lib/client-handshake.o: /usr/include/_G_config.h
libwebsockets/lib/client-handshake.o: /usr/include/wchar.h
libwebsockets/lib/client-handshake.o: /usr/include/xlocale.h
libwebsockets/lib/client-handshake.o: /usr/include/stdlib.h
libwebsockets/lib/client-handshake.o: /usr/include/alloca.h
libwebsockets/lib/client-handshake.o: /usr/include/string.h
libwebsockets/lib/client-handshake.o: /usr/include/strings.h
libwebsockets/lib/client-handshake.o: /usr/include/ctype.h
libwebsockets/lib/client-handshake.o: /usr/include/endian.h
libwebsockets/lib/client-handshake.o: /usr/include/errno.h
libwebsockets/lib/client-handshake.o: /usr/include/fcntl.h
libwebsockets/lib/client-handshake.o: /usr/include/time.h
libwebsockets/lib/client-handshake.o: /usr/include/signal.h
libwebsockets/lib/client-handshake.o: /usr/include/netdb.h
libwebsockets/lib/client-handshake.o: /usr/include/netinet/in.h
libwebsockets/lib/client-handshake.o: /usr/include/stdint.h
libwebsockets/lib/client-handshake.o: /usr/include/rpc/netdb.h
libwebsockets/lib/client-handshake.o: /usr/include/netinet/tcp.h
libwebsockets/lib/client-handshake.o: /usr/include/arpa/inet.h
libwebsockets/lib/client-handshake.o: /usr/include/poll.h
libwebsockets/lib/client-handshake.o: libwebsockets/lib/libwebsockets.h
libwebsockets/lib/extension-deflate-stream.o: libwebsockets/lib/private-libwebsockets.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/unistd.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/features.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/getopt.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/stdio.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/libio.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/_G_config.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/wchar.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/xlocale.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/stdlib.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/alloca.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/string.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/strings.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/ctype.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/endian.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/errno.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/fcntl.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/time.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/signal.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/netdb.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/netinet/in.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/stdint.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/rpc/netdb.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/netinet/tcp.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/arpa/inet.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/poll.h
libwebsockets/lib/extension-deflate-stream.o: libwebsockets/lib/libwebsockets.h
libwebsockets/lib/extension-deflate-stream.o: libwebsockets/lib/extension-deflate-stream.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/zlib.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/zconf.h
libwebsockets/lib/extension-deflate-stream.o: /usr/include/assert.h
libwebsockets/lib/extension-x-google-mux.o: libwebsockets/lib/private-libwebsockets.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/unistd.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/features.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/getopt.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/stdio.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/libio.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/_G_config.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/wchar.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/xlocale.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/stdlib.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/alloca.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/string.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/strings.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/ctype.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/endian.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/errno.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/fcntl.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/time.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/signal.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/netdb.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/netinet/in.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/stdint.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/rpc/netdb.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/netinet/tcp.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/arpa/inet.h
libwebsockets/lib/extension-x-google-mux.o: /usr/include/poll.h
libwebsockets/lib/extension-x-google-mux.o: libwebsockets/lib/libwebsockets.h
libwebsockets/lib/extension-x-google-mux.o: libwebsockets/lib/extension-x-google-mux.h
jansson/src/value.o: /usr/include/stdlib.h /usr/include/features.h
jansson/src/value.o: /usr/include/xlocale.h /usr/include/alloca.h
jansson/src/value.o: /usr/include/string.h /usr/include/math.h
jansson/src/value.o: jansson/src/jansson.h /usr/include/stdio.h
jansson/src/value.o: /usr/include/libio.h /usr/include/_G_config.h
jansson/src/value.o: /usr/include/wchar.h jansson/src/jansson_config.h
jansson/src/value.o: jansson/src/hashtable.h jansson/src/jansson_private.h
jansson/src/value.o: jansson/src/strbuffer.h jansson/src/utf.h
jansson/src/value.o: /usr/include/inttypes.h /usr/include/stdint.h
jansson/src/memory.o: /usr/include/stdlib.h /usr/include/features.h
jansson/src/memory.o: /usr/include/xlocale.h /usr/include/alloca.h
jansson/src/memory.o: /usr/include/string.h jansson/src/jansson.h
jansson/src/memory.o: /usr/include/stdio.h /usr/include/libio.h
jansson/src/memory.o: /usr/include/_G_config.h /usr/include/wchar.h
jansson/src/memory.o: jansson/src/jansson_config.h
jansson/src/memory.o: jansson/src/jansson_private.h jansson/src/hashtable.h
jansson/src/memory.o: jansson/src/strbuffer.h
jansson/src/dump.o: /usr/include/stdio.h /usr/include/features.h
jansson/src/dump.o: /usr/include/libio.h /usr/include/_G_config.h
jansson/src/dump.o: /usr/include/wchar.h /usr/include/xlocale.h
jansson/src/dump.o: /usr/include/stdlib.h /usr/include/alloca.h
jansson/src/dump.o: /usr/include/string.h /usr/include/assert.h
jansson/src/dump.o: jansson/src/jansson.h jansson/src/jansson_config.h
jansson/src/dump.o: jansson/src/jansson_private.h jansson/src/hashtable.h
jansson/src/dump.o: jansson/src/strbuffer.h jansson/src/utf.h
jansson/src/dump.o: /usr/include/inttypes.h /usr/include/stdint.h
jansson/src/hashtable.o: /usr/include/stdlib.h /usr/include/features.h
jansson/src/hashtable.o: /usr/include/xlocale.h /usr/include/alloca.h
jansson/src/hashtable.o: /usr/include/string.h jansson/src/jansson_config.h
jansson/src/hashtable.o: jansson/src/jansson_private.h jansson/src/jansson.h
jansson/src/hashtable.o: /usr/include/stdio.h /usr/include/libio.h
jansson/src/hashtable.o: /usr/include/_G_config.h /usr/include/wchar.h
jansson/src/hashtable.o: jansson/src/hashtable.h jansson/src/strbuffer.h
jansson/src/strbuffer.o: /usr/include/stdlib.h /usr/include/features.h
jansson/src/strbuffer.o: /usr/include/xlocale.h /usr/include/alloca.h
jansson/src/strbuffer.o: /usr/include/string.h jansson/src/jansson_private.h
jansson/src/strbuffer.o: jansson/src/jansson.h /usr/include/stdio.h
jansson/src/strbuffer.o: /usr/include/libio.h /usr/include/_G_config.h
jansson/src/strbuffer.o: /usr/include/wchar.h jansson/src/jansson_config.h
jansson/src/strbuffer.o: jansson/src/hashtable.h jansson/src/strbuffer.h
jansson/src/utf.o: /usr/include/string.h /usr/include/features.h
jansson/src/utf.o: /usr/include/xlocale.h jansson/src/utf.h
jansson/src/utf.o: /usr/include/inttypes.h /usr/include/stdint.h
jansson/src/pack_unpack.o: /usr/include/string.h /usr/include/features.h
jansson/src/pack_unpack.o: /usr/include/xlocale.h jansson/src/jansson.h
jansson/src/pack_unpack.o: /usr/include/stdio.h /usr/include/libio.h
jansson/src/pack_unpack.o: /usr/include/_G_config.h /usr/include/wchar.h
jansson/src/pack_unpack.o: /usr/include/stdlib.h /usr/include/alloca.h
jansson/src/pack_unpack.o: jansson/src/jansson_config.h
jansson/src/pack_unpack.o: jansson/src/jansson_private.h
jansson/src/pack_unpack.o: jansson/src/hashtable.h jansson/src/strbuffer.h
jansson/src/pack_unpack.o: jansson/src/utf.h /usr/include/inttypes.h
jansson/src/pack_unpack.o: /usr/include/stdint.h
jansson/src/error.o: /usr/include/string.h /usr/include/features.h
jansson/src/error.o: /usr/include/xlocale.h jansson/src/jansson_private.h
jansson/src/error.o: jansson/src/jansson.h /usr/include/stdio.h
jansson/src/error.o: /usr/include/libio.h /usr/include/_G_config.h
jansson/src/error.o: /usr/include/wchar.h /usr/include/stdlib.h
jansson/src/error.o: /usr/include/alloca.h jansson/src/jansson_config.h
jansson/src/error.o: jansson/src/hashtable.h jansson/src/strbuffer.h
jansson/src/strconv.o: /usr/include/assert.h /usr/include/features.h
jansson/src/strconv.o: /usr/include/errno.h /usr/include/stdio.h
jansson/src/strconv.o: /usr/include/libio.h /usr/include/_G_config.h
jansson/src/strconv.o: /usr/include/wchar.h /usr/include/xlocale.h
jansson/src/strconv.o: /usr/include/string.h jansson/src/jansson_private.h
jansson/src/strconv.o: jansson/src/jansson.h /usr/include/stdlib.h
jansson/src/strconv.o: /usr/include/alloca.h jansson/src/jansson_config.h
jansson/src/strconv.o: jansson/src/hashtable.h jansson/src/strbuffer.h
jansson/src/strconv.o: /usr/include/locale.h
