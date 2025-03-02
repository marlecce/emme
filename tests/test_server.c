#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include "server.h"
#include "config.h"
#include <sys/stat.h>

#define BUFFER_SIZE 1024

int main() {
    // Create a temporary directory and static file
    const char *static_dir = "temp_static";
    mkdir(static_dir, 0755);
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/index.html", static_dir);
    FILE *f = fopen(filepath, "w");
    assert(f != NULL);
    fprintf(f, "Hello, world!");
    fclose(f);

    // Create a temporary configuration to serve static files
    ServerConfig config;
    config.port = 8080;
    config.max_connections = 10;
    strcpy(config.log_level, "INFO");
    config.route_count = 1;
    strcpy(config.routes[0].path, "/static/");
    strcpy(config.routes[0].technology, "static");
    strcpy(config.routes[0].document_root, static_dir);

    // Create a socket pair to simulate client-server communication
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    // Simulate a GET request for /static/index.html
    const char *request = "GET /static/index.html HTTP/1.1\r\nHost: localhost\r\n\r\n";
    write(sv[0], request, strlen(request));

    // Call handle_client() on the server-side socket.
    // The handle_client function has been updated to accept a pointer to the configuration.
    extern void handle_client(int, ServerConfig *);
    handle_client(sv[1], &config);

    // Read the response sent by the server (client-side of the socket pair)
    char buffer[BUFFER_SIZE];
    ssize_t n = read(sv[0], buffer, BUFFER_SIZE - 1);
    assert(n > 0);
    buffer[n] = '\0';

    // The response must contain "Hello, world!"
    assert(strstr(buffer, "Hello, world!") != NULL);
    assert(strstr(buffer, "HTTP/1.1 200 OK") != NULL);
    printf("Test handle_client passed!\n");

    // Test for a non-existent file: /static/notfound.html
    const char *request_404 = "GET /static/notfound.html HTTP/1.1\r\nHost: localhost\r\n\r\n";
    write(sv[0], request_404, strlen(request_404));

    // Handle the new request
    handle_client(sv[1], &config);

    // Read the response from the server
    n = read(sv[0], buffer, BUFFER_SIZE - 1);
    assert(n > 0);
    buffer[n] = '\0';

    // The response must contain "404 Not Found"
    assert(strstr(buffer, "HTTP/1.1 404 Not Found") != NULL);
    printf("Test for non-existing resource (404) passed!\n");


    close(sv[0]);
    close(sv[1]);

    // Clean up temporary file and directory
    remove(filepath);
    rmdir(static_dir);

    return 0;
}
