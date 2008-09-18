LIBEVENT_PATH = libs/libevent
LIBFUSE_PATH = libs/libfuse

LIBRARY_PATHS = -L${LIBEVENT_PATH}/lib -L${LIBFUSE_PATH}/lib
INCLUDE_PATHS = -I${LIBEVENT_PATH}/include -I${LIBFUSE_PATH}/include
LDLIBS = -levent

DEFINES =
MY_CFLAGS = -Wall -g -std=gnu99

BIN_NAMES = helloworld

bin/helloworld: obj/helloworld.o

# computed
LDFLAGS = ${LIBRARY_PATHS} ${LIBRARY_LIST}
CFLAGS = ${INCLUDE_PATHS} ${DEFINES} ${MY_CFLAGS}

SRC_PATHS = $(wildcard src/*.c)
SRC_NAMES = $(patsubst src/%,%,$(SRC_PATHS))

BIN_PATHS = $(addprefix bin/,$(BIN_NAMES))

# targets
all: depend ${BIN_PATHS}

clean :
	-rm obj/* bin/*

depend:
	cd src
	makedepend -p../obj/ -Y -- $(CFLAGS) -- $(SRC_NAMES) 2> /dev/null
	cd ..

obj/%.o : src/%.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@

bin/% : obj/%.o
	$(CC) $(LDFLAGS) $< $(LOADLIBES) $(LDLIBS) -o $@

# DO NOT DELETE THIS LINE -- make depend depends on it.
