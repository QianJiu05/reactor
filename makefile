
all: reactor

HEADERS = 	config.h \
			inc/connect_pool.h \
			inc/reactor.h \
			inc/http_handler.h \
			inc/recv_resource.h \
			inc/http_helper.h \
			inc/server_init.h\
			inc/epoll.h\
			inc/accept.h\
			inc/recv.h\
			inc/callback.h


SRC_FILES = src/reactor.c \
			src/connect_pool.c \
			src/http_handler.c \
			src/recv_resource.c \
			src/server_init.c\
			src/epoll.c\
			src/accept.c\
			src/recv.c\
			src/callback.c\
			src/main.c
			

reactor: $(SRC_FILES) $(HEADERS)
	gcc -o reactor $(SRC_FILES) -Iinc -I.