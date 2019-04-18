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
#include <semaphore.h>
#include "flowtrace.h"

#define INI_FILE "flowtrace.ini"
#define DATA_FOLDER "/data/data"
#define SMAL_BUF_SIZE 63
pthread_mutex_t g_mutex_log;
const char *TAG = "FLOW_TRACE";
int initialized = 0;
char app_name[MAX_APP_NAME_LEN + 1] = {0};
int cb_app_name = 0;
int app_pid = 0;
static char ip[SMAL_BUF_SIZE + 1] = { 0 };
static int port = 0;
unsigned int NN = 0;
static int retry_delay = 0;
static int retry_count = -1;

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
            if (strstr(key, "retry_delay")) retry_delay = atoi(val);
            if (strstr(key, "retry_count")) retry_count = atoi(val);
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
#ifndef _USE_ADB
    if(ip[0] == 0 || port == 0)
    {
        TRACE_ERR("Failed to read %s\n", INI_FILE);
        return 0;
    }
#endif //USE_UDP
    return 1;
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

// for internal tracing
void MyAndroidLogWrite(int priority, const char *fn_name, int call_line, const char *fmt, ...)
{
    // Do not use TRACE here
#ifdef WITH_TRACE
    fprintf(stderr, "Flow trace: -> initialized: %d fn_name: %p\n", initialized, fn_name);
#endif
    va_list args;
    va_start(args, fmt);

    char trace[ MAX_LOG_LEN + 1];
    int cb = 0;
    cb += snprintf(trace + cb, MAX_LOG_LEN - cb, "FLOWTRACE LOG [%s, %d] ", fn_name, call_line);
    if (cb < MAX_LOG_LEN)
        cb += vsnprintf(trace + cb, MAX_LOG_LEN - cb, fmt, args);
    trace[MAX_LOG_LEN] = 0;
    AndroidTrace(trace, priority);

    va_end(args);
#ifdef WITH_TRACE
    fprintf(stderr, "Flow trace: <- \n");
#endif
}

int FlowTraceSendTrace(flow_LogPriority priority, int flags, const char* fn_name, int cb_fn_name, int fn_line, int call_line, const char *fmt, ...)
{
    int cb_trace;
    va_list args;
    va_start(args, fmt);
    cb_trace = SendTrace("", 0, 0, priority, flags, fn_name, cb_fn_name, fn_line, call_line, fmt, args);
    va_end(args);
    return cb_trace;
}

JNIEXPORT void JNICALL
Java_proguard_inject_FlowTraceWriter_FlowTraceLogTrace(
        JNIEnv *env, jclass type, jint priority,
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
            0, //overrides jint priority
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

    //! Very confusing parameters names for SendLog.
    //! This is due to trace for cpp code has oposite flow.
    SendLog("", 0, 0,
                     fn_name, //szCallClassName:szCallMethodName
                     cb_fn_name,
                     thisLineNumber,//fn_line
                     cb_trace,
                     trace,//szThisClassName:szThisMethodName
                     callLineNumber,//call_line
                     thisID, //this_fn - callee
                     callID, //call_site - caller
                     (short)log_type,
                     log_flags|LOG_FLAG_JAVA,
                     0,
                     0
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
    ret = ret && init_sender(ip, port, retry_delay, retry_count);

    init_dalvik_hook();
#ifdef _TEST_THREAD
    startTest();
#endif //_TEST_THREAD
    initialized = 1;
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

static int valist_printf(const char *fmt, va_list args)
{
    int cb_trace;
    va_list arg_copy;
    va_copy(arg_copy, args);
    cb_trace = vprintf(fmt, arg_copy);
    va_end(arg_copy);
    return cb_trace;
}

void AndroidTrace(const char* trace, flow_LogPriority priority) {
    if (priority < FLOW_LOG_DEBUG)
        priority = FLOW_LOG_DEBUG;
    if (priority > FLOW_LOG_ERROR)
        priority = FLOW_LOG_ERROR;
    __android_log_write(priority, TAG, trace);
}

/////////////////////////////////////////////////////////////////////
// common part
/////////////////////////////////////////////////////////////////////
#ifdef PARCE_COLOR
static inline int parceCollor(char** c)
{
    int color = 0;
    if (isdigit(**c))
    {
        color = (**c) - '0';
        (*c)++;
        if (isdigit(**c))
        {
            color = (10 * color) + ((**c) - '0');
            (*c)++;
        }
        if (!((color >= 30 && color <= 37) || (color >= 40 && color <= 47)))
            color = 0;
    }
    return color;
}
#endif // PARCE_COLOR

int SendTrace(const char* module_name, int cb_module_name, unsigned int  module_base, flow_LogPriority priority, int flags, const char* fn_name, int cb_fn_name, int fn_line, int call_line, const char *fmt, va_list args)
{
    char trace[ MAX_LOG_LEN + EXTRA_BUF];
    int cb_trace, i, send_pos = 0;
    int trace_color = 0;
    int old_color = 0;
    va_list arg_copy;

    va_copy(arg_copy, args);

    if (!initialized) {
        cb_trace = valist_printf(fmt, arg_copy);
    }
    else {
        //Upon successful return, these functions return the number of characters printed (excluding the null byte used to end output to strings).
        //return value of MAX_LOG_LEN or more means that the output was truncated
        cb_trace = vsnprintf(trace, MAX_LOG_LEN, fmt, arg_copy);
        if ( cb_trace >= MAX_LOG_LEN )
        {
            cb_trace = MAX_LOG_LEN -1;
            trace[cb_trace - 1] = '.';
            trace[cb_trace - 2] = '.';
            trace[cb_trace - 3] = '.';
        }
        if ((flags & LOG_FLAG_NEW_LINE) && (trace[cb_trace - 1] != '\n')) {
            cb_trace++;
            trace[cb_trace - 1] = '\n';
        }
        trace[cb_trace] = 0;

#ifdef PARCE_COLOR
        char *start = trace;
        char *end = trace;
        flags |= LOG_FLAG_COLOR_PARCED;
        while(*end) {
            while (*(end) >= ' ')
                end++;
            if (*end == '\n' || *end == '\r') {
                old_color = trace_color;
                HandleLog(module_name, cb_module_name, module_base, fn_name, cb_fn_name, fn_line, end - start + 1, start, call_line, 0, 0, LOG_INFO_TRACE, flags, trace_color, priority);
                while (*end == '\n' || *end == '\r')
                    end++;
                start = end;
            }
            else if (*end == '\033' && *(end + 1) == '[') {
                int c1 = 0, c2 = 0, c3 = 0;
                char* colorPos = end;
                end += 2;
                c1 = parceCollor(&end);
                if (*end == ';') {
                    end++;
                    c2 = parceCollor(&end);
                }
                if (*end == ';') {
                    end++;
                    c3 = parceCollor(&end);
                }
                if (*end == 'm')
                {
                    end++;
                    if (!trace_color) trace_color = c1;
                    if (!trace_color) trace_color = c2;
                    if (!trace_color) trace_color = c3;
                }
                if (colorPos > start) {
                    HandleLog(module_name, cb_module_name, module_base, fn_name, cb_fn_name, fn_line, colorPos - start, start, call_line, 0, 0, LOG_INFO_TRACE, flags, trace_color, priority);
                }
                old_color = trace_color;
                start = end;
            }
            else if (*end) { //*end < ' '
                end++;
            }
        }
        if (end > start)
        {
            HandleLog(module_name, cb_module_name, module_base, fn_name, cb_fn_name, fn_line, end - start, start, call_line, 0, 0, LOG_INFO_TRACE, flags, trace_color, priority);
        }
        else if (old_color != trace_color) {
            HandleLog(module_name, cb_module_name, module_base, fn_name, cb_fn_name, fn_line, 0, end, call_line, 0, 0, LOG_INFO_TRACE, flags, trace_color, priority);
        }
#else // PARCE_COLOR
        if (cb_trace)
        {
            HandleLog(module_name, cb_module_name, module_base, fn_name, cb_fn_name, fn_line, cb_trace, trace, call_line, 0, 0, LOG_INFO_TRACE, flags, 0, priority);
        }
#endif // PARCE_COLOR
    }
    va_end(arg_copy);

    return cb_trace;
}

void SendLog(const char* module_name, int cb_module_name, unsigned int  module_base,
             const char* fn_name, int cb_fn_name, int fn_line, int cb_trace,
             char* trace, int call_line, unsigned int this_fn, unsigned int call_site,
             unsigned char log_type, unsigned char flags, unsigned char color, unsigned char priority)
{
    if (initialized)
    {
        HandleLog(module_name, cb_module_name, module_base, fn_name, cb_fn_name, fn_line, cb_trace, trace, call_line, this_fn, call_site, log_type, flags, color, priority);
    }
}

