#pragma once

#include <stdint.h>
#include <stddef.h>

struct HeapItem {
    uint64_t val;
    size_t *ref;
};

void heap_up(HeapItem* a, size_t pos);
void heap_down(HeapItem* a, size_t pos, size_t len);
void heap_update(HeapItem* a, size_t pos, size_t len);