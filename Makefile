.PHONY: test clean install

CXX?=g++
CFLAGS+=-Wall -g -O2 -fPIC -D_GNU_SOURCE
LDFLAGS+=-shared

BUILDDIR=build
SRCDIR=src
TESTDIR=test

SOCK=/tmp/ddb_socket

OUT=$(BUILDDIR)/ddb_ipc.so

SOURCES?=$(wildcard $(SRCDIR)/*.cpp)
TESTSOURCES?=$(wildcard $(TESTDIR)/*.cpp)
COMMON=$(SRCDIR)/defs.hpp
OBJ?=$(patsubst $(SRCDIR)/%.cpp, $(BUILDDIR)/%.o, $(SOURCES))
TESTOBJ?=$(patsubst $(TESTDIR)/%.cpp, $(BUILDDIR)/%.o, $(TESTSOURCES))

all: $(OUT) test

install: $(OUT)
	cp -t ~/.local/lib/deadbeef $(OUT)

test: $(OUT) $(BUILDDIR)/test
	(sleep 2 && echo "Hello world!" | socat - $(SOCK)) &
	$(BUILDDIR)/test
	rm -f $(SOCK)

$(BUILDDIR)/test: $(TESTOBJ) $(OUT)
	$(CXX) $(CFLAGS) -ldl -lpthread $(TESTOBJ) -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp $(SRCDIR)/%.hpp $(COMMON)
	$(CXX) $(CFLAGS) $< -c -o $@

$(BUILDDIR)/%.o: $(TESTDIR)/%.cpp $(TESTDIR)/%.hpp $(COMMON)
	$(CXX) $(CFLAGS) $< -c -o $@

$(OUT): $(OBJ) $(BUILDDIR)
	$(CXX) $(CFLAGS$) $(LDFLAGS) $(OBJ) -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)
	rm -f $(SOCK)
