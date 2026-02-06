#include <pthread.h>


#include "config.h"

pthread_t threads[NUM_OF_REACTOR];// one reactor --> one thread (one loop)
void create_threads(void *(*func_reactor) (void *)) {
    for (int i = 0; i < NUM_OF_REACTOR; i++) {
        threads[i] = pthread_create(&threads[i], NULL, func_reactor, NULL);
    }
}


