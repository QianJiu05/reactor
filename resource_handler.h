#ifndef __RESOURCE_HANDLER_H__
#define __RESOURCE_HANDLER_H__
#include "connect_pool.h"

int get_resource_callback(struct connect* conn);
int handle_resource(struct connect* conn);

#endif