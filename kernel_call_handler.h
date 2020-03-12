#ifndef KERNEL_CALL_HANDLER_H
#define KERNEL_CALL_HANDLER_H

#include <comp421/hardware.h>
#include <comp421/yalnix.h>

#include "memory_manager.h"
#include "trap_handler.h"
#include "process_controller.h"

int handle_getpid();

int handle_fork();

int handle_delay(int delay);

int handle_brk(void *addr);

int handle_exec(char *filename, char ** argvec, ExceptionInfo *info);

void handle_exit(int status);

int handle_wait(int *status_ptr);

#endif