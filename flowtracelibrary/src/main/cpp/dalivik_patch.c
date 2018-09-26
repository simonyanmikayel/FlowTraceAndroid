/*
 * dalivik_patch.c
 *
 *  Created on: Jul 20, 2018
 *      Author: misha
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>

#include "dexstuff.h"
#include "dalvik_hook.h"
#include "corkscrew.h"
#include "flowtrace.h"

static const int VERBOSE = 2;
static const int DEBUG = 3;
static const int INFO = 4;
static const int WARN = 5;
static const int ERROR = 6;
static const int EXCEPTION = 7;
static char msg_log[MAX_LOG_LEN];

// https://android.googlesource.com/platform/libnativehelper/+/brillo-m7-dev/include/nativehelper/jni.h
// You can get the method signatures from class files with javap with -s option: javap -classpath '/path/to/jre/lib/rt.jar' -s java.lang.Throwable

static int priority2severity(int priority)
{
    UDP_LOG_Severity severity;
    if (priority == VERBOSE || priority == DEBUG)
        severity = UDP_LOG_DEBUG;
    else if (priority == INFO)
        severity = UDP_LOG_INFO;
    else if (priority == WARN)
        severity = UDP_LOG_WARNING;
    else if (priority == ERROR || priority == EXCEPTION)
        severity = UDP_LOG_ERROR;
    else
        severity = UDP_LOG_FATAL;
    return severity;
}
static void LogString(JNIEnv *env, int priority, jobject tag, jobject msg)
{
	const char *s_tag = 0;
	const char *s_msg = 0;

	UDP_LOG_Severity severity = priority2severity(priority);

	if (tag) {
		s_tag = (*env)->GetStringUTFChars(env, tag, 0);
	}
	if (msg) {
		s_msg = (*env)->GetStringUTFChars(env, msg, 0);
	}

	snprintf(msg_log, sizeof(msg_log) - 1, "%s: %s\n", s_tag ? s_tag : "", s_msg ? s_msg : "");
	msg_log[sizeof(msg_log) - 1] = 0;
    FlowTraceSendTrace(severity, LOG_FLAG_JAVA, "java", -1, 0, 0, msg_log);
	//TRACE_INFO(msg_log);

//	backtrace_frame_t frames[256] = {0,};
//	backtrace_symbol_t symbols[256] = {0,};
//	unsigned int frame_count = get_backtrace(frames, symbols, 256);

	if (s_tag) {
		(*env)->ReleaseStringUTFChars(env, tag, s_tag);
	}
	if (s_msg) {
		(*env)->ReleaseStringUTFChars(env, msg, s_msg);
	}
}

///////////////////////////////////////////////////////////////////////
// Log
///////////////////////////////////////////////////////////////////////

static struct dalvik_hook_t dh_log_getStackTraceString;
static struct dalvik_hook_t dh_log_println_native;
static struct dalvik_hook_t dh_log_logTrace;

static void print_log(JNIEnv *env, jclass cls, int priority, jobject tag, jobject msg, jobject tr)
{
    if (dh_log_logTrace.mid != 0)
    {
        jvalue args[4];
        args[0].i = priority2severity(priority);
        args[1].l = tag;
        args[2].l = msg;
        args[3].l = tr;
        (*env)->CallStaticVoidMethodA(env, cls , dh_log_logTrace.mid, args);
    }
    else
    {
        jvalue args[4];
        args[0].i = 0;
        args[1].i = priority2severity(priority);
        args[2].l = tag;
        args[3].l = msg;

        if (dh_log_println_native.mid != 0)
        {
            (*env)->CallStaticIntMethodA(env, cls , dh_log_println_native.mid, args);
        }

        LogString(env, tr ? UDP_LOG_ERROR : priority2severity(priority), tag, msg);

        if (dh_log_getStackTraceString.mid && tr)
        {
            args[0].l = tr;
            void *res = (*env)->CallStaticObjectMethodA(env, cls , dh_log_getStackTraceString.mid, args);
            if (res)
                print_log(env, cls, EXCEPTION, tag, res, 0);
        }
    }
}

static struct dalvik_hook_t dh_log_d1;
static void* hook_log_d1(JNIEnv *env, jclass cls, jobject tag, jobject msg)
{
	print_log(env, cls, DEBUG, tag, msg, 0);
	return (void *)1;
}
static struct dalvik_hook_t dh_log_d2;
static void* hook_log_d2(JNIEnv *env, jclass cls, jobject tag, jobject msg, jobject tr)
{
	print_log(env, cls, DEBUG, tag, msg, tr);
	return (void *)1;
}

static struct dalvik_hook_t dh_log_i1;
static void* hook_log_i1(JNIEnv *env, jclass cls, jobject tag, jobject msg)
{
	print_log(env, cls, INFO, tag, msg, 0);
	return (void *)1;
}
static struct dalvik_hook_t dh_log_i2;
static void* hook_log_i2(JNIEnv *env, jclass cls, jobject tag, jobject msg, jobject tr)
{
	print_log(env, cls, INFO, tag, msg, tr);
	return (void *)1;
}

static struct dalvik_hook_t dh_log_v1;
static void* hook_log_v1(JNIEnv *env, jclass cls, jobject tag, jobject msg)
{
	print_log(env, cls, VERBOSE, tag, msg, 0);
	return (void *)1;
}
static struct dalvik_hook_t dh_log_v2;
static void* hook_log_v2(JNIEnv *env, jclass cls, jobject tag, jobject msg, jobject tr)
{
	print_log(env, cls, VERBOSE, tag, msg, tr);
	return (void *)1;
}

static struct dalvik_hook_t dh_log_w1;
static void* hook_log_w1(JNIEnv *env, jclass cls, jobject tag, jobject msg)
{
	print_log(env, cls, WARN, tag, msg, 0);
	return (void *)1;
}
static struct dalvik_hook_t dh_log_w2;
static void* hook_log_w2(JNIEnv *env, jclass cls, jobject tag, jobject msg, jobject tr)
{
	print_log(env, cls, WARN, tag, msg, tr);
	return (void *)1;
}

static struct dalvik_hook_t dh_log_e2;
static void* hook_log_e2(JNIEnv *env, jclass cls, jobject tag, jobject msg, jobject tr)
{
	print_log(env, cls, ERROR, tag, msg, tr);
	return (void *)1;
}

static struct dalvik_hook_t dh_log_println;
static void* hook_log_println(JNIEnv *env, jclass cls, jint i, jobject tag, jobject msg)
{
	print_log(env, cls, i, tag, msg, 0);
	return (void *)1;
}

static struct dalvik_hook_t dh_log_flowtrace;
static void* hook_log_flowtrace(JNIEnv *env, jclass cls, jint i, jobject tag, jobject msg)
{
    print_log(env, cls, i, tag, msg, 0);
    return (void *)1;
}

int do_patch()
{
    int ret = 1;
//	TRACE_INFO("do_patch -> \n");

    //dalvik_dump_class(&d, "Landroid/util/Log;");

    //dh_log_println.debug_me = dh_log_e2.debug_me = dh_log_w2.debug_me = dh_log_w1.debug_me = dh_log_v2.debug_me = dh_log_v1.debug_me = dh_log_i2.debug_me = dh_log_i1.debug_me = dh_log_d2.debug_me = dh_log_d1.debug_me = dh_log_getStackTraceString.debug_me = dh_log_println_native.debug_me = 1;

    dh_log_getStackTraceString.sm = 1;
    dalvik_resolve(&dh_log_getStackTraceString, "Landroid/util/Log;",  "getStackTraceString",  "(Ljava/lang/Throwable;)Ljava/lang/String;", 0);

    dh_log_println_native.sm = 1;
    dalvik_resolve(&dh_log_println_native, "Landroid/util/Log;",  "println_native",  "(IILjava/lang/String;Ljava/lang/String;)I", 0);

    dh_log_flowtrace.sm = 1;
    dalvik_resolve(&dh_log_logTrace, "Lproguard/inject/FlowTraceWriter;",  "logTrace",  "(ILjava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)V", 0);
    if (dh_log_logTrace.mid == 0)
        TRACE_ERR("Could not resolve proguard.inject.FlowTraceWriter.logTrace\n");

    dalvik_hook_setup(&dh_log_d1, "Landroid/util/Log;",  "d",  "(Ljava/lang/String;Ljava/lang/String;)I", 2, hook_log_d1);
    dalvik_hook_setup(&dh_log_d2, "Landroid/util/Log;",  "d",  "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I", 3, hook_log_d2);

    dalvik_hook_setup(&dh_log_i1, "Landroid/util/Log;",  "i",  "(Ljava/lang/String;Ljava/lang/String;)I", 2, hook_log_i1);
    dalvik_hook_setup(&dh_log_i2, "Landroid/util/Log;",  "i",  "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I", 3, hook_log_i2);

    dalvik_hook_setup(&dh_log_v1, "Landroid/util/Log;",  "v",  "(Ljava/lang/String;Ljava/lang/String;)I", 2, hook_log_v1);
    dalvik_hook_setup(&dh_log_v2, "Landroid/util/Log;",  "v",  "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I", 3, hook_log_v2);

    dalvik_hook_setup(&dh_log_w1, "Landroid/util/Log;",  "w",  "(Ljava/lang/String;Ljava/lang/String;)I", 2, hook_log_w1);
    dalvik_hook_setup(&dh_log_w2, "Landroid/util/Log;",  "w",  "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I", 3, hook_log_w2);

    dalvik_hook_setup(&dh_log_e2, "Landroid/util/Log;",  "e",  "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I", 3, hook_log_e2);

    dalvik_hook_setup(&dh_log_println, "Landroid/util/Log;",  "println",  "(ILjava/lang/String;Ljava/lang/String;)I", 3, hook_log_println);

//	TRACE_INFO("do_patch <- \n");
    return ret;
}
