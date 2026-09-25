SYSROOT ?= /opt/sysroot
PKG_CONFIG ?= pkg-config
CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?= -Wl,-rpath-link,/usr/lib/x86_64-linux-gnu -lepoxy -lXcursor -lwayland-client -lwayland-cursor -lwayland-egl -lxkbcommon -lpangoft2-1.0 -lXinerama -lXi -lXext -lXfixes -lXrender -lX11 -lXrandr -lXcomposite

ifneq ($(wildcard $(SYSROOT)/usr/bin/pkg-config),)
PKG_CONFIG := $(SYSROOT)/usr/bin/pkg-config
endif

PKG_ENV := LD_LIBRARY_PATH=$(SYSROOT)/usr/lib/x86_64-linux-gnu PKG_CONFIG_SYSROOT_DIR=$(SYSROOT) PKG_CONFIG_PATH=$(SYSROOT)/usr/lib/x86_64-linux-gnu/pkgconfig:$(SYSROOT)/usr/share/pkgconfig
GTK_CFLAGS := $(shell $(PKG_ENV) $(PKG_CONFIG) --cflags gtk+-3.0)
GTK_LIBS := $(shell $(PKG_ENV) $(PKG_CONFIG) --libs gtk+-3.0)

.PHONY: all test clean
all: bin/ACS-cpp

bin/ACS-cpp: src/main.cpp src/ui.cpp src/sys.cpp src/parse.cpp src/parse.hpp src/sys.hpp src/version.hpp
	mkdir -p bin
	$(CXX) $(CXXFLAGS) $(GTK_CFLAGS) -o $@ src/main.cpp src/ui.cpp src/sys.cpp src/parse.cpp $(GTK_LIBS) -pthread $(LDFLAGS)

test: bin/parse-tests
	./bin/parse-tests

bin/parse-tests: src/test_parse.cpp src/parse.cpp src/parse.hpp
	mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ src/test_parse.cpp src/parse.cpp

clean:
	rm -rf bin
