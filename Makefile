LIBEVENT_PATH = ../libs/libevent-dev
LIBFUSE_PATH = ../libs/libfuse-2.7.4

LIBRARY_PATHS = -L${LIBEVENT_PATH}/lib -L${LIBFUSE_PATH}/lib
INCLUDE_PATHS = -I${LIBEVENT_PATH}/include -I${LIBFUSE_PATH}/include
LDLIBS = -levent -lfuse -lpq

# XXX: ugh... use `pkg-config fuse`
DEFINES = -D_FILE_OFFSET_BITS=64
MY_CFLAGS = -Wall -g -std=gnu99

BIN_NAMES = helloworld hello simple_hello evpq_test url_test dbfs
BIN_PATHS = $(addprefix bin/,$(BIN_NAMES))

# first target
all: ${BIN_PATHS}

# binaries
bin/helloworld: 
bin/hello: obj/evfuse.o obj/dirbuf.o obj/lib/log.o obj/lib/signals.o
bin/simple_hello: obj/evfuse.o obj/dirbuf.o obj/lib/log.o obj/lib/signals.o obj/simple.o
bin/evpq_test: obj/evpq.o obj/lib/log.o
bin/url_test: obj/lib/url.o obj/lib/lex.o obj/lib/log.o
bin/dbfs: obj/evsql.o obj/evsql_util.o obj/evpq.o obj/evfuse.o obj/dirbuf.o obj/lib/log.o obj/lib/signals.o

# computed
LDFLAGS = ${LIBRARY_PATHS} ${LIBRARY_LIST}
CFLAGSX = ${DEFINES} ${MY_CFLAGS}
CFLAGS = ${INCLUDE_PATHS} ${CFLAGSX}

SRC_PATHS = $(wildcard src/*.c) $(wildcard src/*/*.c)
SRC_NAMES = $(patsubst src/%,%,$(SRC_PATHS))
SRC_DIRS = $(dir $(SRC_NAMES))

# other targets
clean :
	-rm obj/* bin/*

#obj-dirs: 
#	python build/make_obj_dirs.py $(BIN_PATHS)

build/deps/%.d : src/%.c
	@set -e; rm -f $@; \
	 $(CC) -MM -MT __ $(CPPFLAGS) $< > $@.$$$$; \
	 sed 's,__[ :]*,obj/$*.o $@ : ,g' < $@.$$$$ > $@; \
	 rm -f $@.$$$$

include $(SRC_NAMES:%.c=build/deps/%.d)

obj/%.o : src/%.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@

bin/% : obj/%.o
	$(CC) $(LDFLAGS) $+ $(LOADLIBES) $(LDLIBS) -o $@

