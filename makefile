all: client reactor

client : client.c 
	gcc -o client client.c -lpthread

reactor : reactor.c connect_pool.c
	gcc -o reactor reactor.c connect_pool.c

# client.o : gcc -o client client.c -lpthread