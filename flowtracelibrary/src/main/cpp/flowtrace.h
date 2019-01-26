//
// Created by misha on 9/15/2018.
//

#ifndef FLOWTRACEANDROID_FLOWTRACE_H
#define FLOWTRACEANDROID_FLOWTRACE_H

//#define MAX_NET_BUF 1400 // this size of UDP guaranteed to be send as single package
#define MAX_NET_BUF 8*1024 // max UDP datagam is 65515 Bytes
#define MAX_APP_PATH_LEN 128
#define MAX_APP_NAME_LEN 128
#define MAX_MODULE_NAME_LEN 128
#define MAX_FUNC_NAME_LEN 532
#define MAX_LOG_LEN 1200

#define EXTRA_BUF 20 // extra bytes for colors (2*8) new lines(2) and terminating null
#define LOG_FLAG_NEW_LINE 1
#define LOG_FLAG_JAVA 2
#define LOG_FLAG_EXCEPTION 4
#define LOG_FLAG_RUNNABLE_INIT 8
#define LOG_FLAG_RUNNABLE_RUN 16

//#define WITH_TRACE

#define TRACE_ERR(fmt, arg...)  { FlowTraceLogWrite(6, __FUNCTION__, __LINE__, fmt, ##arg); }
#define TRACE_INFO(fmt, arg...) { FlowTraceLogWrite(4, __FUNCTION__, __LINE__, fmt, ##arg); }

#ifdef WITH_TRACE
    #define TRACE(fmt, arg...) { FlowTraceLogWrite(4, __FUNCTION__, __LINE__, fmt, ##arg); }
#else
    #define TRACE(fmt, arg...) {}
#endif

void FlowTraceLogWriteV(int severity, const char *fn_name, int call_line, const char *fmt, va_list args);
void FlowTraceLogWrite(int severity, const char *fn_name, int call_line, const char *fmt, ...);

typedef enum {
    UDP_LOG_FATAL,
    UDP_LOG_ERROR,
    UDP_LOG_WARNING,
    UDP_LOG_INFO,
    UDP_LOG_DEBUG,
    UDP_LOG_COMMON,
} UDP_LOG_Severity;

typedef enum {
    LOG_INFO_ENTER,
    LOG_INFO_EXIT,
    LOG_INFO_TRACE,
    LOG_EMPTY_METHOD_ENTER_EXIT,
    LOG_INFO_ENTER_FIRST,
    LOG_INFO_EXIT_LAST,
} LOG_INFO_TYPE;

#pragma pack(push, 4)

typedef struct
{
    int len;
    short log_type;
    short log_flags;
    unsigned int nn;
    short cb_app_name;
    short cb_module_name;
    short cb_fn_name;
    short cb_trace;
    int tid;
    int pid;
    unsigned int sec;
    unsigned int msec;
    unsigned int this_fn;
    unsigned int call_site;
    int fn_line;
    int call_line;
    char data[1];
} LOG_REC;

typedef struct
{
    int data_len;
    int pack_nn;
    short retry_nn;
    short full;
} NET_PACK_INFO;

typedef struct
{
    NET_PACK_INFO info;
    char data[MAX_NET_BUF + 16];
} NET_PACK;

#pragma pack(pop)

void FlowTraceSendLog(const char* module_name, int cb_module_name, unsigned int  module_base,
             const char* fn_name, int cb_fn_name, int fn_line, int cb_trace,
             char* trace, int call_line, unsigned int this_fn,
             unsigned int call_site, short log_type, short flags)  __attribute__((used));

int FlowTraceSendTrace(UDP_LOG_Severity severity, int flags, const char* fn_name, int cb_fn_name, int fn_line, int call_line, const char *fmt, ...)  __attribute__((used));
void init_dalvik_hook();
int init_sender(char* ip, int port);
void net_send_pack( NET_PACK* pack  );
void dump_rec( LOG_REC* rec );
#endif //FLOWTRACEANDROID_FLOWTRACE_H
