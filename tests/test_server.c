#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include "server.h"

#define BUFFER_SIZE 1024

int main()
{
    int sv[2];
    // Crea una socket pair per simulare la comunicazione client-server
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1)
    {
        perror("socketpair");
        return 1;
    }

    // Simula una richiesta HTTP inviata dal client
    const char *request = "GET / HTTP/1.1\\r\\nHost: localhost\\r\\n\\r\\n";
    write(sv[0], request, strlen(request));

    // Chiama handle_client sulla socket corrispondente (simulando il server che elabora la richiesta)
    handle_client(sv[1]);

    // Legge la risposta inviata dal server (dall'altro terminale della socket pair)
    char buffer[BUFFER_SIZE];
    ssize_t n = read(sv[0], buffer, BUFFER_SIZE - 1);
    if (n < 0)
    {
        perror("read");
        return 1;
    }
    buffer[n] = '\0';

    // Verifica che la risposta contenga le stringhe attese
    assert(strstr(buffer, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(buffer, "Hello, world!") != NULL);

    printf("Test handle_client passed!\\n");

    close(sv[0]);
    close(sv[1]);
    return 0;
}
