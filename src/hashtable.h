#pragma once

#include <stdint.h>
#include <stddef.h>

struct HNode {
    HNode *next = NULL;
    uint64_t hcode = 0;
};

struct HTab {
    HNode **tab = NULL;
    size_t mask = 0;
    size_t size = 0;
};

struct HMap {
    HTab ht1;
    HTab ht2;
    size_t resizing_pos = 0;
};

uint64_t str_hash(const uint8_t *data, size_t len);

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*cmp)(HNode *, HNode *));
void hm_insert(HMap *hmap, HNode *node);
HNode *hm_pop(HMap *hmap, HNode *key, bool (*cmp)(HNode *, HNode *));