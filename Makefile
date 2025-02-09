CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude -D_GNU_SOURCE
LDFLAGS = -luring -lpthread

SRC = src/main.c src/config.c src/server.c
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
	$(CC) $(CFLAGS) -Iinclude -o test_config tests/test_config.c src/config.c $(LDFLAGS)

# Target per compilare il test del server
test_server: tests/test_server.c src/server.c
	$(CC) $(CFLAGS) -Iinclude -o test_server tests/test_server.c src/server.c $(LDFLAGS)

# Target test: compila ed esegue tutti i test
test: test_config test_server
	@echo \"Esecuzione test_config...\"\n./test_config
	@echo \"Esecuzione test_server...\"\n./test_server

clean:
	rm -f $(OBJ) $(EXEC) test_config test_server
