.PHONY: clean mine

.NOTPARALLEL:

SRCS = main.c

HEADERS = include/plugin.h

ifdef PLUGINS
$(info Compiling with plugins)
SRCS += $(wildcard plugins/*/*.c)
HEADERS += $(wildcard plugins/*/*.h)
endif

mine: $(SRCS) $(HEADERS) build/headers build/lib
	gcc $(SRCS) -Werror -Wall -O2 -o mine -I include -I build/headers -lpthread -ljson-c -L. -l:build/lib/libXKCP.so

build/headers: build/lib

build/lib: ./XKCP
	./build-xkcp.sh

clean:
	cd XKCP; make clean
	rm -rf build
	rm -f mine
	rm -f rocketpool
