//
// Created by misha on 9/15/2018.
//

#ifndef FLOWTRACEANDROID_FLOWTRACE_H
#define FLOWTRACEANDROID_FLOWTRACE_H

#define MAX_NET_BUF 1400 // this size of UDP guaranteed to be send as single package
//#define MAX_NET_BUF 8*1024 // max UDP datagam is 65515 Bytes
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
#define LOG_FLAG_OUTER_LOG 32
#define LOG_FLAG_COLOR_PARCED 64

//#define WITH_TRACE
//#define _USE_ADB
#define _CONTROL_SEND_COUNT
//#define _TEST_THREAD
#define PARCE_COLOR

#define TRACE_ERR(fmt, arg...)  { MyAndroidLogWrite(FLOW_LOG_ERROR, __FUNCTION__, __LINE__, fmt, ##arg); }
#define TRACE_INFO(fmt, arg...) { MyAndroidLogWrite(FLOW_LOG_INFO, __FUNCTION__, __LINE__, fmt, ##arg); }
#define TRACE_TEMP(fmt, arg...) { MyAndroidLogWrite(FLOW_LOG_DEBUG, __FUNCTION__, __LINE__, fmt, ##arg); }

#ifdef WITH_TRACE
    #define TRACE(fmt, arg...) { MyAndroidLogWrite(FLOW_LOG_DEBUG, __FUNCTION__, __LINE__, fmt, ##arg); }
#else
    #define TRACE(fmt, arg...) {}
#endif

void MyAndroidLogWrite(int priority, const char *fn_name, int call_line, const char *fmt, ...);
void startTest();

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
    unsigned char log_type;
    unsigned char log_flags;
    unsigned char color;
    unsigned char priority;
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
    int retry_nn;
    int buff_nn;
    int retry_delay;
    int retry_count;
} NET_PACK_INFO;

typedef struct
{
    NET_PACK_INFO info;
    char data[MAX_NET_BUF + 16];
} NET_PACK;

// same as android_LogPriority
typedef enum _flow_LogPriority {
    /** For internal use only.  */
            FLOW_LOG_UNKNOWN = 0,
    /** The default priority, for internal use only.  */
            FLOW_LOG_DEFAULT, /* only for SetMinPriority() */
    /** Verbose logging. Should typically be disabled for a release apk. */
            FLOW_LOG_VERBOSE,
    /** Debug logging. Should typically be disabled for a release apk. */
            FLOW_LOG_DEBUG,
    /** Informational logging. Should typically be disabled for a release apk. */
            FLOW_LOG_INFO,
    /** Warning logging. For use with recoverable failures. */
            FLOW_LOG_WARN,
    /** Error logging. For use with unrecoverable failures. */
            FLOW_LOG_ERROR,
    /** Fatal logging. For use when aborting. */
            FLOW_LOG_FATAL,
    /** For internal use only.  */
            FLOW_LOG_SILENT, /* only for SetMinPriority(); must be last */
} flow_LogPriority;

#pragma pack(pop)

void SendLog(const char* module_name, int cb_module_name, unsigned int  module_base,
             const char* fn_name, int cb_fn_name, int fn_line, int cb_trace,
             char* trace, int call_line, unsigned int this_fn, unsigned int call_site,
             unsigned char log_type, unsigned char flags, unsigned char color, unsigned char priority)  __attribute__((used));

int SendTrace(const char* module_name, int cb_module_name, unsigned int  module_base,
        flow_LogPriority priority, int flags,
        const char* fn_name, int cb_fn_name, int fn_line,
        int call_line, const char *fmt, va_list args)  __attribute__((used));

void HandleLog(const char* module_name, int cb_module_name, unsigned int  module_base,
             const char* fn_name, int cb_fn_name, int fn_line, int cb_trace,
             char* trace, int call_line, unsigned int this_fn, unsigned int call_site,
             unsigned char log_type, unsigned char flags, unsigned char color, unsigned char priority)  __attribute__((used));

int FlowTraceSendTrace(flow_LogPriority priority, int flags, const char* fn_name, int cb_fn_name, int fn_line, int call_line, const char *fmt, ...)  __attribute__((used));
void init_dalvik_hook();
int init_sender(char* ip, int port, int retry_delay, int retry_count);
void net_send( LOG_REC* rec );
void dump_rec( LOG_REC* rec );
void loc_send();
void unloc_send();
void AndroidTrace(const char* trace, flow_LogPriority priority);
#define TRACE_OFFSET(rec) (rec->cb_app_name + rec->cb_module_name + rec->cb_fn_name)

#endif //FLOWTRACEANDROID_FLOWTRACE_H
