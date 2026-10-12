
#ifndef __RECV_H__
#define __RECV_H__

#include "connect_pool.h"


#define SERVE_NOT_INIT     -1
#define SERVE_ECHO          0
#define SERVE_HTTP          1
#define SERVE_GET_RESOURCE  2

/* enum conn_phase 与 MAX_HANDSHAKE_LEN 见 connect_pool.h / config.h */

int recv_callback(struct connect* conn);

#endif
