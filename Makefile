mine: ./XKCP/bin/generic64/libXKCP.so main.c
	gcc main.c -O2 -o mine -I XKCP/bin/generic64/libXKCP.so.headers -L. -l:XKCP/bin/generic64/libXKCP.so

./XKCP/bin/AVX2/libXKCP.so:
	cd XKCP; make AVX2/libXKCP.so

./XKCP/bin/generic64/libXKCP.so:
	cd XKCP; make generic64/libXKCP.so
