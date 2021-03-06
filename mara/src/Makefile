

include ../conf/mara.conf


SRC_C   = $(wildcard *.c)
SRC_CPP = $(wildcard *.cpp)
OBJ_C   = $(SRC_C:.c=.o)
OBJ_CPP = $(SRC_CPP:.cpp=.o)
MARA_I  = -I../include
MARA_A  = ../lib/libmara.a

LUA_DIR = ../lib/lua/5.2
LUA_TARGET = \
	$(LUA_DIR)/json.lua \
	$(LUA_DIR)/matrix.lua \
	$(LUA_DIR)/util.lua \
	$(LUA_DIR)/tabeos.lua \
	$(LUA_DIR)/tests.lua

default : $(MARA_A) $(LUA_TARGET)

test :
	$(CXX) shen.cpp -o shen -D__MAIN__

%.o : %.c
	$(CC) $(CFLAGS) -c $<

%.o : %.cpp
	$(CC) $(CFLAGS) -c $<

../lib/lua/5.2/%.lua : %.lua
	cp $< $@

fft_3d.o : fft_3d.c
	$(CC) $(CFLAGS) -c $< $(FFTW_I) -DFFT_FFTW

lua_vis.o : lua_vis.c
	$(CC) $(CFLAGS) -c $< $(MARA_I) -std=c99

luaU.o : luaU.c
	$(CC) $(CFLAGS) -c $< $(MARA_I) -std=c99

lua_fft.o : lua_fft.cpp
	$(CC) $(CFLAGS) -c $< $(MARA_I) $(FFTW_I) -DFFT_FFTW

lua_h5.o : lua_h5.c
	$(CC) $(CFLAGS) -c $< $(MARA_I) $(HDF5_I)

h5ser.o : h5ser.c
	$(CC) $(CFLAGS) -c $< $(HDF5_I)

h5mpi.o : h5mpi.c
	$(CC) $(CFLAGS) -c $< $(HDF5_I)

mara_io.o : mara_io.c
	$(CC) $(CFLAGS) -c $< $(HDF5_I)

histogram.o : histogram.cpp
	$(CC) $(CFLAGS) -c $< $(HDF5_I)

mara_mpi.o : mara_mpi.c
	$(CC) $(CFLAGS) -c $< $(MARA_I)

measure.o : measure.cpp
	$(CC) $(CFLAGS) -c $< $(MARA_I)

mara.o : mara.cpp
	$(CC) $(CFLAGS) -c $< $(MARA_I)

$(MARA_A) : $(OBJ_C) $(OBJ_CPP)
	$(AR) $@ $?

clean :
	rm -f $(MARA_A) $(OBJ_C) $(OBJ_CPP)
