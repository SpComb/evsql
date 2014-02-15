# evsql

Non-blocking SQL API for use with [libevent](http://monkey.org/~provos/libevent/).

Currently only includes support for [PostgreSQL libpq](http://www.postgresql.org/docs/8.3/static/libpq.html), so consider this more of a generically named libpq wrapper, rather than a truly generic SQL API.

## Dependencies

### APT
* `cmake`
* `libevent-dev`
* `libpq-dev`

## Build

    $ cmake -D BUILD_SHARED_LIBS=true .
    $ make evsql

## Install
    $ cmake -D CMAKE_INSTALL_PREFIX=/opt/evsql .
    $ make install

## Test

Per default, the test will connect using libpq defaults, i.e. `localhost`, and `dbname/role=$USER`.

    $ vim src/evsql_test.c

        #define CONNINFO_DEFAULT ""

### Build
    $ make evsql_test

### Run
    $ ./src/evsql_test

## Documentation

### Dependencies (APT)
* `doxygen`
    
### Build
    $ cmake .
    $ make doc
    $ ls doc/html/
