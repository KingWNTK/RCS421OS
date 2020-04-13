#include <stdio.h>
#include <comp421/hardware.h>
#include <comp421/yalnix.h>

char s[10000];

int main(int argc, char **argv) {
    if(argc < 3) {
        printf("wrong arg");
    }
    else {
        int a = atoi(argv[1]);
        int b = atoi(argv[2]);
        printf("pid: %d, a + b = %d\n", GetPid(), a + b);
    }
    Exit(0);
}