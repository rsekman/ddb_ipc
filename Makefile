.PHONY: test clean

CXX?=g++
CFLAGS+=-Wall -g -O2 -fPIC -D_GNU_SOURCE
LDFLAGS+=-shared

BUILDDIR=build
SRCDIR=src

OUT=ddb_ipc.so

SOURCES?=$(wildcard $(SRCDIR)/*.cpp)
OBJ?=$(patsubst $(SRCDIR)/%.cpp, $(BUILDDIR)/%.o, $(SOURCES))

all: $(BUILDDIR) $(BUILDDIR)/$(OUT)

test: $(BUILDDIR)/$(OUT) $(BUILDDIR)/test
	$(BUILDDIR)/test

$(BUILDDIR)/test: $(SRCDIR)/test.cpp
	$(CXX) $(CFLAGS) -ldl $< -o $@

$(BUILDDIR)/%.o: src/%.cpp src/%.hpp
	$(CXX) $(CFLAGS) $< -c -o $@

$(BUILDDIR)/$(OUT): $(OBJ) $(BUILDDIR)
	$(CXX) $(CFLAGS$) $(LDFLAGS) $(OBJ) -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)
