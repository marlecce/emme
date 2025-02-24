#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "http_parser.h"

int main() {
    int ret;

    // Test 1: GET request with only the Request-Line
    char request1[] =
        "GET /index.html HTTP/1.1\r\n"
        "\r\n";
    HttpRequest req1;
    ret = parse_http_request(request1, strlen(request1), &req1);
    assert(ret == 0);
    assert(strcmp(req1.method, "GET") == 0);
    assert(strcmp(req1.path, "/index.html") == 0);
    assert(strcmp(req1.version, "HTTP/1.1") == 0);

    // Test 2: POST request
    char request2[] =
        "POST /submit HTTP/1.0\r\n"
        "\r\n";
    HttpRequest req2;
    ret = parse_http_request(request2, strlen(request2), &req2);
    assert(ret == 0);
    assert(strcmp(req2.method, "POST") == 0);
    assert(strcmp(req2.path, "/submit") == 0);
    assert(strcmp(req2.version, "HTTP/1.0") == 0);

    // Test 3: Malformed request (missing CRLF)
    char request3[] = "INVALIDREQUEST\r\n\r\n";
    HttpRequest req3;
    ret = parse_http_request(request3, strlen(request3), &req3);
    assert(ret == -1);

    printf("All HTTP parser tests passed!\n");
    return 0;
}
