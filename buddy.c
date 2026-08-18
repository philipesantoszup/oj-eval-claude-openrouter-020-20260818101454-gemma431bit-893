#include "buddy.h"
#include <stddef.h>
#include <string.h>

#define PAGE_SIZE 4096
#define MAX_RANK 16
#define MAX_PAGES 65536

typedef struct FreeBlock {
    struct FreeBlock *next;
    struct FreeBlock *prev;
} FreeBlock;

static void *p_start = NULL;
static int total_pages = 0;
static unsigned char status[MAX_PAGES];
static FreeBlock *free_lists[MAX_RANK + 1];
static int free_counts[MAX_RANK + 1];

static void add_to_free_list(int rank, int idx) {
    void *ptr = (char *)p_start + ((size_t)idx * PAGE_SIZE);
    FreeBlock *block = (FreeBlock *)ptr;
    block->prev = NULL;
    block->next = free_lists[rank];
    if (free_lists[rank]) {
        free_lists[rank]->prev = block;
    }
    free_lists[rank] = block;
    free_counts[rank]++;
}

static void remove_from_free_list(int rank, int idx) {
    void *ptr = (char *)p_start + ((size_t)idx * PAGE_SIZE);
    FreeBlock *block = (FreeBlock *)ptr;
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        free_lists[rank] = block->next;
    }
    if (block->next) {
        block->next->prev = block->prev;
    }
    block->next = NULL;
    block->prev = NULL;
    free_counts[rank]--;
}

int init_page(void *p, int pgcount) {
    if (!p || pgcount <= 0) return -EINVAL;

    p_start = p;
    total_pages = pgcount;

    memset(status, 0, sizeof(status));
    for (int i = 0; i <= MAX_RANK; i++) {
        free_lists[i] = NULL;
        free_counts[i] = 0;
    }

    int curr_idx = 0;
    int remaining = pgcount;
    while (remaining > 0) {
        int r = 0;
        while (r < MAX_RANK && (1 << r) <= remaining) {
            r++;
        }
        r--; // Largest power of 2 <= remaining

        int rank = r + 1;
        status[curr_idx] = rank + 16;
        add_to_free_list(rank, curr_idx);

        curr_idx += (1 << r);
        remaining -= (1 << r);
    }

    return OK;
}

void *alloc_pages(int rank) {
    if (rank < 1 || rank > MAX_RANK) return ERR_PTR(-EINVAL);

    int r = rank;
    while (r <= MAX_RANK && !free_lists[r]) {
        r++;
    }

    if (r > MAX_RANK) return ERR_PTR(-ENOSPC);

    // Pop block from free_lists[r]
    FreeBlock *block = free_lists[r];
    int block_idx = ((char *)block - (char *)p_start) / PAGE_SIZE;
    remove_from_free_list(r, block_idx);
    status[block_idx] = 0; // Temporarily 0

    while (r > rank) {
        r--;
        int buddy_idx = block_idx + (1 << (r - 1));
        status[buddy_idx] = r + 16;
        add_to_free_list(r, buddy_idx);
    }

    status[block_idx] = rank;
    return (void *)((char *)p_start + ((size_t)block_idx * PAGE_SIZE));
}

int return_pages(void *p) {
    if (!p || p_start == NULL) return -EINVAL;

    size_t offset = (char *)p - (char *)p_start;
    if (offset % PAGE_SIZE != 0) return -EINVAL;

    int i = offset / PAGE_SIZE;
    if (i < 0 || i >= total_pages || status[i] == 0 || status[i] > MAX_RANK) {
        return -EINVAL;
    }

    int r = status[i];
    status[i] = 0;

    while (r < MAX_RANK) {
        int buddy_idx = i ^ (1 << (r - 1));
        if (buddy_idx < 0 || buddy_idx >= total_pages || status[buddy_idx] != r + 16) {
            break;
        }

        remove_from_free_list(r, buddy_idx);
        status[buddy_idx] = 0;
        i = i & ~(1 << (r - 1));
        r++;
    }

    status[i] = r + 16;
    add_to_free_list(r, i);

    return OK;
}

int query_ranks(void *p) {
    if (!p || p_start == NULL) return -EINVAL;

    size_t offset = (char *)p - (char *)p_start;
    if (offset % PAGE_SIZE != 0) return -EINVAL;

    int i = offset / PAGE_SIZE;
    if (i < 0 || i >= total_pages) return -EINVAL;

    if (status[i] >= 1 && status[i] <= MAX_RANK) {
        return status[i];
    }

    for (int r = MAX_RANK; r >= 1; r--) {
        int j = i & ~((1 << (r - 1)) - 1);
        if (j >= 0 && j < total_pages && status[j] == r + 16) {
            return r;
        }
    }

    return -EINVAL; // Should not happen if p is within range
}

int query_page_counts(int rank) {
    if (rank < 1 || rank > MAX_RANK) return -EINVAL;
    return free_counts[rank];
}
