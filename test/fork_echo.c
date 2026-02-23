#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(void) {
    const int N = 2;
    for (int i = 0; i < N; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }
        if (pid == 0) {
            execl("./echoth", NULL, (char *)NULL);
            perror("execl");
            _exit(127);
        }
    }

    // 父进程等待所有子进程结束
    for (int i = 0; i < N; ++i) {
        int status = 0;
        if (wait(&status) < 0) {
            perror("wait");
            return 1;
        }
    }
    return 0;
}