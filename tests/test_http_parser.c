// test_http_parser.c
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "http_parser.h"

int main() {
    int ret;

    // Test 1: Richiesta GET corretta con due header
    char request1[] =
        "GET /index.html HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "User-Agent: TestClient\r\n"
        "\r\n";
    HttpRequest req1;
    ret = parse_http_request(request1, strlen(request1), &req1);
    assert(ret == 0);
    assert(strcmp(req1.method, "GET") == 0);
    assert(strcmp(req1.path, "/index.html") == 0);
    assert(strcmp(req1.version, "HTTP/1.1") == 0);
    assert(req1.header_count == 2);
    assert(strcmp(req1.headers[0].field, "Host") == 0);
    assert(strcmp(req1.headers[0].value, "example.com") == 0);
    assert(strcmp(req1.headers[1].field, "User-Agent") == 0);
    assert(strcmp(req1.headers[1].value, "TestClient") == 0);

    // Test 2: Richiesta POST corretta con header Content-Type e Content-Length
    char request2[] =
        "POST /submit HTTP/1.0\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 27\r\n"
        "\r\n";
    HttpRequest req2;
    ret = parse_http_request(request2, strlen(request2), &req2);
    assert(ret == 0);
    assert(strcmp(req2.method, "POST") == 0);
    assert(strcmp(req2.path, "/submit") == 0);
    assert(strcmp(req2.version, "HTTP/1.0") == 0);
    assert(req2.header_count == 2);
    assert(strcmp(req2.headers[0].field, "Content-Type") == 0);
    assert(strcmp(req2.headers[0].value, "application/x-www-form-urlencoded") == 0);
    assert(strcmp(req2.headers[1].field, "Content-Length") == 0);
    assert(strcmp(req2.headers[1].value, "27") == 0);

    // Test 3: Richiesta malformata - manca il CRLF dopo la Request-Line
    char request3[] =
        "INVALIDREQUEST\r\n\r\n";
    HttpRequest req3;
    ret = parse_http_request(request3, strlen(request3), &req3);
    assert(ret == -1);

    // Test 4: Richiesta malformata - header senza ':' (errore)
    char request4[] =
        "GET / HTTP/1.1\r\n"
        "Host localhost\r\n"
        "\r\n";
    HttpRequest req4;
    ret = parse_http_request(request4, strlen(request4), &req4);
    assert(ret == -1);

    // Test 5: Richiesta con header extra (oltre al limite MAX_HEADERS)
    // In questo test si verifica che il parser ignori header in eccesso (o si comporti come da logica implementata)
    // Adattare il test in base alla gestione che si vuole dare agli header extra.
    char request5[1024];
    strcpy(request5, "GET / HTTP/1.1\r\n");
    for (int i = 0; i < MAX_HEADERS + 5; i++) {
        char line[64];
        snprintf(line, sizeof(line), "Header%d: Value%d\r\n", i, i);
        strcat(request5, line);
    }
    strcat(request5, "\r\n");
    HttpRequest req5;
    ret = parse_http_request(request5, strlen(request5), &req5);
    assert(ret == 0);
    // Verifica che si siano registrati solo MAX_HEADERS header
    assert(req5.header_count == MAX_HEADERS);

    printf("Tutti i test del parser sono passati!\n");
    return 0;
}
