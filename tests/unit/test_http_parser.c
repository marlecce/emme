#include <criterion/criterion.h>
#include <string.h>
#include "http_parser.h"

Test(http_parser, get_request)
{
    char request[] = "GET /index.html HTTP/1.1\r\n\r\n";
    HttpRequest req;
    int ret = parse_http_request(request, strlen(request), &req);
    cr_assert_eq(ret, 0, "GET request should parse successfully");
    cr_assert_str_eq(req.method, "GET", "Method mismatch");
    cr_assert_str_eq(req.path, "/index.html", "Path mismatch");
    cr_assert_str_eq(req.version, "HTTP/1.1", "Version mismatch");
}

Test(http_parser, post_request)
{
    char request[] = "POST /submit HTTP/1.0\r\n\r\n";
    HttpRequest req;
    int ret = parse_http_request(request, strlen(request), &req);
    cr_assert_eq(ret, 0, "POST request should parse successfully");
    cr_assert_str_eq(req.method, "POST", "Method mismatch");
    cr_assert_str_eq(req.path, "/submit", "Path mismatch");
    cr_assert_str_eq(req.version, "HTTP/1.0", "Version mismatch");
}

Test(http_parser, malformed_request)
{
    char request[] = "INVALIDREQUEST\r\n\r\n";
    HttpRequest req;
    int ret = parse_http_request(request, strlen(request), &req);
    cr_assert_eq(ret, -1, "Malformed request should fail to parse");
}