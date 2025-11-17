CC = gcc
CFLAGS = -Wall -pthread -g
LIBS = -lpthread

all: naming_server storage_server client

naming_server: naming_server.c common.c common.h
	$(CC) $(CFLAGS) -o naming_server naming_server.c common.c $(LIBS)

storage_server: storage_server.c common.c common.h
	$(CC) $(CFLAGS) -o storage_server storage_server.c common.c $(LIBS)

client: client.c common.c common.h
	$(CC) $(CFLAGS) -o client client.c common.c $(LIBS)

clean:
	rm -f naming_server storage_server client *.o system.log nm_metadata.dat
	rm -rf storage[0-9]*

run_nm:
	./naming_server 8080

run_ss1:
	mkdir -p storage1
	./storage_server 127.0.0.1 8080 9001 storage1

run_ss2:
	mkdir -p storage2
	./storage_server 127.0.0.1 8080 9002 storage2

run_client:
	./client 127.0.0.1 8080

.PHONY: all clean run_nm run_ss1 run_ss2 run_client