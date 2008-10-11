
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "lib/url.h"

#define FAIL(...) do { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); return -1; } while (0)

struct url_schema
    basic_http = { 1, { "http" } },
    svn_ssh = { 2, { "svn", "ssh" } },
    schema_unix = { 1, { "unix" } }
    ;

struct url_opts
    opts_single = { 1, { { "key0", "val0" } } },
    opts_multi = { 2, { { "key0", "val0" }, { "key1", "val1" } } },
    opts_nullval = { 1, { { "keyN", NULL } } }
    ;

struct url_test {
    const char *url;
    const struct url expected;
} url_tests[] = {
    {   "localhost:http",   {
        NULL, NULL, NULL, "localhost", "http", NULL, NULL
    } },

    {   "http://example.com/path",  {
        &basic_http, NULL, NULL, "example.com", NULL, "path", NULL 
    } },

    {   "svn+ssh://user:passwd@someplace:someport/something",   {
        &svn_ssh, "user", "passwd", "someplace", "someport", "something"
    } },

    {   "user@:service/",   {
        NULL, "user", NULL, NULL, "service", NULL
    } },

    {   "unix:////tmp/foo.sock",    {
        &schema_unix, NULL, NULL, NULL, NULL, "/tmp/foo.sock"
    } },

    {   "unix:///tmp/foo.sock", {
        &schema_unix, NULL, NULL, NULL, NULL, "tmp/foo.sock"
    } },

    {   "/tmp/foo.sock",    {
        NULL, NULL, NULL, NULL, NULL, "tmp/foo.sock"
    } },

    {   "?key0=val0",   {
        NULL, NULL, NULL, NULL, NULL, NULL, &opts_single
    } },

    {   "http://foo.com/index.php?key0=val0&key1=val1",  {
        &basic_http, NULL, NULL, "foo.com", NULL, "index.php", &opts_multi
    } },

    {   "example.org:81/?keyN", {
        NULL, NULL, NULL, "example.org", "81", NULL, &opts_nullval
    } },

    {   NULL,               {   } },
};

int cmp_url_str (const char *field, const char *test, const char *real) {
    if (!test) {
        if (real)
            FAIL("%s shouldn't be present", field);

    } else if (!real) {
        FAIL("%s is missing", field);

    } else {
        if (strcmp(test, real) != 0)
            FAIL("%s differs: %s -> %s", field, test, real);
    }

    // ok
    return 0;
}

int cmp_url (const struct url *test, const struct url *real) {
    int i;

    // test schema
    if (!test->schema) {
        if (real->schema)
            FAIL("test has no schema, but real does");

    } else if (!real->schema) {
        FAIL("test has a schema, but real doesn't");

    } else {
        if (test->schema->count != test->schema->count)
            FAIL("inconsistent scheme count");
        
        for (i = 0; i < test->schema->count; i++) {
            if (strcmp(test->schema->list[i], real->schema->list[i]) != 0)
                FAIL("differing scheme #%d", i);
        }
    }
    
    // test username
    if (cmp_url_str("username", test->username, real->username))
        goto error;

    // test password
    if (cmp_url_str("password", test->password, real->password))
        goto error;

    // test hostname
    if (cmp_url_str("hostname", test->hostname, real->hostname))
        goto error;

    // test service
    if (cmp_url_str("service", test->service, real->service))
        goto error;

    // test path
    if (cmp_url_str("path", test->path, real->path))
        goto error;

    // test query
    if (!test->opts) {
        if (real->opts)
            FAIL("test has no opts, but real does");

    } else if (!real->opts) {
        FAIL("test has opts, but real doesn't");

    } else {
        if (test->opts->count != test->opts->count)
            FAIL("inconsistent opts count");
        
        for (i = 0; i < test->opts->count; i++) {
            if (cmp_url_str("opt key", test->opts->list[i].key, real->opts->list[i].key))
                FAIL("differing opt key #%d", i);
            
            if (cmp_url_str("opt value", test->opts->list[i].value, real->opts->list[i].value))
                FAIL("differing opt value #%d", i);
        }
    }

    // ok
    return 0;

error:
    return -1;
}

void usage (const char *exec_name) {
    printf("Usage: %s\n\n\tNo arguments are accepted\n", exec_name);

    exit(EXIT_FAILURE);
}

int main (int argc, char **argv) {
    const struct url_test *test;
    struct url url;

    if (argc > 1)
        usage(argv[0]);

    // run the tests
    for (test = url_tests; test->url; test++) {
        // first output the URL we are handling...
        printf("%-80s - ", test->url);
        fflush(stdout);
        
        // parse the URL
        memset(&url, 0, sizeof(url));

        if (url_parse(&url, test->url)) {
            printf("FATAL: url_parse failed\n");
            return EXIT_FAILURE;
        }
        
        // compare it
        if (cmp_url(&test->expected, &url)) {
            printf("\texpected: ");
            url_dump(&test->expected, stdout);

            printf("\tresult:   ");
            url_dump(&url, stdout);

        } else {
            printf("OK\n\t");
            url_dump(&url, stdout);
        }

        printf("\n");
    }
}

