.PHONY: clean install

CXX?=g++
CFLAGS+=-Wall -g -O2 -fPIC -D_GNU_SOURCE --std=c++17
LDFLAGS+=-shared

ifneq ($(DDB_IPC_LOGLEVEL),)
	CFLAGS+=-DDDB_IPC_LOGLEVEL=$(DDB_IPC_LOGLEVEL)
endif

export CXX CFLAGS

BUILDDIR=build
SRCDIR=src

SOCK=/tmp/ddb_socket

OUT=$(BUILDDIR)/ddb_ipc.so

SOURCES?=$(wildcard $(SRCDIR)/*.cpp)
TESTSOURCES?=$(wildcard $(TESTDIR)/*.cpp)
COMMON=

SUBOBJ=cpp-base64/base64.o

OBJ=$(patsubst $(SRCDIR)/%.cpp, $(BUILDDIR)/%.lo, $(SOURCES))
ifneq ($(SUBOBJ),)
OBJ+=$(addprefix submodules,/$(SUBOBJ))
endif

all: $(OUT)

install: $(OUT)
	cp -t ~/.local/lib/deadbeef $(OUT)

$(BUILDDIR)/%.lo: $(SRCDIR)/%.cpp $(SRCDIR)/%.hpp $(COMMON)
	$(CXX) $(CFLAGS) $< -c -o $@

$(BUILDDIR)/%.lo: submodules/%.o
	cp $< $@

$(BUILDDIR)/%.lo: submodules/%.lo
	cp $< $@

submodules/%.lo:
	$(MAKE) -C $(dir $@) $(notdir $@)

submodules/%.o:
	echo $(SUBOBJ) $@
	$(MAKE) -C $(dir $@) $(notdir $@)

$(OUT): $(OBJ)
	$(CXX) $(CFLAGS) $(LDFLAGS) $(OBJ) -o $@



$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)
	rm -f $(SOCK)
