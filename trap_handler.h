#ifndef TRAP_HANDLER_H
#define TRAP_HANDLER_H

#include "process_controller.h"

typedef struct tty_buf {
    queue in_buf;
    char out_buf[TERMINAL_MAX_LINE];
    int writing, reading;
} tty_buf;

extern tty_buf tty[NUM_TERMINALS];
void init_trap_handler();

#endif