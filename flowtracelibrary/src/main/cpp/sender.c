//
// Created by misha on 9/23/2018.
//

#include <string.h>
//#include <cstdio>
#include <syslog.h>
#include <unistd.h>
#include <sys/types.h>
#include <ctype.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include "flowtrace.h"
#include <time.h>
#include <semaphore.h>

static int sock = -1;

#ifdef USE_UDP
static struct sockaddr_in send_sin;

int init_sender(char* ip, int port)
{
    //create a UDP socket
    if ((sock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        TRACE_ERR("socket error: %s, %d\n", strerror(errno), errno);
        return 0;
    }

    send_sin.sin_family = AF_INET;
    send_sin.sin_port = htons(port);
    if (inet_aton(ip, &send_sin.sin_addr) == 0)
    {
        close(sock);
        sock = -1;
        TRACE_ERR("inet_aton error: %s, %d\n", strerror(errno), errno);
        return 0;
    }
    return 1;
}

void send_udp_package( NET_PACK* netPack )
{
    if (sock < 0)
        return;
    struct timespec time_stamp;
    clock_gettime( CLOCK_REALTIME, &time_stamp );
    netPack->info.term_sec = time_stamp.tv_sec;
    netPack->info.term_msec = time_stamp.tv_nsec / 1000000;
    if (sendto(sock, netPack, netPack->info.data_len + sizeof(NET_PACK_INFO), 0, (struct sockaddr*) &send_sin, sizeof(send_sin)) < 0)
    {
        close(sock);
        sock = -1;
        TRACE_ERR("sendto error: %s, %d\n", strerror(errno), errno);
    }
}

#else //USE_UDP
static pthread_mutex_t g_mutex;
static int working = 1;
static struct timespec wait = {3, 0};
#define MAX_NET_BUF 16*1024  //maximum TCP window size in Microsoft Windows 2000 is 17,520 bytes 
static char* buf[MAX_NET_BUF + sizeof(NET_PACK_INFO)];
NET_PACK* netPack = (NET_PACK*)buf;
static struct sockaddr_in server;
static char* srvr_ip;
static int srvr_port;
static sem_t sema;
static struct timespec time_stamp;
static int waiting = 0;

static void reset()
{
    netPack->info.data_len = 0;
    if (sock != -1) {
        close(sock);
        sock = -1;
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

static void send_pack( )
{
    if (sock == -1)
    {
        netPack->info.data_len = 0;
        return;
    }
    TRACE(" Sending %d bytes from thread %d\n", netPack->info.data_len + sizeof(NET_PACK_INFO), pthread_self());
    clock_gettime( CLOCK_REALTIME, &time_stamp );
    netPack->info.term_sec = time_stamp.tv_sec;
    netPack->info.term_msec = time_stamp.tv_nsec / 1000000;
    if( netPack->info.data_len && send(sock , netPack , netPack->info.data_len + sizeof(NET_PACK_INFO), 0) < 0)
    {
        TRACE_ERR("Send failed");
        reset();
    }
    netPack->info.data_len = 0;

}

static void* send_thread (void *arg)
{
    TRACE("started send thread\n");
    while (working)
    {
        if (sock == -1)
        {
            sock = socket(AF_INET , SOCK_STREAM , 0);
            if (sock == -1)
            {
                TRACE_ERR("Could not create socket\n");
                break;
            }
            if (connect(sock , (struct sockaddr *)&server , sizeof(server)) < 0)
            {
                TRACE_ERR("Could not connect to %s:%d", srvr_ip, srvr_port);
                continue;
            }
        }
        waiting = 1;
        sem_wait(&sema);
        waiting = 0;
        lock(__FUNCTION__, __LINE__);
        send_pack();
        unlock(__FUNCTION__, __LINE__);
    }

    TRACE("exit send thread\n");
    return 0;
}

void send_rec( LOG_REC* rec )
{
    if (sock == -1 || rec->len == 0) {
        if (waiting) {
            sem_post(&sema); //trigger connection
        }
        return;
    }
    lock(__FUNCTION__, __LINE__);
    if (rec->len + netPack->info.data_len >= MAX_NET_BUF)
    {
        send_pack();
    }
    memcpy(netPack->data + netPack->info.data_len, rec, rec->len);
    netPack->info.data_len += rec->len;
    sem_post(&sema);
    unlock(__FUNCTION__, __LINE__);
}

int init_sender(char* ip, int port)
{
    sem_init(&sema, 0,0);

    srvr_ip = strdup(ip);
    srvr_port = port;
    server.sin_addr.s_addr = inet_addr(ip);
    server.sin_family = AF_INET;
    server.sin_port = htons( port );

    pthread_mutex_init( &g_mutex, NULL );
    pthread_t threadId;
    if (pthread_create(&threadId, NULL, &send_thread, NULL) != 0)
    {
        TRACE_ERR("Error creating thread\n");
        return 0;
    }
    return 1;
}

static void exit_udp_trace(void) __attribute__((destructor));
static void exit_udp_trace(void) __attribute__((no_instrument_function));
static void exit_udp_trace(void)
{
    working = 0;
    reset();
}

#endif //USE_UDP