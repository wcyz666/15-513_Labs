/* 
 * Name: Wang Cheng
 * CMU ID: chengw1 
 */


#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "csapp.h"
#include "cache.h"
/* Constant defined here */

#define boolean int
#define true 1
#define false 0

/* the number of the digit of port is between 1 and 5, plus the null char */
#define MAXPORT 6

/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

extern pthread_rwlock_t rwMutex;
extern sem_t acMutex;
extern ProxyCache proxyCache;

static boolean isAddtReq(char*);
static int myOpen_clientfd(char *, char *);
static void* serveClient(void *);
static void clienterror(int, char *, char *, char *, char *);
static char* assemHeaders(rio_t*, char*, char*, char*);
static void serveContentByWeb(char*, char*, char*, char*, int);
static void serveContentByCache(char*, int, int, char*);

/*
 * clienterror - returns an error message to the client
 */

static void clienterror(int fd, char *cause, char *errnum,
         char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The proxy server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    rio_writen(fd, buf, strlen(buf));
    rio_writen(fd, body, strlen(body));
}

/*
 * serveClient - worker thread routine
 *     receive a socket as client file descriptor.
 *
 *        It will parse the incoming HTTP headers and search the cache using
 *     corresponding information and decide whether to server the content
 *     by cache directly or by web.
 */

static void* serveClient(void* connfd) {

    char method[MAXLINE] = "\0", uri[MAXLINE] = "\0", version[MAXLINE] = "\0";
    char firstline[MAXLINE], host[MAXLINE] = "\0", buf[MAXLINE] = "\0",
        port[MAXPORT] = "\0", filename[MAXLINE] = "\0";
    rio_t rio;
    int fd = (int)(size_t)connfd, size;
    char* header, *pos, *ciPtr, *type = NULL;

    /* Detach the thread to avoid explicit thread join. */
    pthread_detach(pthread_self());

    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    if (rio_readlineb(&rio, buf, MAXLINE) < 0)
        return NULL;

    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) {
        clienterror(fd, method, "501", "Not Implemented",
                    "Proxy does not forward this method");
        return NULL;
    }

    sprintf(firstline, "%s ", method);

    /* parse uri 
     *    1. eliminate http:// protocol
     *    2. parse filename, port number and host from uri
     *     For example:
     *
     *     1. http://www.cmu.edu
     *          host: www.cmu.edu
     *           filename: /
     *          port: 80
     *        2. www.cmu.edu:8080/index.html
     *          host: www.cmu.edu
     *          port: 8080
     *           filename: /index.html
     *
     *    3. throw exception if illegal header is found
     */

    if (!strncasecmp(uri, "http://", 7)) {
        if ((pos = strchr(uri + 7, '/')) == NULL) {

            strcat(firstline, "/");
            strcpy(host, uri + 7);

            strcat(filename, "/");
        }
        else {
            strcat(firstline, pos);
            strcat(filename, pos);
            *pos = '\0';
            strcpy(host, uri + 7);
        }
        if (strlen(host) == 0) {
            clienterror(fd, uri, "400", "Bad Request",
                    "Proxy can not parse the request");
            return NULL;
        }
    }
    else {
        strcpy(firstline, uri);
        strcpy(filename, uri);
    }

    if ((pos = strchr(host + 6, ':')) == NULL) {
        strcpy(port, "80");
    }
    else {
        *pos = '\0';
        strcpy(port, pos + 1);
    }

    if (strcasecmp(version, "HTTP/1.1") + strcasecmp(version, "HTTP/1.0")
            + strcasecmp(version, "HTTP/0.9") == 1) {
        clienterror(fd, method, "400", "Bad Request",
                    "Proxy can not parse the request");
        return NULL;
    }

    sprintf(firstline, "%s HTTP/1.0\r\n", firstline);
    if ((header = assemHeaders(&rio, firstline, host, port)) == NULL) {
        close(fd);
        return NULL;    
    }
    if (strlen(header) == 0) {
        clienterror(fd, method, "400", "Bad Request",
                    "Invalid Header");
        return NULL;
    }

    /* type and size will be assigned in findItemInCache if cache hit */
    if ((ciPtr = findItemInCache(port, host, filename, &size, type)) == NULL)
        serveContentByWeb(header, host, filename, port, fd);
    else {
        serveContentByCache(ciPtr, fd, size, type);
        Free(ciPtr);
        Free(type);
    }
    close(fd);

    return NULL;
}

/* 
 * serveContentByCache - send web object back using cached object 
 *     the header contains Content-Length and Content-type. 
 */

static void serveContentByCache(char* content, int fd, int size, char* type) 
{
    char buf[MAXBUF] = "\0";
    printf("Cache Hit\n");
    sprintf(buf, "HTTP/1.0 200 OK\r\n");    
    sprintf(buf, "%sConnection: close\r\n%s", buf, type);
    sprintf(buf, "%sContent-length: %d\r\n\r\n", buf, (int)strlen(content));
    rio_writen(fd, buf, strlen(buf));       
    rio_writen(fd, content, size);         
}


/* 
 * serveContentByWeb - When cache miss, we connect to the host and
 *    retrieve the file.
 */

static void serveContentByWeb(char* header, char* host, 
    char* filename, char* port, int fd) {

    int proxyfd = 0, length = -1, count = 0;
    rio_t rio_p;
    char buf[MAXLINE], type[MAXLINE] = "\0";
    char content[MAX_OBJECT_SIZE] = "\0";

    if ((proxyfd = myOpen_clientfd(host, port)) < 0) {
        clienterror(fd, host, "400", "Bad Request",
                    "Proxy can not connect to the specified server");
        return;
    }

    Rio_readinitb(&rio_p, proxyfd);

    if (rio_writen(proxyfd, header, strlen(header)) != (int)strlen(header)) {
        clienterror(fd, "Unknown Error", "500", "Internal Error",
                    "Proxy encountered an critical error.");
        close(proxyfd);
        return;
    }

    Free(header);

    /* received header will be forwarded to the client */
    do {
        if (rio_readlineb(&rio_p, buf, MAXLINE) < 0) {
            close(proxyfd);
            return;
        }

        printf("%s", buf);
        if (strncasecmp(buf, "Content-Length: ", 16) == 0) {
            length = atoi(buf + 16);
        }
        if (strncasecmp(buf, "Content-Type: ", 14) == 0) {
            strcpy(type, buf);
        }
        rio_writen(fd, buf, strlen(buf));
    }
    while(strcmp(buf, "\r\n"));
    
    /* If length is specified in content-length, we check if length 
     * is smaller than Max_Object_Size. If so, we save it to the cache.
     * 
     * If no length is specified, we just read like the length is Max.
     */

    if (length >= 0 && length <= MAX_OBJECT_SIZE) {
        if (rio_readnb(&rio_p, content, length) < 1) {
            close(proxyfd);
            return;
        }

        rio_writen(fd, content, length);
        addToCache(port, host, filename, length, content, type);
    }
    else if (length > MAX_OBJECT_SIZE) {
        while ((count = rio_readnb(&rio_p, content, MAX_OBJECT_SIZE)) != 0)
            rio_writen(fd, content, count);
    }
    else {
        if ((count = rio_readnb(&rio_p, content, MAX_OBJECT_SIZE)) < 0) {
            close(proxyfd);
            return;
        }
        rio_writen(fd, content, count);
        if (count < MAX_OBJECT_SIZE) {
            addToCache(port, host, filename, length, content, type);
        }
        else {
            /* Use loop to read */
            while ((count = rio_readnb(&rio_p, content, MAX_OBJECT_SIZE)) > 0)
                rio_writen(fd, content, count);
            if (count < 0) {
                close(proxyfd);
                return;
            }
        }
    }
    
    printf("Received: %d\n", length);
    close(proxyfd);
}


/*
 * assemHeaders - read HTTP request headers
 */

static char* assemHeaders(rio_t *rp, char* firstline, char* host, char* port)
{
    char buf[MAXLINE];
    char* header, *pos;
    int charCount = 0;
    int curSize = MAXBUF;
    int hasHost = false;

    header = (char *)malloc(curSize * sizeof(char));
    memset(header, 0, sizeof(char));
    strcat(header, firstline);
    strcat(header, user_agent_hdr);
    strcat(header, connection_hdr);
    strcat(header, proxy_connection_hdr);
    charCount = strlen(header);
    do {
        if (rio_readlineb(rp, buf, MAXLINE) < 0) {
            Free(header);
            return NULL;
        }
        charCount += strlen(buf);
        if (charCount >= curSize) {
            curSize *= 2;
            header = (char *)realloc(header, curSize * sizeof(char));
        }

        /* 
         * If host is specified in client header, it should override 
         * the previous parsed one.
         */

        if (strncasecmp(buf, "Host: ", 6) == 0) {
            hasHost = true;
            *(strchr(buf, '\r')) = '\0';

            if ((pos = strchr(buf + 6, ':')) == NULL) {
                strcpy(port, "80");
            }
            else {
                *pos = '\0';
                strcpy(port, pos + 1);
            }
            strcpy(host, buf + 6);
            strcat(buf, "\r\n");
        }

        /* Only additional headers could be forwarded. */
        if (isAddtReq(buf)) {
            strcat(header, buf);
        }
        else {
            continue;
        }
    }
    while(strcmp(buf, "\r\n"));

    if (!hasHost) {
        pos = header + (strlen(header) - 2);
        sprintf(pos, "Host: %s\r\n\r\n", host);
    }

    return header;
}

/* 
 * isAddtReq - return true if the header should not be override
 *       by our proxy.
 */

static boolean isAddtReq(char* buf) {

    if (strncasecmp(buf, "Connection: ", 12) == 0)
        return false;

    if (strncasecmp(buf, "Proxy-Connection: ", 18) == 0)
        return false;

    if (strncasecmp(buf, "User-Agent: ", 12) == 0)
        return false;

    return true;
}

int main(int argc, char* argv[])
{
    int listenfd, connfd, clientlen;
    struct sockaddr_in clientaddr;
    int port;
    pthread_t pid;
    
    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    /* ignore the SIGPIPE signal */
    Signal(SIGPIPE, SIG_IGN);

    port = atoi(argv[1]);

    if (port <= 1024 || port >= 65536) {
        fprintf(stderr, "Invalid port number.\n");
        exit(1);
    }

    if ((listenfd = Open_listenfd(argv[1])) < 0) {
        exit(1);
    }
    
    /* Initialize proxyCache and mutex */
    proxyCache.tail = proxyCache.head;
    proxyCache.remainSpace = MAX_CACHE_SIZE;
    Sem_init(&acMutex, 0, 1);
    pthread_rwlock_init(&rwMutex, NULL);

    /* 
     * Waiting for incoming request. If so, create new threads and serve the
     * request.
     */

    clientlen = (int)sizeof(clientaddr);
    for (; ;) {
        connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *)&clientlen);
        
        /* 
         * It should be noted that we pretend connfd to be an address, instead
         * of a number. It will be tranferred to int the worker thread.
         */

        Pthread_create(&pid, NULL, serveClient, (void *)(size_t)(connfd));
    }
    pthread_rwlock_destroy(&rwMutex);
    return 0;
}

/* 
 * myOpen_clientfd - When getaddrinfo fails, it won't exit
 */

static int myOpen_clientfd(char *hostname, char *port) {
    int clientfd;
    struct addrinfo hints, *listp, *p;

    /* Get a list of potential server addresses */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;  /* Open a connection */
    hints.ai_flags = AI_NUMERICSERV;  /* ... using a numeric port arg. */
    hints.ai_flags |= AI_ADDRCONFIG;  /* Recommended for connections */
    if (getaddrinfo(hostname, port, &hints, &listp) < 0) {
        printf("get address info failed\n");
        return -1;
    }

    /* Walk the list for one that we can successfully connect to */
    for (p = listp; p; p = p->ai_next) {
        /* Create a socket descriptor */
        if ((clientfd = 
                socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
            continue; /* Socket failed, try the next */

        /* Connect to the server */
        if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1)
            break; /* Success */
        close(clientfd); /* Connect failed, try another */  
    }
    /* Clean up */
    Freeaddrinfo(listp);
    if (!p) {/* All connects failed */
        return -1;
    }
    else {
        return clientfd;
    }
}


