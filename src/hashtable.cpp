#include "hashtable.h"
#include <stdlib.h>
#include <assert.h>

const size_t k_resizing_work = 128;

uint64_t str_hash(const uint8_t *data, size_t len) {
    uint32_t h = 0x811C9DC5;
    for (size_t i = 0; i < len; i++) {
        h = (h + data[i]) * 0x01000193;
    }
    return h;
}

static void h_init(HTab *ht, size_t n) {
    assert(n > 0 && ((n - 1) & n) == 0);
    ht->tab = (HNode **)calloc(n, sizeof(HNode *));
    ht->mask = n - 1;
    ht->size = 0;
}

static void h_insert(HTab *ht, HNode *node) {
    size_t pos = node->hcode & ht->mask;
    node->next = ht->tab[pos];
    ht->tab[pos] = node;
    ht->size++;
}

static HNode **h_lookup(HTab *ht, HNode *key, bool (*cmp)(HNode *, HNode *)) {
    if (!ht->tab) return NULL;
    size_t pos = key->hcode & ht->mask;
    HNode **from = &ht->tab[pos];
    while (*from) {
        if ((*from)->hcode == key->hcode && cmp(*from, key)) return from;
        from = &(*from)->next;
    }
    return NULL;
}

static HNode *h_detach(HTab *ht, HNode **from) {
    HNode *node = *from;
    *from = (*from)->next;
    ht->size--;
    return node;
}

static void hm_help_resizing(HMap *hmap) {
    if (hmap->ht2.tab == NULL) return;
    size_t nwork = 0;
    while (nwork < k_resizing_work && hmap->ht2.size > 0) {
        HNode **from = &hmap->ht2.tab[hmap->resizing_pos];
        if (!*from) {
            hmap->resizing_pos++;
            continue;
        }
        h_insert(&hmap->ht1, h_detach(&hmap->ht2, from));
        nwork++;
    }
    if (hmap->ht2.size == 0) {
        free(hmap->ht2.tab);
        hmap->ht2 = HTab{};
    }
}

static void hm_start_resizing(HMap *hmap) {
    assert(hmap->ht2.tab == NULL);
    hmap->ht2 = hmap->ht1;
    h_init(&hmap->ht1, (hmap->ht1.mask + 1) * 2);
    hmap->resizing_pos = 0;
}

HNode *hm_lookup(HMap *hmap, HNode *key, bool (*cmp)(HNode *, HNode *)) {
    hm_help_resizing(hmap);
    HNode **from = h_lookup(&hmap->ht1, key, cmp);
    if (!from) from = h_lookup(&hmap->ht2, key, cmp);
    return from ? *from : NULL;
}

void hm_insert(HMap *hmap, HNode *node) {
    if (!hmap->ht1.tab) h_init(&hmap->ht1, 4);
    h_insert(&hmap->ht1, node);
    if (!hmap->ht2.tab) {
        size_t load_factor = hmap->ht1.size / (hmap->ht1.mask + 1);
        if (load_factor >= 8) hm_start_resizing(hmap);
    }
    hm_help_resizing(hmap);
}

HNode *hm_pop(HMap *hmap, HNode *key, bool (*cmp)(HNode *, HNode *)) {
    hm_help_resizing(hmap);
    HNode **from = h_lookup(&hmap->ht1, key, cmp);
    if (from) return h_detach(&hmap->ht1, from);
    from = h_lookup(&hmap->ht2, key, cmp);
    if (from) return h_detach(&hmap->ht2, from);
    return NULL;
}