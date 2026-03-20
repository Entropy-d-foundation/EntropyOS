// SPDX-License-Identifier: GPL-3.0
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

#define SCHEDULER_MAX_TASKS 16

typedef void (*sched_task_fn)(void *arg, double dt);

void scheduler_init(void);
int scheduler_add_task(sched_task_fn fn, void *arg);
void scheduler_remove_task(int id);
void scheduler_run_once(double dt);
void scheduler_run_all(double dt);
void scheduler_run_loop(void);

#endif // SCHEDULER_H
