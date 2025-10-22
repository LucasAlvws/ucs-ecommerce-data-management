CC = gcc
CFLAGS = -O2 -std=c11 -Wall -Wextra

BIN = trabalho
SRC = trabalho.c

.PHONY: all clean

all: $(BIN)
	./$(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(BIN) *.dat *.idx
