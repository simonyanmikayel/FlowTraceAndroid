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

extern char* app_name[];
extern int cb_app_name;
extern int app_pid;

static char *net_ip = "";
static int net_port;
static int working = 1;
static pthread_mutex_t send_mutex;
static sem_t send_sem;
static int retryDelay = 20*1000;
static int max_retry = 5;
static int udpSock = -1;
static struct sockaddr_in send_sin;
static const short maxBufIndex = 1000;
static NET_PACK packets[maxBufIndex];
static int noRespoce = 0;
static unsigned int REC_NN = 0;
static int PACK_NN = 0;
static int sendIndex = 0; 
static int addIndex = 0;
static LOG_REC* last_rec = 0; //if null then curAddPack() have no log rec
static int tid = 0;
static struct timespec time_stamp;

#ifdef _TEST_THREAD
static void* test_send_thread (void *arg) {
    int i;
    TRACE("Flow trace: test_send_thread\n");
    for(i = 0; i < 10000000; i++) {
        FlowTraceSendTrace(6, LOG_FLAG_JAVA, __FILE__, -1, 0, __LINE__, "test %d\n", i);
        //usleep(3000); //1000000microsecond =1sec
    }
    return 0;
}

void startTest() {
    static int thradStarted = 0;
    if (!thradStarted) {
        thradStarted = 1;
        pthread_t threadId;
        pthread_create(&threadId, NULL, &test_send_thread, NULL);
    }
}
#endif //_TEST_THREAD

static inline void set_last_rec(LOG_REC* rec) {
    if (last_rec && last_rec->log_type == LOG_INFO_TRACE) {
        char *trace = last_rec->data + TRACE_OFFSET(last_rec);
        AndroidTrace(trace, last_rec->severity);
    }
    last_rec = rec;
}

static inline int nextIndex(int i) {
    return (i < (maxBufIndex - 1)) ? (i + 1) : 0;
}

static inline NET_PACK* curAddPack() {
        return &packets[addIndex];
}

static inline NET_PACK* curSendPack() {
        return &packets[sendIndex];
}

static inline NET_PACK* nextAddPack() {
    return &packets[nextIndex(addIndex)];
}

static inline NET_PACK* nextSendPack() {
    return &packets[nextIndex(sendIndex)];
}

static inline void purgePack(NET_PACK* pack) {
    if (pack == curAddPack())
        set_last_rec(0);
    pack->info.data_len = 0;
}

static inline void moveSendPack() {
    sendIndex = nextIndex(sendIndex);
}

static inline void moveAddPack() {
    if (nextAddPack() == curSendPack())
        moveSendPack();
    addIndex = nextIndex(addIndex);
    purgePack(curAddPack());
}

//static inline int prevIndex(int i) {
//    return (i != 0) ? (i - 1) : (maxBufIndex - 1);
//}

void loc_send() {
    pthread_mutex_lock(&send_mutex);
}

void unloc_send() {
    pthread_mutex_unlock(&send_mutex);
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

static int send_pack(NET_PACK* pack) {
    int ret = 0;
    if (udpSock < 0) {
        return 0;
    }

    pack->info.retry_nn = 0;
    pack->info.retry_delay = retryDelay;
    pack->info.retry_count = max_retry;

    send_again:
    if (sendto(udpSock, pack, pack->info.data_len + sizeof(NET_PACK_INFO), 0,
               (const struct sockaddr *)&send_sin, sizeof(send_sin)) < 0) {
        TRACE_ERR("Flow trace: sendto error: %s, %d (dest: %s:%d)\n", strerror(errno), errno, net_ip, net_port);
        stop_udp_trace();
        return 0;
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
                }
            } else {
                if (ack.pack_nn < 0) {
                    ack.pack_nn = -ack.pack_nn;
                }
                if (ack.pack_nn == pack->info.pack_nn) {
                    ret = 1;
                }
                if (ack.pack_nn != pack->info.pack_nn || ack.retry_nn != pack->info.retry_nn) {
                    TRACE("Flow trace: received old ack: pack_nn=%d/%d pack:%d retry:%d/%d\n",
                          ack.pack_nn,
                          pack->info.pack_nn, ack.retry_nn, pack->info.retry_nn);
                    goto recv_again;
                }
            }
        } else {
            ret = 1;
        }
    }

    return ret;
}

static void send_cash() {
    again:
    if (curSendPack()->info.data_len) {
        if (send_pack(curSendPack())) {
            noRespoce = 0;
            purgePack(curSendPack());
            if (nextSendPack()->info.data_len) {
                moveSendPack();
                goto again;
            }
        } else {
            noRespoce = 1;
        }
    } else {
        noRespoce = 0;
    }
}

static void *send_thread(void *arg) {
    TRACE("Flow trace: started send thread\n");
//    static struct timespec ts;
//    int iddle_timeout = 1000000000; //1 sec
//    int ping_timeout =  1000000000; //100 msec
    while (working) {
//        clock_gettime(CLOCK_REALTIME, &ts);
//        ts.tv_nsec += noRespoce ? ping_timeout : iddle_timeout;
//        ts.tv_sec += ts.tv_nsec / 1000000000;
//        ts.tv_nsec %= 1000000000;
//        sem_timedwait(&send_sem, &ts);
        sem_wait(&send_sem);

        if (working) {
            loc_send();
            //TRACE_TEMP("Flow trace: try send from send_thread\n");
            send_cash();
            unloc_send();
        }
    }
    TRACE("Flow trace: exit send thread\n");
    return 0;
}

static void add_trace(LOG_REC* rec, char* trace, int cb_trace, unsigned char flags, unsigned char severity) {
    int len = 0;
    int cur_len = (last_rec == rec) ? last_rec->len : 0;
    int cur_trace_end = rec->cb_app_name + rec->cb_module_name + rec->cb_fn_name + rec->cb_trace;
    if (cb_trace) {
        if ( cb_trace > (MAX_NET_BUF - cur_trace_end - sizeof(LOG_REC))) {
            cb_trace -= (MAX_NET_BUF - cur_trace_end - sizeof(LOG_REC));
        }
        memcpy(rec->data + cur_trace_end, trace, cb_trace);
    }
    rec->cb_trace += cb_trace;
    rec->data[cur_trace_end + cb_trace] = 0;
    len = sizeof(LOG_REC) + cur_trace_end + cb_trace + 1;
    // make sure that length is 4-byte aligned
    if (len & 0x3)
        len = ((len / 4) * 4) + 4; //len = ((len >> 2) << 2) + 4;
    rec->len = len;
    curAddPack()->info.data_len += (rec->len - cur_len);

    if (rec->severity < severity)
        rec->severity = severity;
}

static LOG_REC* add_rec(const char* module_name, int cb_module_name, unsigned int  module_base,
                  const char* fn_name, int cb_fn_name, int fn_line, int cb_trace,
                  char* trace, int call_line, unsigned int this_fn, unsigned int call_site,
                  unsigned char log_type, unsigned char flags, unsigned char severity) {
    LOG_REC* rec = 0;
    int rec_len = sizeof(LOG_REC) + cb_app_name + cb_module_name + cb_fn_name + cb_trace;
    if (last_rec) {
        if (curAddPack()->info.data_len +  rec_len > MAX_NET_BUF)
            return 0;
        rec = (LOG_REC* )(curAddPack()->data + curAddPack()->info.data_len);
    } else {
        rec = (LOG_REC*)(curAddPack()->data);
    }

    rec->nn = ++REC_NN;
    rec->log_type = log_type;
    rec->log_flags = flags;
    rec->severity = severity;
    rec->tid = tid;
    rec->pid = app_pid;

    clock_gettime( CLOCK_REALTIME, &time_stamp );
    rec->sec = (unsigned int)time_stamp.tv_sec;
    rec->msec = (unsigned int)(time_stamp.tv_nsec / 1000000);
    rec->cb_app_name = (short)cb_app_name;
    rec->cb_module_name = (short)cb_module_name;
    rec->cb_fn_name = (short)cb_fn_name;
    rec->this_fn = this_fn - module_base;
    rec->call_site = call_site - module_base;
    rec->fn_line = fn_line;
    rec->call_line = call_line;

    if (cb_app_name)
        memcpy(rec->data, app_name, (size_t)cb_app_name);
    if (cb_module_name)
        memcpy(rec->data + cb_app_name, module_name, (size_t)cb_module_name);
    if (cb_fn_name)
        memcpy(rec->data + cb_app_name + cb_module_name, fn_name, (size_t)cb_fn_name);

    rec->cb_trace = 0;
    add_trace(rec, trace, cb_trace, flags, severity);

    if (!last_rec) { //new packet
        curAddPack()->info.pack_nn = ++PACK_NN;
        curAddPack()->info.buff_nn = addIndex;
    }
    set_last_rec(rec);
    sem_post(&send_sem);

    return rec;
}
#define ADD_REC() add_rec(module_name, cb_module_name, module_base, fn_name, cb_fn_name, fn_line, cb_trace, trace, call_line, this_fn, call_site, log_type, flags, severity)

static LOG_REC* add_log(const char* module_name, int cb_module_name, unsigned int  module_base,
              const char* fn_name, int cb_fn_name, int fn_line, int cb_trace,
              char* trace, int call_line, unsigned int this_fn, unsigned int call_site,
              unsigned char log_type, unsigned char flags, unsigned char severity)
{
    LOG_REC* rec = 0;
    tid = (int)pthread_self();

    if (!fn_name)
        cb_fn_name = 0;
    else if (cb_fn_name <= 0)
        cb_fn_name = (int)strlen(fn_name);

    if (cb_fn_name > MAX_FUNC_NAME_LEN)
        cb_fn_name = MAX_FUNC_NAME_LEN;
    if (cb_module_name > MAX_MODULE_NAME_LEN)
        cb_module_name = MAX_MODULE_NAME_LEN;

    if (last_rec) {
        if ( log_type == LOG_INFO_TRACE && last_rec->log_type == LOG_INFO_TRACE && tid == last_rec->tid && last_rec->call_line == call_line && curAddPack()->info.data_len + cb_trace < MAX_NET_BUF ) {
            add_trace(last_rec, trace, cb_trace, flags, severity);
            rec = last_rec;
        } else {
            rec = ADD_REC();
        }
    } else {
        rec = ADD_REC();
    }

    return rec;
}
#define ADD_LOG() add_log(module_name, cb_module_name, module_base, fn_name, cb_fn_name, fn_line, cb_trace, trace, call_line, this_fn, call_site, log_type, flags, severity)

void HandleLog(const char* module_name, int cb_module_name, unsigned int  module_base,
             const char* fn_name, int cb_fn_name, int fn_line, int cb_trace,
             char* trace, int call_line, unsigned int this_fn, unsigned int call_site,
             unsigned char log_type, unsigned char flags, unsigned char severity)
{
    if (!ADD_LOG()) {
        //curAddPack() is full, add to next
        if (nextAddPack()->info.data_len != 0 && !noRespoce) {
            send_cash();
        }
        moveAddPack();
        ADD_LOG();
    }
}

int init_sender(char *p_ip, int p_port, short retry_delay, short retry_count) {
    net_ip = strdup(p_ip);
    net_port = p_port;
    if (retry_count >= 0 && retry_delay >= 0) {
        retryDelay = retry_delay*1000; //convert milisecunds to microsecunds
        max_retry = retry_count;
    }
    int ret = 1;
    pthread_t threadId;
    pthread_mutex_init(&send_mutex, NULL);
    sem_init(&send_sem, 0, 0);
    ret = ret && init_udp_socket();
    ret = ret && (pthread_create(&threadId, NULL, &send_thread, NULL) == 0);
    if (!ret) {
        TRACE_ERR("Flow trace: Error in init_sendere\n");
    } else {
#ifdef _TEST_THREAD
        startTest();
#endif
    }
    return ret;
}
