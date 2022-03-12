.PHONY: clean

.NOTPARALLEL:

mine: ./XKCP/bin/generic64/libXKCP.so ./XKCP/bin/AVX/libXKCP.so ./XKCP/bin/AVX2/libXKCP.so ./XKCP/bin/AVX512/libXKCP.so  main.c
	gcc main.c -O2 -o mine -I XKCP/bin/AVX2/libXKCP.so.headers -lpthread -LXKCP/bin/generic64 -l:libXKCP.so

./XKCP/bin/AVX/libXKCP.so:
	cd XKCP; make -j CFLAGS="-march=sandybridge -fpic" AVX/libXKCP.so

./XKCP/bin/AVX2/libXKCP.so:
	cd XKCP; make -j CFLAGS="-march=skylake -fpic" AVX2/libXKCP.so

./XKCP/bin/AVX512/libXKCP.so:
	cd XKCP; make -j CFLAGS="-march=skylake-avx512 -fpic" AVX512/libXKCP.so

./XKCP/bin/generic64/libXKCP.so:
	cd XKCP; make -j generic64/libXKCP.so

clean:
	cd XKCP; make clean
	rm -f mine
