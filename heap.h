#pragma once

#include <stddef.h>
#include <stdint.h>

// binary min-heap item, used for TTL timers
// val -> expiration time
// ref -> points back to the owner's index field (Entry::heap_idx), the heap
//        keeps it updated when items move so the owner can find its item
struct HeapItem {
    uint64_t val = 0;
    size_t *ref = NULL;
};

// restore the heap property after a[pos] was changed or added
void heap_update(HeapItem *a, size_t pos, size_t len);
