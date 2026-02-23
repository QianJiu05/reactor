#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "connect_pool.h"
#include "reactor.h"

struct connect_pool pool;

/*************** structure ***************/
static struct connect_node* alloc_new_pool(void) {
    printf("alloc new pool\n");
    struct connect_node* new = malloc(sizeof(struct connect_node));
    if (new == NULL) {
        printf("alloc new pool err\n");
        return NULL;
    }
    memset(new, 0, sizeof(struct connect_node));
    pool.last->next = new;
    pool.last = new;
    pool.num_of_pool++;
    printf("ok\n");
    return new;
}

void connect_pool_init(void) {
    memset(&pool, 0, sizeof(struct connect_pool));

    // pool.start = alloc_new_pool();
    // pool.last = pool.start;

    // 先创建第一个节点，不依赖 pool.last
    struct connect_node* first = malloc(sizeof(struct connect_node));
    memset(first, 0, sizeof(struct connect_node));
    
    pool.start = first;
    pool.last = first;
    pool.num_of_pool = 1;
}

static struct connect_node* get_pool (int num) {
    //num_of_pool是从1开始的,numpool=3:0,1,2
    // num是从0开始的，1/128 =0
    //比如num=3，num_of_pool=3,
    // printf("num =%d, numofpool=%d\n",num,pool.num_of_pool);
    while (num >= pool.num_of_pool) {
        // printf("pool not exist,creating..\n");
        alloc_new_pool();
    }

    struct connect_node* ret = pool.start;
    for(int i = 0; i < num; i++) {
        ret = ret->next;
    }
    return ret;
}

struct connect* get_connector(int fd) {
    //在第几个pool
    struct connect_node* node = get_pool(fd / NUM_OF_CONNECTOR);
    //在这个pool的第几个
    int cnt = fd % NUM_OF_CONNECTOR;
    return &node->pool[cnt];
}