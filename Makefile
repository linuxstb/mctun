all: client server

client: client.c common.h
	gcc -W -Wall -o client client.c

server: server.c common.h
	gcc -W -Wall -o server server.c

clean:
	rm -f client server
