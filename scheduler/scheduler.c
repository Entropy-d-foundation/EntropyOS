/*
    GloamOS
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

#include "scheduler.h"
#include <stddef.h>
#include <stdint.h>

/* ---- priority bands ---- */
typedef enum {
    PRIO_LOW      = 0,
    PRIO_NORMAL   = 1,
    PRIO_HIGH     = 2,
    PRIO_CRITICAL = 3,
} task_priority_t;

/* ---- task states ---- */
typedef enum {
    TASK_EMPTY     = 0,
    TASK_READY     = 1,
    TASK_SLEEPING  = 2,
    TASK_SUSPENDED = 3,
} task_state_t;

/* ---- per-task stats ---- */
typedef struct {
    uint64_t run_count;
    double   total_time_s;
} task_stats_t;

#define SCHED_NAME_MAX 24

/* ---- internal task record ---- */
typedef struct {
    sched_task_fn    fn;
    void            *arg;
    task_state_t     state;
    task_priority_t  prio;
    double           sleep_remaining;
    task_stats_t     stats;
    char             name[SCHED_NAME_MAX];
} task_entry_t;

static task_entry_t g_tasks[SCHEDULER_MAX_TASKS];
static int          g_task_count = 0;
static int          g_next_index = 0;

/* ---- helpers ---- */
static inline int id_valid(int id)
{
    return (id >= 0 && id < SCHEDULER_MAX_TASKS);
}

static void sched_strncpy(char *dst, const char *src, int n)
{
    int i = 0;
    if (src) { for (; i < n - 1 && src[i]; ++i) dst[i] = src[i]; }
    dst[i] = '\0';
}

static void tick_sleep_timers(double dt)
{
    for (int i = 0; i < SCHEDULER_MAX_TASKS; ++i) {
        if (g_tasks[i].state != TASK_SLEEPING) continue;
        g_tasks[i].sleep_remaining -= dt;
        if (g_tasks[i].sleep_remaining <= 0.0) {
            g_tasks[i].sleep_remaining = 0.0;
            g_tasks[i].state           = TASK_READY;
        }
    }
}

static void run_task(int idx, double dt)
{
    g_tasks[idx].fn(g_tasks[idx].arg, dt);
    g_tasks[idx].stats.run_count++;
    g_tasks[idx].stats.total_time_s += dt;
}

/* =========================================================================
 * Original API
 * ========================================================================= */

void scheduler_init(void)
{
    for (int i = 0; i < SCHEDULER_MAX_TASKS; ++i) {
        g_tasks[i].fn                  = NULL;
        g_tasks[i].arg                 = NULL;
        g_tasks[i].state               = TASK_EMPTY;
        g_tasks[i].prio                = PRIO_NORMAL;
        g_tasks[i].sleep_remaining     = 0.0;
        g_tasks[i].stats.run_count     = 0;
        g_tasks[i].stats.total_time_s  = 0.0;
        g_tasks[i].name[0]             = '\0';
    }
    g_task_count = 0;
    g_next_index = 0;
}

int scheduler_add_task(sched_task_fn fn, void *arg)
{
    if (!fn) return -1;

    /* Dedup: same fn+arg already registered → return existing id */
    for (int i = 0; i < SCHEDULER_MAX_TASKS; ++i) {
        if (g_tasks[i].state != TASK_EMPTY &&
            g_tasks[i].fn  == fn &&
            g_tasks[i].arg == arg)
            return i;
    }

    /* Find free slot from round-robin cursor */
    for (int i = 0; i < SCHEDULER_MAX_TASKS; ++i) {
        int idx = (g_next_index + i) % SCHEDULER_MAX_TASKS;
        if (g_tasks[idx].state == TASK_EMPTY) {
            g_tasks[idx].fn                 = fn;
            g_tasks[idx].arg                = arg;
            g_tasks[idx].state              = TASK_READY;
            g_tasks[idx].prio               = PRIO_NORMAL;
            g_tasks[idx].sleep_remaining    = 0.0;
            g_tasks[idx].stats.run_count    = 0;
            g_tasks[idx].stats.total_time_s = 0.0;
            sched_strncpy(g_tasks[idx].name, "unnamed", SCHED_NAME_MAX);
            g_task_count++;
            g_next_index = (idx + 1) % SCHEDULER_MAX_TASKS;
            return idx;
        }
    }
    return -1; /* full */
}

void scheduler_remove_task(int id)
{
    if (!id_valid(id)) return;
    if (g_tasks[id].state == TASK_EMPTY) return;
    g_tasks[id].fn              = NULL;
    g_tasks[id].arg             = NULL;
    g_tasks[id].state           = TASK_EMPTY;
    g_tasks[id].prio            = PRIO_NORMAL;
    g_tasks[id].sleep_remaining = 0.0;
    g_tasks[id].name[0]         = '\0';
    if (g_task_count > 0) g_task_count--;
}

/* scheduler_run_all: ticks sleep timers, then runs all READY tasks in
 * priority order (CRITICAL first → LOW last). Within the same band the
 * order is round-robin from g_next_index. Cursor advances one slot per
 * call for fairness across frames. */
void scheduler_run_all(double dt)
{
    tick_sleep_timers(dt);

    for (int prio = PRIO_CRITICAL; prio >= PRIO_LOW; --prio) {
        for (int i = 0; i < SCHEDULER_MAX_TASKS; ++i) {
            int idx = (g_next_index + i) % SCHEDULER_MAX_TASKS;
            if (g_tasks[idx].state == TASK_READY  &&
                g_tasks[idx].fn    != NULL         &&
                (int)g_tasks[idx].prio == prio) {
                run_task(idx, dt);
            }
        }
    }

    g_next_index = (g_next_index + 1) % SCHEDULER_MAX_TASKS;
}

/* scheduler_run_once: runs exactly one READY task in round-robin order.
 * FIX: advances next_index only when a task actually ran — the original
 * advanced it while scanning past empty/sleeping slots, causing starvation
 * in sparse tables. */
void scheduler_run_once(double dt)
{
    tick_sleep_timers(dt);

    for (int i = 0; i < SCHEDULER_MAX_TASKS; ++i) {
        int idx = (g_next_index + i) % SCHEDULER_MAX_TASKS;
        if (g_tasks[idx].state == TASK_READY && g_tasks[idx].fn != NULL) {
            run_task(idx, dt);
            g_next_index = (idx + 1) % SCHEDULER_MAX_TASKS;
            return;
        }
    }
    /* nothing runnable — leave cursor where it is */
}

void scheduler_run_loop(void)
{
    while (1)
        scheduler_run_once(0.0);
}

/* =========================================================================
 * Extended API — callable from other .c files, not declared in .h
 * ========================================================================= */

void scheduler_set_priority(int id, task_priority_t prio)
{
    if (!id_valid(id)) return;
    if (g_tasks[id].state == TASK_EMPTY) return;
    g_tasks[id].prio = prio;
}

void scheduler_suspend(int id)
{
    if (!id_valid(id)) return;
    if (g_tasks[id].state == TASK_READY)
        g_tasks[id].state = TASK_SUSPENDED;
}

void scheduler_resume(int id)
{
    if (!id_valid(id)) return;
    if (g_tasks[id].state == TASK_SUSPENDED ||
        g_tasks[id].state == TASK_SLEEPING) {
        g_tasks[id].sleep_remaining = 0.0;
        g_tasks[id].state           = TASK_READY;
    }
}

void scheduler_sleep(int id, double seconds)
{
    if (!id_valid(id)) return;
    if (g_tasks[id].state == TASK_EMPTY) return;
    if (seconds <= 0.0) return;
    g_tasks[id].sleep_remaining = seconds;
    g_tasks[id].state           = TASK_SLEEPING;
}

void scheduler_remove_by_fn(sched_task_fn fn)
{
    if (!fn) return;
    for (int i = 0; i < SCHEDULER_MAX_TASKS; ++i)
        if (g_tasks[i].state != TASK_EMPTY && g_tasks[i].fn == fn)
            scheduler_remove_task(i);
}