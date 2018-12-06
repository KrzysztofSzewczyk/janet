# Copyright (c) 2018 Calvin Rose
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to
# deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

################################
##### Set global variables #####
################################

PREFIX?=/usr/local

INCLUDEDIR=$(PREFIX)/include/janet
LIBDIR=$(PREFIX)/lib
BINDIR=$(PREFIX)/bin
JANET_VERSION?="\"commit-$(shell git log --pretty=format:'%h' -n 1)\""

#CFLAGS=-std=c99 -Wall -Wextra -Isrc/include -fpic -g -DJANET_VERSION=$(JANET_VERSION)
CFLAGS=-std=c99 -Wall -Wextra -Isrc/include -fpic -O2 -fvisibility=hidden \
	   -DJANET_VERSION=$(JANET_VERSION)
CLIBS=-lm -ldl
JANET_TARGET=janet
JANET_LIBRARY=libjanet.so
JANET_PATH?=/usr/local/lib/janet
DEBUGGER=gdb

UNAME:=$(shell uname -s)
LDCONFIG:=ldconfig
ifeq ($(UNAME), Darwin) 
	# Add other macos/clang flags
	LDCONFIG:=
else
	CFLAGS:=$(CFLAGS) -rdynamic
	CLIBS:=$(CLIBS) -lrt
endif

# Source headers
JANET_HEADERS=$(sort $(wildcard src/include/janet/*.h))
JANET_LOCAL_HEADERS=$(sort $(wildcard src/*/*.h))

# Source files
JANET_CORE_SOURCES=$(sort $(wildcard src/core/*.c)) src/core/core.gen.c
JANET_MAINCLIENT_SOURCES=$(sort $(wildcard src/mainclient/*.c)) src/mainclient/init.gen.c
JANET_WEBCLIENT_SOURCES=$(sort $(wildcard src/webclient/*.c)) src/webclient/webinit.gen.c

all: $(JANET_TARGET) $(JANET_LIBRARY)

##########################################################
##### The main interpreter program and shared object #####
##########################################################

JANET_ALL_SOURCES=$(JANET_CORE_SOURCES) \
				$(JANET_MAINCLIENT_SOURCES)

JANET_CORE_OBJECTS=$(patsubst %.c,%.o,$(JANET_CORE_SOURCES))
JANET_ALL_OBJECTS=$(patsubst %.c,%.o,$(JANET_ALL_SOURCES))

%.o: %.c $(JANET_HEADERS) $(JANET_LOCAL_HEADERS)
	$(CC) $(CFLAGS) -o $@ -c $<

$(JANET_TARGET): $(JANET_ALL_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS)

$(JANET_LIBRARY): $(JANET_CORE_OBJECTS)
	$(CC) $(CFLAGS) -shared -o $@ $^ $(CLIBS)

######################
##### Emscripten #####
######################

EMCC=emcc
EMCCFLAGS=-std=c99 -Wall -Wextra -Isrc/include -O2 \
		  -s EXTRA_EXPORTED_RUNTIME_METHODS='["cwrap"]' \
		  -s ALLOW_MEMORY_GROWTH=1 \
		  -s AGGRESSIVE_VARIABLE_ELIMINATION=1 \
		  -DJANET_VERSION=$(JANET_VERSION)
JANET_EMTARGET=janet.js
JANET_WEB_SOURCES=$(JANET_CORE_SOURCES) $(JANET_WEBCLIENT_SOURCES)
JANET_EMOBJECTS=$(patsubst %.c,%.bc,$(JANET_WEB_SOURCES))

%.bc: %.c $(JANET_HEADERS) $(JANET_LOCAL_HEADERS)
	$(EMCC) $(EMCCFLAGS) -o $@ -c $<

$(JANET_EMTARGET): $(JANET_EMOBJECTS)
	$(EMCC) $(EMCCFLAGS) -shared -o $@ $^

#############################
##### Generated C files #####
#############################

xxd: src/tools/xxd.c
	$(CC) $< -o $@

%.gen.c: %.janet xxd
	./xxd $< $@ janet_gen_$(*F)

###################
##### Testing #####
###################

TEST_SOURCES=$(wildcard ctest/*.c)
TEST_OBJECTS=$(patsubst %.c,%.o,$(TEST_SOURCES))
TEST_PROGRAMS=$(patsubst %.c,%.out,$(TEST_SOURCES))
TEST_SCRIPTS=$(wildcard test/suite*.janet)

ctest/%.o: ctest/%.c $(JANET_HEADERS)
	$(CC) $(CFLAGS) -o $@ -c $<

ctest/%.out: ctest/%.o $(JANET_CORE_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS)

repl: $(JANET_TARGET)
	./$(JANET_TARGET)

debug: $(JANET_TARGET)
	$(DEBUGGER) ./$(JANET_TARGET)

valgrind: $(JANET_TARGET)
	valgrind --leak-check=full -v ./$(JANET_TARGET)

test: $(JANET_TARGET) $(TEST_PROGRAMS)
	for f in ctest/*.out; do "$$f" || exit; done
	for f in test/*.janet; do ./$(JANET_TARGET) "$$f" || exit; done

VALGRIND_COMMAND=valgrind --leak-check=full -v 

valtest: $(JANET_TARGET) $(TEST_PROGRAMS)
	for f in ctest/*.out; do $(VALGRIND_COMMAND) "$$f" || exit; done
	for f in test/*.janet; do $(VALGRIND_COMMAND) ./$(JANET_TARGET) "$$f" || exit; done

###################
##### Natives #####
###################

natives: $(JANET_TARGET)
	$(MAKE) -C natives/json
	$(MAKE) -j 8 -C natives/sqlite3

clean-natives:
	$(MAKE) -C natives/json clean
	$(MAKE) -C natives/sqlite3 clean

#################
##### Other #####
#################

clean:
	-rm $(JANET_TARGET)
	-rm $(JANET_LIBRARY)
	-rm ctest/*.o ctest/*.out
	-rm src/**/*.o src/**/*.bc vgcore.* *.js *.wasm *.html
	-rm src/**/*.gen.c

install: $(JANET_TARGET)
	mkdir -p $(BINDIR)
	cp $(JANET_TARGET) $(BINDIR)/$(JANET_TARGET)
	mkdir -p $(INCLUDEDIR)
	cp $(JANET_HEADERS) $(INCLUDEDIR)
	mkdir -p $(LIBDIR)
	cp $(JANET_LIBRARY) $(LIBDIR)/$(JANET_LIBRARY)
	cp janet.1 /usr/local/share/man/man1/
	mandb
	$(LDCONFIG)

install-libs: natives
	mkdir -p $(JANET_PATH)
	cp -r lib $(JANET_PATH)
	cp natives/*/*.so $(JANET_PATH)

uninstall:
	-rm $(BINDIR)/$(JANET_TARGET)
	-rm $(LIBDIR)/$(JANET_LIBRARY)
	-rm -rf $(INCLUDEDIR)
	$(LDCONFIG)

.PHONY: clean install repl debug valgrind test valtest install uninstall \
	$(TEST_PROGRAM_PHONIES) $(TEST_PROGRAM_VALPHONIES)
