#include <stdio.h>
#include <stdlib.h>

typedef struct hash_item_t {
    struct hash_item_t *next;

    int32_t key;
    void *p;
} hash_item_s;

typedef struct hashtable_t {

    size_t          size;
    hash_item_s     *T;

} hashtable_s;

size_t calc_hash(hashtable_s *h, int32_t key) {
    return key % h->size;
}

hashtable_s* hashtable_create(size_t size) {
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
    hashtable_s *h = calloc(1, sizeof(hashtable_s));
    if (!h) {
        fprintf(stderr, "out of memory in hashtable_create()\n");
        return NULL;
    }

    h->size = size;
    h->T = calloc(h->size, sizeof(hash_item_s));
    printf("T: %p\n", h->T);
    if (!h->T) {
        fprintf(stderr, "out of memory in hashtable_create()\n");
        return NULL;
    }
    
    return h;
}

void hashtable_free(hashtable_s *h) {
    if (h) {
        size_t i = 0;
        hash_item_s *iter = NULL;
        hash_item_s *temp = NULL;

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

int hashtable_insert(hashtable_s *h, int32_t key, void *p) {
    size_t hashv = calc_hash(h, key);
    hash_item_s *item = h->T + hashv;

    if (!item->p) {
        item->key = key;
        item->p = p;
        return 0;

    } else {
        hash_item_s *new_item = calloc(1, sizeof(hash_item_s));
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

void *hashtable_search(hashtable_s *h, int32_t key) {
    size_t hashv = calc_hash(h, key);
    hash_item_s *iter = h->T + hashv;

    while (iter) {
        if (iter->key == key) {
            return iter->p;
        }
        iter = iter->next;
    }
    return NULL;
}

void hashtable_delete(hashtable_s *h, int32_t key) {
    size_t hashv = calc_hash(h, key);
    hash_item_s *item = h->T + hashv;

    if (!item->p) return;
    
    if (key == item->key) {
        if (item->next) {
            hash_item_s *temp = item->next;
            item->next = temp->next;
            item->key = temp->key;
            item->p = temp->p;
            free(temp);

        } else {
            item->key = 0;
            item->p = NULL;
        }

    } else {
        hash_item_s *iter = item;
        while (iter->next) {
            if (iter->next->key == key) {
                hash_item_s *temp = iter->next;
                iter->next = temp->next;
                free(temp);
            }
            iter = iter->next;
        }
    }
}

int main() {
    hashtable_s *h = hashtable_create(1024);
    int i = 0;
    for (i = 1; i < 20; ++i) {
        hashtable_insert(h, i, (void*)i);
    }

    for (i = 1; i < 20; ++i) {
        printf("%d\n", (int)hashtable_search(h, i));
    }

    for (i = 3; i < 15; i+=3) {
        hashtable_delete(h, i);
    }

    for (i = 1; i < 20; ++i) {
        printf("%d\n", (int)hashtable_search(h, i));
    }

    for (i = 3; i < 15; i+=3) {
        hashtable_insert(h, i, (void*)i);
    }
    for (i = 1; i < 20; ++i) {
        printf("%d\n", (int)hashtable_search(h, i));
    }

    hashtable_free(h);

    return 0;
}
