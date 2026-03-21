/*
    EntropyOS
    Copyright (C) 2025  Gabriel Sîrbu

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
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
