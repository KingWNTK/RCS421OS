#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include <stdio.h>

void test_exec() {
    int ret = Fork();
    if (ret != 0) {
        char *filename = "add";
        char *argvec[] = {
            "add",
            "123",
            "456",
            NULL};
        Exec(filename, argvec);
    }

    while (1) {
        printf("pause in init\n");
        Pause();
    }
}

void test_brk() {
    while (1) {
        printf("pid: %d, Delay\n", GetPid());
        printf("Delay: %d\n", Delay(2));
        printf("Brk: %d\n", Brk(0x30000));
        char *s = 0x2f000;
        int i = 0;
        for (i = 0; i < 50; i++) {
            s[i] = 'a' + i % 26;
        }
        s[50] = '\0';
        printf("%s\n", s);
        Brk(0x2f000);
    }
}

void test_fork() {
    int ret = Fork();
    if (ret == 0)
        Fork();
    while (1) {
        printf("pid: %d pause\n", GetPid());
        Pause();
    }
}

void test_wait() {
    int ret = Fork();
    if (ret != 0) {
        char *filename = "add";
        char *argvec[] = {
            "add",
            "123",
            "456",
            NULL};
        Exec(filename, argvec);
    }

    while (1) {
        printf("pause in init\n");
        int stat = -1;
        printf("start waiting\n");
        int pid = Wait(&stat);
        printf("ret from wait, child pid: %d, stat: %d\n", pid, stat);
        Pause();
    }
}

int main() {
    test_wait();
}