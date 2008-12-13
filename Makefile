LIBEVENT_PATH = ../libs/libevent-dev
LIBFUSE_PATH = ../opt

LIBRARY_PATHS = -L${LIBEVENT_PATH}/lib -L${LIBFUSE_PATH}/lib
INCLUDE_PATHS = -I${LIBEVENT_PATH}/include -I${LIBFUSE_PATH}/include
LDLIBS = -levent -lfuse -lpq

# default is test
ifndef MODE
MODE = test
endif

ifeq ($(MODE), debug)
MODE_CFLAGS = -g -DDEBUG_ENABLED
else ifeq ($(MODE), dev)
MODE_CFLAGS = -g
else ifeq ($(MODE), test)
MODE_CFLAGS = -g -DINFO_DISABLED
else ifeq ($(MODE), release)
MODE_CFLAGS = -DINFO_DISABLED -O2
endif

# XXX: ugh... use `pkg-config fuse`
DEFINES = -D_FILE_OFFSET_BITS=64
FIXED_CFLAGS = -Wall -std=gnu99

BIN_NAMES = helloworld hello simple_hello evpq_test url_test dbfs evsql_test
BIN_PATHS = $(addprefix bin/,$(BIN_NAMES))

# modules
module_objs = $(patsubst src/%.c,obj/%.o,$(wildcard src/$(1)/*.c))

# complex modules
CORE_OBJS = obj/lib/log.o obj/lib/signals.o
EVSQL_OBJS = $(call module_objs,evsql) obj/evpq.o
DBFS_OBJS = $(call module_objs,dbfs) obj/dirbuf.o 

# first target
all: ${BIN_PATHS}

# binaries
bin/helloworld: 
bin/hello: obj/evfuse.o obj/dirbuf.o ${CORE_OBJS}
bin/simple_hello: obj/evfuse.o obj/dirbuf.o obj/simple.o ${CORE_OBJS}
bin/evpq_test: obj/evpq.o obj/lib/log.o
bin/url_test: obj/lib/url.o obj/lib/lex.o obj/lib/log.o
bin/dbfs: ${DBFS_OBJS} ${EVSQL_OBJS} obj/evfuse.o ${CORE_OBJS}
bin/evsql_test: ${EVSQL_OBJS} ${CORE_OBJS}

# computed
LDFLAGS = ${LIBRARY_PATHS}

CPPFLAGS = ${INCLUDE_PATHS} ${DEFINES}
CFLAGS = ${MODE_CFLAGS} ${FIXED_CFLAGS}

SRC_PATHS = $(wildcard src/*.c) $(wildcard src/*/*.c)
SRC_NAMES = $(patsubst src/%,%,$(SRC_PATHS))
SRC_DIRS = $(dir $(SRC_NAMES))

# other targets
clean :
	-rm obj/*.o obj/*/*.o
	-rm bin/* 
	-rm build/deps/*.d build/deps/*/*.d

clean-deps:
	-rm build/deps/*/*.d 
	-rm build/deps/*.d

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

# documentation
DOXYGEN_PATH = /usr/bin/doxygen
DOXYGEN_CONF_PATH = doc/doxygen.conf
DOXYGEN_OUTPUT_FILE = doc/html/index.html

docs :
	${DOXYGEN_PATH} ${DOXYGEN_CONF_PATH}

