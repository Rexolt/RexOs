/* Rex OS - Cooperative kernel scheduler */
#pragma once
#include <rexos/types.h>

#define TASK_STACK_SIZE   (16 * 1024)   /* 16 KB per task */
#define TASK_NAME_MAX     32

typedef enum {
    TASK_STATE_READY = 0,
    TASK_STATE_RUNNING,
    TASK_STATE_DEAD,
} task_state_t;

typedef void (*task_entry_fn)(void *arg);

typedef struct task {
    uint64_t       rsp;             /* mentett kernel-stack pointer (context switch idején) */
    uint8_t       *stack_base;      /* heap-en allokált stack alja (kfree-hez) */
    uint64_t       cr3;             /* A taszk fizikai PML4 címe (VMM) */
    uint64_t       id;
    char           name[TASK_NAME_MAX];
    task_state_t   state;
    task_entry_fn  entry;
    void          *arg;
    struct vfs_node *fd_table[16];
    uint64_t       fd_offset[16];

    /* User heap (brk) */
    uint64_t       brk_start;       /* heap kezdete (ELF vége utáni első page) */
    uint64_t       brk_current;     /* aktuális brk pozíció */

    /* Round-robin lánc */
    struct task   *next;
} task_t;

/* Init: az aktuálisan futó kódot átkereszteli "task 0"-vá. */
void   sched_init(void);

/* Új kernel task létrehozása, ready queue-ba sorolása. */
task_t *task_create(const char *name, task_entry_fn entry, void *arg);

/* Új felhasználói (Ring 3) taszk indítása a megadott ELF fájlból (VFS node). */
struct vfs_node;
task_t *task_spawn_user(const char *name, struct vfs_node *elf_file);

/* Aktuálisan futó task. */
task_t *task_current(void);

/* Aktuális task feladja a CPU-t a következő ready task javára.
 * Ezt egy software interrupt (vector 0xFE) triggereli. */
void   task_yield(void);

/* Aktuális task befejezi futását. SOHA nem tér vissza. */
__noreturn void task_exit(void);

/* Diagnosztika: lista a serial/console-ra. */
void   sched_dump(void);
size_t sched_task_count(void);
int    sched_task_alive(uint64_t pid);

/* --- Preempció ---------------------------------------------------------
 * Ezt a flag-et:
 *   - a PIT IRQ handler 1-re állítja, ha az aktuális task quantuma elfogyott
 *   - task_yield() 1-re állítja explicit yieldnél
 * Az IRQ stub epilógusa olvassa ki és context-switchel ha 1.
 * ----------------------------------------------------------------------- */
extern volatile uint8_t g_need_resched;

/* Az IRQ exit asm hívja meg. Visszaadott érték:
 *    0           = nincs switch, marad a current task
 *    !0 (új rsp) = switch, az asm tegye rsp-be ezt az értéket
 *
 * A current_rsp paraméter az aktuális task interrupt frame-jének tetejére
 * mutat (a 15 mentett GP reg utáni állapot pontjára).
 */
uint64_t sched_maybe_switch(uint64_t current_rsp);

/* Vector 0xFE software interrupt-ot bekötő helper, melyet az irq_init után
 * kell hívni. Itt regisztráljuk az IDT-be a yield stubot. */
void   sched_install_yield_vector(void);
