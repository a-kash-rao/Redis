// unit tests for the data structures: AVL tree, avl_offset, heap, hashtable
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <set>
#include <vector>
#include "../common.h"
#include "../avl.h"
#include "../heap.h"
#include "../hashtable.h"

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "CHECK failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
    abort(); } } while (0)

// ======================= AVL =======================

struct Data {
    AVLNode node;
    uint32_t val = 0;
};

struct Container {
    AVLNode *root = NULL;
};

static uint32_t val_of(AVLNode *node) {
    return container_of(node, Data, node)->val;
}

static void add(Container &c, uint32_t val) {
    Data *data = new Data();
    avl_init(&data->node);
    data->val = val;

    AVLNode *cur = NULL;
    AVLNode **from = &c.root;
    while (*from) {
        cur = *from;
        from = (val < val_of(cur)) ? &cur->left : &cur->right;
    }
    *from = &data->node;
    data->node.parent = cur;
    c.root = avl_fix(&data->node);
}

static bool del(Container &c, uint32_t val) {
    AVLNode *cur = c.root;
    while (cur) {
        uint32_t node_val = val_of(cur);
        if (val == node_val) {
            break;
        }
        cur = val < node_val ? cur->left : cur->right;
    }
    if (!cur) {
        return false;
    }
    c.root = avl_del(cur);
    delete container_of(cur, Data, node);
    return true;
}

static void avl_verify(AVLNode *parent, AVLNode *node) {
    if (!node) {
        return;
    }
    CHECK(node->parent == parent);
    avl_verify(node, node->left);
    avl_verify(node, node->right);

    CHECK(node->cnt == 1 + avl_cnt(node->left) + avl_cnt(node->right));
    uint32_t l = avl_height(node->left);
    uint32_t r = avl_height(node->right);
    CHECK(l == r || l + 1 == r || l == r + 1);
    CHECK(node->height == 1 + std::max(l, r));

    uint32_t val = val_of(node);
    if (node->left) {
        CHECK(val_of(node->left) <= val);
    }
    if (node->right) {
        CHECK(val_of(node->right) >= val);
    }
}

static void extract(AVLNode *node, std::multiset<uint32_t> &out) {
    if (!node) {
        return;
    }
    extract(node->left, out);
    out.insert(val_of(node));
    extract(node->right, out);
}

static void container_verify(Container &c, const std::multiset<uint32_t> &ref) {
    avl_verify(NULL, c.root);
    CHECK(avl_cnt(c.root) == ref.size());
    std::multiset<uint32_t> extracted;
    extract(c.root, extracted);
    CHECK(extracted == ref);
}

static void dispose(Container &c) {
    while (c.root) {
        AVLNode *node = c.root;
        c.root = avl_del(c.root);
        delete container_of(node, Data, node);
    }
}

static void test_avl() {
    Container c;
    std::multiset<uint32_t> ref;

    // empty tree
    container_verify(c, ref);
    CHECK(!del(c, 123));

    // sequential insertion
    for (uint32_t i = 0; i < 1000; i += 3) {
        add(c, i);
        ref.insert(i);
        container_verify(c, ref);
    }
    // random insertion
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t val = (uint32_t)rand() % 1000;
        add(c, val);
        ref.insert(val);
        container_verify(c, ref);
    }
    // random deletion
    for (uint32_t i = 0; i < 200; i++) {
        uint32_t val = (uint32_t)rand() % 1000;
        auto it = ref.find(val);
        if (it == ref.end()) {
            CHECK(!del(c, val));
        } else {
            CHECK(del(c, val));
            ref.erase(it);
        }
        container_verify(c, ref);
    }
    dispose(c);

    // insert/delete every position in trees of various sizes
    for (uint32_t sz = 0; sz < 200; ++sz) {
        for (uint32_t val = 0; val < sz; ++val) {
            Container c2;
            std::multiset<uint32_t> ref2;
            for (uint32_t i = 0; i < sz; ++i) {
                if (i == val) {
                    continue;
                }
                add(c2, i);
                ref2.insert(i);
            }
            container_verify(c2, ref2);
            add(c2, val);
            ref2.insert(val);
            container_verify(c2, ref2);
            CHECK(del(c2, val));
            ref2.erase(val);
            container_verify(c2, ref2);
            dispose(c2);
        }
    }
    printf("avl: ok\n");
}

static void test_avl_offset() {
    for (uint32_t sz = 1; sz < 200; ++sz) {
        Container c;
        for (uint32_t i = 0; i < sz; ++i) {
            add(c, i);
        }
        AVLNode *min = c.root;
        while (min->left) {
            min = min->left;
        }
        for (uint32_t i = 0; i < sz; ++i) {
            AVLNode *node = avl_offset(min, (int64_t)i);
            CHECK(node && val_of(node) == i);
            for (uint32_t j = 0; j < sz; ++j) {
                int64_t offset = (int64_t)j - (int64_t)i;
                AVLNode *n2 = avl_offset(node, offset);
                CHECK(n2 && val_of(n2) == j);
            }
            CHECK(!avl_offset(node, (int64_t)(sz - i)));
            CHECK(!avl_offset(node, -(int64_t)i - 1));
        }
        dispose(c);
    }
    printf("avl_offset: ok\n");
}

// ======================= heap =======================

static void heap_verify(const std::vector<HeapItem> &a, const std::vector<size_t> &refs) {
    for (size_t i = 0; i < a.size(); ++i) {
        CHECK(*a[i].ref == i);
        size_t l = i * 2 + 1, r = i * 2 + 2;
        if (l < a.size()) {
            CHECK(a[i].val <= a[l].val);
        }
        if (r < a.size()) {
            CHECK(a[i].val <= a[r].val);
        }
    }
    (void)refs;
}

static void test_heap() {
    const size_t N = 2000;
    std::vector<size_t> refs(N);    // fixed size, so the pointers stay valid
    std::vector<HeapItem> heap;
    std::multiset<uint64_t> ref;

    // push
    for (size_t i = 0; i < N; ++i) {
        HeapItem item = {(uint64_t)(rand() % 10000), &refs[i]};
        heap.push_back(item);
        heap_update(heap.data(), heap.size() - 1, heap.size());
        ref.insert(item.val);
    }
    heap_verify(heap, refs);

    // update random items through their back-references
    for (size_t k = 0; k < N; ++k) {
        size_t owner = (size_t)rand() % N;
        size_t pos = refs[owner];
        ref.erase(ref.find(heap[pos].val));
        heap[pos].val = (uint64_t)(rand() % 10000);
        ref.insert(heap[pos].val);
        heap_update(heap.data(), pos, heap.size());
    }
    heap_verify(heap, refs);

    // pop everything, must come out sorted
    std::vector<uint64_t> popped;
    while (!heap.empty()) {
        popped.push_back(heap[0].val);
        heap[0] = heap.back();
        heap.pop_back();
        if (!heap.empty()) {
            heap_update(heap.data(), 0, heap.size());
        }
    }
    CHECK(std::vector<uint64_t>(ref.begin(), ref.end()) == popped);
    printf("heap: ok\n");
}

// ======================= hashtable =======================

struct HEntry {
    HNode node;
    uint64_t key = 0;
};

static bool hentry_eq(HNode *lhs, HNode *rhs) {
    return container_of(lhs, HEntry, node)->key == container_of(rhs, HEntry, node)->key;
}

static uint64_t key_hash(uint64_t key) {
    return str_hash((const uint8_t *)&key, sizeof(key));
}

static HEntry *find(HMap &hmap, uint64_t key) {
    HEntry q;
    q.key = key;
    q.node.hcode = key_hash(key);
    HNode *node = hm_lookup(&hmap, &q.node, &hentry_eq);
    return node ? container_of(node, HEntry, node) : NULL;
}

static bool count_cb(HNode *, void *arg) {
    (*(size_t *)arg)++;
    return true;
}

static void test_hashtable() {
    HMap hmap;
    const uint64_t N = 100000;
    for (uint64_t i = 0; i < N; ++i) {
        HEntry *e = new HEntry();
        e->key = i;
        e->node.hcode = key_hash(i);
        hm_insert(&hmap, &e->node);
        CHECK(hm_size(&hmap) == i + 1);
    }
    for (uint64_t i = 0; i < N; ++i) {
        HEntry *e = find(hmap, i);
        CHECK(e && e->key == i);
    }
    CHECK(!find(hmap, N + 1));

    // delete the even keys
    for (uint64_t i = 0; i < N; i += 2) {
        HEntry q;
        q.key = i;
        q.node.hcode = key_hash(i);
        HNode *node = hm_delete(&hmap, &q.node, &hentry_eq);
        CHECK(node);
        delete container_of(node, HEntry, node);
    }
    CHECK(hm_size(&hmap) == N / 2);
    for (uint64_t i = 0; i < N; ++i) {
        CHECK((find(hmap, i) != NULL) == (i % 2 == 1));
    }
    size_t cnt = 0;
    hm_foreach(&hmap, &count_cb, &cnt);
    CHECK(cnt == N / 2);

    // cleanup
    for (uint64_t i = 1; i < N; i += 2) {
        HEntry q;
        q.key = i;
        q.node.hcode = key_hash(i);
        HNode *node = hm_delete(&hmap, &q.node, &hentry_eq);
        CHECK(node);
        delete container_of(node, HEntry, node);
    }
    CHECK(hm_size(&hmap) == 0);
    hm_clear(&hmap);
    printf("hashtable: ok\n");
}

int main() {
    srand(42);
    test_avl();
    test_avl_offset();
    test_heap();
    test_hashtable();
    printf("all data structure tests passed\n");
    return 0;
}
