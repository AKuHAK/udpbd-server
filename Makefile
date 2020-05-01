BIN=udpbd-server
OBJS=main.o

udpbd-server: $(OBJS)

all: $(BIN)

clean:
	rm -f $(BIN) $(OBJS)
