#include <comp421/hardware.h>
#include <stdio.h>

int main() {
    printf("running ide\n");
    while(1) {
        printf("idle called pause\n");
        Pause();
    }
}