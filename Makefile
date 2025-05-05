CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude -D_GNU_SOURCE
LDFLAGS = -luring -lpthread -lssl -lcrypto -lyaml -lnghttp2

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
EXEC = emme

# Target principale per compilare il server
all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) $(CFLAGS) -o $(EXEC) $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Target per compilare il test della configurazione
test_config: tests/test_config.c src/config.c
	$(CC) $(CFLAGS) -Iinclude -o test_config tests/test_config.c src/config.c src/log.c $(LDFLAGS)

# Target per compilare il test del server
test_server: tests/test_server.c src/server.c
	$(CC) $(CFLAGS) -Iinclude -o test_server tests/test_server.c src/tls.c src/http_parser.c src/router.c src/thread_pool.c src/server.c src/log.c $(LDFLAGS)

test_http_parser: tests/test_http_parser.c
	$(CC) $(CFLAGS) -Iinclude -o test_http_parser tests/test_http_parser.c src/router.c src/http_parser.c src/log.c $(LDFLAGS)

# Target test: compila ed esegue tutti i test
test: test_config test_server test_http_parser
	@echo \"Running test_config...\"\n./test_config
	@echo \"Running test_server...\"\n./test_server
	@echo \"Running test_http_parser...\"\n./test_http_parser

clean:
	rm -f $(OBJ) $(EXEC) test_config test_server test_http_parser *.log
