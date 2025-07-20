CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude -D_GNU_SOURCE
LDFLAGS = -luring -lpthread -lssl -lcrypto -lyaml -lnghttp2
CRITERION_FLAGS = $(shell pkg-config --cflags --libs criterion)

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
OBJ_NO_MAIN = $(filter-out src/main.o, $(OBJ))
EXEC = emme

# Target principale per compilare il server
all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) $(CFLAGS) -o $(EXEC) $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Unit tests
unit_tests := $(wildcard tests/unit/*.c)
unit_binaries := $(unit_tests:.c=)

$(unit_binaries): %: %.c $(OBJ_NO_MAIN)
	$(CC) $(CFLAGS) -Iinclude -o $@ $< $(OBJ_NO_MAIN) $(CRITERION_FLAGS) $(LDFLAGS)

# Integration tests
integration_tests := $(wildcard tests/integration/*.c)
integration_binaries := $(integration_tests:.c=)

$(integration_binaries): %: %.c $(OBJ_NO_MAIN)
	$(CC) $(CFLAGS) -Iinclude -o $@ $< $(OBJ_NO_MAIN) $(CRITERION_FLAGS) $(LDFLAGS)

# E2E tests
e2e_tests := $(wildcard tests/e2e/*.c)
e2e_binaries := $(e2e_tests:.c=)

$(e2e_binaries): %: %.c $(OBJ_NO_MAIN)
	$(CC) $(CFLAGS) -Iinclude -o $@ $< $(OBJ_NO_MAIN) $(CRITERION_FLAGS) $(LDFLAGS)

# Run all tests
test: $(unit_binaries) $(integration_binaries) $(e2e_binaries)
	@for t in $(unit_binaries) $(integration_binaries) $(e2e_binaries); do \
		echo "Running $$t..."; ./$$t; \
	done

clean:
	rm -f $(OBJ) $(EXEC) $(unit_binaries) $(integration_binaries) $(e2e_binaries) *.log
