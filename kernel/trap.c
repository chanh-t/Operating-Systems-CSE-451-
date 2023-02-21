#include <cdefs.h>
#include <defs.h>
#include <memlayout.h>
#include <mmu.h>
#include <param.h>
#include <proc.h>
#include <spinlock.h>
#include <trap.h>
#include <x86_64.h>

// Interrupt descriptor table (shared by all CPUs).
struct gate_desc idt[256];
extern void *vectors[]; // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

int num_page_faults = 0;

void tvinit(void) {
  int i;

  for (i = 0; i < 256; i++)
    set_gate_desc(&idt[i], 0, SEG_KCODE << 3, vectors[i], KERNEL_PL);
  set_gate_desc(&idt[TRAP_SYSCALL], 1, SEG_KCODE << 3, vectors[TRAP_SYSCALL],
                USER_PL);

  initlock(&tickslock, "time");
}

void idtinit(void) { lidt((void *)idt, sizeof(idt)); }

void trap(struct trap_frame *tf) {
  uint64_t addr;

  if (tf->trapno == TRAP_SYSCALL) {
    if (myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if (myproc()->killed)
      exit();
    return;
  }

  switch (tf->trapno) {
  case TRAP_IRQ0 + IRQ_TIMER:
    if (cpunum() == 0) {
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case TRAP_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case TRAP_IRQ0 + IRQ_IDE + 1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case TRAP_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case TRAP_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case TRAP_IRQ0 + 7:
  case TRAP_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n", cpunum(), tf->cs, tf->rip);
    lapiceoi();
    break;

  default:
    addr = rcr2();

    if (tf->trapno == TRAP_PF) {
      num_page_faults += 1;

      struct vregion *vreg;
      struct vpage_info *vpi;

      if((vreg = va2vregion(&myproc()->vspace, addr)) != 0 && (vpi = va2vpage_info(vreg, addr)) != 0 ){
        
        struct core_map_entry* map = (struct core_map_entry *)pa2page(vpi->ppn<<PT_SHIFT);

        // cases: cow 1, writable 0 or cow 0, writable 0
        // if ref = 1 after dereference how do we let other process know
        // that they can write to it?
        if (map->ref > 1 && vpi->copy_on_write == 1 && vpi->writable == 0) {
          char* new_frame = kalloc();



          acquire_core_map_lock();
          memset(new_frame, 0, PGSIZE);
          memmove(new_frame, P2V(vpi->vpn << PT_SHIFT), PGSIZE);
          map->ref--;
          vpi->used = 1;
          vpi->ppn = PGNUM(V2P(new_frame));
          vpi->present = 1;
          vpi->writable = 1;
          vpi->copy_on_write = 0;
          release_core_map_lock():

          vspaceinvalidate(&myproc()->vspace);
          vspaceinstall(myproc());
          break;
        } else if (map->ref == 1 && vpi->copy_on_write == 1 && vpi->writable == 0) {
          // since cow = 1, we know it was writable before.
          acquire_core_map_lock();
          vpi->writable = 1;
          vpi->copy_on_write = 0;

          vspaceinstall(&myproc()->vspace);
          vspaceinstall(myproc());
          release_core_map_lock();
          break;
        }
      }
      /*
      num_page_faults += 1;

      if (myproc() == 0 || (tf->cs & 3) == 0) {
        // In kernel, it must be our mistake.
        cprintf("unexpected trap %d from cpu %d rip %lx (cr2=0x%x)\n",
                tf->trapno, cpunum(), tf->rip, addr);
        panic("trap");
      }
      */
    }

    // Assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "rip 0x%lx addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno, tf->err, cpunum(),
            tf->rip, addr);
    myproc()->killed = 1;
  }
  // end if switch statement here

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if (myproc() && myproc()->killed && (tf->cs & 3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if (myproc() && myproc()->state == RUNNING &&
      tf->trapno == TRAP_IRQ0 + IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if (myproc() && myproc()->killed && (tf->cs & 3) == DPL_USER)
    exit();
}
