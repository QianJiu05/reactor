
all: reactor

HEADERS = 	config.h \
			inc/connect_pool.h \
			inc/reactor.h \
			inc/server_init.h\
			inc/epoll.h\
			inc/accept.h\
			inc/recv.h\
			inc/callback.h\
			inc/http/http_handler.h \
			inc/http/recv_resource.h \
			inc/http/http_helper.h 

SRC_FILES = example/main.c\
			src/reactor.c \
			src/connect_pool.c \
			src/server_init.c\
			src/epoll.c\
			src/accept.c\
			src/recv.c\
			src/callback.c\
			src/http/http_handler.c \
			src/http/recv_resource.c 
			
			

reactor: $(SRC_FILES) $(HEADERS)
	gcc -o reactor $(SRC_FILES) -Iinc -Iinc/http -I. 