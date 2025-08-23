CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude -D_GNU_SOURCE
LDFLAGS = -luring -lpthread -lssl -lcrypto -lyaml -lnghttp2
CRITERION_FLAGS = $(shell pkg-config --cflags --libs criterion)

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
OBJ_NO_MAIN = $(filter-out src/main.o, $(OBJ))
EXEC = emme

# --- Coverage toggles ---
COVERAGE ?= 0

ifeq ($(COVERAGE),1)
  CFLAGS   += -O0 -g -fprofile-arcs -ftest-coverage
  LDFLAGS  += -fprofile-arcs -ftest-coverage
endif

# Ensure debug symbols even in regular builds (helps reports)
CFLAGS += -g

.PHONY: coverage coverage-clean coverage-report

coverage-clean:
	@rm -rf coverage *.gcda *.gcno **/*.gcda **/*.gcno

coverage: coverage-clean
	$(MAKE) clean
	$(MAKE) COVERAGE=1 all
	$(MAKE) COVERAGE=1 test
	$(MAKE) coverage-report

# Text + HTML + XML (CI friendly)
coverage-report:
	@mkdir -p coverage
	gcovr -r . --exclude 'tests' --branches --txt > coverage/summary.txt
	gcovr -r . --exclude 'tests' --branches --xml-pretty -o coverage/coverage.xml
	gcovr -r . --exclude 'tests' --branches --html --html-title "emme coverage" -o coverage/coverage.html
	gcovr -r . --exclude 'tests' --branches --html-details coverage/index.html --html-title "emme coverage"
	@echo "Open coverage/index.html"

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

$(integration_binaries): %: %.c
	$(CC) $(CFLAGS) -Iinclude -o $@ $< $(OBJ_NO_MAIN) $(CRITERION_FLAGS) $(LDFLAGS)

# E2E tests
e2e_tests := $(wildcard tests/e2e/*.c)
e2e_binaries := $(e2e_tests:.c=)

$(e2e_binaries): %: %.c $(OBJ_NO_MAIN)
	$(CC) $(CFLAGS) -Iinclude -o $@ $< $(OBJ_NO_MAIN) $(CRITERION_FLAGS) $(LDFLAGS)

certs/dev.crt certs/dev.key:
	mkdir -p certs
	openssl req -newkey rsa:2048 -nodes \
	  -keyout certs/dev.key \
	  -x509 -days 365 \
	  -out certs/dev.crt \
	  -subj "/CN=localhost"

# Ensure certs exist before building server or running tests
emme: certs/dev.crt certs/dev.key

# Run all tests
#tests/all_tests: $(unit_tests) $(integration_tests) $(e2e_tests) $(OBJ_NO_MAIN)
#	$(CC) $(CFLAGS) -Iinclude -o $@ $^ $(CRITERION_FLAGS) $(LDFLAGS)

#test: certs/dev.crt certs/dev.key $(EXEC) tests/all_tests
#	./tests/all_tests

test: certs/dev.crt certs/dev.key $(EXEC) $(unit_binaries) $(integration_binaries) $(e2e_binaries)
	@set -e; \
	for t in $(unit_binaries) $(integration_binaries) $(e2e_binaries); do \
		echo "Running $$t..."; ./$$t; \
	done

clean: coverage-clean
	rm -f $(OBJ) $(EXEC) $(unit_binaries) $(integration_binaries) $(e2e_binaries) *.log
