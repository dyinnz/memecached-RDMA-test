
#include <stdio.h>
#include <stdlib.h>
#include "hashtable.h"

/*******************************************************************************/

size_t calc_hash(hashtable_t *h, int32_t key) {
    return key % h->size;
}

hashtable_t* hashtable_create(size_t size) {
    /* book proper size, if size is too large, just return NULL */
    const static int kPrime[4] = {1543, 3079, 6151, 12289};

    size = size * 3 / 2;
    if (size > kPrime[3]) {
        fprintf(stderr, "the size of hashtable is too large\n");
        return NULL;
    }

    int i = 0;
    for (i = 0; i < 4; ++i) if (size <= kPrime[i]) {
        size = kPrime[i];
        break;
    }

    /* allocate memory */
    hashtable_t *h = calloc(1, sizeof(hashtable_t));
    if (!h) {
        fprintf(stderr, "out of memory in hashtable_create()\n");
        return NULL;
    }

    h->size = size;
    h->T = calloc(h->size, sizeof(hash_item_t));
    if (!h->T) {
        free(h);
        fprintf(stderr, "out of memory in hashtable_create()\n");
        return NULL;
    }
    
    return h;
}

void hashtable_free(hashtable_t *h) {
    if (h) {
        size_t i = 0;
        hash_item_t *iter = NULL;
        hash_item_t *temp = NULL;

        for (i = 0; i < h->size; ++i) {
            iter = h->T[i].next;
            while (iter) {
                temp = iter;
                iter = iter->next;
                free(temp);
            }
        }
        free(h->T);
        free(h);
    }
}

int hashtable_insert(hashtable_t *h, int32_t key, void *p) {
    size_t hashv = calc_hash(h, key);
    hash_item_t *item = h->T + hashv;

    if (!item->p) {
        item->key = key;
        item->p = p;
        return 0;

    } else {
        hash_item_t *new_item = calloc(1, sizeof(hash_item_t));
        if (!new_item) {
            fprintf(stderr, "out of memory in hashtable_insert()\n");
            return -1;
        }

        new_item->key = key;
        new_item->p = p;

        new_item->next = item->next;
        item->next = new_item;
        return 0;
    }
}

void *hashtable_search(hashtable_t *h, int32_t key) {
    size_t hashv = calc_hash(h, key);
    hash_item_t *iter = h->T + hashv;

    while (iter) {
        if (iter->key == key) {
            return iter->p;
        }
        iter = iter->next;
    }
    return NULL;
}

void hashtable_delete(hashtable_t *h, int32_t key) {
    size_t hashv = calc_hash(h, key);
    hash_item_t *item = h->T + hashv;

    if (!item->p) return;
    
    if (key == item->key) {
        if (item->next) {
            hash_item_t *temp = item->next;
            item->next = temp->next;
            item->key = temp->key;
            item->p = temp->p;
            free(temp);

        } else {
            item->key = 0;
            item->p = NULL;
        }

    } else {
        hash_item_t *iter = item;
        while (iter->next) {
            if (iter->next->key == key) {
                hash_item_t *temp = iter->next;
                iter->next = temp->next;
                free(temp);
            }
            iter = iter->next;
        }
    }
}
