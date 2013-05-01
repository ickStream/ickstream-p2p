# --------------------------------------------------------------
#
# Description     : makefile for the ickstream p2p library
#
# Comments        : -
#
# Date            : 16.02.2013
#
# Updates         : 17.02.03 - added -Y option to makedepend
#
# Author          : 
#                  
# Remarks         : -
#
# Copyright (c) 2013 ickStream GmbH.
# All rights reserved.
# --------------------------------------------------------------


LIBDIR          = lib
INCLUDEDIR	= include
LIBNAME         = libickstreamp2p
#GITVERSION      = $(shell git rev-list HEAD --count)
GITVERSION      = $(shell git rev-list HEAD --max-count=1)

AR              = ar
CC              = cc
CFLAGS          = -Wall -g -rdynamic -DLWS_NO_FORK -DGIT_VERSION=$(GITVERSION) -D_GNU_SOURCE
MKDEPFLAGS	= -Y

# Source files to process
ICKP2PSRCS      = ickP2P/C/P2P/ickDiscovery.c  ickP2P/C/P2P/ickDiscoveryRegistry.c  \
                  ickP2P/C/P2P/ickP2PComm.c ickP2P/C/P2P/logutils.c
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
ZLIBINCLUDES     = 
INCLUDES         = $(ZLIBINCLUDES)

# How to compile c source files
%.o: %.c Makefile
	$(CC) $(INTERNALINCLUDES) $(INCLUDES) $(CFLAGS) $(DEBUGFLAGS) -c $< -o $@


# Default rule: make all
all:  $(GENHEADERS) $(INCLUDEDIR) $(LIBDIR)/$(LIBNAME).a 

# Variant: make for debugging
debug: DEBUGFLAGS = -g -DDEBUG
debug: $(GENHEADERS) $(INCLUDEDIR) $(LIBDIR)/$(LIBNAME).a
 

# Provide public headers
$(INCLUDEDIR): $(PUBLICHEADERS) Makefile
	@echo '*************************************************************'
	@echo "Collecting public headers:"
	@mkdir -p $(INCLUDEDIR)
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
	@mkdir -p $(LIBDIR)
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
	makedepend $(MKDEPFLAGS) -- $(INTERNALINCLUDES) $(INCLUDES) $(CFLAGS) -- $(SRC) 2>/dev/null


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
ickP2P/C/P2P/ickDiscovery.o: libwebsockets/lib/libwebsockets.h
ickP2P/C/P2P/ickDiscovery.o: ickP2P/C/P2P/logutils.h
ickP2P/C/P2P/ickDiscovery.o: miniupnp/minissdpd/openssdpsocket.h
ickP2P/C/P2P/ickDiscovery.o: miniupnp/minissdpd/upnputils.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: ickP2P/C/P2P/ickDiscovery.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: ickP2P/C/P2P/ickDiscoveryInternal.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: libwebsockets/lib/libwebsockets.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: ickP2P/C/P2P/logutils.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: miniupnp/miniupnpc/miniwget.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: miniupnp/miniupnpc/declspec.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: miniupnp/miniupnpc/minixml.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: jansson/src/jansson.h
ickP2P/C/P2P/ickDiscoveryRegistry.o: jansson/src/jansson_config.h
ickP2P/C/P2P/ickP2PComm.o: libwebsockets/lib/libwebsockets.h
ickP2P/C/P2P/ickP2PComm.o: ickP2P/C/P2P/ickDiscovery.h
ickP2P/C/P2P/ickP2PComm.o: ickP2P/C/P2P/ickDiscoveryInternal.h
ickP2P/C/P2P/ickP2PComm.o: ickP2P/C/P2P/logutils.h
ickP2P/C/P2P/logutils.o: ickP2P/C/P2P/ickDiscovery.h
miniupnp/miniupnpc/connecthostport.o: miniupnp/miniupnpc/connecthostport.h
miniupnp/miniupnpc/miniwget.o: miniupnp/miniupnpc/miniupnpcstrings.h
miniupnp/miniupnpc/miniwget.o: miniupnp/miniupnpc/miniwget.h
miniupnp/miniupnpc/miniwget.o: miniupnp/miniupnpc/declspec.h
miniupnp/miniupnpc/miniwget.o: miniupnp/miniupnpc/connecthostport.h
miniupnp/miniupnpc/miniwget.o: miniupnp/miniupnpc/receivedata.h
miniupnp/miniupnpc/minixml.o: miniupnp/miniupnpc/minixml.h
miniupnp/miniupnpc/receivedata.o: miniupnp/miniupnpc/receivedata.h
miniupnp/minissdpd/openssdpsocket.o: miniupnp/minissdpd/config.h
miniupnp/minissdpd/openssdpsocket.o: miniupnp/minissdpd/openssdpsocket.h
miniupnp/minissdpd/openssdpsocket.o: miniupnp/minissdpd/upnputils.h
miniupnp/minissdpd/upnputils.o: miniupnp/minissdpd/config.h
miniupnp/minissdpd/upnputils.o: miniupnp/minissdpd/upnputils.h
libwebsockets/lib/libwebsockets.o: libwebsockets/lib/private-libwebsockets.h
libwebsockets/lib/libwebsockets.o: libwebsockets/lib/libwebsockets.h
libwebsockets/lib/parsers.o: libwebsockets/lib/private-libwebsockets.h
libwebsockets/lib/parsers.o: libwebsockets/lib/libwebsockets.h
libwebsockets/lib/handshake.o: libwebsockets/lib/private-libwebsockets.h
libwebsockets/lib/handshake.o: libwebsockets/lib/libwebsockets.h
libwebsockets/lib/extension.o: libwebsockets/lib/private-libwebsockets.h
libwebsockets/lib/extension.o: libwebsockets/lib/libwebsockets.h
libwebsockets/lib/extension.o: libwebsockets/lib/extension-deflate-stream.h
libwebsockets/lib/extension.o: libwebsockets/lib/extension-x-google-mux.h
libwebsockets/lib/client-handshake.o: libwebsockets/lib/private-libwebsockets.h
libwebsockets/lib/client-handshake.o: libwebsockets/lib/libwebsockets.h
libwebsockets/lib/extension-deflate-stream.o: libwebsockets/lib/private-libwebsockets.h
libwebsockets/lib/extension-deflate-stream.o: libwebsockets/lib/libwebsockets.h
libwebsockets/lib/extension-deflate-stream.o: libwebsockets/lib/extension-deflate-stream.h
libwebsockets/lib/extension-x-google-mux.o: libwebsockets/lib/private-libwebsockets.h
libwebsockets/lib/extension-x-google-mux.o: libwebsockets/lib/libwebsockets.h
libwebsockets/lib/extension-x-google-mux.o: libwebsockets/lib/extension-x-google-mux.h
jansson/src/value.o: jansson/src/jansson.h jansson/src/jansson_config.h
jansson/src/value.o: jansson/src/hashtable.h jansson/src/jansson_private.h
jansson/src/value.o: jansson/src/strbuffer.h jansson/src/utf.h
jansson/src/memory.o: jansson/src/jansson.h jansson/src/jansson_config.h
jansson/src/memory.o: jansson/src/jansson_private.h jansson/src/hashtable.h
jansson/src/memory.o: jansson/src/strbuffer.h
jansson/src/dump.o: jansson/src/jansson.h jansson/src/jansson_config.h
jansson/src/dump.o: jansson/src/jansson_private.h jansson/src/hashtable.h
jansson/src/dump.o: jansson/src/strbuffer.h jansson/src/utf.h
jansson/src/hashtable.o: jansson/src/jansson_config.h
jansson/src/hashtable.o: jansson/src/jansson_private.h jansson/src/jansson.h
jansson/src/hashtable.o: jansson/src/hashtable.h jansson/src/strbuffer.h
jansson/src/strbuffer.o: jansson/src/jansson_private.h jansson/src/jansson.h
jansson/src/strbuffer.o: jansson/src/jansson_config.h jansson/src/hashtable.h
jansson/src/strbuffer.o: jansson/src/strbuffer.h
jansson/src/utf.o: jansson/src/utf.h
jansson/src/pack_unpack.o: jansson/src/jansson.h jansson/src/jansson_config.h
jansson/src/pack_unpack.o: jansson/src/jansson_private.h
jansson/src/pack_unpack.o: jansson/src/hashtable.h jansson/src/strbuffer.h
jansson/src/pack_unpack.o: jansson/src/utf.h
jansson/src/error.o: jansson/src/jansson_private.h jansson/src/jansson.h
jansson/src/error.o: jansson/src/jansson_config.h jansson/src/hashtable.h
jansson/src/error.o: jansson/src/strbuffer.h
jansson/src/strconv.o: jansson/src/jansson_private.h jansson/src/jansson.h
jansson/src/strconv.o: jansson/src/jansson_config.h jansson/src/hashtable.h
jansson/src/strconv.o: jansson/src/strbuffer.h
