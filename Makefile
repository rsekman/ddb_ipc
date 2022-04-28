.PHONY: clean install

CXX?=g++
CFLAGS+=-Wall -g -O2 -fPIC -D_GNU_SOURCE
LDFLAGS+=-shared

BUILDDIR=build
SRCDIR=src

SOCK=/tmp/ddb_socket

OUT=$(BUILDDIR)/ddb_ipc.so

SOURCES?=$(wildcard $(SRCDIR)/*.cpp)
TESTSOURCES?=$(wildcard $(TESTDIR)/*.cpp)
COMMON=
OBJ?=$(patsubst $(SRCDIR)/%.cpp, $(BUILDDIR)/%.lo, $(SOURCES))
TESTOBJ?=$(patsubst $(TESTDIR)/%.cpp, $(BUILDDIR)/%.lo, $(TESTSOURCES))

all: $(OUT)

install: $(OUT)
	cp -t ~/.local/lib/deadbeef $(OUT)

$(BUILDDIR)/%.lo: $(SRCDIR)/%.cpp $(SRCDIR)/%.hpp $(COMMON)
	$(CXX) $(CFLAGS) $< -c -o $@

$(BUILDDIR)/%.lo: $(TESTDIR)/%.cpp $(TESTDIR)/%.hpp $(COMMON)
	$(CXX) $(CFLAGS) $< -c -o $@

$(OUT): $(OBJ)
	$(CXX) $(CFLAGS$) $(LDFLAGS) $(OBJ) -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)
	rm -f $(SOCK)
