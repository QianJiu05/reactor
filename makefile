all: reactor

HEADERS = connect_pool.h reactor.h http_handler.h resource_handler.h http_helper.h config.h server_init.h
REACTOR_DEPS = reactor.c connect_pool.c http_handler.c resource_handler.c server_init.c $(HEADERS)


reactor : $(REACTOR_DEPS)
	gcc -o reactor $(REACTOR_DEPS)
