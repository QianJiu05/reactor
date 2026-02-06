#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "connect_pool.h"
#include "reactor.h"
// struct connect_pool pool;

/*************** structure ***************/
static struct connect_node* alloc_new_pool(struct reactor* reactor) {
    printf("alloc new pool\n");
    struct connect_node* new = malloc(sizeof(struct connect_node));
    if (new == NULL) {
        printf("alloc new pool err\n");
        return NULL;
    }
    memset(new, 0, sizeof(struct connect_node));
    if (reactor->pool.last != NULL) {
        reactor->pool.last->next = new;
        reactor->pool.last = new;
    }
    reactor->pool.num_of_pool++;
    return new;
}

void connect_pool_init(struct reactor* reactor) {
    memset(&reactor->pool, 0, sizeof(struct connect_pool));

    reactor->pool.start = alloc_new_pool(reactor);
    reactor->pool.last = reactor->pool.start;
}

static struct connect_node* get_pool (int num, struct reactor* reactor) {
    //num_of_pool是从1开始的,numpool=3:0,1,2
    // num是从0开始的，1/128 =0
    //比如num=3，num_of_pool=3,
    // printf("num =%d, numofpool=%d\n",num,pool.num_of_pool);
    while (num >= reactor->pool.num_of_pool) {
        // printf("pool not exist,creating..\n");
        alloc_new_pool();
    }

    struct connect_node* ret = reactor->pool.start;
    for(int i = 0; i < num; i++) {
        ret = ret->next;
    }
    return ret;
}

struct connect* get_connector(int fd, struct reactor* reactor) {
    //在第几个pool
    struct connect_node* node = get_pool(fd / NUM_OF_CONNECTOR, reactor);
    //在这个pool的第几个
    int cnt = fd % NUM_OF_CONNECTOR;
    return &node->pool[cnt];
}