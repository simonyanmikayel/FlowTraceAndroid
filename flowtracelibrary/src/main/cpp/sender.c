//
// Created by misha on 9/23/2018.
//

#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include "flowtrace.h"
#include <time.h>
#include <semaphore.h>

static pthread_mutex_t g_mutex;
static int working = 1;
static const int maxBufIndex = 32;
static int sendIndex = 0;
static int copyIndex = 0;
static int lockIndex = -1;
NET_PACK packets[maxBufIndex];
static struct timespec time_stamp;
static char *net_ip;
static int net_port;
static int PACK_NN = 0;
static int REC_NN = 0;
static sem_t sema;
static struct sockaddr_in send_sin;
static int udpSock = -1;
static const int retryDelay = 200000; //microseconds
static const int max_retry = 3;
static int idle = 0;

static inline void lock() {
    pthread_mutex_lock(&g_mutex);
}

static inline void unlock() {
    pthread_mutex_unlock(&g_mutex);
}

static void stop_udp_trace(void) {
    working = 0;
    close(udpSock);
    udpSock = -1;
    sem_post(&sema);
}

static int init_udp_socket() {
    //create a UDP socket
    if ((udpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        TRACE_ERR("Flow trace: socket error: %s, %d\n", strerror(errno), errno);
        return 0;
    }

//    int sockfd, sendbuff = 1; // Set buffer size
//    setsockopt(udpSock, SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff));

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = retryDelay; //microseconds
    if (setsockopt(udpSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        TRACE_ERR("Flow trace: faile to set receive timeout: %s, %d\n", strerror(errno), errno);
        stop_udp_trace();
        return 0;
    }

    send_sin.sin_family = AF_INET;
    send_sin.sin_port = htons(net_port);
    if (inet_aton(net_ip, &send_sin.sin_addr) == 0) {
        TRACE_ERR("Flow trace: inet_aton error: %s, %d\n", strerror(errno), errno);
        stop_udp_trace();
        return 0;
    }
    return 1;
}

static inline int nextIndex(int i) {
    return i < (maxBufIndex - 1) ? (i + 1) : 0;
}

static inline int prevIndex(int i) {
    return i != 0 ? (i - 1) : (maxBufIndex - 1);
}

//return indexof buffer were data have been copied
static int copy_to_cash_buf(LOG_REC *rec) {
    if (idle || udpSock < 0) {
        return 1; //return success
    }
    //dump_rec(rec);
    int curIdx = copyIndex;
    int idx = -1;
    for (int j = 0, i = copyIndex; j < maxBufIndex; j++) {
        if (rec->len + packets[i].info.data_len < MAX_NET_BUF && i != lockIndex) {
            memcpy(packets[i].data + packets[i].info.data_len, rec, rec->len);
            packets[i].info.data_len += rec->len;
            copyIndex = i;
            idx = i;
            break;
        }
        i = nextIndex(i);
    }
    if (idx >= 0) {
        if (idx != curIdx)
            sem_post(&sema);
    }
    return idx >= 0;
}

static void udp_send_cashed_buf() {
    const int i = sendIndex;

    if ((!idle && packets[i].info.data_len == 0) || udpSock < 0) {
        //TRACE("Flow trace: No data at index %d\n", i);
        return;
    }

//    TRACE("Flow trace: Sending index %d\n", i);
    packets[i].info.pack_nn = ++PACK_NN;
    packets[i].info.retry_nn = 0;
    lockIndex = i;  //block this buffer
    if (copyIndex == sendIndex)
        copyIndex = nextIndex(i);

    send_again:
    if (sendto(udpSock, &packets[i], packets[i].info.data_len + sizeof(NET_PACK_INFO), 0,
               (const struct sockaddr *)&send_sin, sizeof(send_sin)) < 0) {
        TRACE_ERR("Flow trace: sendto error: %s, %d\n", strerror(errno), errno);
        stop_udp_trace();
    } else {
        NET_PACK_INFO ack;
        ssize_t cb;
        recv_again:
        cb = recvfrom(udpSock, &ack, sizeof(ack), (idle ? MSG_DONTWAIT : 0),
                      (struct sockaddr *) 0, 0);
        if (cb != sizeof(ack)) {
            TRACE("Flow trace: no ack received index=%d [%s, %d]\n", i, strerror(errno), errno);
            if (packets[i].info.retry_nn < max_retry) {
                packets[i].info.retry_nn++;
                goto send_again;
            } else {
                idle = 1;
            }
        } else {
            idle = 0;
            if (ack.pack_nn != packets[i].info.pack_nn || ack.retry_nn != packets[i].info.retry_nn) {
                TRACE("Flow trace: received old ack: index=%d pack:%d/%d retry:%d/%d\n", i, ack.pack_nn,
                      packets[i].info.pack_nn, ack.retry_nn, packets[i].info.retry_nn);
                goto recv_again;
            }
        }
    }

    if (packets[i].info.data_len != 0) {
        TRACE("Flow trace: could not send index %d\n", i);
    }

    if (!idle) {
        packets[i].info.pack_nn = 0;
        packets[i].info.data_len = 0;
        sendIndex = nextIndex(i);
    } else if (idle == 1) {
        TRACE("Flow trace: could not send. Reseting buffers\n");
        //memset(packets, 0, sizeof(packets));
        for (int k = 0; k < maxBufIndex; k++) {
            packets[k].info.pack_nn = 0;
            packets[k].info.data_len = 0;
        }
        sendIndex = copyIndex = 0;
        idle = 2;
        //stop_udp_trace();
    }
    lockIndex = -1;
}

static void *udp_send_thread(void *arg) {
    TRACE("Flow trace: started send thread\n");

    struct timespec ts = {1, 0};

    while (working) {
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 2;
        if (idle) {
            udp_send_cashed_buf();
        } else {
            sem_timedwait(&sema, &ts);
            lock();
            udp_send_cashed_buf();
            unlock();
        }
    }

    TRACE("Flow trace: exit send thread\n");
    return 0;
}

void net_send_pack(NET_PACK *pack) {
    static int NN = 0;
    LOG_REC *rec = (LOG_REC *) (pack->data);
    lock();
    rec->nn = ++NN;
    int ok = copy_to_cash_buf(rec);
    if ( !ok ) {
        udp_send_cashed_buf();
        ok = copy_to_cash_buf(rec);
    }
    if ( !ok ) {
        TRACE("Flow trace: UDP Send faild\n");
    }
    unlock();
}

//#define _TEST_THREAD
#ifdef _TEST_THREAD
static void* test_send_thread (void *arg) {
    int i = 0;
    TRACE("Flow trace: test_send_thread\n");
    for(;;) {
        FlowTraceSendTrace(6, LOG_FLAG_JAVA, __FILE__, -1, 0, __LINE__, "test %d\n", ++i);
        //usleep(1000000); //microsecond
    }
    return 0;
}
static void startTest() {
    static int thtradStarted = 0;
    if (!thtradStarted) {
        thtradStarted = 1;
        pthread_t threadId;
        pthread_create(&threadId, NULL, &test_send_thread, NULL);
    }
}
#endif

int init_sender(char *p_ip, int p_port) {
    net_ip = strdup(p_ip);
    net_port = p_port;
    int ret = 0;
    pthread_t threadId;
    pthread_mutex_init(&g_mutex, NULL);
    sem_init(&sema, 0, 0);
    ret = init_udp_socket();
    ret = ret && (pthread_create(&threadId, NULL, &udp_send_thread, NULL) == 0);

    if (!ret) {
        TRACE_ERR("Flow trace: Error in init_sendere\n");
    } else {
#ifdef _TEST_THREAD
        startTest();
#endif
    }
    return ret;
}

