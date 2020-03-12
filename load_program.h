#ifndef LOAD_PROGRAM_H
#define LOAD_PROGRAM_H
#include <comp421/hardware.h>
#include <comp421/loadinfo.h>

#include "memory_manager.h"
#include "process_controller.h"

int LoadProgram(char *name, char **args, ExceptionInfo *exp_info, struct pte *pt0, pcb *cur_pcb);

#endif