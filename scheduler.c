#include "scheduler.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "msg.h"

static const char *SCHED_NAMES[] = { "FIFO", "SJF", "RR", "MLFQ", NULL };
static sched_algo_en sched_algo = SCHED_FIFO;

static queue_t mlfq[MLFQ_LEVELS] = {0};
static uint32_t last_priority_reset_ms = 0;

int set_sched_algo(const char *name) {
    for (int i = 0; SCHED_NAMES[i] != NULL; i++) {
        if (strcasecmp(SCHED_NAMES[i], name) == 0) {
            sched_algo = (sched_algo_en) i;
            return sched_algo;
        }
    }
    return -1;
}

const char *get_sched_algo_str(void) {
    return SCHED_NAMES[sched_algo];
}

/**
 * Send DONE to the application and move the finished burst to the command queue.
 */
static void finish_burst(uint32_t current_time_ms, queue_t *cq, pcb_t *task) {
    msg_t msg = {
        .pid = task->pid,
        .request = PROCESS_REQUEST_DONE,
        .time_ms = current_time_ms
    };
    if (write(task->sockfd, &msg, sizeof(msg_t)) != sizeof(msg_t)) {
        perror("write");
    }
    enqueue_pcb(cq, task);
}

static void add_to_mlfq(pcb_t *task) {
    enqueue_pcb(&mlfq[task->priority], task);
}

static void distribute_ready_queue(queue_t *rq) {
    pcb_t *task;

    while ((task = dequeue_pcb(rq)) != NULL) {
        add_to_mlfq(task);
    }
}



static pcb_t *dequeue_mlfq(void) {
    for (int i = 0; i < MLFQ_LEVELS; i++) {
        pcb_t *task = dequeue_pcb(&mlfq[i]);

        if (task != NULL) {
            return task;
        }
    }

    return NULL;
}

static void reset_mlfq_priorities(pcb_t **cpu_task) {
    if (*cpu_task != NULL) {
        (*cpu_task)->priority = 0;
    }

    for (int i = 1; i < MLFQ_LEVELS; i++) {
        pcb_t *task;

        while ((task = dequeue_pcb(&mlfq[i])) != NULL) {
            task->priority = 0;
            enqueue_pcb(&mlfq[0], task);
        }
    }
}

static void lower_mlfq_priority(pcb_t *task) {
    if (task->priority < MLFQ_LEVELS - 1) {
        printf("[MLFQ] PID %d: fila %d -> fila %d\n",
               task->pid,
               task->priority,
               task->priority + 1);

        task->priority++;
    }

    add_to_mlfq(task);
}

/**
 * FIFO (First-In, First-Out) — não preemptivo.
 *
 * O primeiro processo a chegar à ready queue corre até o seu burst de CPU
 * terminar; só então o CPU fica livre para o processo seguinte.
 *
 * TODO (alunos): acrescentar os algoritmos SJF, RR (time-slice = TIME_SLICE_MS)
 * e MLFQ, selecionados por `sched_algo` / `--sched`.
 */
int scheduler(uint32_t current_time_ms, queue_t *rq, queue_t *cq, pcb_t **cpu_task) {
   /** if (*cpu_task) {
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;
        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            finish_burst(current_time_ms, cq, *cpu_task);
            *cpu_task = NULL;
        }
    }
    */

    if (*cpu_task) {
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;

        // The CPU burst has finished.
        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            finish_burst(current_time_ms, cq, *cpu_task);
            *cpu_task = NULL;
        }
        // Round Robin: the process used its entire time slice.
        else if (sched_algo == SCHED_RR &&
                 current_time_ms - (*cpu_task)->slice_start_ms >= TIME_SLICE_MS) {
            printf("[RR] PID %d saiu do CPU em %u ms\n",
           (*cpu_task)->pid,
           current_time_ms);

            enqueue_pcb(rq, *cpu_task);
            *cpu_task = NULL;
                 }
        else if (sched_algo == SCHED_MLFQ &&
         current_time_ms - (*cpu_task)->slice_start_ms >= TIME_SLICE_MS) {
            lower_mlfq_priority(*cpu_task);
            *cpu_task = NULL;
         }
    }
    if (sched_algo == SCHED_MLFQ) {
        distribute_ready_queue(rq);
    }
    if (sched_algo == SCHED_MLFQ &&
    current_time_ms - last_priority_reset_ms >= MLFQ_RESET_MS) {

        reset_mlfq_priorities(cpu_task);
        last_priority_reset_ms = current_time_ms;
    }
    if (*cpu_task == NULL) {
        if (sched_algo == SCHED_FIFO) {
            *cpu_task = dequeue_pcb(rq);
        }
        if (sched_algo == SCHED_SJF) {
            *cpu_task = dequeue_pcb_sjf(rq);
        }
        if (sched_algo == SCHED_RR) {
            *cpu_task = dequeue_pcb(rq);
        }
        if (sched_algo == SCHED_MLFQ) {
            *cpu_task = dequeue_mlfq();
        }
        if (*cpu_task) {
            (*cpu_task)->slice_start_ms = current_time_ms;

            uint32_t response_time =
                current_time_ms - (*cpu_task)->last_update_time_ms;

            if (sched_algo == SCHED_MLFQ) {
                printf("[MLFQ] PID %d entrou no CPU na fila %d em %u ms | resposta: %u ms\n",
                       (*cpu_task)->pid,
                       (*cpu_task)->priority,
                       current_time_ms,
                       response_time);
            }
            else {
                printf("[%s] PID %d entrou no CPU em %u ms | resposta: %u ms\n",
                       get_sched_algo_str(),
                       (*cpu_task)->pid,
                       current_time_ms,
                       response_time);
            }

            return 1;
        }
    }
    return 0;
}
