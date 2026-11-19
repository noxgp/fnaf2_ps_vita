SRC=src/load.c src/run.c src/evt.c src/ext.c src/font.c src/audio.c src/third.c third/lz4/lib/lz4.c
INC=-Isrc -Ithird/stb -Ithird/dr_libs -Ithird/lz4/lib
fp2: $(SRC) src/plat_soft.c src/fp.h
	gcc -O2 -g -std=gnu11 -Wall -Wno-unused-function -Wno-misleading-indentation $(INC) -o $@ $(SRC) src/plat_soft.c -lm -lpthread
