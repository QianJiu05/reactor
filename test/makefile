all : test_server test_client

test_server : test_server.c
	gcc -o test_server test_server.c

test_client : client_send.c
	gcc -o test_client client_send.c
