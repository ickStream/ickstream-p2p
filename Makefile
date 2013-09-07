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
SSDPLOGEXEC     = ssdplog

ICKLIB          = $(LIBDIR)/$(LIBNAME).a
OS             := $(shell uname)

#GITVERSION      = $(shell git rev-list HEAD --count)
GITVERSION      = $(shell git rev-list HEAD --max-count=1)
#GITVERSION      = "undef"

AR              = ar
CC              = cc
CFLAGS          = -Wall -DLWS_NO_FORK -DGIT_VERSION=$(GITVERSION) -D_GNU_SOURCE
LFLAGS          = -rdynamic
MKDEPFLAGS      = -Y

# Source files to process
ICKP2PSRCS      = ickP2p.c ickMainThread.c ickDevice.c ickSSDP.c ickDescription.c ickP2pCom.c ickP2pDebug.c ickErrors.c ickWGet.c ickIpTools.c logutils.c
MINIUPNPSRCS    = miniupnp/miniupnpc/connecthostport.c miniupnp/miniupnpc/miniwget.c \
                  miniupnp/miniupnpc/minixml.c miniupnp/miniupnpc/receivedata.c
TESTSRC         = test/ickp2ptest.c test/config.c
TESTOBJ         = $(TESTSRC:.c=.o)
SSDPLOGSRC      = test/ssdplog.c test/config.c
SSDPLOGOBJ      = $(SSDPLOGSRC:.c=.o)

LIBSRC          = $(addprefix ickp2p/,$(ICKP2PSRCS)) $(MINIUPNPSRCS)
LIBOBJ          = $(LIBSRC:.c=.o)

# Include directories and special headers
PUBLICHEADERS    = ickp2p/ickP2p.h
INTERNALINCLUDES = -Iminiupnp/miniupnpc
INCLUDES         =
GENHEADERS	 = miniupnp/miniupnpc/miniupnpcstrings.h


# OS specific settings
ifeq ($(OS),Linux)
EXTRALIBS	= -luuid
TESTFALGS = -DIF1NAME="lo"
endif
ifeq ($(OS),Darwin)
TESTFALGS = -DIF1NAME="lo0"
endif



# Default rule: make all
all: $(GENHEADERS) $(INCLUDEDIR) $(ICKLIB) 

# Variant: make for debugging
debug: DEBUGFLAGS = -g -DICK_DEBUG
debug: $(GENHEADERS) $(INCLUDEDIR) $(ICKLIB)

# Variant: make test executable in debug mode
test: DEBUGFLAGS = -g -DICK_DEBUG
test: $(TESTEXEC) $(SSDPLOGEXEC)

# How to compile c source files
%.o: %.c Makefile 
	$(CC) $(INTERNALINCLUDES) $(INCLUDES) $(CFLAGS) $(DEBUGFLAGS) -c $< -o $@

# Minimal configuration of miniupnp
miniupnp/miniupnpc/miniupnpcstrings.h: miniupnp/miniupnpc/miniupnpcstrings.h.in
	cd miniupnp/miniupnpc;$(MAKE) miniupnpcstrings.h

# How to build the static library
$(ICKLIB): $(LIBOBJ)
	@echo '*************************************************************'
	@echo "Building library:"
	@mkdir -p $(LIBDIR)
	ar cr $@ $?
	ar ts >/dev/null $@
 
# make test executable
$(TESTEXEC): $(GENHEADERS) $(INCLUDEDIR) $(TESTSRC) $(ICKLIB) Makefile
	@echo '*************************************************************'
	@echo "Building test executable:"
	$(CC) -I$(INCLUDEDIR) $(DEBUGFLAGS) $(TESTFLAGS) $(LFLAGS) $(TESTSRC) -L$(LIBDIR) -lickp2p -lwebsockets -lpthread $(EXTRALIBS) -o $(TESTEXEC)

# make ssdp logger
$(SSDPLOGEXEC): $(SSDPLOGSRC) Makefile
	@echo '*************************************************************'
	@echo "Building ssdp logger executable:"
	$(CC) $(DEBUGFLAGS) $(SSDPLOGSRC) -o $(SSDPLOGEXEC)

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
	rm -f $(LIBOBJ) $(TESTOBJ) $(SSDPLOGOBJ)


# How to clean all
cleanall: clean
	@echo '*************************************************************'
	@echo "Clean all:"
	rm -rf $(LIBDIR) $(INCLUDEDIR) $(TESTEXEC) $(SSDPLOGEXEC)

# End of Makefile -- makedepend output might follow ...

# DO NOT DELETE

ickp2p/ickP2p.o: ickp2p/ickP2p.h ickp2p/ickP2pInternal.h ickp2p/logutils.h
ickp2p/ickP2p.o: ickp2p/ickIpTools.h ickp2p/ickSSDP.h ickp2p/ickDescription.h
ickp2p/ickP2p.o: ickp2p/ickWGet.h ickp2p/ickDevice.h ickp2p/ickMainThread.h
ickp2p/ickMainThread.o: ickp2p/ickP2p.h ickp2p/ickP2pInternal.h
ickp2p/ickMainThread.o: ickp2p/logutils.h ickp2p/ickIpTools.h
ickp2p/ickMainThread.o: ickp2p/ickDevice.h ickp2p/ickDescription.h
ickp2p/ickMainThread.o: ickp2p/ickWGet.h ickp2p/ickSSDP.h ickp2p/ickP2pCom.h
ickp2p/ickMainThread.o: ickp2p/ickP2pDebug.h ickp2p/ickMainThread.h
ickp2p/ickDevice.o: ickp2p/ickP2p.h ickp2p/ickP2pInternal.h ickp2p/logutils.h
ickp2p/ickDevice.o: ickp2p/ickDevice.h ickp2p/ickDescription.h
ickp2p/ickDevice.o: ickp2p/ickWGet.h
ickp2p/ickSSDP.o: ickp2p/ickP2p.h ickp2p/ickP2pInternal.h ickp2p/logutils.h
ickp2p/ickSSDP.o: ickp2p/ickIpTools.h ickp2p/ickDevice.h
ickp2p/ickSSDP.o: ickp2p/ickDescription.h ickp2p/ickWGet.h
ickp2p/ickSSDP.o: ickp2p/ickMainThread.h ickp2p/ickSSDP.h
ickp2p/ickDescription.o: miniupnp/miniupnpc/minixml.h ickp2p/ickP2p.h
ickp2p/ickDescription.o: ickp2p/ickP2pInternal.h ickp2p/logutils.h
ickp2p/ickDescription.o: ickp2p/ickDevice.h ickp2p/ickDescription.h
ickp2p/ickDescription.o: ickp2p/ickWGet.h ickp2p/ickSSDP.h
ickp2p/ickDescription.o: ickp2p/ickMainThread.h
ickp2p/ickP2pCom.o: ickp2p/ickP2p.h ickp2p/ickP2pInternal.h ickp2p/logutils.h
ickp2p/ickP2pCom.o: ickp2p/ickMainThread.h ickp2p/ickDescription.h
ickp2p/ickP2pCom.o: ickp2p/ickWGet.h ickp2p/ickDevice.h ickp2p/ickP2pCom.h
ickp2p/ickP2pDebug.o: miniupnp/miniupnpc/miniwget.h
ickp2p/ickP2pDebug.o: miniupnp/miniupnpc/declspec.h ickp2p/ickP2p.h
ickp2p/ickP2pDebug.o: ickp2p/ickP2pInternal.h ickp2p/logutils.h
ickp2p/ickP2pDebug.o: ickp2p/ickDevice.h ickp2p/ickDescription.h
ickp2p/ickP2pDebug.o: ickp2p/ickWGet.h ickp2p/ickP2pCom.h
ickp2p/ickP2pDebug.o: ickp2p/ickP2pDebug.h
ickp2p/ickErrors.o: ickp2p/ickP2p.h
ickp2p/ickWGet.o: miniupnp/miniupnpc/miniwget.h miniupnp/miniupnpc/declspec.h
ickp2p/ickWGet.o: ickp2p/ickP2p.h ickp2p/ickP2pInternal.h
ickp2p/ickWGet.o: ickp2p/ickMainThread.h ickp2p/logutils.h ickp2p/ickWGet.h
ickp2p/ickIpTools.o: ickp2p/ickP2p.h ickp2p/ickP2pInternal.h
ickp2p/ickIpTools.o: ickp2p/logutils.h ickp2p/ickIpTools.h
ickp2p/logutils.o: ickp2p/logutils.h ickp2p/ickP2p.h
miniupnp/miniupnpc/connecthostport.o: miniupnp/miniupnpc/connecthostport.h
miniupnp/miniupnpc/miniwget.o: miniupnp/miniupnpc/miniupnpcstrings.h
miniupnp/miniupnpc/miniwget.o: miniupnp/miniupnpc/miniwget.h
miniupnp/miniupnpc/miniwget.o: miniupnp/miniupnpc/declspec.h
miniupnp/miniupnpc/miniwget.o: miniupnp/miniupnpc/connecthostport.h
miniupnp/miniupnpc/miniwget.o: miniupnp/miniupnpc/receivedata.h
miniupnp/miniupnpc/minixml.o: miniupnp/miniupnpc/minixml.h
miniupnp/miniupnpc/receivedata.o: miniupnp/miniupnpc/receivedata.h

test/ickp2ptest.o: test/config.h include/ickP2p.h
test/config.o: test/config.h
