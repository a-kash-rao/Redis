#pragma once

#include <stddef.h>
#include <stdint.h>

// intrusive AVL tree node
// height -> used for balancing, |height(left) - height(right)| <= 1
// cnt    -> size of the subtree, used for rank queries (avl_offset)
struct AVLNode {
    AVLNode *parent = NULL;
    AVLNode *left = NULL;
    AVLNode *right = NULL;
    uint32_t height = 0;
    uint32_t cnt = 0;
};

inline void avl_init(AVLNode *node) {
    node->left = node->right = node->parent = NULL;
    node->height = 1;
    node->cnt = 1;
}

inline uint32_t avl_height(AVLNode *node) { return node ? node->height : 0; }
inline uint32_t avl_cnt(AVLNode *node) { return node ? node->cnt : 0; }

// fix the tree after inserting/updating `node`, returns the new root
AVLNode *avl_fix(AVLNode *node);
// detach `node` from the tree, returns the new root
AVLNode *avl_del(AVLNode *node);
// go to the node `offset` positions away in sorted order (can be negative),
// returns NULL if out of range; O(log n)
AVLNode *avl_offset(AVLNode *node, int64_t offset);
