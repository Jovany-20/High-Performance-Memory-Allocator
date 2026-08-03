/**
 * Copyright (c) 2015 MIT License by 6.172 Staff
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 **/

#include "./allocator_interface.h"
#include "./memlib.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Don't call libc malloc!
#define malloc(...) (USE_MY_MALLOC)
#define free(...) (USE_MY_FREE)
#define realloc(...) (USE_MY_REALLOC)

// All blocks must have a specified minimum alignment.
// The alignment requirement (from config.h) is >= 8 bytes.
#ifndef ALIGNMENT
#define ALIGNMENT 8
#endif

// Rounds up to the nearest multiple of ALIGNMENT.
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

// The smallest aligned size that will hold a size_t value.
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
#define NUM_BINS 10
typedef struct block_t {
  // pointer to its start (so far)
  size_t size;
  union {
    struct {
      struct block_t *next;
      struct block_t *prev;
    } free_pointers;
    char payload[0];
  } body;
} block_t;

block_t *heap;
block_t *free_list[NUM_BINS];

size_t get_size(block_t *block) { return block->size & ~0x7; }

int is_allocated(block_t *block) { return block->size & 0x1; }

int get_bin(size_t size) {
  if (size <= 32)
    return 0;

  // For 33-64, this evaluates to 6.
  int bin = 32 - __builtin_clz(size - 1);

  bin = bin - 5; // Shift down so 33-64 becomes Bin 1!

  if (bin >= NUM_BINS) {
    return NUM_BINS - 1; // Anything over 8192 bytes goes in the final bin
  }
  return bin;
}

// check - This checks our invariant that the size_t header before every
// block points to either the beginning of the next block, or the end of the
// heap.
int my_check() {
  char *p;
  char *lo = (char *)mem_heap_lo();
  char *hi = (char *)mem_heap_hi() + 1;
  size_t size = 0;

  p = lo;
  while (lo <= p && p < hi) {
    size = get_size((block_t *)p);

    // NEW SAFETY CHECK:
    if (size == 0) {
      printf("Checker stuck! Block size is 0 at memory address %p!\n", p);
      return -1; // Abort the loop!
    }

    p += size;
  }

  if (p != hi) {
    printf("Bad headers did not end at heap_hi!\n");
    printf("heap_lo: %p, heap_hi: %p, size: %lu, p: %p\n", lo, hi, size, p);
    return -1;
  }

  return 0;
}

// init - Initialize the malloc package.  Called once before any other
// calls are made.  Since this is a very simple implementation, we just
// return success.
int my_init() {
  // reserve one size of size_t and mark it as reserved to never look outside
  // our heap
  for (int i = 0; i < NUM_BINS; i++)
    free_list[i] = NULL;

  void *p = mem_sbrk(sizeof(size_t));

  heap = (block_t *)p;
  heap->size = SIZE_T_SIZE + 1;
  // heap->body.payload[0] = *(size_t *)p;

  return 0;
}

//  malloc - Allocate a block by incrementing the brk pointer.
//  Always allocate a block whose size is a multiple of the alignment.
void *my_malloc(size_t size) {
  // We allocate a little bit of extra memory so that we can store the
  // size of the block we've allocated.  Take a look at realloc to see
  // one example of a place where this can come in handy.
  int aligned_size = ALIGN(size + 2 * SIZE_T_SIZE);
  if (aligned_size < 32) {
    aligned_size = 32;
  }

  // Expands the heap by the given number of bytes and returns a pointer to
  // the newly-allocated area.  This is a slow call, so you will want to
  // make sure you don't wind up calling it on every malloc.

  block_t *temp_free_list_head = NULL;

  for (int i = get_bin(aligned_size); i < NUM_BINS; i++) {
    temp_free_list_head = free_list[i];

    while (temp_free_list_head) {
      if (temp_free_list_head->size < aligned_size) {
        // Too small, move to the next block in this Bin
        temp_free_list_head = temp_free_list_head->body.free_pointers.next;
      } else if (temp_free_list_head->size == aligned_size) {
        // EXACT MATCH: Unlink it completely
        block_t *prev_temp = temp_free_list_head->body.free_pointers.prev;
        block_t *next_temp = temp_free_list_head->body.free_pointers.next;
        if (prev_temp)
          prev_temp->body.free_pointers.next = next_temp;
        else
          free_list[i] = next_temp;
        if (next_temp)
          next_temp->body.free_pointers.prev = prev_temp;
        break;
      } else {
        // BIGGER THAN WE NEED
        if (temp_free_list_head->size - aligned_size >= 32) {
          // SPLIT THE BLOCK!

          block_t *prev_temp = temp_free_list_head->body.free_pointers.prev;
          block_t *next_temp = temp_free_list_head->body.free_pointers.next;
          if (prev_temp)
            prev_temp->body.free_pointers.next = next_temp;
          else
            free_list[i] = next_temp;
          if (next_temp)
            next_temp->body.free_pointers.prev = prev_temp;

          block_t *temp =
              (block_t *)((char *)temp_free_list_head + aligned_size);
          temp->size = temp_free_list_head->size - aligned_size;
          *(size_t *)((char *)temp + temp->size - SIZE_T_SIZE) = temp->size;

          int splinter_bin = get_bin(temp->size);
          temp->body.free_pointers.next = free_list[splinter_bin];
          temp->body.free_pointers.prev = NULL;
          if (free_list[splinter_bin])
            free_list[splinter_bin]->body.free_pointers.prev = temp;
          free_list[splinter_bin] = temp;

          temp_free_list_head->size = aligned_size;
          break;
        } else {
          // SPLINTER IS TOO SMALL TO BE FREE: Treat exactly like Exact Match
          block_t *prev_temp = temp_free_list_head->body.free_pointers.prev;
          block_t *next_temp = temp_free_list_head->body.free_pointers.next;
          if (prev_temp)
            prev_temp->body.free_pointers.next = next_temp;
          else
            free_list[i] = next_temp;
          if (next_temp)
            next_temp->body.free_pointers.prev = prev_temp;
          break;
        }
      }
    }

    // If we found a block, break out of the FOR loop too!
    if (temp_free_list_head) {
      break;
    }
  }

  if (temp_free_list_head) {
    *(size_t *)((char *)temp_free_list_head + temp_free_list_head->size -
                SIZE_T_SIZE) = temp_free_list_head->size + 1;
    temp_free_list_head->size += 1;
    return (void *)temp_free_list_head->body.payload;
  } else {
    void *p = mem_sbrk(aligned_size);
    if (p == (void *)-1) {
      return NULL;
    }
    temp_free_list_head = (block_t *)p;
    temp_free_list_head->size = aligned_size;
    *(size_t *)((char *)temp_free_list_head + temp_free_list_head->size -
                SIZE_T_SIZE) = temp_free_list_head->size + 1;
    temp_free_list_head->size += 1;
    return (void *)temp_free_list_head->body.payload;
  }
}

// free - Freeing a block does nothing.
void my_free(void *ptr) {
  if (!ptr)
    return;
  block_t *freed_block = (block_t *)((char *)ptr - SIZE_T_SIZE);
  size_t true_size = get_size(freed_block);

  size_t *prev_block_footer = (size_t *)((char *)freed_block - SIZE_T_SIZE);
  int prev_allocated = *prev_block_footer & 0x1;
  size_t *next_block_header = (size_t *)((char *)freed_block + true_size);

  int next_allocated = 1;
  if ((char *)next_block_header <= (char *)mem_heap_hi()) {
    next_allocated = *(next_block_header) & 0x1;
  }
  // if both of them are free
  if (!next_allocated && !prev_allocated) {
    block_t *prev = (block_t *)((char *)prev_block_footer - *prev_block_footer +
                                SIZE_T_SIZE);
    block_t *next = (block_t *)next_block_header;

    int bin = get_bin(prev->size);
    // remove prev from its bin
    block_t *prev1 = prev->body.free_pointers.prev;
    block_t *next1 = prev->body.free_pointers.next;
    if (prev1)
      prev1->body.free_pointers.next = next1;
    else
      free_list[bin] = next1;
    if (next1)
      next1->body.free_pointers.prev = prev1;

    bin = get_bin(next->size);
    // remove next from its bin
    block_t *prev2 = next->body.free_pointers.prev;
    block_t *next2 = next->body.free_pointers.next;
    if (prev2)
      prev2->body.free_pointers.next = next2;
    else
      free_list[bin] = next2;
    if (next2)
      next2->body.free_pointers.prev = prev2;

    prev->size += true_size + next->size;
    *(size_t *)((char *)prev + prev->size - SIZE_T_SIZE) = prev->size;

    bin = get_bin(prev->size);
    prev->body.free_pointers.next = free_list[bin];
    prev->body.free_pointers.prev = NULL;
    if (free_list[bin])
      free_list[bin]->body.free_pointers.prev = prev;
    free_list[bin] = prev;

  }
  // if the block before it only is free
  else if (!prev_allocated) {
    block_t *temp = (block_t *)((char *)prev_block_footer - *prev_block_footer +
                                SIZE_T_SIZE);
    int bin = get_bin(temp->size);
    // remove temp from its bin
    block_t *prev = temp->body.free_pointers.prev;
    block_t *next = temp->body.free_pointers.next;
    if (prev)
      prev->body.free_pointers.next = next;
    else
      free_list[bin] = next;
    if (next)
      next->body.free_pointers.prev = prev;
    temp->size += true_size;
    *(size_t *)((char *)temp + temp->size - SIZE_T_SIZE) = temp->size;
    bin = get_bin(temp->size);
    temp->body.free_pointers.next = free_list[bin];
    temp->body.free_pointers.prev = NULL;
    if (free_list[bin])
      free_list[bin]->body.free_pointers.prev = temp;
    free_list[bin] = temp;

  }

  // if the block after it only is free
  else if (!next_allocated) {
    block_t *temp = (block_t *)next_block_header;
    int bin = get_bin(temp->size);
    // remove temp from its bin
    block_t *prev = temp->body.free_pointers.prev;
    block_t *next = temp->body.free_pointers.next;
    if (prev)
      prev->body.free_pointers.next = next;
    else
      free_list[bin] = next;
    if (next)
      next->body.free_pointers.prev = prev;

    freed_block->size = true_size + temp->size;
    *(size_t *)((char *)freed_block + freed_block->size - SIZE_T_SIZE) =
        freed_block->size;
    bin = get_bin(freed_block->size);
    freed_block->body.free_pointers.next = free_list[bin];
    freed_block->body.free_pointers.prev = NULL;
    if (free_list[bin])
      free_list[bin]->body.free_pointers.prev = freed_block;
    free_list[bin] = freed_block;
  }

  // if not
  else {
    int bin = get_bin(true_size);
    freed_block->body.free_pointers.next = free_list[bin];
    freed_block->body.free_pointers.prev = NULL;
    if (free_list[bin]) {
      free_list[bin]->body.free_pointers.prev = freed_block;
    }
    free_list[bin] = freed_block;
    freed_block->size = true_size;
    *(size_t *)((char *)freed_block + true_size - SIZE_T_SIZE) = true_size;
  }
}

// realloc - Implemented simply in terms of malloc and free
void *my_realloc(void *ptr, size_t size) {
  // Edge cases standard to C
  if (!ptr)
    return my_malloc(size);
  if (size == 0) {
    my_free(ptr);
    return NULL;
  }

  block_t *block = (block_t *)((char *)ptr - SIZE_T_SIZE);
  size_t true_size = get_size(block);
  int aligned_new_size = ALIGN(size + 2 * SIZE_T_SIZE);
  if (aligned_new_size < 32) {
    aligned_new_size = 32;
  }

  // Optimization 1: Do we already have enough space?
  if (true_size >= aligned_new_size) {

    // If the leftover space is big enough to form a Free Block, let's split it!
    if (true_size - aligned_new_size >= 32) {
      // 1. Shrink our current block
      block->size = aligned_new_size + 1; // Keep it allocated!
      *(size_t *)((char *)block + aligned_new_size - SIZE_T_SIZE) =
          aligned_new_size + 1;

      // 2. Create the leftover block
      block_t *leftover = (block_t *)((char *)block + aligned_new_size);
      leftover->size = true_size - aligned_new_size; // Not allocated!
      *(size_t *)((char *)leftover + leftover->size - SIZE_T_SIZE) =
          leftover->size;

      // 3. Send the leftover block to my_free so it gets added to the Free
      // List! (Remember my_free expects a pointer to the Payload, not the
      // Header!)
      my_free((void *)((char *)leftover + SIZE_T_SIZE));
    }

    return ptr;
  }

  // Optimization 2: Are we the very last block in the heap?
  // (The very last byte of our block is: Header Address + Total Size - 1)
  if ((char *)block + true_size - 1 == (char *)mem_heap_hi()) {

    // 1. Calculate how much extra space we need
    size_t extra_needed = aligned_new_size - true_size;

    // 2. Ask the OS to expand the heap by exactly that much!
    void *p = mem_sbrk(extra_needed);

    if (p == (void *)-1) {
      // If the OS refuses, we can't grow in-place.
      // (You could fallback to standard realloc here, but returning NULL is
      // fine for now)
      return NULL;
    }

    // 3. Update our Header to the new massive size (Keep it allocated!)
    block->size = aligned_new_size + 1;

    // 4. Write the new Footer at the very end!
    // (Hint: use aligned_new_size and SIZE_T_SIZE just like Optimization 1)
    *(size_t *)((char *)block + aligned_new_size - SIZE_T_SIZE) = block->size;
    return ptr; // We didn't move!
  }

  // Standard Case: We are stuck in the middle of the heap.
  else {
    // 1. Ask for a brand new block somewhere else
    void *newptr = my_malloc(size);
    if (NULL == newptr) {
      return NULL;
    }

    // 2. Figure out how many bytes of Payload we can safely copy
    size_t copy_size = true_size - (2 * SIZE_T_SIZE);
    if (size < copy_size) {
      copy_size = size;
    }

    // 3. Move the data, free the old block, and return the new one!
    memcpy(newptr, ptr, copy_size);
    my_free(ptr);
    return newptr;
  }
}
