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
# --------------------------------------------------------------

# *************************************************************************
# Copyright (c) 2013, ickStream GmbH
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without 
# modification, are permitted provided that the following conditions are met:
#
#   * Redistributions of source code must retain the above copyright 
#     notice, this list of conditions and the following disclaimer.
#   * Redistributions in binary form must reproduce the above copyright 
#     notice, this list of conditions and the following disclaimer in the 
#     documentation and/or other materials provided with the distribution.
#   * Neither the name of ickStream nor the names of its contributors 
#     may be used to endorse or promote products derived from this software 
#     without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
# IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, 
# EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# ************************************************************************


LIBDIR          = lib
INCLUDEDIR      = include
LIBNAME         = libickp2p
TESTEXEC        = ickp2ptest
MTESTEXEC       = ickp2pmtest
#P2PSHEXEC       = ickp2psh
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
TESTSRC         = test/ickp2ptest.c test/testmisc.c test/config.c
TESTOBJ         = $(TESTSRC:.c=.o)
MTESTSRC        = test/ickp2pmtest.c test/testmisc.c test/config.c
MTESTOBJ        = $(MTESTSRC:.c=.o)
P2PSHSRC        = test/ickp2psh.c test/config.c
P2PSHOBJ        = $(P2PSHSRC:.c=.o)
SSDPLOGSRC      = test/ssdplog.c test/config.c
SSDPLOGOBJ      = $(SSDPLOGSRC:.c=.o)

LIBSRC          = $(addprefix ickp2p/,$(ICKP2PSRCS)) $(MINIUPNPSRCS)
LIBOBJ          = $(LIBSRC:.c=.o)

# Include directories and special headers
PUBLICHEADERS    = ickp2p/ickP2p.h
INTERNALINCLUDES = -Iminiupnp/miniupnpc
INCLUDES         =
GENHEADERS       = miniupnp/miniupnpc/miniupnpcstrings.h


# OS specific settings
ifeq ($(OS),Linux)
EXTRALIBS       = -luuid
TESTFALGS       = -DIF1NAME="lo"
endif
ifeq ($(OS),Darwin)
TESTFALGS       = -DIF1NAME="lo0"
CFLAGS          += -DICK_USE_SO_REUSEPORT
endif


# Default rule: make all
all: $(GENHEADERS) $(INCLUDEDIR) $(ICKLIB) 

# Variant: make for debugging
debug: DEBUGFLAGS = -g -DICK_DEBUG
debug: $(GENHEADERS) $(INCLUDEDIR) $(ICKLIB)

# Variant: make test executable in debug mode
test: DEBUGFLAGS = -g -DICK_DEBUG
test: $(TESTEXEC) $(MTESTEXEC) $(P2PSHEXEC) $(SSDPLOGEXEC)

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
	$(CC) -I$(INCLUDEDIR) $(DEBUGFLAGS) $(TESTFLAGS) $(CFLAGS) $(LFLAGS) $(TESTSRC) -L$(LIBDIR) -lickp2p -lwebsockets -lpthread $(EXTRALIBS) -o $(TESTEXEC)

# make mtest executable
$(MTESTEXEC): $(GENHEADERS) $(INCLUDEDIR) $(MTESTSRC) $(ICKLIB) Makefile
	@echo '*************************************************************'
	@echo "Building mtest executable:"
	$(CC) -I$(INCLUDEDIR) $(DEBUGFLAGS) $(TESTFLAGS) $(CFLAGS) $(LFLAGS) $(MTESTSRC) -L$(LIBDIR) -lickp2p -lwebsockets -lpthread $(EXTRALIBS) -o $(MTESTEXEC)
	
# make api shell
$(P2PSHEXEC): $(GENHEADERS) $(INCLUDEDIR) $(P2PSHSRC) $(ICKLIB) Makefile
	@echo '*************************************************************'
	@echo "Building api shell executable:"
	$(CC) -I$(INCLUDEDIR) $(DEBUGFLAGS) $(TESTFLAGS) $(CFLAGS) $(LFLAGS) $(P2PSHSRC) -L$(LIBDIR) -lreadline -lickp2p -lwebsockets -lpthread $(EXTRALIBS) -o $(P2PSHEXEC)

# make ssdp logger
$(SSDPLOGEXEC): $(SSDPLOGSRC) Makefile
	@echo '*************************************************************'
	@echo "Building ssdp logger executable:"
	$(CC) $(DEBUGFLAGS) $(SSDPLOGSRC) $(CFLAGS) -o $(SSDPLOGEXEC)

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
	makedepend $(MKDEPFLAGS) -a -- -I$(INCLUDEDIR) $(CFLAGS) -- $(MTESTSRC) 2>/dev/null


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
ickp2p/ickSSDP.o: ickp2p/ickMainThread.h ickp2p/ickSSDP.h ickp2p/ickP2pCom.h
ickp2p/ickDescription.o: miniupnp/miniupnpc/minixml.h ickp2p/ickP2p.h
ickp2p/ickDescription.o: ickp2p/ickP2pInternal.h ickp2p/logutils.h
ickp2p/ickDescription.o: ickp2p/ickDevice.h ickp2p/ickDescription.h
ickp2p/ickDescription.o: ickp2p/ickWGet.h ickp2p/ickSSDP.h ickp2p/ickP2pCom.h
ickp2p/ickDescription.o: ickp2p/ickMainThread.h
ickp2p/ickP2pCom.o: ickp2p/ickP2p.h ickp2p/ickP2pInternal.h ickp2p/logutils.h
ickp2p/ickP2pCom.o: ickp2p/ickMainThread.h ickp2p/ickDescription.h
ickp2p/ickP2pCom.o: ickp2p/ickWGet.h ickp2p/ickDevice.h ickp2p/ickSSDP.h
ickp2p/ickP2pCom.o: ickp2p/ickP2pCom.h
ickp2p/ickP2pDebug.o: miniupnp/miniupnpc/miniwget.h
ickp2p/ickP2pDebug.o: miniupnp/miniupnpc/declspec.h ickp2p/ickP2p.h
ickp2p/ickP2pDebug.o: ickp2p/ickP2pInternal.h ickp2p/logutils.h
ickp2p/ickP2pDebug.o: ickp2p/ickIpTools.h ickp2p/ickDevice.h
ickp2p/ickP2pDebug.o: ickp2p/ickDescription.h ickp2p/ickWGet.h
ickp2p/ickP2pDebug.o: ickp2p/ickP2pCom.h ickp2p/ickP2pDebug.h
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

test/ickp2ptest.o: include/ickP2p.h test/config.h test/testmisc.h
test/testmisc.o: include/ickP2p.h test/testmisc.h
test/config.o: test/config.h

test/ickp2pmtest.o: include/ickP2p.h test/config.h test/testmisc.h
test/testmisc.o: include/ickP2p.h test/testmisc.h
test/config.o: test/config.h
