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

#define USE_UDP
//#define USE_TCP
static pthread_mutex_t g_mutex;
static int working = 1;
static struct timespec wait = {3, 0};

static char* buf[MAX_NET_BUF + sizeof(NET_PACK_INFO)];
NET_PACK* netPackCache = (NET_PACK*)buf;
static struct sockaddr_in server;
static sem_t sema;
static struct timespec time_stamp;

static char* net_ip;
static int net_port;

static int udpSock = -1;
static int tcpSock = -1;

static struct sockaddr_in send_sin;

static int init_udp_socket()
{
#ifdef USE_TCP
    return 1;
#endif
    //create a UDP socket
    if ((udpSock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        TRACE_ERR("socket error: %s, %d\n", strerror(errno), errno);
        return 0;
    }

    send_sin.sin_family = AF_INET;
    send_sin.sin_port = htons(net_port);
    if (inet_aton(net_ip, &send_sin.sin_addr) == 0)
    {
        close(udpSock);
        udpSock = -1;
        TRACE_ERR("inet_aton error: %s, %d\n", strerror(errno), errno);
        return 0;
    }
    return 1;
}

static void resetTcpSocket(const char* fn, int line)
{
    if (tcpSock != -1) {
        TRACE_INFO("[%s, %d] resetTcpSocket %d", fn, line, tcpSock);
        close(tcpSock);
        tcpSock = -1;
    }
    sem_post(&sema);
}

static void lock(const char* coller, int call_line)
{
    TRACE(" -> %s %d \n", coller, call_line);
    pthread_mutex_lock( &g_mutex );
}

static void unlock(const char* coller, int call_line)
{
    TRACE(" <- %s %d \n", coller, call_line);
    pthread_mutex_unlock( &g_mutex );
}

static void udp_send_pack( NET_PACK* pack )
{
#ifdef USE_TCP
    return;
#endif
    if (udpSock < 0 || pack->info.data_len == 0)
        return;
    struct timespec time_stamp;
    clock_gettime( CLOCK_REALTIME, &time_stamp );
    pack->info.term_sec = time_stamp.tv_sec;
    pack->info.term_msec = time_stamp.tv_nsec / 1000000;
    if (sendto(udpSock, pack, pack->info.data_len + sizeof(NET_PACK_INFO), 0, (struct sockaddr*) &send_sin, sizeof(send_sin)) < 0)
    {
        close(udpSock);
        udpSock = -1;
        TRACE_ERR("sendto error: %s, %d\n", strerror(errno), errno);
    } else {
        pack->info.data_len = 0;
    }
}

static int tcp_send_pack( )
{
    if (tcpSock == -1 || netPackCache->info.data_len == 0)
    {
        return netPackCache->info.data_len;
    }
    TRACE(" Sending %d bytes from thread %d\n", netPackCache->info.data_len + sizeof(NET_PACK_INFO), pthread_self());
    clock_gettime( CLOCK_REALTIME, &time_stamp );
    netPackCache->info.term_sec = time_stamp.tv_sec;
    netPackCache->info.term_msec = time_stamp.tv_nsec / 1000000;
    if( send(tcpSock , netPackCache , netPackCache->info.data_len + sizeof(NET_PACK_INFO), 0) < 0)
    {
        TRACE_ERR("Send failed. error: %s, %d", strerror(errno), errno);
        resetTcpSocket(__FUNCTION__, __LINE__);
    } else {
        netPackCache->info.data_len = 0;
    }
    return netPackCache->info.data_len;
}

void net_send_pack( NET_PACK* pack )
{
#ifdef USE_UDP
    udp_send_pack(pack);
    return;
#endif
    LOG_REC* rec  = (LOG_REC*)(pack->data);
    lock(__FUNCTION__, __LINE__);
    if (rec->len + netPackCache->info.data_len >= MAX_NET_BUF)
    {
        if (0 != tcp_send_pack())
            udp_send_pack(netPackCache);
    }
    if (rec->len + netPackCache->info.data_len < MAX_NET_BUF) {
        memcpy(netPackCache->data + netPackCache->info.data_len, rec, rec->len);
        netPackCache->info.data_len += rec->len;
    }
    if (rec->len + netPackCache->info.data_len >= MAX_NET_BUF)
        TRACE_ERR("Buffer is full %d %d", MAX_NET_BUF - (rec->len + netPackCache->info.data_len), tcpSock);

    unlock(__FUNCTION__, __LINE__);
    sem_post(&sema);
}

static void send_cahced_pack ()
{
    lock(__FUNCTION__, __LINE__);
    if (0 != tcp_send_pack())
        udp_send_pack(netPackCache);
    unlock(__FUNCTION__, __LINE__);
}

static void* send_thread (void *arg)
{
#ifdef USE_UDP
    return 0;
#endif
    TRACE("started send thread\n");

    signal(SIGPIPE, SIG_IGN);

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
                TRACE_ERR("Could not create socket\n");
                break;
            }
            if (connect(tcpSock , (struct sockaddr *)&server , sizeof(server)) < 0)
            {
                TRACE_ERR("Could not connect to %s:%d", net_ip, net_port);
                resetTcpSocket(__FUNCTION__, __LINE__);
                send_cahced_pack();
                continue;
            }
        }

        sem_wait(&sema);
        send_cahced_pack();
    }

    TRACE("exit send thread\n");
    return 0;
}

int init_sender(char* p_ip, int p_port)
{
    net_ip = strdup(p_ip);
    net_port = p_port;
    sem_init(&sema, 0,0);

    pthread_mutex_init( &g_mutex, NULL );
    pthread_t threadId;
    int udp_ok = init_udp_socket();
    int tcp_ok = (pthread_create(&threadId, NULL, &send_thread, NULL) == 0);
    if ( !tcp_ok )
    {
        TRACE_ERR("Error creating thread for tcp receive\n");
        return 0;
    }
    return udp_ok || tcp_ok;
}

static void exit_udp_trace(void) __attribute__((destructor));
static void exit_udp_trace(void) __attribute__((no_instrument_function));
static void exit_udp_trace(void)
{
    working = 0;
    resetTcpSocket(__FUNCTION__, __LINE__);
    sem_post(&sema);
    sem_destroy(&sema);
}
