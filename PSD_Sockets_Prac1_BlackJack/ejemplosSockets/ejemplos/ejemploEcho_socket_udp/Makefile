CC = gcc
CFLAGS = -c -g -Wall

.SUFFIXES: .o .c
.c.o:

	$(CC)  $(CFLAGS)  $*.c

EXECS = clientUDP  serverUDP

all: $(EXECS)

serverUDP: serverUDP.o
	$(CC)  serverUDP.o  -o $@

clientUDP: clientUDP.o 
	$(CC)  clientUDP.o -o $@

clean:
	rm -f  *.o
	rm -f $(EXECS)
