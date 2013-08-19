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
INCLUDEDIR      = include
LIBNAME         = libickp2p
TESTEXEC        = ickp2ptest

ICKLIB          = $(LIBDIR)/$(LIBNAME).a

#GITVERSION      = $(shell git rev-list HEAD --count)
#GITVERSION      = $(shell git rev-list HEAD --max-count=1)
GITVERSION      = "undef"

AR              = ar
CC              = cc
CFLAGS          = -Wall -Wno-pragmas -rdynamic -DLWS_NO_FORK -DGIT_VERSION=$(GITVERSION) -D_GNU_SOURCE
MKDEPFLAGS	= -Y

# Source files to process
ICKP2PSRCS      = ickP2p.c ickMainThread.c ickDiscovery.c ickSSDP.c ickErrors.c ickIpTools.c logutils.c
MINIUPNPSRCS    = miniupnp/miniupnpc/connecthostport.c miniupnp/miniupnpc/miniwget.c \
                  miniupnp/miniupnpc/minixml.c miniupnp/miniupnpc/receivedata.c
MINISSDPDSRCS   = miniupnp/minissdpd/openssdpsocket.c miniupnp/minissdpd/upnputils.c                  
TESTSRC         = test/ickp2ptest.c
TESTOBJ         = $(TESTSRC:.c=.o)

LIBSRC          = $(addprefix ickp2p/,$(ICKP2PSRCS))
LIBOBJ          = $(LIBSRC:.c=.o)


# Include directories and special headers
PUBLICHEADERS    = ickp2p/ickP2p.h
INTERNALINCLUDES = -Iminiupnp/minissdpd -Iminiupnp/miniupnpc
INCLUDES         =

# Default rule: make all
all: $(INCLUDEDIR) $(ICKLIB) 

# Variant: make for debugging
debug: DEBUGFLAGS = -g -DICK_DEBUG
debug: $(INCLUDEDIR) $(ICKLIB)

# Variant: make test executable in debug mode
test: DEBUGFLAGS = -g -DICK_DEBUG
test: $(TESTEXEC)

# How to compile c source files
%.o: %.c Makefile
	$(CC) $(INTERNALINCLUDES) $(INCLUDES) $(CFLAGS) $(DEBUGFLAGS) -c $< -o $@

# How to build the static library
$(ICKLIB): $(LIBOBJ)
	@echo '*************************************************************'
	@echo "Building library:"
	@mkdir -p $(LIBDIR)
	ar cr $@ $?
	ar ts >/dev/null $@
 
# make test executable
$(TESTEXEC): $(INCLUDEDIR) $(TESTSRC) $(ICKLIB) Makefile
	@echo '*************************************************************'
	@echo "Building test executable:"
	$(CC) -I$(INCLUDEDIR) $(DEBUGFLAGS) $(TESTSRC) -L$(LIBDIR) -lickp2p -lpthread -o $(TESTEXEC)

# Provide public headers
$(INCLUDEDIR): $(PUBLICHEADERS)
	@echo '*************************************************************'
	@echo "Collecting public headers:"
	@mkdir -p $(INCLUDEDIR)
	ln -sf ../$? $@


# How to update from git
update:
	@echo '*************************************************************'
	@echo "Updating from git repository:"
	git pull


# How to create dependencies
depend:
	@echo '*************************************************************'
	@echo "Creating dependencies:"
	makedepend $(MKDEPFLAGS) -- $(INTERNALINCLUDES) $(INCLUDES) $(CFLAGS) -- $(LIBSRC) 2>/dev/null
	makedepend $(MKDEPFLAGS) -a -- -I$(INCLUDEDIR) $(CFLAGS) -- $(TESTSRC) 2>/dev/null


# How to clean tempoarary files
clean:
	@echo '*************************************************************'
	@echo "Deleting intermediate files:"
	rm -f $(LIBOBJ) $(TESTOBJ)


# How to clean all
cleanall: clean
	@echo '*************************************************************'
	@echo "Clean all:"
	rm -rf $(LIBDIR) $(INCLUDEDIR) $(TESTEXEC)

# End of Makefile -- makedepend output might follow ...

# DO NOT DELETE

ickp2p/ickP2p.o: ickp2p/ickP2p.h ickp2p/ickP2pInternal.h
ickp2p/ickP2p.o: ickp2p/ickMainThread.h ickp2p/ickDiscovery.h
ickp2p/ickP2p.o: ickp2p/logutils.h
ickp2p/ickMainThread.o: ickp2p/ickP2p.h ickp2p/ickP2pInternal.h
ickp2p/ickMainThread.o: ickp2p/ickDiscovery.h ickp2p/ickSSDP.h
ickp2p/ickMainThread.o: ickp2p/logutils.h ickp2p/ickMainThread.h
ickp2p/ickDiscovery.o: miniupnp/minissdpd/openssdpsocket.h ickp2p/ickP2p.h
ickp2p/ickDiscovery.o: ickp2p/ickP2pInternal.h ickp2p/ickIpTools.h
ickp2p/ickDiscovery.o: ickp2p/logutils.h ickp2p/ickDiscovery.h
ickp2p/ickSSDP.o: ickp2p/ickP2p.h ickp2p/ickP2pInternal.h ickp2p/logutils.h
ickp2p/ickSSDP.o: ickp2p/ickSSDP.h
ickp2p/ickErrors.o: ickp2p/ickP2p.h
ickp2p/ickIpTools.o: ickp2p/ickP2p.h ickp2p/ickP2pInternal.h
ickp2p/ickIpTools.o: ickp2p/logutils.h
ickp2p/logutils.o: ickp2p/logutils.h ickp2p/ickP2p.h
