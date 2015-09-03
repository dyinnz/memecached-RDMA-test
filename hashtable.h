/*
 * Author: Dyinnz.HUST
 * GitHub: https://github.com/dyinnz
 * Description: a simple hash table for rdma
 */

#pragma once

/* the item in hash table */
typedef struct hash_item_t {
    struct hash_item_t *next;
    int32_t key;
    void *p;
} hash_item_s;

/* the struct of hash table */
typedef struct hashtable_t {
    size_t size;
    hash_item_s *T;
} hashtable_s;

/***************************************************************************//**
 * Create a empty hashtable
 *
 * @param[in] size  the size of hashtable
 * @return          the pointer to hashtable
 *
 ******************************************************************************/
hashtable_s* hashtable_create(size_t size);

/***************************************************************************//**
 * Calculate the hash value for a given key
 *
 * @param[in] h     the pointer to hashtable
 * @param[in] key   calc hash value by using the key
 * @return          the index in the array of hash table 
 *
 ******************************************************************************/
size_t calc_hash(hashtable_s *h, int32_t key);

/***************************************************************************//**
 * Free the hashtable 
 *
 * @param[in] h     the pointer to hashtable
 *
 ******************************************************************************/
void hashtable_free(hashtable_s *h);

/***************************************************************************//**
 * Insert a key and a void pointer to hash table, allowing duplicate key.
 * Dose it allow duplicate item ?
 *
 * @param[in] h     the pointer to hashtable
 * @param[in] key   the key to dentify the item
 * @return          0 on success, -1 on failure
 *
 ******************************************************************************/
int hashtable_insert(hashtable_s *h, int32_t key, void *p);

/***************************************************************************//**
 * Search a item in hashtable 
 *
 * @param[in] h     the pointer to hashtable
 * @param[in] key   the key to dentify the item
 * @return          the void pointer store in hashtable
 *
 ******************************************************************************/
void *hashtable_search(hashtable_s *h, int32_t key);


/***************************************************************************//**
 * Delete a item in hashtable 
 *
 * @param[in] h     the pointer to hashtable
 * @param[in] key   the key to dentify the item
 *
 ******************************************************************************/
void hashtable_delete(hashtable_s *h, int32_t key);

