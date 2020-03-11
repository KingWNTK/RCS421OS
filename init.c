#include <comp421/hardware.h>
#include <comp421/yalnix.h>
#include <stdio.h>

int main() {
    printf("running init\n");
    int ret = Fork();
    Fork();
    while(1) {
        printf("pid:%d called pause\n", GetPid());
        Pause();
    }
}