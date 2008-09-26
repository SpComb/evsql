LIBEVENT_PATH = ../libs/libevent-dev
LIBFUSE_PATH = ../libs/libfuse-2.7.4

LIBRARY_PATHS = -L${LIBEVENT_PATH}/lib -L${LIBFUSE_PATH}/lib
INCLUDE_PATHS = -I${LIBEVENT_PATH}/include -I${LIBFUSE_PATH}/include
LDLIBS = -levent -lfuse

# XXX: ugh... use `pkg-config fuse`
DEFINES = -D_FILE_OFFSET_BITS=64
MY_CFLAGS = -Wall -g -std=gnu99

BIN_NAMES = hello helloworld

bin/helloworld: 
bin/hello: obj/evfuse.o obj/dirbuf.o obj/lib/log.o obj/lib/signals.o
bin/simple_hello: obj/evfuse.o obj/dirbuf.o obj/lib/log.o obj/lib/signals.o obj/simple.o

# computed
LDFLAGS = ${LIBRARY_PATHS} ${LIBRARY_LIST}
CFLAGS = ${INCLUDE_PATHS} ${DEFINES} ${MY_CFLAGS}

SRC_PATHS = $(wildcard src/*.c)
SRC_NAMES = $(patsubst src/%,%,$(SRC_PATHS))
SRC_DIRS = $(dir $(SRC_NAMES))

BIN_PATHS = $(addprefix bin/,$(BIN_NAMES))

# targets
all: depend ${BIN_PATHS}

clean :
	-rm obj/* bin/*

depend:
	cd src
	makedepend -p../obj/ -Y -- $(CFLAGS) -- $(SRC_NAMES) 2> /dev/null
	cd ..

obj-dirs: 
	python build/make_obj_dirs.py $(BIN_PATHS)

obj/%.o : src/%.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $^ -o $@

bin/% : obj/%.o
	$(CC) $(LDFLAGS) $+ $(LOADLIBES) $(LDLIBS) -o $@

# DO NOT DELETE THIS LINE -- make depend depends on it.
