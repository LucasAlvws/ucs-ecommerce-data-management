CC = gcc
CFLAGS = -O2 -std=c11 -Wall -Wextra

BIN = trabalho
DUMP_BIN = dump
EXPORT_BIN = export
SRC = trabalho.c
DUMP_SRC = dump.c
EXPORT_SRC = export.c

.PHONY: all run-dump run-export clean

all: $(BIN)
	./$(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

$(DUMP_BIN): $(DUMP_SRC)
	$(CC) $(CFLAGS) -o $@ $<

$(EXPORT_BIN): $(EXPORT_SRC)
	$(CC) $(CFLAGS) -o $@ $<

run-dump: $(DUMP_BIN)
	./$(DUMP_BIN)
	@echo ""
	@echo "Visualizar dump:"
	@echo "  cat dump.txt | less"

run-export: $(EXPORT_BIN)
	./$(EXPORT_BIN)

clean:
	rm -f $(BIN) $(DUMP_BIN) $(EXPORT_BIN) dump.txt *.dat *.idx joias.txt joias_idx.txt pedidos.txt pedidos_idx.txt
