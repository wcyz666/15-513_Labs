#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "csapp.h"
/* Constant defined here */

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/*
 * Cache is defined as followed:
 *     Cache header:
 *           [size]: the current available space 
 *           [head, tail]: point to the head and tail and the 
 *            linked list.
 *        Cache item:
 *            double linked list, the recently accessed cache
 *         item will be placed into the head of the list.
 *         The tail of the list will be evicted if the space
 *         is not enough.
 *         [host, port, filename]: used as the index.
 *         [size, type]: used when cache hits. These two parameters
 *         will be sent in response headers.
 *         [object]: store the real web object.
 *         [atime]: access time, used as LRU flag
 *         [prev, next]: used to construct double linked list. 
 */

typedef struct _cacheItem {
    int size;
    unsigned long atime;
    char host[MAXLINE];
    char port[MAXLINE];
    char filename[MAXLINE];
    char type[MAXLINE];
    char* object;
    struct _cacheItem* prev;
    struct _cacheItem* next;
} CacheItem;

typedef struct _proxyCache {
    int remainSpace;
    CacheItem *head;
    CacheItem *tail;
} ProxyCache;

/* Two lock:
 *     RW_lock: this lock allows multiple parallel readers and only one writer.
 *     
 *     acMutex: When there is a cache hit, we must change the atime of the 
 *     item. In order to make this change without race condition, we acquire
 *     the second mutex.
 */

pthread_rwlock_t rwMutex;
sem_t acMutex;
ProxyCache proxyCache;

void addToCache(char*, char*, char*, int, char*, char*);
char* findItemInCache(char*, char*, char*, int*, char*);
void evictFromCache();
unsigned long getTime();

