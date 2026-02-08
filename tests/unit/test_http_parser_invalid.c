// tests/unit/test_http_parser_invalid.c

#include <criterion/criterion.h>
#include <string.h>
#include <stdio.h>
#include "http_parser.h"

Test(http_parser_invalid, rejects_bad_method_and_version)
{
    char buf[] = "GeT / HTTP/1.1\r\nHost: x\r\n\r\n";
    HttpRequest req = {0};
    cr_assert_eq(parse_http_request(buf, strlen(buf), &req), -1);
}

Test(http_parser_invalid, rejects_long_request_line)
{
    char path[3000];
    memset(path, 'a', sizeof(path));
    path[sizeof(path) - 1] = '\0';

    char buf[4096];
    snprintf(buf, sizeof(buf), "GET /%s HTTP/1.1\r\nHost: x\r\n\r\n", path);
    HttpRequest req = {0};
    cr_assert_eq(parse_http_request(buf, strlen(buf), &req), -1);
}

Test(http_parser_invalid, rejects_too_many_headers)
{
    char buf[4096];
    size_t off = 0;
    off += snprintf(buf + off, sizeof(buf) - off, "GET / HTTP/1.1\r\n");
    for (int i = 0; i < MAX_HEADERS + 2; i++) {
        off += snprintf(buf + off, sizeof(buf) - off, "X-%d: v\r\n", i);
    }
    off += snprintf(buf + off, sizeof(buf) - off, "\r\n");
    HttpRequest req = {0};
    cr_assert_eq(parse_http_request(buf, off, &req), -1);
}
