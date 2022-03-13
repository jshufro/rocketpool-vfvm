.PHONY: clean

.NOTPARALLEL:

mine: main.c build/headers build/lib
	gcc main.c -O2 -o mine -I build/headers -lpthread -ljson-c -L. -l:build/lib/libXKCP.so

build/headers: build/lib

build/lib: ./XKCP
	./build-xkcp.sh

rocketpool: main.go rocketpool.go
	go build -o rocketpool main.go rocketpool.go

clean:
	cd XKCP; make clean
	rm -rf build
	rm -f mine
