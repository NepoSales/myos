/* programs.h - built-in programs that can be started as tasks with `run` */
#ifndef PROC_PROGRAMS_H
#define PROC_PROGRAMS_H

#include "proc/task.h"

#define SPINNER_COL   72      /* status bar columns 72..79 are spinner slots */
#define SPINNER_SLOTS 8

typedef struct {
    const char*  name;
    const char*  usage;
    const char*  description;
    task_entry_t entry;
} program_t;

const program_t* program_find(const char* name);
const program_t* program_get(int index);        /* NULL past the last one */

#endif
