# OS Jackfruit — Multi-Container Runtime

**Aditya Alur & Aadish Sarin**  
PES2UG24CS052 · PES2UG24CS007  
4th Semester, Section A — CSE

---

## Overview

This project implements a lightweight Linux container runtime in C, featuring a long-running supervisor process and a kernel-space memory monitor. The runtime supports multiple concurrent containers, CLI management, structured logging, kernel-module integration, scheduling experiments, and graceful cleanup.

---

## Tasks

### Task 1 — Multi-Container Runtime

Implemented a supervisor that spawns and manages multiple isolated containers. Both containers (`alpha` and `beta`) coexist and run simultaneously under a single supervisor process.

**Key Output:** Terminal 2 confirms both containers are running in parallel, with independent process trees and namespace isolation.

---

### Task 2 — CLI & Container Management

Implemented CLI commands to start, stop, and manage containers. The supervisor correctly tracks container instances and handles repeated invocations.

**Demonstrated:** `alpha` container launched once and `beta` launched twice — all tracked correctly by the supervisor.

---

### Task 3 — Logging

Implemented structured logging for all container lifecycle events and supervisor operations.

**Key Design Decisions — Synchronization:**

The system uses three core synchronization primitives to coordinate concurrent activity:

**Mutex Locks (`pthread_mutex_t`)**  
Used to enforce mutual exclusion on shared data structures — the container list, log buffers, and command queues. Only one thread may access a critical section at a time, preventing data corruption during concurrent container add/remove operations or log writes.

**Condition Variables (`pthread_cond_t`)**  
Implement a producer–consumer model between the engine (command issuer) and the supervisor (command processor). The supervisor sleeps until a command arrives, waking only when notified — eliminating busy-waiting and reducing CPU overhead.

**Signals (`SIGCHLD`, `SIGINT`, `SIGTERM`)**  
`SIGCHLD` detects container process exit, triggering cleanup to prevent zombie processes. `SIGINT`/`SIGTERM` initiate graceful shutdown: the supervisor forwards the signal to all running containers before terminating itself.

**Bounded Buffer (Circular Queue)**  
Commands are exchanged via a fixed-size circular queue. The producer (engine) inserts commands; the consumer (supervisor) retrieves them. Mutex + condition variable pairs (`not_empty`, `not_full`) guarantee no data loss, no corruption, and no deadlock. Strict lock-ordering discipline prevents deadlock.

**Race Conditions Prevented:**
- Concurrent modification of the container list → lost updates or invalid memory access
- Concurrent log writes → interleaved or corrupted output
- Simultaneous buffer access by producer/consumer → lost or overwritten commands
- Unreaped child processes → zombie accumulation
- Signal delivery during critical sections → inconsistent state or crash

---

### Task 4 — Kernel Memory Monitor

Integrated a loadable kernel module (`monitor.c`) that tracks per-container memory usage from kernel space. The supervisor communicates with the module via `ioctl`.

**Verification:** Three terminals used — Terminal 1 (supervisor), Terminal 2 (container workload), Terminal 3 (double-verification of kernel module readings).

**Key Files Modified:**
- `supervisor.c` — added ioctl calls to query the kernel module
- `monitor.c` — updated to expose per-container memory statistics

---

### Task 5 — Scheduling Experiments

Ran controlled workloads to observe Linux CFS (Completely Fair Scheduler) behavior.

#### Workloads
- `cpu_stress.py` — CPU-bound: tight compute loop
- `io_stress.py` — I/O-bound: repeated read/write operations

#### Experiment 1 — Priority Comparison

Two instances of `cpu_stress.py` pinned to Core 0 with different nice values:

| Process | nice value | Expected Behaviour |
|---------|------------|--------------------|
| Terminal A | `-20` (highest priority) | Fewer involuntary preemptions |
| Terminal B | `+19` (lowest priority) | Many more involuntary preemptions |

**Observable Outcome:** The low-priority process accumulates significantly more involuntary context switches, confirming CFS preempts it more aggressively.

#### Experiment 2 — CPU vs. I/O Comparison

`cpu_stress.py` and `io_stress.py` run concurrently on Core 0.

**Observable Outcome:** Wall-clock (elapsed) time for the CPU-bound task exceeds 30 seconds because it shares the core with the I/O task — demonstrating CPU time-sharing under CFS.

#### Measurement Analysis

**`/usr/bin/time` Output (cpu_stress.py):**

| Metric | Value | Interpretation |
|--------|-------|----------------|
| CPU usage | 99% | Classic CPU-bound process — occupied the full core |
| Involuntary context switches | 2,490 | CFS forcibly preempted the script 2,490 times |
| Voluntary context switches | 1 | Script never yielded the CPU (no I/O waits) |

**`/proc/sched` Output:**

| Field | Value | Interpretation |
|-------|-------|----------------|
| `se.sum_exec_runtime` | 45.65 ms | Total physical time on hardware |
| `nr_switches` | 155 | Total CPU swap-in/swap-out events |

---

### Task 6 — Cleanup

Proper teardown implemented for all tasks:

- **Task 1 cleanup** — container namespaces and cgroups released on supervisor exit
- **Task 3 cleanup** — log file handles flushed and closed; mutex/condvar destroyed
- **Task 4 cleanup** — kernel module unloaded (`rmmod`); ioctl file descriptor closed
- **Task 5 cleanup** — stress processes terminated; CPU affinity (taskset) pinning released

---

## Build

```bash
# Install dependencies (Ubuntu 22.04 / 24.04, Secure Boot OFF)
sudo apt update
sudo apt install -y build-essential linux-headers-$(uname -r)

# Build all targets
cd boilerplate
make

# CI-safe build (no kernel module)
make -C boilerplate ci
```

---

## Repository Structure

```
WorkingCodes/
├── Task1engine.c            
├── Task2engine.c           
├── Task3engine.c     
├── Task4supervisor.c          
├── Task4monitor.c           
├── cpu_stress.py          
├── Io_stress.py.c        


---

## Environment

- **OS:** Ubuntu 22.04 or 24.04
- **Secure Boot:** OFF (required for kernel module loading)
- **WSL:** Not supported
- **Root filesystem:** Alpine Linux minirootfs 3.20.3 (x86_64)

> `rootfs-base/`, `rootfs-alpha/`, and `rootfs-beta/` are not committed to the repository.
