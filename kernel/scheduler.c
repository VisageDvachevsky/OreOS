#include "kernel.h"

#define THREAD_STACK_PAGES 4
#define MAX_THREADS 32

typedef struct {
    Spinlock lock;
    Thread *current;
    Thread *run_queue;
    uint64_t switches;
    uint32_t started;
} SchedulerCpu;

static SchedulerCpu sched[ORE_MAX_CPUS];
static Thread threads[MAX_THREADS];
static uint32_t thread_count;
static Thread bootstrap_threads[ORE_MAX_CPUS];
static uint32_t trace_switches;
static volatile uint32_t sleep_test_done;

void thread_bootstrap(Thread *thread);
extern void thread_entry_asm(void);

static void enqueue_thread(uint32_t cpu_id, Thread *thread) {
    thread->next = 0;
    if (!sched[cpu_id].run_queue) {
        sched[cpu_id].run_queue = thread;
        return;
    }
    Thread *tail = sched[cpu_id].run_queue;
    while (tail->next) tail = tail->next;
    tail->next = thread;
}

static Thread *dequeue_thread(uint32_t cpu_id) {
    Thread *thread = sched[cpu_id].run_queue;
    if (thread) {
        sched[cpu_id].run_queue = thread->next;
        thread->next = 0;
    }
    return thread;
}

static void wake_sleepers(uint32_t cpu_id) {
    uint64_t now = timer_ticks(cpu_id);
    for (uint32_t i = 0; i < thread_count; ++i) {
        Thread *thread = &threads[i];
        if (thread->cpu_id == cpu_id && thread->state == THREAD_SLEEPING && thread->wake_tick <= now) {
            thread->state = THREAD_RUNNABLE;
            enqueue_thread(cpu_id, thread);
        }
    }
}

void scheduler_init(void) {
    thread_count = 0;
    for (uint32_t i = 0; i < ORE_MAX_CPUS; ++i) {
        sched[i].current = 0;
        sched[i].run_queue = 0;
        sched[i].switches = 0;
        sched[i].started = 0;
    }
    kprintf("scheduler: initialized\n");
}

static uint64_t *build_initial_stack(Thread *thread) {
    uint64_t *sp = (uint64_t *)thread->stack_top;
    *--sp = 0x10;                          /* ss if iret pops privilege frame */
    *--sp = thread->stack_top - 8;          /* rsp if iret pops privilege frame */
    *--sp = 0x202;                         /* rflags */
    *--sp = 0x08;                          /* cs */
    *--sp = (uint64_t)(uintptr_t)thread_entry_asm;
    *--sp = 0;                             /* error */
    *--sp = 32;                            /* vector */
    *--sp = 0;                             /* rax */
    *--sp = 0;                             /* rbx */
    *--sp = 0;                             /* rcx */
    *--sp = 0;                             /* rdx */
    *--sp = 0;                             /* rbp */
    *--sp = 0;                             /* rsi */
    *--sp = (uint64_t)(uintptr_t)thread;   /* rdi */
    *--sp = 0;                             /* r8 */
    *--sp = 0;                             /* r9 */
    *--sp = 0;                             /* r10 */
    *--sp = 0;                             /* r11 */
    *--sp = 0;                             /* r12 */
    *--sp = 0;                             /* r13 */
    *--sp = 0;                             /* r14 */
    *--sp = 0;                             /* r15 */
    return sp;
}

int thread_create(void (*fn)(void *), void *arg, uint32_t cpu_id, const char *name) {
    if (!fn || cpu_id >= ORE_MAX_CPUS) return -1;
    if (thread_count >= MAX_THREADS) return -1;
    Thread *thread = &threads[thread_count];
    void *stack = pmm_alloc_pages(THREAD_STACK_PAGES);
    if (!stack) return -1;
    thread->id = thread_count + 1;
    thread->state = THREAD_RUNNABLE;
    thread->cpu_id = cpu_id;
    thread->stack_base = (uint64_t)(uintptr_t)stack;
    thread->stack_top = thread->stack_base + THREAD_STACK_PAGES * 4096ULL;
    thread->switches = 0;
    thread->entry = fn;
    thread->arg = arg;
    thread->name = name;
    thread->wake_tick = 0;
    thread->process = process_kernel();
    thread->saved_sp = build_initial_stack(thread);
    thread_count++;
    spinlock_lock(&sched[cpu_id].lock);
    enqueue_thread(cpu_id, thread);
    spinlock_unlock(&sched[cpu_id].lock);
    kprintf("scheduler: created thread %u cpu %u %s\n", thread->id, cpu_id, name ? name : "(unnamed)");
    return (int)thread->id;
}

void thread_bootstrap(Thread *thread) {
    __asm__ volatile("sti");
    kprintf("scheduler: bootstrap thread %u %s\n", thread->id, thread->name ? thread->name : "(unnamed)");
    thread->entry(thread->arg);
    thread->state = THREAD_DEAD;
    thread_yield();
    for (;;) __asm__ volatile("hlt");
}

void scheduler_start_current_cpu(void) {
    uint32_t cpu = current_cpu_id();
    bootstrap_threads[cpu].id = 0;
    bootstrap_threads[cpu].state = THREAD_RUNNING;
    bootstrap_threads[cpu].cpu_id = cpu;
    bootstrap_threads[cpu].name = "bootstrap";
    sched[cpu].current = &bootstrap_threads[cpu];
    sched[cpu].started = cpu == 0 ? 1 : 0;
    if (sched[cpu].started) {
        kprintf("scheduler: CPU %u started\n", cpu);
    }
}

uint64_t *scheduler_tick(uint32_t cpu_id, uint64_t *interrupted_sp) {
    if (cpu_id != 0 || !sched[cpu_id].started) return 0;
    spinlock_lock(&sched[cpu_id].lock);
    wake_sleepers(cpu_id);
    Thread *current = sched[cpu_id].current;
    Thread *next = dequeue_thread(cpu_id);
    if (!next) {
        spinlock_unlock(&sched[cpu_id].lock);
        return 0;
    }
    if (current && current->state == THREAD_RUNNING && current->id != 0) {
        current->saved_sp = interrupted_sp;
        current->state = THREAD_RUNNABLE;
        enqueue_thread(cpu_id, current);
    } else if (current && current->id != 0) {
        current->saved_sp = interrupted_sp;
    }
    next->state = THREAD_RUNNING;
    sched[cpu_id].current = next;
    sched[cpu_id].switches++;
    next->switches++;
    if (trace_switches < 8) {
        trace_switches++;
        kprintf("scheduler: switch cpu %u -> thread %u %s\n",
                cpu_id, next->id, next->name ? next->name : "(unnamed)");
    }
    uint64_t *new_sp = next->saved_sp;
    spinlock_unlock(&sched[cpu_id].lock);
    return new_sp;
}

void thread_yield(void) {
    __asm__ volatile("int $32");
}

uint64_t scheduler_switch_count(void) {
    return sched[0].switches;
}

void thread_sleep(uint64_t ticks) {
    uint32_t cpu = current_cpu_id();
    if (cpu != 0 || !sched[cpu].current || sched[cpu].current->id == 0) return;
    spinlock_lock(&sched[cpu].lock);
    sched[cpu].current->wake_tick = timer_ticks(cpu) + ticks;
    sched[cpu].current->state = THREAD_SLEEPING;
    spinlock_unlock(&sched[cpu].lock);
    thread_yield();
}

void wait_queue_init(WaitQueue *queue) {
    if (queue) queue->head = 0;
}

void thread_block(WaitQueue *queue) {
    uint32_t cpu = current_cpu_id();
    Thread *current = sched[cpu].current;
    if (!queue || !current || current->id == 0) return;
    spinlock_lock(&sched[cpu].lock);
    current->state = THREAD_BLOCKED;
    current->next = queue->head;
    queue->head = current;
    spinlock_unlock(&sched[cpu].lock);
    thread_yield();
}

void thread_unblock(Thread *thread) {
    if (!thread || thread->state == THREAD_RUNNABLE || thread->state == THREAD_RUNNING) return;
    uint32_t cpu = thread->cpu_id;
    spinlock_lock(&sched[cpu].lock);
    thread->state = THREAD_RUNNABLE;
    enqueue_thread(cpu, thread);
    spinlock_unlock(&sched[cpu].lock);
}

void mutex_init(Mutex *mutex) {
    if (!mutex) return;
    mutex->held = 0;
    mutex->owner = 0;
    wait_queue_init(&mutex->waiters);
}

void mutex_lock(Mutex *mutex) {
    if (!mutex) return;
    for (;;) {
        spinlock_lock(&mutex->lock);
        if (!mutex->held) {
            mutex->held = 1;
            mutex->owner = sched[current_cpu_id()].current;
            spinlock_unlock(&mutex->lock);
            return;
        }
        spinlock_unlock(&mutex->lock);
        thread_yield();
    }
}

void mutex_unlock(Mutex *mutex) {
    if (!mutex) return;
    spinlock_lock(&mutex->lock);
    mutex->held = 0;
    mutex->owner = 0;
    spinlock_unlock(&mutex->lock);
}

static void scheduler_sleep_test_thread(void *arg) {
    (void)arg;
    uint64_t before = timer_ticks(0);
    thread_sleep(2);
    uint64_t after = timer_ticks(0);
    if (after < before + 2) panic("scheduler: sleep self-test woke too early");
    sleep_test_done = 1;
    kprintf("scheduler: blocking self-test ok\n");
    for (;;) thread_sleep(64);
}

void scheduler_self_test_start(void) {
    sleep_test_done = 0;
    if (thread_create(scheduler_sleep_test_thread, 0, 0, "sched-test") < 0) {
        panic("scheduler: self-test thread create failed");
    }
}
