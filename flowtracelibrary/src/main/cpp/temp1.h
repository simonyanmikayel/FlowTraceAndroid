#if 1
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

pthread_mutex_t g_mutex_send;
sem_t send_sem;

#ifdef _TEST_THREAD
static void* test_send_thread (void *arg) {
    int i = 0;
    TRACE("Flow trace: test_send_thread\n");
    for(;;) {
        FlowTraceSendTrace(6, LOG_FLAG_JAVA, __FILE__, -1, 0, __LINE__, "test %d\n", ++i);
        //usleep(3000); //1000000microsecond =1sec
    }
    return 0;
}
void startTest() {
    static int thtradStarted = 0;
    if (!thtradStarted) {
        thtradStarted = 1;
        pthread_t threadId;
        //pthread_create(&threadId, NULL, &test_send_thread, NULL);
        //pthread_create(&threadId, NULL, &test_send_thread, NULL);
        pthread_create(&threadId, NULL, &test_send_thread, NULL);
    }
}
#endif //_TEST_THREAD

#ifndef _USE_ADB

#define USE_UDP

#ifdef USE_UDP

static int sendIndex = 0;
static int copyIndex = 0;
static const short maxBufIndex = 1000;
NET_PACK packets[maxBufIndex];
static NET_PACK pingPack;

static char *net_ip = "";
static int net_port;
static int working = 1;

static pthread_mutex_t g_mutex_copy;
static pthread_mutex_t g_mutex_pack;
static struct timespec time_stamp;
static struct sockaddr_in send_sin;
static int udpSock = -1;
static int retryDelay = 20*1000;
static int max_retry = 5;
static int connected = 1;
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

    if (retryDelay) {
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = retryDelay; //microseconds
        if (setsockopt(udpSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            TRACE_ERR("Flow trace: faile to set receive timeout: %s, %d\n", strerror(errno), errno);
            stop_udp_trace();
            return 0;
        }
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
            //TRACE("Flow trace: copy buf %d\n", i);
            break;
        } else {
            if (!packets[i].info.full)
                packets[i].info.full = -1; //if it is set in udp_send_pack then keep it
            //TRACE("Flow trace: full buf %d\n", i);
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

static int udp_send_pack(NET_PACK* pack, int ping) {

    if (udpSock < 0) {
        return 0;
    }

    pack->info.retry_nn = 0;
    pack->info.retry_delay = retryDelay;
    pack->info.retry_count = max_retry;

    send_again:
    if (sendto(udpSock, pack, pack->info.data_len + sizeof(NET_PACK_INFO), 0,
               (const struct sockaddr *)&send_sin, sizeof(send_sin)) < 0) {
        TRACE_ERR("Flow trace: sendto error: %s, %d (dest: %s:%d)\n", strerror(errno), errno,
                  net_ip, net_port);
        stop_udp_trace();
        connected = 0;
    } else {
        if (max_retry > 0 && retryDelay > 0) {
            NET_PACK_INFO ack;
            ssize_t cb;
            int flags;
            recv_again:
            flags = 0; //(pack->info.retry_nn < max_retry) ? 0 : MSG_DONTWAIT;
            cb = recvfrom(udpSock, &ack, sizeof(ack), flags, (struct sockaddr *) 0, 0);
            if (cb != sizeof(ack)) {
                TRACE("Flow trace: no ack received [%s, %d] pack:%d retry:%d\n", strerror(errno), errno, pack->info.pack_nn,  pack->info.retry_nn);
                if (pack->info.retry_nn < max_retry) {
                    pack->info.retry_nn++;
                    goto send_again;
                } else {
                    connected = 0;
                }
            } else {
                if (ack.pack_nn < 0) {
                    ack.pack_nn = -ack.pack_nn;
                }
                if (ack.pack_nn == pack->info.pack_nn) {
                    connected = 1;
                }
                if (ack.pack_nn != pack->info.pack_nn || ack.retry_nn != pack->info.retry_nn) {
                    TRACE("Flow trace: received old ack: pack_nn=%d/%d pack:%d retry:%d/%d\n",
                          ack.pack_nn,
                          pack->info.pack_nn, ack.retry_nn, pack->info.retry_nn);
                    goto recv_again;
                }
            }
        } else {
            connected = 1;
        }
    }
    return connected;
}

#define SEND_IN_PROGRES 1
#define SEND_PING 2
#define SEND_SUCSSEED 4
// return int with bits set 0-could not lock, 1-sent iddle package, 3-sent succeed
static int udp_send_cashed_buf(const char* from) {
    int ret = 0;
    if (0 == trylock(&g_mutex_send)) {
        //TRACE("Flow trace: Locked buf %d [from %s connected=%d data_len=%d]\n", i, from, connected, packets[i].info.data_len);
        if ( !connected || packets[sendIndex].info.data_len != 0) {
            lock(&g_mutex_copy);
            int pinging =  (packets[sendIndex].info.data_len == 0);
            if (!pinging) {
                if (packets[sendIndex].info.full <= 0) {
                    packets[sendIndex].info.pack_nn = ++PACK_NN;
                    packets[sendIndex].info.full = sendIndex + 1; //set as full. We put (i+1) for debugging on rececer side
                }
            } else {
                ret |= SEND_PING;
            }
            unlock(&g_mutex_copy);

            //TRACE("Flow trace: sending buf %d [from %s connected=%d pinging=%d data_len=%d]\n", i, from, connected, pinging, packets[i].info.data_len);
            if (udp_send_pack((pinging ? &pingPack : &packets[sendIndex]), pinging)) {
                ret |= SEND_SUCSSEED;
            }

            if (!pinging) {
                lock(&g_mutex_copy);
                if (connected) {
                    //TRACE("Flow trace: sent %d [from %s connected=%d data_len=%d]\n", i, from, connected, packets[i].info.data_len);
                    packets[sendIndex].info.data_len = 0;
                    packets[sendIndex].info.full = 0;
                    sendIndex = nextIndex(sendIndex);
                    if (sendIndex == copyIndex)
                        copyIndex = nextIndex(sendIndex);;
                } else {
                    TRACE_TEMP("Flow trace: could not send. Reseting buffers [from %s pack:%d retry:%d buf_nn: %d]\n",
                               from, packets[sendIndex].info.pack_nn, packets[sendIndex].info.retry_nn, sendIndex);
//                    //memset(packets, 0, sizeof(packets));
//                    for (int k = 0; k < maxBufIndex; k++) {
//                        packets[k].info.data_len = 0;
//                        packets[k].info.full = 0;
//                    }
//                    sendIndex = copyIndex = 0;
//                    //stop_udp_trace();
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
        set_ts(&ts, 1000000000); //1 sec
        sem_timedwait(&send_sem, &ts);
        do {
            //TRACE("Flow trace: try send from udp_send_thread\n");
            sendRes = udp_send_cashed_buf(__FUNCTION__);
        } while(working && (sendRes & SEND_SUCSSEED) && !(sendRes & SEND_PING) && connected);
    }

    TRACE("Flow trace: exit send thread\n");
    return 0;
}

void net_send(LOG_REC* rec) {
    int ok = 0, sendRes = 0, i;

    if (udpSock < 0)
        return;

    lock(&g_mutex_pack);
    rec->nn = ++REC_NN;

    ok = copy_to_cash_buf(rec);
    if ( !ok ) {
        for (i = 0; i < 12 * max_retry + 1; i++) {
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
}



int init_sender(char *p_ip, int p_port, short retry_delay, short retry_count) {
    net_ip = strdup(p_ip);
    net_port = p_port;
    if (retry_count >= 0) {
        retryDelay = retry_delay*1000; //convert milisecunds to microsecunds
        max_retry = retry_count;
    }
    int ret = 0;
    pthread_t threadId;
    pthread_mutex_init(&g_mutex_send, NULL);
    pthread_mutex_init(&g_mutex_copy, NULL);
    pthread_mutex_init(&g_mutex_pack, NULL);
    sem_init(&send_sem, 0, 0);
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

#else //USE_UDP

static char *net_ip = "127.0.0.1"; //adb reverse tcp:8889 tcp:8889
static int net_port;
static sem_t sema;
static pthread_mutex_t g_mutex;
static struct sockaddr_in server;
static int working = 1;
static int waiting = 0;
static int tcpSock = -1;
#define MAX_TCP_BUF 16*1024  //maximum TCP window size in Microsoft Windows 2000 is 17,520 bytes
static char* buf[MAX_TCP_BUF + sizeof(NET_PACK_INFO)];
NET_PACK* netPackCache = (NET_PACK*)buf;

static void lock(const char* coller, int call_line)
{
//    TRACE("Flow trace:  -> %s %d \n", coller, call_line);
    pthread_mutex_lock( &g_mutex );
}

static void unlock(const char* coller, int call_line)
{
//    TRACE("Flow trace:  <- %s %d \n", coller, call_line);
    pthread_mutex_unlock( &g_mutex );
}

static void resetTcpSocket()
{
    if (tcpSock != -1) {
        close(tcpSock);
        tcpSock = -1;
    }
    sem_post(&sema);
}


static void tcp_send_pack( )
{
    if (tcpSock == -1 || netPackCache->info.data_len == 0)
    {
        return;
    }
    TRACE("Flow trace: Sending %d bytes from thread %d\n", netPackCache->info.data_len + sizeof(NET_PACK_INFO), pthread_self());
    netPackCache->info.pack_nn = 0;
    netPackCache->info.retry_nn = 0;
    if( netPackCache->info.data_len && send(tcpSock , netPackCache , netPackCache->info.data_len + sizeof(NET_PACK_INFO), 0) < 0)
    {
        TRACE_ERR("Flow trace: Flow trace: Send failed");
        resetTcpSocket();
    } else {
        netPackCache->info.data_len = 0;
    }

}

static void* tcp_send_thread (void *arg)
{
    server.sin_addr.s_addr = inet_addr(net_ip);
    server.sin_family = AF_INET;
    server.sin_port = htons( net_port );

    while (working)
    {
        if (tcpSock == -1)
        {
            tcpSock = socket(AF_INET , SOCK_STREAM , 0);
            if (tcpSock == -1)
            {
                TRACE_ERR("Flow trace: Could not create socket\n");
                break;
            }
            if (connect(tcpSock , (struct sockaddr *)&server , sizeof(server)) < 0)
            {
                TRACE_ERR("Flow trace: Could not connect to %s:%d", net_ip, net_port);
                resetTcpSocket();
                continue;
            }
        }
        waiting = 1;
        sem_wait(&sema);
        waiting = 0;
        if (working)
        {
            lock(__FUNCTION__, __LINE__);
            tcp_send_pack();
            unlock(__FUNCTION__, __LINE__);
        }
    }

    TRACE("Flow trace: exit send thread\n");
    return 0;
}


void net_send(LOG_REC* rec) {
    lock(__FUNCTION__, __LINE__);
    if (tcpSock == -1 && (rec->len + netPackCache->info.data_len >= MAX_TCP_BUF))
    {
        if (waiting) {
            sem_post(&sema); //trigger connection
        }
    }
    else
    {
        if (rec->len + netPackCache->info.data_len >= MAX_TCP_BUF)
        {
            tcp_send_pack();
        }
        if (rec->len + netPackCache->info.data_len < MAX_TCP_BUF) {
            memcpy(netPackCache->data + netPackCache->info.data_len, rec, rec->len);
            netPackCache->info.data_len += rec->len;
        }
        sem_post(&sema);
    }
    unlock(__FUNCTION__, __LINE__);
}

int init_sender(char *p_ip, int p_port, short retry_delay, short retry_count) {
    //net_ip = strdup(p_ip);
    net_port = p_port;
    sem_init(&sema, 0,0);

    pthread_mutex_init( &g_mutex, NULL );
    pthread_t threadId;
    int tcp_ok = (pthread_create(&threadId, NULL, &tcp_send_thread, NULL) == 0);
    if ( !tcp_ok )
    {
        TRACE_ERR("Flow trace: Error creating thread for tcp sender\n");
        return 0;
    }
    return tcp_ok;
}

#endif //USE_UDP

#else //USE_UDP
int init_sender(char *p_ip, int p_port, short retry_delay, short retry_count) {
    pthread_mutex_init(&g_mutex_send, NULL);
    sem_init(&send_sem, 0, 0);
    return 1;
}
#endif //USE_UDP
endif //0

public static String toString(byte[] bcd, int exponent) {

    StringBuffer sb = new StringBuffer();

    for (int i = 0; i < bcd.length; i++) {
        if (i == bcd.length - exponent)
            sb.append('.');
        sb.append(toString(bcd[i]));
    }

    return sb.toString();
}