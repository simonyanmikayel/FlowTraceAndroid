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

//#define USE_TCP

static pthread_mutex_t g_mutex;
static int working = 1;
static struct timespec wait = {3, 0};
static char buf[MAX_NET_BUF + sizeof(NET_PACK_INFO)];
NET_PACK *netPackCache = (NET_PACK *) buf;
static struct timespec time_stamp;
static char *net_ip;
static int net_port;
static int PACK_NN = 0;
static int REC_NN = 0;

static inline void lock() {
    pthread_mutex_lock(&g_mutex);
}

static inline void unlock() {
    pthread_mutex_unlock(&g_mutex);
}

#ifdef USE_TCP

static struct sockaddr_in server;
static sem_t sema;
static int tcpSock = -1;

static void resetTcpSocket(const char *fn, int line) {
    if (tcpSock != -1) {
        TRACE_INFO("Flow trace: [%s, %d] Reseting Tcp Socket %d", fn, line, tcpSock);
        close(tcpSock);
        tcpSock = -1;
    }
    sem_post(&sema);
}

static void tcp_send_cashed_buf() {

    if (tcpSock != -1 && netPackCache->info.data_len != 0) {
        clock_gettime(CLOCK_REALTIME, &time_stamp);
        netPackCache->info.term_sec = time_stamp.tv_sec;
        netPackCache->info.term_msec = time_stamp.tv_nsec / 1000000;
        if (send(tcpSock, netPackCache, netPackCache->info.data_len + sizeof(NET_PACK_INFO), 0) < 0) {
            TRACE_ERR("Flow trace: Send failed with error: %s, %d", strerror(errno), errno);
            resetTcpSocket(__FUNCTION__, __LINE__);
        } else {
            netPackCache->info.data_len = 0;
        }
    }
}

static void *tcp_send_thread(void *arg) {
    TRACE("started send thread\n");

    signal(SIGPIPE, SIG_IGN);

    server.sin_addr.s_addr = inet_addr(net_ip);
    server.sin_family = AF_INET;
    server.sin_port = htons(net_port);

    while (working) {
        if (tcpSock == -1) {
            tcpSock = socket(AF_INET, SOCK_STREAM, 0);
            if (tcpSock == -1) {
                TRACE_ERR("Flow trace: Could not create socket\n");
                break;
            }
            if (connect(tcpSock, (struct sockaddr *) &server, sizeof(server)) < 0) {
                TRACE_ERR("Flow trace: Could not connect to %s:%d", net_ip, net_port);
                resetTcpSocket(__FUNCTION__, __LINE__);
            } else {
            }
        }

        sem_wait(&sema);
        lock();
        tcp_send_cashed_buf();
        unlock();
    }

    TRACE("exit send thread\n");
    return 0;
}

static void exit_udp_trace(void) __attribute__((destructor));

static void exit_udp_trace(void) __attribute__((no_instrument_function));

#else //not USE_TCP

static void exit_udp_trace(void) {
    working = 0;
#ifdef USE_TCP
    resetTcpSocket(__FUNCTION__, __LINE__);
    sem_post(&sema);
    sem_destroy(&sema);
#endif
}

static struct sockaddr_in send_sin;
static int udpSock = -1;
static int blocking = 0;
static int max_retry = 3;
static int cur_max_retry = 3;
static int init_udp_socket()
{
    //create a UDP socket
    if ((udpSock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        TRACE_ERR("Flow trace: socket error: %s, %d\n", strerror(errno), errno);
        return 0;
    }

//    int sockfd, sendbuff = 1; // Set buffer size
//    setsockopt(udpSock, SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff));

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000; //microseconds
    if (setsockopt(udpSock, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
        TRACE_ERR("Flow trace: faile to set receive timeout: %s, %d\n", strerror(errno), errno);
        blocking = 1;
    }

    send_sin.sin_family = AF_INET;
    send_sin.sin_port = htons(net_port);
    if (inet_aton(net_ip, &send_sin.sin_addr) == 0)
    {
        close(udpSock);
        udpSock = -1;
        TRACE_ERR("Flow trace: inet_aton error: %s, %d\n", strerror(errno), errno);
        return 0;
    }
    return 1;
}

static void udp_send_cashed_buf( )
{
    if (udpSock < 0 || netPackCache->info.data_len == 0)
        return;
    struct timespec time_stamp;
    clock_gettime( CLOCK_REALTIME, &time_stamp );
    netPackCache->info.pack_nn = ++PACK_NN;
    netPackCache->info.retry_nn = 0;
    send_again:
    if (sendto(udpSock, netPackCache, netPackCache->info.data_len + sizeof(NET_PACK_INFO), 0, (struct sockaddr*) &send_sin, sizeof(send_sin)) < 0)
    {
        close(udpSock);
        udpSock = -1;
        TRACE_ERR("Flow trace: sendto error: %s, %d\n", strerror(errno), errno);
    } else {
        if (!blocking) {
            NET_PACK_INFO ack;
            int cb;
            recv_again:
            cb = recvfrom(udpSock, &ack, sizeof(ack), (cur_max_retry == 0 ? MSG_DONTWAIT : 0), (struct sockaddr*)0, 0);
            if (cb != sizeof(ack)) {
                TRACE_INFO("Flow trace: no ack received (cur_max_retry=%d): %s, %d\n", cur_max_retry, strerror(errno), errno);
                if (netPackCache->info.retry_nn < cur_max_retry) {
                    netPackCache->info.retry_nn++;
                    goto send_again;
                } else if (cur_max_retry) {
                    cur_max_retry--;
                }
            } else {
                cur_max_retry = max_retry;
                if (ack.pack_nn != netPackCache->info.pack_nn) {
                    TRACE_INFO("Flow trace: received old ack: %d != %d\n", ack.pack_nn, netPackCache->info.pack_nn);
                    goto recv_again;
                }
            }
        }
        netPackCache->info.data_len = 0;
    }
}

static void *udp_send_thread(void *arg) {
    TRACE("started send thread\n");

    while (working) {
        sleep(1);
        lock();
        udp_send_cashed_buf();
        unlock();
    }

    TRACE("exit send thread\n");
    return 0;
}
#endif

void net_send_pack(NET_PACK *pack) {
    LOG_REC *rec = (LOG_REC *) (pack->data);
#ifdef USE_TCP

    int trysend = 1;
    static const int maxRetry = 100;
    static connetRety = 0;
    while(trysend && connetRety < maxRetry) {
        while (connetRety < 100 && (rec->len + netPackCache->info.data_len >= MAX_NET_BUF)) {
            //tru to evacuate cache
            lock();
            tcp_send_cashed_buf();
            trysend = (rec->len + netPackCache->info.data_len >= MAX_NET_BUF);
            unlock();
            if (trysend) {
                connetRety++;
                usleep(10000);
            }
        }

        lock();
        if (rec->len + netPackCache->info.data_len < MAX_NET_BUF) {
            memcpy(netPackCache->data + netPackCache->info.data_len, rec, rec->len);
            netPackCache->info.data_len += rec->len;
            trysend = 0;
            sem_post(&sema);
        }
        unlock();

        if (connetRety >= maxRetry) {
            sem_post(&sema);
            TRACE_ERR("Flow trace: TCP Send faild\n");
        }
    }

#else
    lock();
    rec->nn = ++REC_NN;
    if (rec->len + netPackCache->info.data_len >= MAX_NET_BUF) {
        //tru to evacuate cache
        udp_send_cashed_buf();
    }

    if (rec->len + netPackCache->info.data_len < MAX_NET_BUF) {
        memcpy(netPackCache->data + netPackCache->info.data_len, rec, rec->len);
        netPackCache->info.data_len += rec->len;
    } else {
        TRACE_ERR("Flow trace: UDP Send faild. Rec len %d, cached %d\n", rec->len, netPackCache->info.data_len);
    }
    unlock();
#endif
}

//#define _TEST_THREAD
#ifdef _TEST_THREAD
static void* test_send_thread (void *arg) {
    int i = 0;
    agsim:
    FlowTraceSendTrace(6, LOG_FLAG_JAVA, __FILE__, -1, 0, __LINE__, "test %d\n", ++i);
    //usleep(1000); //microsecond
    goto agsim;
    return 0;
}
#endif

int init_sender(char *p_ip, int p_port) {
    net_ip = strdup(p_ip);
    net_port = p_port;
    int ret = 0;
    pthread_t threadId;
    pthread_mutex_init(&g_mutex, NULL);
#ifdef USE_TCP
    sem_init(&sema, 0, 0);
    ret = (pthread_create(&threadId, NULL, &tcp_send_thread, NULL) == 0);
#else
    ret = init_udp_socket();
    ret = ret && (pthread_create(&threadId, NULL, &udp_send_thread, NULL) == 0);
#endif

    if (!ret) {
        TRACE_ERR("Flow trace: Error in init_sendere\n");
    } else {
#ifdef _TEST_THREAD
        static int thtradStarted = 0;
        if (!thtradStarted) {
            thtradStarted = 1;
            pthread_t threadId;
            pthread_create(&threadId, NULL, &test_send_thread, NULL);
        }
#endif
    }
    return ret;
}

