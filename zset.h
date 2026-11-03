#pragma once

#include "avl.h"
#include "hashtable.h"

// sorted set: every member is indexed twice
//   hmap -> name to node, for O(1) point lookups (zscore, zrem)
//   tree -> sorted by (score, name), for range queries (zquery)
struct ZSet {
    AVLNode *root = NULL;
    HMap hmap;
};

struct ZNode {
    AVLNode tree;
    HNode hmap;
    double score = 0;
    size_t len = 0;
    char name[0];   // flexible array, the name is allocated right after the struct
};

// returns true if a new member was added, false if an existing score was updated
bool   zset_insert(ZSet *zset, const char *name, size_t len, double score);
ZNode *zset_lookup(ZSet *zset, const char *name, size_t len);
void   zset_delete(ZSet *zset, ZNode *node);
// first member that is >= (score, name)
ZNode *zset_seekge(ZSet *zset, double score, const char *name, size_t len);
void   zset_clear(ZSet *zset);
ZNode *znode_offset(ZNode *node, int64_t offset);
