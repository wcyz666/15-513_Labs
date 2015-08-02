#include "cache.h"

/*
 * findItemInCache - find cache using port, host and filename
 *    if these three indices match, the stored object is returned 
 *    and size/type are set. Otherwise, NULL is returned. 
 */

char* findItemInCache(char* port, char* host, 
    char* filename, int* intPtr, char* type) {
    
    CacheItem* ptr = NULL;
    char* content = NULL;

    /* 
     * When accessing the cache, we must lock it using read lock 
     * to avoid race condition 
     */
    pthread_rwlock_rdlock(&rwMutex);
    
    /* 
     * When cache hit, the atime of that cache item should be updated. We 
     * acquire the second mutex to ensure that atime will hold the lastest
     * time.
     *
     * Since we need to access the content when sending object back, instead
     * of directly returning the object pointer back, we malloc a new memory
     * area, copy the object into this area and return it.
     * 
     * This char array will be freed after serving content.
     */
    for (ptr = proxyCache.head; ptr; ptr = ptr->next) {
        if ((!strcasecmp(port, ptr->port)) && (!strcasecmp(host, ptr->host))
            && (!strcasecmp(filename, ptr->filename))) {
            
            P(&acMutex);
                ptr->atime = getTime();
            V(&acMutex);
        
            content = (char*)Malloc(ptr->size);
            type = (char*)Malloc(strlen(ptr->type) + 1);
            memcpy(content, ptr->object, ptr->size);
            strcpy(type, ptr->type);
            type[strlen(ptr->type)] = '\0';
            *intPtr = ptr->size;
            break;
        }
    }

    pthread_rwlock_unlock(&rwMutex);

	return content;
}

/* 
 * evictFromCache - traverse the list and evict the item with the least
 *        access time from the cache list. Evicted item will be freed.
 */

void evictFromCache() {

    CacheItem* evicted = NULL,*ptr = NULL;
    int size = 0;
    unsigned long minAtime = ~0L;

    printf("Cache evicted\n");
    if (proxyCache.tail == NULL)
        return;

    /* find the item with the lease atime */
    for (ptr = proxyCache.head; ptr; ptr = ptr->next) {
        if (ptr->atime < minAtime){
            minAtime = ptr->atime;
            evicted = ptr;
        }
    }

    if (evicted == NULL)
        return;

    size = evicted->size;
    proxyCache.remainSpace += size;

    /* remove this item from cache list */
    if (proxyCache.head == proxyCache.tail) {
        proxyCache.head = proxyCache.tail = NULL;
    }
    else if (proxyCache.tail == evicted){
        proxyCache.tail = evicted->prev;
        proxyCache.tail->next = NULL;
    }
    else if (proxyCache.head == evicted){
        proxyCache.head = evicted->next;
        proxyCache.head->prev = NULL;
    }
    else {
        evicted->prev->next = evicted->next;
        evicted->next->prev = evicted->prev;
    }
    Free(evicted->object);
    Free(evicted);    
}

/* 
 * addToCache - Add an new item to the cache item list.
 */

void addToCache(char* port, char* host, 
    char* filename, int size, char *content, char* type) {
    
    CacheItem *item;

    /* We create a new item before accessing and locking the cache list */
    if ((item = (CacheItem *)malloc(sizeof(CacheItem))) == NULL)
        return;
    
    strcpy(item->port, port);
    strcpy(item->host, host);
    strcpy(item->filename, filename);
    strcpy(item->type, type);
    item->size = size;
    if ((item->object = (char*)malloc(size)) == NULL)
        return;
    item->prev = NULL;
    
    memcpy(item->object, content, size);
    
    /* 
     * Since we need to make change on the whole list, we acquire the 
     * write lock.
     */
    pthread_rwlock_wrlock(&rwMutex);

        /* Evict item from cache until we get enough space */
        while (proxyCache.remainSpace < size) {
            evictFromCache();
        }

        item->atime = getTime();

        /* Insert the new cache item to the head of the list */
        proxyCache.remainSpace -= size;
        item->next = proxyCache.head;

        if (proxyCache.head != NULL)
            proxyCache.head->prev = item;
        
        proxyCache.head = item;
        if (proxyCache.tail == NULL)
            proxyCache.tail = item;
        
    pthread_rwlock_unlock(&rwMutex);
}

/* 
 * getTime - get the current time in mini-second.
 */

unsigned long getTime() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
} 
