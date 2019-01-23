//
// Created by misha on 9/15/2018.
//

#include <string.h>
//#include <cstdio>
#include <jni.h>
#include <android/log.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/types.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include "flowtrace.h"

#define INI_FILE "flowtrace.ini"
#define DATA_FOLDER "/data/data"
#define SMAL_BUF_SIZE 63
const char *TAG = "FLOWTRACE_JNI";
int initialized = 0;
char app_name[MAX_APP_NAME_LEN + 1] = {0};
int cb_app_name = 0;
int app_pid = 0;
char ip[SMAL_BUF_SIZE + 1] = { 0 };
int port = 0;
static unsigned int NN = 0;

static void get_app_path(char* name, int pid)
{
    name[0] = 0;
    sprintf(name, "/proc/%d/cmdline", pid);
    FILE* f = fopen(name,"r");
    if(f)
    {
        size_t size = fread(name, sizeof(char), MAX_APP_PATH_LEN, f);
        if(size < 1)
            size = 1;
        name[size-1]='\0';
        fclose(f);
    }
    else
    {
        name[0] = 0;
    }
}

static void set_app_name(char* name)
{
    cb_app_name = strlen(name);
    if (cb_app_name > MAX_APP_NAME_LEN)
        cb_app_name = MAX_APP_NAME_LEN;
    memcpy(app_name, name, cb_app_name);
    app_name[cb_app_name] = 0;
}

static char* get_app_name_part(char* name)
{
    char* name_part = strrchr(name, '/');
    if (name_part)
        name_part++;
    else
        name_part = name;
    return name_part;
}

static int init_app()
{
    char app_path[MAX_APP_PATH_LEN + 1] = {0};
    char *name_part, *p;

    app_name[0] = 0;
    app_path[0] = 0;
    app_pid = getpid();
    get_app_path(app_path, app_pid);

    app_path[MAX_APP_PATH_LEN] = 0;
    name_part = get_app_name_part(app_path);

    if (name_part[0])
    {
        set_app_name(name_part);
        TRACE_INFO("Starting app %s pid: %d\n", app_name, app_pid);
        return 1;
    }
    else
    {
        TRACE_ERR("Error getting app name for pid: %d\n", app_pid);
        return 0;
    }
}

static int read_config( char* path)
{
    FILE *ini_file;
    char key[SMAL_BUF_SIZE+1], val[SMAL_BUF_SIZE+1], *p;

    ip[0] = 0;
    port = 0;

    if ((ini_file = fopen(path, "r")))
    {
        while ( fscanf(ini_file, "%[^=]=%s", key, val) == 2 )
        {
            key[SMAL_BUF_SIZE] = 0; val[SMAL_BUF_SIZE] = 0;
            p = key;  for ( ; *p; ++p) *p = tolower(*p);
            p = val;  for ( ; *p; ++p) *p = tolower(*p);
            if (strstr(key, "ip")) strcpy(ip, val);
            if (strstr(key, "port")) port = atoi(val);
        }
        fclose(ini_file);
    }
    if(ip[0] != 0 && port != 0)
    {
        TRACE_INFO("reading %s: ip=%s port=%d\n", path, ip, port);
        return 1;
    }
    return 0;
}

static int init_config()
{
    char ini_path[2 * MAX_APP_PATH_LEN] = {0};
    sprintf(ini_path, "%s/%s", DATA_FOLDER, INI_FILE);
    if (!read_config(ini_path)) {
        sprintf(ini_path, "%s/%s/%s", DATA_FOLDER, app_name, INI_FILE);
        read_config(ini_path);
    }

    if(ip[0] == 0 || port == 0)
    {
        TRACE_ERR("Failed to read %s\n", INI_FILE);
        return 0;
    }
    return 1;
}

static int valist_printf(const char *fmt, va_list args)
{
    int cb_trace;
    va_list arg_copy;
    va_copy(arg_copy, args);
    cb_trace = vprintf(fmt, arg_copy);
    va_end(arg_copy);
    return cb_trace;
}

void dump_rec( LOG_REC* rec )
{
    TRACE("Flow trace: rec-> len: %d, type: %d, flags: %d, nn: %d, "
          "cb_app: %d, cb_module: %d, cb_fn: %d, cb_trace: %d, "
          "pid: %d, tid: %d, sec: %d, msec: %u, "
          "this_fn: %x, call_site: %x, fn_line: %d, call_line: %d, data: %s\n\n",
          rec->len, rec->log_type, rec->log_flags, rec->nn,
          rec->cb_app_name, rec->cb_module_name, rec->cb_fn_name, rec->cb_trace,
          rec->pid, rec->tid, rec->sec, rec->msec,
          rec->this_fn, rec->call_site, rec->fn_line, rec->call_line, rec->data);
}

static int get_color(UDP_LOG_Severity clr)
{
    //-1 default; 0 black; 1 red; 2 green; 3 yellow; 4 blue; 5 magenta; 6 cyan; 7 white
    switch (clr)
    {
        case UDP_LOG_FATAL:
        case UDP_LOG_ERROR:
            return 31;
        case UDP_LOG_WARNING:
            return 33;
        case UDP_LOG_INFO:
        case UDP_LOG_DEBUG:
            return 32;
        default:
            return -1;
    }
}

void FlowTraceSendLog(const char* module_name, int cb_module_name, unsigned int  module_base,
                    const char* fn_name, int cb_fn_name, int fn_line, int cb_trace,
                    char* trace, int call_line, unsigned int this_fn,
                    unsigned int call_site, short log_type, short flags)
{
    struct timespec time_stamp;
    int len;
    NET_PACK pack;
    LOG_REC* rec;

    if (!initialized)
        return;

    if (cb_fn_name > MAX_FUNC_NAME_LEN)
        cb_fn_name = MAX_FUNC_NAME_LEN;
    if (cb_module_name > MAX_MODULE_NAME_LEN)
        cb_module_name = MAX_MODULE_NAME_LEN;

    //pack = (NET_PACK*)buf;
    rec  = (LOG_REC*)(pack.data);

    rec->nn = ++NN;
    rec->log_type = log_type;
    rec->log_flags = flags;
    rec->tid = pthread_self();
    rec->pid = app_pid;

    clock_gettime( CLOCK_REALTIME, &time_stamp );
    rec->sec = time_stamp.tv_sec;
    rec->msec = time_stamp.tv_nsec / 1000000;
    rec->cb_app_name = cb_app_name;
    rec->cb_module_name = cb_module_name;
    rec->cb_fn_name = cb_fn_name;
    rec->cb_trace = cb_trace;
    rec->this_fn = this_fn - module_base;
    rec->call_site = call_site - module_base;
    rec->fn_line = fn_line;
    rec->call_line = call_line;

    if (cb_app_name)
        memcpy(rec->data, app_name, cb_app_name);
    if (cb_module_name)
        memcpy(rec->data + cb_app_name, module_name, cb_module_name);
    if (cb_fn_name)
        memcpy(rec->data + cb_app_name + cb_module_name, fn_name, cb_fn_name);
    if (cb_trace) {
        if ( cb_trace > (MAX_NET_BUF - cb_app_name - cb_module_name - cb_fn_name - sizeof(LOG_REC)))
            cb_trace -= (MAX_NET_BUF - cb_app_name - cb_module_name - cb_fn_name - sizeof(LOG_REC));
        memcpy(rec->data + cb_app_name + cb_module_name + cb_fn_name, trace, cb_trace);
    }

    rec->data[cb_app_name + cb_module_name + cb_fn_name + cb_trace] = 0;
    len = sizeof(LOG_REC) + cb_app_name + cb_module_name + cb_fn_name + cb_trace + 1;

    // make sure that length is 4-byte aligned
    if (len & 0x3)
        len = ((len / 4) * 4) + 4; //len = ((len >> 2) << 2) + 4;

    //dump_rec(rec);
    pack.info.data_len = rec->len = len;
    net_send_pack(&pack);
}

static int send_trace(UDP_LOG_Severity severity, int flags, const char* fn_name, int cb_fn_name, int fn_line, int call_line, const char *fmt, va_list args)
{
    int cb;
    int cb_trace = 0;
    va_list arg_copy;
    char trace[ MAX_LOG_LEN + EXTRA_BUF];

    TRACE(" -> initialized: %d fn_name: %p\n", initialized, fn_name);

    if (cb_fn_name < 0)
        cb_fn_name = strlen(fn_name);

    va_copy(arg_copy, args);

    if (!initialized)
    {
        cb = valist_printf(fmt, arg_copy);
    }
    else
    {
        if (severity < UDP_LOG_COMMON)
        {
            cb_trace += sprintf(trace + cb_trace, "\033[%dm", get_color(severity));
        }

        cb = vsnprintf(trace + cb_trace, MAX_LOG_LEN, fmt, arg_copy);
        if ( cb < 0 )
        {
            cb = snprintf(trace, MAX_LOG_LEN, "\n\033[31m Could not trace: %s", fmt);
            if ( cb < 0 )
            {
                TRACE_ERR("Could not trace:  %s", fmt);
            }
        }
        else
        {
            if (cb > MAX_LOG_LEN) // return value of size or more means that the output was truncated.
            {
                cb = MAX_LOG_LEN;
            }

            cb_trace += cb;

            if ((flags & LOG_FLAG_NEW_LINE) && (trace[cb_trace - 1] != '\n'))
            {
                trace[cb_trace] = '\n';
                cb_trace++;
            }

            if (severity < UDP_LOG_COMMON)
            {
                cb_trace +=  sprintf(trace + cb_trace, "\033[0m");
            }

            FlowTraceSendLog("", 0, 0, fn_name, cb_fn_name, fn_line, cb_trace, trace, call_line, 0, 0, LOG_INFO_TRACE, flags);
        }
    }

    va_end(arg_copy);
    TRACE(" <- \n");
    return cb;
}

static int Severity2Priority(int severity)
{
    if (severity == UDP_LOG_FATAL)
        return ANDROID_LOG_FATAL;
    else if (severity == UDP_LOG_ERROR)
        return ANDROID_LOG_ERROR;
    else if (severity == UDP_LOG_WARNING)
        return ANDROID_LOG_WARN;
    else if (severity == UDP_LOG_INFO)
        return ANDROID_LOG_INFO;
    else
        return ANDROID_LOG_DEBUG;

};

void FlowTraceLogWriteV(int severity, const char *fn_name, int call_line, const char *fmt, va_list args)
{
    // Do not use TRACE here
#ifdef WITH_TRACE
    fprintf(stderr, "FLOWTRACE -> [FlowTraceLogWriteV] initialized: %d fn_name: %p\n", initialized, fn_name);
#endif
    va_list arg_copy;
    va_copy(arg_copy, args);

    char trace[ MAX_LOG_LEN + 1];
    int cb = 0;
    cb += snprintf(trace + cb, MAX_LOG_LEN - cb, "[%s, %d] ", fn_name, call_line);
    if (cb < MAX_LOG_LEN)
        cb += vsnprintf(trace + cb, MAX_LOG_LEN - cb, fmt, args);
    trace[MAX_LOG_LEN] = 0;
    __android_log_write(Severity2Priority(severity), TAG, trace);

    va_end(arg_copy);
#ifdef WITH_TRACE
    fprintf(stderr, "FLOWTRACE <- [FlowTraceLogWriteV]\n");
#endif
}

void FlowTraceLogWrite(int severity, const char *fn_name, int call_line, const char *fmt, ...)
{
    char trace[ MAX_LOG_LEN + 1];
    va_list args;
    va_start(args, fmt);
    FlowTraceLogWriteV(severity, fn_name, call_line, fmt, args);
    va_end(args);
}

int FlowTraceSendTrace(UDP_LOG_Severity severity, int flags, const char* fn_name, int cb_fn_name, int fn_line, int call_line, const char *fmt, ...)
{
    int cb_trace;
    va_list args;
    va_start(args, fmt);
    cb_trace = send_trace(severity, flags, fn_name, cb_fn_name, fn_line, call_line, fmt, args);
    va_end(args);
    return cb_trace;
}

JNIEXPORT void JNICALL
Java_proguard_inject_FlowTraceWriter_FlowTraceLogTrace(
        JNIEnv *env, jclass type, jint severity,
        jstring  thisClassName, jstring thisMethodName, jint thisLineNumber, jint callLineNumber,
        jstring tag, jstring msg, jint flags
)
{
    const char *szThisClassName = (*env)->GetStringUTFChars(env, thisClassName, 0);
    const char *szThisMethodName = (*env)->GetStringUTFChars(env, thisMethodName, 0);
    const char *szTag = (*env)->GetStringUTFChars(env, tag, 0);
    const char *szMsg = (*env)->GetStringUTFChars(env, msg, 0);

    char fn_name[MAX_FUNC_NAME_LEN + 1]; //calling this function
    int cb_fn_name = snprintf(fn_name, sizeof(fn_name) - 1, "%s.%s", szThisClassName, szThisMethodName);
    if (cb_fn_name < 0) {
        cb_fn_name = MAX_FUNC_NAME_LEN;
    }

    FlowTraceSendTrace(
            severity,
            flags, //flags
            fn_name,
            cb_fn_name,
            thisLineNumber, //fn_line
            callLineNumber, //call_line
            "%s: %s\n", // fmt
            szTag, szMsg
    );

    (*env)->ReleaseStringUTFChars(env, thisClassName, szThisClassName);
    (*env)->ReleaseStringUTFChars(env, thisMethodName, szThisMethodName);
    (*env)->ReleaseStringUTFChars(env, thisClassName, szTag);
    (*env)->ReleaseStringUTFChars(env, thisMethodName, szMsg);
}

//extern "C"
JNIEXPORT void JNICALL
Java_proguard_inject_FlowTraceWriter_FlowTraceLogFlow(
        JNIEnv *env, jclass type,
        jint log_type, jint log_flags,
        jstring  thisClassName, jstring thisMethodName,
        jstring callClassName, jstring callMethodName,
        jint thisID, jint callID,
        jint thisLineNumber, jint callLineNumber
)
{
    const char *szThisClassName = (*env)->GetStringUTFChars(env, thisClassName, 0);
    const char *szThisMethodName = (*env)->GetStringUTFChars(env, thisMethodName, 0);
    const char *szCallClassName = (*env)->GetStringUTFChars(env, callClassName, 0);
    const char *szCallMethodName = (*env)->GetStringUTFChars(env, callMethodName, 0);

    char fn_name[MAX_FUNC_NAME_LEN + 1]; //calling this function
    int cb_fn_name = snprintf(fn_name, MAX_FUNC_NAME_LEN, "%s.%s", szThisClassName, szThisMethodName);
    if (cb_fn_name < 0) {
        cb_fn_name = MAX_FUNC_NAME_LEN;
    }

    char trace[MAX_FUNC_NAME_LEN + 1]; //caller function gous as trace
    int cb_trace = snprintf(trace, MAX_FUNC_NAME_LEN, "%s.%s", szCallClassName, szCallMethodName);
    if (cb_trace < 0) {
        cb_trace = MAX_FUNC_NAME_LEN;
    }

    //TRACE_INFO("%d %s %s %s (%d) %d [%s] %s %s (%d) %d ", log_type, (log_type == 0 ? "Before -> " : "After <- "), szThisClassName, szThisMethodName, thisID, thisLineNumber, (log_type == 0 ? "->" : "<-"), szCallClassName, szCallMethodName, callID, callLineNumber);

    //! Very confusing parameters names for FlowTraceSendLog.
    //! This is due to trace for cpp code has oposite flow.
    FlowTraceSendLog("", 0, 0,
                     fn_name, //szCallClassName:szCallMethodName
                     cb_fn_name,
                     thisLineNumber,//fn_line
                     cb_trace,
                     trace,//szThisClassName:szThisMethodName
                     callLineNumber,//call_line
                     thisID, //this_fn - callee
                     callID, //call_site - caller
                     (short)log_type,
                     log_flags|LOG_FLAG_JAVA
    );

    (*env)->ReleaseStringUTFChars(env, thisClassName, szThisClassName);
    (*env)->ReleaseStringUTFChars(env, thisMethodName, szThisMethodName);
    (*env)->ReleaseStringUTFChars(env, callClassName, szCallClassName);
    (*env)->ReleaseStringUTFChars(env, callMethodName, szCallMethodName);
}

int FlowTraceInitialize()
{
    int ret = 1;
    if (initialized)
    {
        TRACE_INFO("already initialized")
        return 1;
    }
    ret = ret && init_config();
    ret = ret && init_sender(ip, port);
    init_dalvik_hook();
    initialized = (ret != 0);
    return ret;
}
//extern "C"
JNIEXPORT jint JNICALL
Java_proguard_inject_FlowTraceWriter_initTraces(JNIEnv *env, jclass type) {
    int ret = 1;
    ret = ret && init_app();
    ret = ret && FlowTraceInitialize();
    return ret;
}

JNIEXPORT jint JNICALL
Java_com_example_testapplication_ExampleInstrumentedTest_testJNI(JNIEnv *env, jclass type) {
    int ret = 1;
    init_dalvik_hook();
    return ret;
}
