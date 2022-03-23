CC=g++
CFLAGS=-O3 -std=c++17 -ggdb -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include -I/usr/include/gdk-pixbuf-2.0
LDFLAGS=-lX11 -lgpgme -lnotify

MAKEFILE=Makefile
CLANGDINFO=compile_commands.json

SRC=.
SRCS=$(wildcard $(SRC)/*.cpp)

OBJ=obj
OBJS=$(patsubst $(SRC)/%.cpp, $(OBJ)/%.o, $(SRCS))

BIN=dmenupass

make: $(CLANGDINFO) $(BIN)

run: $(BIN)
	./$(BIN)

clean:
	rm -r $(OBJ) || true
	rm $(BIN) || true
	rm $(CLANGDINFO) || true

# Rules for compilation

$(BIN): $(OBJS) $(OBJ)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ)/%.o: $(SRC)/%.cpp $(OBJ)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ):
	mkdir -p $@


# Create compile commands
makebin: $(BIN)

$(CLANGDINFO): $(MAKEFILE)
	make clean
	bear -- make makebin
