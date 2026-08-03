# High-Performance Memory Allocator

A custom dynamic memory allocator (`malloc`, `free`, `realloc`) written in C, originally developed as part of the MIT 6.172 Performance Engineering curriculum. This project replaces the standard `libc` allocator and heavily optimizes for both **throughput** (operations per second) and **space utilization** against real-world benchmark traces.

## 🚀 Performance Metrics
*   **Throughput:** Achieved nearly 100,000 ops/sec on intensive traces (99.7% of optimal hardware speed limit).
*   **Space Utilization:** Sustained 85%+ memory utilization by aggressively managing internal and external fragmentation.
*   **Composite Score:** 92.15 / 100 on the automated MIT driver benchmark.
*   **Trace 9 Speedup:** Achieved a **500x speedup** over the baseline allocator on heavy `realloc` workloads.

## 🧠 Core Architecture & Optimizations

### 1. Segregated Free Lists (Bins)
Instead of a single global free list, the allocator uses an array of **10 Segregated Free Lists** separated by block size ranges. 
*   **O(1) Average Search Time:** Finding a free block is mathematically optimized. The allocator uses the `__builtin_clz` (Count Leading Zeros) hardware intrinsic instruction to map a requested size to the correct bin in a single CPU cycle.
*   **Block Splitting:** If a retrieved block is significantly larger than the requested size, it is dynamically split to minimize internal fragmentation, and the leftover "splinter" is mathematically routed to its respective bin.

### 2. Boundary-Tag Coalescing
To eliminate external fragmentation, every block contains an 8-byte header and footer. 
When `free()` is called, the allocator checks the boundary tags of the adjacent physical memory blocks in O(1) time. If the adjacent blocks are also free, it safely unlinks them from their respective bins, merges them into a massive contiguous block, and inserts the new block into the correct larger bin.

### 3. Smart `realloc` with In-Place Heap Expansion
The `realloc` implementation is highly optimized to avoid expensive `memcpy` operations whenever possible:
*   **Trimming:** If a user requests a smaller size, the block shrinks in-place and the leftover space is freed.
*   **Top-of-Heap Expansion:** If a block sits at the edge of the physical heap (`mem_heap_hi`), `realloc` dynamically stretches the block by calling `mem_sbrk` to request exactly the missing bytes from the OS. This optimization alone completely bypassed `memcpy` on Trace 9, resulting in a 500x throughput increase.

## 🛠️ Build and Test

Compile the allocator and test driver:
```bash
cd ./mymalloc
make
```

Run the benchmark suite against the trace files:
```bash
./mdriver -c
```
