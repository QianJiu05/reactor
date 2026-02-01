all: client reactor

HEADERS = connect_pool.h reactor.h http_handler.h get_resource.h
REACTOR_DEPS = reactor.c connect_pool.c http_handler.c get_resource.c $(HEADERS)
CLIENT_DEPS = client.c


client : $(CLIENT_DEPS)
	gcc -o client client.c -lpthread

reactor : $(REACTOR_DEPS)
	gcc -o reactor reactor.c connect_pool.c http_handler.c get_resource.c

# client.o : gcc -o client client.c -lpthread