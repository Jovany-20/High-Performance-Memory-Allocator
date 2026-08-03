# 🧠 High-Performance Memory Allocator

> A custom `malloc`, `free`, and `realloc` implementation in C — built from scratch to maximize both **throughput** and **memory utilization**, achieving a **92.15/100** composite score on the MIT 6.172 benchmark driver.

---

## 📊 Performance Results

| Metric | Result |
|---|---|
| 🏆 **Composite Score** | **92.15 / 100** |
| ⚡ **Peak Throughput** | ~100,000 ops/sec (99.7% of hardware speed limit) |
| 💾 **Space Utilization** | 85%+ across all benchmark traces |
| 🚀 **Trace 9 Speedup** | **500×** over the baseline allocator |

> Benchmarked against real-world `malloc`/`free`/`realloc` trace files using the MIT course driver `mdriver`.

---

## 🔑 Core Architecture

This allocator is built on a **Segregated Free List** design, combining several classical and custom optimizations to achieve near-optimal performance.

### 1. Segregated Free Lists (10 Bins)

Instead of a single global free list, the heap is managed by **10 size-segregated bins**:

```
Bin 0:  ≤ 32 bytes
Bin 1:  33–64 bytes
Bin 2:  65–128 bytes
...
Bin 9:  > 8192 bytes (catch-all)
```

Bin lookup is **O(1)** using the `__builtin_clz` (Count Leading Zeros) CPU intrinsic — a single hardware instruction maps any requested size to its bin index. No loops, no comparisons.

```c
int get_bin(size_t size) {
    if (size <= 32) return 0;
    int bin = 32 - __builtin_clz(size - 1);
    bin = bin - 5;
    if (bin >= NUM_BINS) return NUM_BINS - 1;
    return bin;
}
```

### 2. Boundary-Tag Coalescing (O(1) Merge)

Every block carries an **8-byte header** and an **8-byte footer** (boundary tags). On `free()`, adjacent physical neighbors are inspected in constant time. If they are free:

1. Both are **unlinked** from their bins.
2. The three regions are **merged** into one contiguous block.
3. The result is **re-inserted** into the correct larger bin.

This aggressively eliminates external fragmentation without any heap traversal.

### 3. Block Splitting

When a free block is larger than needed, it is **split in-place**:
- The front portion satisfies the allocation request.
- The **splinter** (remainder) is routed to its correct bin via `get_bin`.

Splitting only occurs when the remainder is ≥ 32 bytes (the minimum useful block size), preventing untrackable micro-fragments.

### 4. Smart `realloc` — In-Place Heap Expansion

The `realloc` implementation avoids expensive `memcpy` calls whenever possible through three strategies:

| Case | Action |
|---|---|
| New size == Old size | Return the same pointer immediately |
| New size < Old size | **Trim** the block in-place; free the tail |
| Block is at the top of the heap | **Extend** the heap by exactly the missing bytes via `mem_sbrk` — no copy needed |
| None of the above | Standard `malloc` → `memcpy` → `free` |

The **top-of-heap expansion** case is the key insight behind the **500× speedup on Trace 9**, which consists almost entirely of sequential `realloc` calls on a growing buffer.

---

## 🗂️ Project Structure

```
MIT6_172F18-project3/
├── mymalloc/
│   ├── allocator.c           # ← Core implementation (all optimizations here)
│   ├── allocator_interface.h # Public API: my_malloc, my_free, my_realloc
│   ├── memlib.{c,h}          # Simulated heap (mem_sbrk, mem_heap_lo/hi)
│   ├── mdriver.c             # MIT benchmark driver
│   ├── mdriver.h             # Driver interface
│   ├── config.h              # Benchmark config (heap size, weights, timing)
│   ├── validator.h           # Heap invariant checker
│   ├── traces/               # Standard benchmark trace files
│   ├── short_traces/         # Abbreviated traces for fast testing
│   ├── additional_traces/    # Extra trace files
│   ├── Makefile              # Build system (clang, -O3)
│   └── ...                   # Timing utilities (fcyc, fsecs, ftimer, clock)
├── clint.py                  # MIT C linter
└── README.md
```

---

## 🛠️ Build & Run

> **Requires:** `clang` and a Linux/Unix environment (native build targets Linux; on Windows, use WSL).

### Compile

```bash
cd ./mymalloc
make
```

This produces two binaries:
- `mdriver` — the full benchmark driver
- `allocator_test` — correctness unit tester

### Run the Full Benchmark Suite

```bash
./mdriver -c
```

Common flags:

| Flag | Description |
|---|---|
| `-c` | Run with correctness checking enabled |
| `-v` | Verbose output (per-trace breakdown) |
| `-t <dir>` | Use a custom trace directory |
| `-f <file>` | Run a single trace file |

### Debug Build

```bash
make DEBUG=1
./mdriver -v
```

### Clean

```bash
make clean
```

---

## 🧪 Correctness Checker

The `my_check()` function walks the entire heap and validates that all block headers chain correctly from `heap_lo` to `heap_hi`. It is invoked automatically by `mdriver -c`.

---

## ⚙️ Configuration (`config.h`)

| Constant | Value | Description |
|---|---|---|
| `UTIL_WEIGHT` | `0.50` | Weight of utilization in composite score |
| `MAX_BASE_THROUGHPUT` | `64,000,000 ops/s` | Reference throughput for normalization |
| `MAX_HEAP` | `50 MB` | Maximum simulated heap size |
| `R_ALIGNMENT` | `8 bytes` | Required alignment for all allocations |

---

## 📚 Background

This project is part of **MIT 6.172: Performance Engineering of Software Systems**. The goal is to replace the standard `libc` allocator (`malloc`/`free`/`realloc`) with a hand-optimized implementation that scores highly on a composite benchmark measuring both space efficiency and raw throughput across real-world allocation traces.

---

## 📄 License

Original course framework: Copyright © 2015 MIT 6.172 Staff, MIT License.  
Student implementation (`allocator.c` optimizations): see [LICENSE](LICENSE) or course policy.
