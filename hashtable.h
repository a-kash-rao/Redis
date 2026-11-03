#pragma once

#include <stddef.h>
#include <stdint.h>

// hashtable node, embedded inside the payload (intrusive)
struct HNode {
    HNode *next = NULL;
    uint64_t hcode = 0;     // cached hash value
};

// a simple fixed-size hashtable using chaining
struct HTab {
    HNode **tab = NULL;     // array of slots, each slot is a linked list
    size_t mask = 0;        // size of the array is a power of 2, index = hcode & mask
    size_t size = 0;        // number of keys
};

// the real hashtable interface, it uses 2 HTabs for progressive rehashing:
// when `newer` gets too full, it becomes `older`, and a bigger `newer` is
// created; keys are then moved a few at a time on every operation, so no
// single request has to pay for resizing the whole table (no latency spike)
struct HMap {
    HTab newer;
    HTab older;
    size_t migrate_pos = 0;
};

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
void   hm_insert(HMap *hmap, HNode *node);
HNode *hm_delete(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
void   hm_clear(HMap *hmap);
size_t hm_size(HMap *hmap);
// invoke the callback on each node until it returns false
void   hm_foreach(HMap *hmap, bool (*f)(HNode *, void *), void *arg);
