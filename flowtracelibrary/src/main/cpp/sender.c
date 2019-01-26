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

#define NET_BUFF

#ifdef NET_BUFF
static int sendIndex = 0;
static int copyIndex = 0;
static const int maxBufIndex = 128;
NET_PACK packets[maxBufIndex];
static NET_PACK pingPack;
#endif

static int working = 1;
static pthread_mutex_t g_mutex_send;
static pthread_mutex_t g_mutex_copy;
static pthread_mutex_t g_mutex_pack;
static struct timespec time_stamp;
static char *net_ip = "";
static int net_port;
static sem_t send_sem;
static struct sockaddr_in send_sin;
static int udpSock = -1;
static const int retryDelay = 200000; //microseconds 999999999
static const int max_retry = 3;
static int connected = 0;
unsigned int REC_NN = 0;
unsigned int PACK_NN = 0;

static inline void set_ts(struct timespec* ts, int microseconds)  {
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_nsec += microseconds;
    ts->tv_sec += ts->tv_nsec / 1000000000;
    ts->tv_nsec %= 1000000000;
}

static inline int lock(pthread_mutex_t* mutex) {
    return pthread_mutex_lock(mutex);
}

static inline int unlock(pthread_mutex_t* mutex) {
    return pthread_mutex_unlock(mutex);
}

static inline int trylock(pthread_mutex_t* mutex) {
    return pthread_mutex_trylock(mutex);
}

static void stop_udp_trace(void) {
    working = 0;
    close(udpSock);
    udpSock = -1;
    sem_post(&send_sem);
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

#ifdef NET_BUFF
static inline int nextIndex(int i) {
    return (i < (maxBufIndex - 1)) ? (i + 1) : 0;
}

static inline int prevIndex(int i) {
    return (i != 0) ? (i - 1) : (maxBufIndex - 1);
}

//return indexof buffer were data have been copied
static int copy_to_cash_buf(LOG_REC *rec) {
    //dump_rec(rec);
    lock(&g_mutex_copy);
    int curIdx = copyIndex;
    int idx = -1;
    for (int j = 0, i = copyIndex; j < maxBufIndex; j++) {
        if (packets[i].info.full == 0 && rec->len + packets[i].info.data_len < MAX_NET_BUF) {
            memcpy(packets[i].data + packets[i].info.data_len, rec, rec->len);
            packets[i].info.data_len += rec->len;
            copyIndex = i;
            idx = i;
            TRACE("Flow trace: copy buf %d\n", i);
            break;
        } else {
            if (!packets[i].info.full)
                packets[i].info.full = 1; //if it is set in udp_send_pack then keep it
            TRACE("Flow trace: full buf %d\n", i);
        }
        i = nextIndex(i);
        if (i == sendIndex)
            break;
    }
    if (idx >= 0) {
        if (idx != curIdx)
            sem_post(&send_sem);
    } else {
        TRACE("Flow trace: could not copy NN %d\n", rec->nn);
    }
    unlock(&g_mutex_copy);
    return idx >= 0;
}
#endif

static int udp_send_pack(NET_PACK* pack) {

    connected = 0;
    if (udpSock < 0) {
        return 0;
    }

    pack->info.pack_nn = ++PACK_NN;
    pack->info.retry_nn = 0;

    send_again:
    if (sendto(udpSock, pack, pack->info.data_len + sizeof(NET_PACK_INFO), 0,
               (const struct sockaddr *)&send_sin, sizeof(send_sin)) < 0) {
        TRACE_ERR("Flow trace: sendto error: %s, %d (dest: %s:%d)\n", strerror(errno), errno, net_ip, net_port);
        stop_udp_trace();
    } else {
        NET_PACK_INFO ack;
        ssize_t cb;
        int flags;
        recv_again:
        flags = 0; //(pack->info.retry_nn < max_retry) ? 0 : MSG_DONTWAIT;
        cb = recvfrom(udpSock, &ack, sizeof(ack), flags, (struct sockaddr *) 0, 0);
        if (cb != sizeof(ack)) {
            TRACE("Flow trace: no ack received index=%d [%s, %d]\n", i, strerror(errno), errno);
            if (pack->info.retry_nn < max_retry) {
                pack->info.retry_nn++;
                goto send_again;
            }
        } else {
            if (ack.pack_nn == pack->info.pack_nn) {
                connected = 1;
            }
            if (ack.pack_nn != pack->info.pack_nn || ack.retry_nn != pack->info.retry_nn) {
                TRACE("Flow trace: received old ack: index=%d pack:%d/%d retry:%d/%d\n", i, ack.pack_nn,
                      packets[i].info.pack_nn, ack.retry_nn, packets[i].info.retry_nn);
                goto recv_again;
            }
        }
    }
    return connected;
}

#ifdef NET_BUFF
#define SEND_IN_PROGRES 1
#define SEND_PING 2
#define SEND_SUCSSEED 4
// return int with bits set 0-could not lock, 1-sent iddle package, 3-sent succeed
static int udp_send_cashed_buf(const char* from) {
    int ret = 0;
    int pinging = 0;
    if (0 == trylock(&g_mutex_send)) {
        const int i = sendIndex;
        //TRACE("Flow trace: Locked buf %d [from %s connected=%d data_len=%d]\n", i, from, connected, packets[i].info.data_len);
        if ( !connected || packets[i].info.data_len != 0) {
            lock(&g_mutex_copy);
            int pinging =  (packets[i].info.data_len == 0);
            if (!pinging) {
                packets[i].info.full = i + 1; //set as full. We put (i+1) for debugging on rececer side
                if (i == copyIndex)
                    copyIndex = nextIndex(i);;
            } else {
                ret |= SEND_PING;
            }
            unlock(&g_mutex_copy);

            TRACE("Flow trace: sending buf %d [from %s connected=%d pinging=%d data_len=%d]\n", i, from, connected, pinging, packets[i].info.data_len);

            if (udp_send_pack(pinging ? &pingPack : &packets[i])) {
                ret |= SEND_SUCSSEED;
            }

            if (!pinging) {
                lock(&g_mutex_copy);
                if (connected) {
                    TRACE("Flow trace: sent %d [from %s connected=%d data_len=%d]\n", i, from, connected, packets[i].info.data_len);
                    packets[i].info.data_len = 0;
                    packets[i].info.full = 0;
                    sendIndex = nextIndex(i);
                } else {
                    TRACE("Flow trace: could not send. Reseting buffers [from %s connected=%d data_len=%d]\n", from, connected, packets[i].info.data_len);
                    //memset(packets, 0, sizeof(packets));
                    for (int k = 0; k < maxBufIndex; k++) {
                        packets[k].info.data_len = 0;
                        packets[k].info.full = 0;
                    }
                    sendIndex = copyIndex = 0;
                    //stop_udp_trace();
                }
                unlock(&g_mutex_copy);
            }
        }
        //TRACE("Flow trace: Unlocked buf %d [from %s connected=%d data_len=%d]\n", i, from, connected, packets[i].info.data_len);
        unlock(&g_mutex_send);
    } else {
        //TRACE("Flow trace: can not lock [from %s connected=%d]\n", from, connected);
        ret |= SEND_IN_PROGRES;
    }
    return ret;
}

static void *udp_send_thread(void *arg) {
    TRACE("Flow trace: started send thread\n");

    static struct timespec ts;
    static NET_PACK iddlePack;
    static int sendRes = 0;
    iddlePack.info.data_len = 0;

    while (working) {
        set_ts(&ts, connected ? 1000000000 : retryDelay); //1 sec
        sem_timedwait(&send_sem, &ts);
        do {
            //TRACE("Flow trace: try send from udp_send_thread\n");
            sendRes = udp_send_cashed_buf(__FUNCTION__);
        } while(working && (sendRes & SEND_SUCSSEED) && !(sendRes & SEND_PING) && connected);
    }

    TRACE("Flow trace: exit send thread\n");
    return 0;
}
#endif

void net_send_pack(NET_PACK *pack) {
#ifdef NET_BUFF
    LOG_REC *rec = (LOG_REC *) (pack->data);
    int ok = 0, sendRes = 0, i;

    if (udpSock < 0)
        return;

    lock(&g_mutex_pack);
    rec->nn = ++REC_NN;

    ok = copy_to_cash_buf(rec);
    if ( !ok ) {
        for (i = 0; i < 12 * max_retry; i++) {
            TRACE("Flow trace: try send from net_send_pack NN %d try %d\n", rec->nn, i);
            sendRes = udp_send_cashed_buf(__FUNCTION__);
            ok = copy_to_cash_buf(rec);
            if (ok || !(sendRes & SEND_IN_PROGRES))
                break;
            usleep(retryDelay/10);
        }
    }
    unlock(&g_mutex_pack);

    if ( !ok ) {
        TRACE("Flow trace: UDP Send faild NN %d\n", rec->nn);
    }
#else
    lock(&g_mutex_send);
    udp_send_pack(pack);
    unlock(&g_mutex_send);
#endif
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
        pthread_create(&threadId, NULL, &test_send_thread, NULL);
        pthread_create(&threadId, NULL, &test_send_thread, NULL);
    }
}
#endif

int init_sender(char *p_ip, int p_port) {
    net_ip = strdup(p_ip);
    net_port = p_port;
    int ret = 0;
    pthread_t threadId;
    pthread_mutex_init(&g_mutex_send, NULL);
    pthread_mutex_init(&g_mutex_copy, NULL);
    pthread_mutex_init(&g_mutex_pack, NULL);
    sem_init(&send_sem, 0, 0);
    ret = init_udp_socket();
#ifdef NET_BUFF
    ret = ret && (pthread_create(&threadId, NULL, &udp_send_thread, NULL) == 0);
#endif
    if (!ret) {
        TRACE_ERR("Flow trace: Error in init_sendere\n");
    } else {
#ifdef _TEST_THREAD
        startTest();
#endif
    }
    return ret;
}

