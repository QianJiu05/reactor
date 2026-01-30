all: client reactor

client : client.c 
	gcc -o client client.c -lpthread

reactor : reactor.c
	gcc -o reactor reactor.c

# client.o : gcc -o client client.c -lpthread