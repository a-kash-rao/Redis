#pragma once

#include <stddef.h>
#include <stdint.h>

// intrusive data structures embed a node (HNode, AVLNode, DList) inside the
// owning struct; container_of goes from a pointer to the node back to the owner
// eg. container_of(hnode_ptr, Entry, node) -> Entry*
#define container_of(ptr, T, member) \
    ((T *)( (char *)(ptr) - offsetof(T, member) ))

// FNV-1a style hash, good enough for a hashtable
inline uint64_t str_hash(const uint8_t *data, size_t len) {
    uint32_t h = 0x811C9DC5;
    for (size_t i = 0; i < len; i++) {
        h = (h + data[i]) * 0x01000193;
    }
    return h;
}
