mine: ./XKCP/bin/AVX2/libXKCP.so main.c
	gcc main.c -g -o mine -I XKCP/bin/AVX2/libXKCP.so.headers -L. -l:XKCP/bin/AVX2/libXKCP.so

./XKCP/bin/AVX2/libXKCP.so:
	cd XKCP; make AVX2/libXKCP.so

./XKCP/bin/generic64/libXKCP.so:
	cd XKCP; make generic64/libXKCP.so
