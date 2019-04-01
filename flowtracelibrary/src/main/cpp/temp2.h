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
static short retry_delay = 0;
static short retry_count = -1;

#define TRACE_OFFSET(rec) (rec->cb_app_name + rec->cb_module_name + rec->cb_fn_name)


static inline void enter_critical_section(const char* coller, int call_line)
{
    TRACE(" -> %s %d \n", coller, call_line);
    pthread_mutex_lock( &g_mutex_log );
}

static inline void leave_critical_section(const char* coller, int call_line)
{
    TRACE(" <- %s %d \n", coller, call_line);
    pthread_mutex_unlock( &g_mutex_log );
}

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

// for internal tracing
void AndroidLogWrite(int severity, const char *fn_name, int call_line, const char *fmt, ...)
{
    // Do not use TRACE here
#ifdef WITH_TRACE
    //fprintf(stderr, "FLOWTRACE -> [AndroidLogWrite] initialized: %d fn_name: %p\n", initialized, fn_name);
#endif
    va_list args;
    va_start(args, fmt);

    char trace[ MAX_LOG_LEN + 1];
    int cb = 0;
    cb += snprintf(trace + cb, MAX_LOG_LEN - cb, "FLOWTRACE LOG [%s, %d] ", fn_name, call_line);
    if (cb < MAX_LOG_LEN)
        cb += vsnprintf(trace + cb, MAX_LOG_LEN - cb, fmt, args);
    trace[MAX_LOG_LEN] = 0;
    __android_log_write(Severity2Priority(severity), TAG, trace);

    va_end(args);
#ifdef WITH_TRACE
    //fprintf(stderr, "FLOWTRACE <- [AndroidLogWrite]\n");
#endif
}

int FlowTraceSendTrace(UDP_LOG_Severity severity, int flags, const char* fn_name, int cb_fn_name, int fn_line, int call_line, const char *fmt, ...)
{
    int cb_trace;
    va_list args;
    va_start(args, fmt);
    cb_trace = SendTrace("", 0, 0, severity, flags, fn_name, cb_fn_name, fn_line, call_line, fmt, args);
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
            0,0
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
    pthread_mutexattr_t Attr;
    pthread_mutexattr_init(&Attr);
    pthread_mutexattr_settype(&Attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_mutex_log, &Attr);
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

/////////////////////////////////////////////////////////////////////
// common part
/////////////////////////////////////////////////////////////////////

static inline void AddTrace(LOG_REC* rec, char* trace, int cb_trace, unsigned char flags, unsigned char color, unsigned char severity) {
    int len = 0;
    int cur_trace_end = TRACE_OFFSET(rec) + rec->cb_trace;
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

    if (flags & LOG_FLAG_NEW_LINE)
        rec->log_flags |= LOG_FLAG_NEW_LINE;
    if (!rec->color)
        rec->color = color;
    if (rec->severity < severity)
        rec->severity = severity;
    //dump_rec(rec);
}

static inline void MakeRec(LOG_REC* rec, int tid, const char* module_name, int cb_module_name, unsigned int  module_base,
                           const char* fn_name, int cb_fn_name, int fn_line, int cb_trace,
                           char* trace, int call_line, unsigned int this_fn,
                           unsigned int call_site, unsigned char log_type, unsigned char flags, unsigned char color, unsigned char severity) {

    struct timespec time_stamp;

    if (!fn_name)
        cb_fn_name = 0;
    else if (cb_fn_name <= 0)
        cb_fn_name = strlen(fn_name);

    if (cb_fn_name > MAX_FUNC_NAME_LEN)
        cb_fn_name = MAX_FUNC_NAME_LEN;
    if (cb_module_name > MAX_MODULE_NAME_LEN)
        cb_module_name = MAX_MODULE_NAME_LEN;

//    if (log_type != LOG_INFO_TRACE || cb_trace != 0)
//        NN++; //empty message indicates new line or a color
    rec->nn = ++NN;
    rec->log_type = log_type;
    rec->log_flags = flags;
    rec->color = color;
    rec->severity = severity;
    rec->tid = tid;
    rec->pid = app_pid;

    clock_gettime( CLOCK_REALTIME, &time_stamp );
    rec->sec = time_stamp.tv_sec;
    rec->msec = time_stamp.tv_nsec / 1000000;
    rec->cb_app_name = cb_app_name;
    rec->cb_module_name = cb_module_name;
    rec->cb_fn_name = cb_fn_name;
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

    rec->cb_trace = 0;
    AddTrace(rec, trace, cb_trace, flags, color, severity);
}

static inline int parceCollor(char* pBuf, int *iSkip)
{
    int color = 0;
    if (isdigit(pBuf[*iSkip]))
    {
        color = pBuf[*iSkip] - '0';
        (*iSkip)++;
        if (isdigit(pBuf[*iSkip]))
        {
            color = (10 * color) + (pBuf[*iSkip] - '0');
            (*iSkip)++;
        }
//        TRACE_INFO("~~~ color %d", color);
        if (!((color >= 30 && color <= 37) || (color >= 40 && color <= 47)))
            color = 0;
//        TRACE_INFO("~~~ color %d", color);
    }
    return color;
}

int SendTrace(const char* module_name, int cb_module_name, unsigned int  module_base, UDP_LOG_Severity severity, int flags, const char* fn_name, int cb_fn_name, int fn_line, int call_line, const char *fmt, va_list args)
{
    int cb, i, send_pos = 0;
    va_list arg_copy;
    int trace_color = 0;
    int old_color = 0;
    static char trace[ MAX_LOG_LEN + EXTRA_BUF];

//    if (flags & LOG_FLAG_JAVA)
//        return 1;

//    TRACE_INFO("~~~ ->\n");
    enter_critical_section(__FUNCTION__, __LINE__);

    va_copy(arg_copy, args);

    if (!initialized)
    {
        cb = valist_printf(fmt, arg_copy);
        TRACE_ERR(fmt, arg_copy);
    }
    else
    {
        //Upon successful return, these functions return the number of characters printed (excluding the null byte used to end output to strings).
        //return value of MAX_LOG_LEN or more means that the output was truncated
        cb = vsnprintf(trace, MAX_LOG_LEN, fmt, arg_copy);
        if ( cb >= MAX_LOG_LEN )
        {
            cb = MAX_LOG_LEN -1;
            trace[cb - 1] = '.';
            trace[cb - 2] = '.';
            trace[cb - 3] = '.';
        }
        if ((flags & LOG_FLAG_NEW_LINE) && (trace[cb - 1] != '\n')) {
            cb++;
            trace[cb - 1] = '\n';
        }
        trace[cb] = 0;

//        int jj, kk = 0;
//        static char hex_trace[ 10*MAX_LOG_LEN + EXTRA_BUF];
//        for(jj = 0; jj < cb; jj++) {
//            kk += sprintf(hex_trace+kk, "%2.2X ", (unsigned char)trace[jj]);
//        }
//        TRACE_INFO("~~~ > %s", hex_trace);
//        TRACE_INFO("~~~ > %s", trace);

        // find colors and new lines
        for (i = 0; i < cb; i++)
        {
            if (trace[i] == '\n' || trace[i] == '\r')
            {
                if (i > send_pos) {
                    old_color = trace_color;
                    SendLog( module_name, cb_module_name, module_base, fn_name, cb_fn_name, fn_line, i - send_pos, trace, call_line, 0, 0, LOG_INFO_TRACE, flags | LOG_FLAG_NEW_LINE, trace_color, severity);
//                    TRACE_INFO("new line ~~~ %d %d %s", trace_color, severity, trace);
                }
                while (trace[i + 1] == '\n' || trace[i + 1] == '\r')
                    i++;
                send_pos = i + 1;
            }
            if (trace[i] == '\033' && trace[i + 1] == '[')
            {
                int j = i;
                int c1 = 0, c2 = 0, c3 = 0;
                trace[i] = '['; //for testing
                //TRACE_INFO("~~~ %s %d %d", trace + i + 1, j, start);
                i += 2;
                c1 = parceCollor(trace, &i);
                if (trace[i] == ';')
                {
                    i++;
                    c2 = parceCollor(trace, &i);
                }
                if (trace[i] == ';')
                {
                    i++;
                    c3 = parceCollor(trace, &i);
                }
                if (trace[i] == 'm')
                {
                    if (!trace_color) trace_color = c1;
                    if (!trace_color) trace_color = c2;
                    if (!trace_color) trace_color = c3;
//                    TRACE_INFO("c ~~~ %d %d %d %d", trace_color, c1, c2, c2);
                }
                if (j > send_pos) {
                    old_color = trace_color;
                    SendLog(module_name, cb_module_name, module_base, fn_name, cb_fn_name, fn_line, j - send_pos, trace, call_line, 0, 0, LOG_INFO_TRACE, flags, trace_color, severity);
//                    TRACE_INFO("color ~~~ %d %d %s", trace_color, severity, trace);
                }
                send_pos = i + 1;
            }
        }
        if (i > send_pos)
        {
            SendLog(module_name, cb_module_name, module_base, fn_name, cb_fn_name, fn_line, i - send_pos, trace, call_line, 0, 0, LOG_INFO_TRACE, flags, trace_color, severity);
//            TRACE_INFO("final ~~~ %d %d %s", trace_color, severity, trace);
        }
        else if (old_color != trace_color) {
            SendLog(module_name, cb_module_name, module_base, fn_name, cb_fn_name, fn_line, i - send_pos, trace, call_line, 0, 0, LOG_INFO_TRACE, flags, trace_color, severity);
//            TRACE_INFO("empty ~~~ %d %d %s", trace_color, severity, trace);
        }
//        TRACE_INFO("3 ~~~ %d %d %s", trace_color, severity, trace);
    }

    va_end(arg_copy);
    leave_critical_section(__FUNCTION__, __LINE__);

//    TRACE_INFO("~~~ <- \n");

    return cb;
}

static void SendRec(LOG_REC* rec) {
    if (rec->len != 0) {
        char *trace = rec->data + TRACE_OFFSET(rec);
        if (rec->log_type == LOG_INFO_TRACE) {
            __android_log_write(Severity2Priority(rec->severity), TAG, trace);
        }
//        if (rec->log_type == LOG_INFO_TRACE)
//            TRACE_INFO(" SendRec ~~~ %d %d %d %s", rec->color, rec->severity, rec->cb_trace, trace);
        net_send(rec);
        rec->len = 0;
    }
}

void SendLog(const char* module_name, int cb_module_name, unsigned int  module_base,
             const char* fn_name, int cb_fn_name, int fn_line, int cb_trace,
             char* trace, int call_line, unsigned int this_fn, unsigned int call_site,
             unsigned char log_type, unsigned char flags, unsigned char color, unsigned char severity)
{
    static NET_PACK tracePack;
    static NET_PACK logPack;
    if (!initialized)
        return;

//    if (flags & LOG_FLAG_JAVA || !(log_type == LOG_INFO_TRACE))
//        return;

//    if (log_type == LOG_INFO_TRACE)
//        TRACE_INFO("~~~ NN=%d cb_trace=%d %d %d %s\n", NN, cb_trace, color, severity, trace);

    //TRACE_INFO("~~~ -> \n");
    enter_critical_section(__FUNCTION__, __LINE__);


#ifndef _USE_ADB

    int tid = pthread_self();

//    MakeRec(&logPack.rec, tid, module_name, cb_module_name, module_base, fn_name, cb_fn_name, fn_line, cb_trace, trace, call_line, this_fn, call_site, log_type, flags, 0, 0);
//    SendRec(&logPack.rec);
//    leave_critical_section(__FUNCTION__, __LINE__);
//    return;

    if (log_type == LOG_INFO_TRACE) {
        LOG_REC* rec = &tracePack.rec;
        if (rec->len == 0) {
            MakeRec(rec, tid, module_name, cb_module_name, module_base, fn_name, cb_fn_name, fn_line, cb_trace, trace, call_line, this_fn, call_site, log_type, flags, color, severity);
        } else if (    tid == rec->tid
                       && call_line == rec->call_line
                       && rec->len + cb_trace < MAX_NET_BUF
                       && !(flags & LOG_FLAG_NEW_LINE)
                ) {
            //TRACE_INFO("Flow trace: AddTrace 1 rec->cb_trace=%d cb_trace=%d", rec->cb_trace, cb_trace);
            AddTrace(rec, trace, cb_trace, flags, color, severity);
        } else {
            SendRec(rec);
            MakeRec(rec, tid, module_name, cb_module_name, module_base, fn_name, cb_fn_name, fn_line, cb_trace, trace, call_line, this_fn, call_site, log_type, flags, color, severity);
        }
    } else {
        if (tracePack.rec.len)
            SendRec(&tracePack.rec);
        MakeRec(&logPack.rec, tid, module_name, cb_module_name, module_base, fn_name, cb_fn_name, fn_line, cb_trace, trace, call_line, this_fn, call_site, log_type, flags, color, severity);
        SendRec(&logPack.rec);
    }
#else //_USE_ADB
    /**const char* module_name
                    const char* fn_name
                    char* trace


     */
    char log[ 2*MAX_LOG_LEN ];

    if (cb_fn_name > MAX_FUNC_NAME_LEN)
        cb_fn_name = MAX_FUNC_NAME_LEN;
    if (cb_module_name > MAX_MODULE_NAME_LEN)
        cb_module_name = MAX_MODULE_NAME_LEN;

    this_fn -= -module_base;
    call_site -= -module_base;

    //If an output error is encountered, a negative value is returned.
    //return value of size or more means that the output was truncated.
    if (!cb_trace)
        trace = "";
    if (!cb_module_name)
        module_name = "";
    if (!cb_fn_name)
        fn_name = "";
    int len = snprintf(log, sizeof(log),
            "<~ %d %d %d %d %d %d %u %u %d %d %.*s %.*s %.*s ~>%s",
            log_type, flags, ++NN, cb_app_name, cb_module_name,
            cb_fn_name, this_fn, call_site, fn_line, call_line,
            cb_app_name, app_name, cb_module_name, module_name, cb_fn_name, fn_name, trace);
    if (len > 0) {
        if (len >= sizeof(log)) {
            len = (int)(sizeof(log) - 1);
            log[len] = 0;
        }
#ifdef _CONTROL_SEND_COUNT
        static int byte_per_msec = 100000;
        //1 msec= 1000 mcrsec
        unsigned int mcrsec = (len * 1000) / byte_per_msec;
        if (mcrsec) {
            //usleep(mcrsec);
        }
#endif //_CONTROL_SEND_COUNT

        __android_log_write(ANDROID_LOG_DEBUG, TAG, log);
    }
#endif //_USE_ADB

    leave_critical_section(__FUNCTION__, __LINE__);

    //TRACE_INFO("~~~ <- \n");
}

