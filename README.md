# evsql

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
