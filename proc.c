#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

// Time slice allocations per queue level (in timer ticks)
// Each tick is usually ~10ms
int time_slices[NQUEUE] = {5, 10, 20, 40};

// MLFQ debug setting (set to 1 for verbose output; otherwise, 0)
#define MLFQ_DEBUG 0

struct {
  struct proc *queue_head[NQUEUE];
  struct proc *queue_tail[NQUEUE];
  uint64 ticks_since_boost;     // Ticks since last priority boost
} mlfq;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void syscall_trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");

  // Initialize MLFQ queues
  for (int i = 0; i < NQUEUE; i++) {
    mlfq.queue_head[i] = 0;
    mlfq.queue_tail[i] = 0;
  }
  mlfq.ticks_since_boost = 0;
}

// Add a process to the end of its priority queue
// Must be called with ptable.lock held
static void
enqueue_process(struct proc *p)
{
  int queue_level;

  // Validate priority
  if (p->priority < 0 || p->priority >= NQUEUE)
    panic("enqueue_process: invalid priority");

  queue_level = p->priority;
  p->queue_next = 0;

  if (mlfq.queue_tail[queue_level] == 0) {
    mlfq.queue_head[queue_level] = p;
    mlfq.queue_tail[queue_level] = p;
  } else {
    mlfq.queue_tail[queue_level]->queue_next = p;
    mlfq.queue_tail[queue_level] = p;
  }
}

// Remove and return the first process from a given priority queue
// Return 0 if queue is empty
// Must be called with ptable.lock held
static struct proc*
dequeue_process(int queue_level)
{
  if (queue_level < 0 || queue_level >= NQUEUE)
    panic("dequeue_process: invalid queue level");

  struct proc *p = mlfq.queue_head[queue_level];
  if (p == 0)
    return 0;  // Queue is empty

  // Remove from head
  mlfq.queue_head[queue_level] = p->queue_next;
  if (mlfq.queue_head[queue_level] == 0)
    mlfq.queue_tail[queue_level] = 0;  // Queue is now empty

  p->queue_next = 0;  // Detatch from queue
  return p;
}

// Remove a specific process from its priority queue
// Used when process state changes from `RUNNABLE`
// Must be called with ptable.lock held
static void
remove_from_queue(struct proc *p)
{
  struct proc *current, *prev;
  int queue_level = p->priority;

  if (queue_level < 0 || queue_level >= NQUEUE)
    return;  // Invalid queue

  prev = 0;
  for (current = mlfq.queue_head[queue_level]; current != 0; current = current->queue_next) {
    if (current == p) {
      if (prev == 0) {
        // p is at head
        mlfq.queue_head[queue_level] = p->queue_next;
        if (mlfq.queue_head[queue_level] == 0)
          mlfq.queue_tail[queue_level] = 0;
      } else {
        // p is in middle or end
        prev->queue_next = p->queue_next;
        if (p->queue_next == 0)
          mlfq.queue_tail[queue_level] = prev;  // p was at tail
      }
      p->queue_next = 0;
      return;
    }
    prev = current;
  }
}

// Finds the highest priority level with a runnable process
// Returns queue level (0 to NQUEUE-1), or -1 if all queues empty
// Must be called with ptable.lock held
static int
find_nonempty_queue(void)
{
  int i;
  for (i = 0; i < NQUEUE; i++) {
    if (mlfq.queue_head[i] != 0)
      return i;
  }
  return -1;
}

// Boost all processes' priorities in the MLFQ to highest priority
// This prevents starvation on low-priority processes
// Must be called with ptable.lock held
static void
boost_all_priorities(void)
{
  if (MLFQ_DEBUG)
    cprintf("BOOST: resetting all priorities\n");
  int i;
  struct proc *p;
  for (i = 0; i < NQUEUE; i++) {
    mlfq.queue_head[i] = 0;
    mlfq.queue_tail[i] = 0;
  }
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if (p->state == RUNNABLE) {
      p->priority = 0;
      p->ticks_used = 0;
      enqueue_process(p);
    } else if (p->state == RUNNING) {
      p->priority = 0;
      p->ticks_used = 0;
    }
  }
}

// Check if at least `BOOST_ITVL` ticks have passed
// If true, call `boost_all_processes()`
void
check_priority_boost(void)
{
  acquire(&ptable.lock);
  mlfq.ticks_since_boost++;
  if (mlfq.ticks_since_boost >= BOOST_ITVL) {
    boost_all_priorities();
    mlfq.ticks_since_boost = 0;
  }
  release(&ptable.lock);
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  // Initialize MLFQ fields
  p->priority = 0;
  p->ticks_used = 0;
  p->total_ticks = 0;
  p->queue_next = 0;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= sizeof(addr_t);
  *(addr_t*)sp = (addr_t)syscall_trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->rip = (addr_t)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];
  p = allocproc();

  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");

  inituvm(p->pgdir, _binary_initcode_start,
          (addr_t)_binary_initcode_size);
  p->sz = PGSIZE * 2;
  memset(p->tf, 0, sizeof(*p->tf));

  p->tf->r11 = FL_IF;  // with SYSRET, EFLAGS is in R11
  p->tf->rsp = p->sz;
  p->tf->rcx = PGSIZE;  // with SYSRET, RIP is in RCX

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  __sync_synchronize();
  p->state = RUNNABLE;
  enqueue_process(p);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int64 n)
{
  addr_t sz;

  sz = proc->sz;
  if(n > 0){
    if((sz = allocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  proc->sz = sz;
  switchuvm(proc);
  return 0;
}
//PAGEBREAK!

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;

  // Allocate process.
  if((np = allocproc()) == 0)
    return -1;

  // Copy process state from p.
  if((np->pgdir = copyuvm(proc->pgdir, proc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = proc->sz;
  np->parent = proc;
  *np->tf = *proc->tf;

  // Clear %rax so that fork returns 0 in the child.
  np->tf->rax = 0;

  for(i = 0; i < NOFILE; i++)
    if(proc->ofile[i])
      np->ofile[i] = filedup(proc->ofile[i]);
  np->cwd = idup(proc->cwd);

  safestrcpy(np->name, proc->name, sizeof(proc->name));

  pid = np->pid;

  __sync_synchronize();
  np->state = RUNNABLE;
  enqueue_process(np);

  return pid;
}

//PAGEBREAK!
// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *p;
  int fd;

  if(proc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(proc->ofile[fd]){
      fileclose(proc->ofile[fd]);
      proc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(proc->cwd);
  end_op();
  proc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(proc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == proc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  proc->state = ZOMBIE;
  remove_from_queue(proc);
  sched();
  panic("zombie exit");
}

//PAGEBREAK!
// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != proc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || proc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(proc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  int queue_level;
  for(;;){
    // Enable interrupts on this processor.
    sti();
    // Loop over priority queues from highest to lowest
    acquire(&ptable.lock);
    queue_level = find_nonempty_queue();
    if (queue_level != -1) {
      p = dequeue_process(queue_level);
      if (MLFQ_DEBUG && p != 0)
        cprintf("CPU%d: running pid=%d pri=%d\n",
                cpunum(), p->pid, p->priority);
      if (p != 0 && p->state == RUNNABLE) {
        // Switch to highest priority process. It is the process's job to release
        // ptable.lock and then reacquire it before jumping back to us.
        proc = p;
        switchuvm(p);
        p->state = RUNNING;
        swtch(&cpu->scheduler, p->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        proc = 0;
      }
    }
    release(&ptable.lock);
    // If no processes were runnable, halt CPU to save power
    // CPU will wake on next interrupt
    if (queue_level == -1)
      hlt();
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;


  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(cpu->ncli != 1)
    panic("sched locks");
  if(proc->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = cpu->intena;
  swtch(&proc->context, cpu->scheduler);
  cpu->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  if (proc->ticks_used >= time_slices[proc->priority]) {
    int old_priority = proc->priority;
    if (proc->priority < NQUEUE - 1)
      proc->priority++;
    if (MLFQ_DEBUG)
      cprintf("  yield: pid=%d ticks=%lu/%lu pri=%d->%d\n",
              proc->pid, proc->ticks_used, proc->total_ticks,
              old_priority, proc->priority);
    proc->ticks_used = 0;  // Reset counter
  }
  proc->state = RUNNABLE;
  enqueue_process(proc);
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

//PAGEBREAK!
// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  if(proc == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }

  // Go to sleep.
  proc->chan = chan;
  proc->state = SLEEPING;
  remove_from_queue(proc);
  sched();

  // Tidy up.
  proc->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
      enqueue_process(p);
    }
  }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING) {
        p->state = RUNNABLE;
        enqueue_process(p);
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  addr_t pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s pri=%d ticks=%lu/%lu",
            p->pid, state, p->name,
            p->priority, p->ticks_used, p->total_ticks);
    if(p->state == SLEEPING){
      getstackpcs((addr_t*)p->context->rbp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
