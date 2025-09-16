CC = gcc
CFLAGS = -c -g -Wall

.SUFFIXES: .o .c
.c.o:

	$(CC)  $(CFLAGS)  $*.c

EXECS = client  server

all: $(EXECS)

server: server.o
	$(CC)  server.o -lpthread  -o $@

client: client.o 
	$(CC)  client.o -o $@

clean:
	rm -f  *.o
	rm -f $(EXECS)
