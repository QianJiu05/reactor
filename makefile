
all: reactor

HEADERS = 	inc/connect_pool.h \
			inc/reactor.h \
			inc/http_handler.h \
			inc/recv_resource.h \
			inc/http_helper.h \
			config.h \
			inc/server_init.h


SRC_FILES = src/reactor.c \
			src/connect_pool.c \
			src/http_handler.c \
			src/recv_resource.c \
			src/server_init.c
			

reactor: $(SRC_FILES) $(HEADERS)
	gcc -o reactor $(SRC_FILES) -Iinc