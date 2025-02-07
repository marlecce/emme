CC=gcc
CFLAGS=-Wall -Wextra -std=c11 -Iinclude
LDFLAGS=-lyaml
SRC=src/main.c src/config.c src/server.c
OBJ=$(SRC:.c=.o)
EXEC=emme

all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) $(CFLAGS) -o $(EXEC) $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(EXEC)
