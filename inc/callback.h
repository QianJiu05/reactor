
#ifndef __CALLBACK_H__
#define __CALLBACK_H__

int echo_callback(struct connect* conn);
void close_callback(struct connect* conn);
int print_callback(struct connect* conn);

#endif