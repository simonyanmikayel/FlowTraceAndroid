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
#include <android/log.h>
#include <string.h>

#include "dexstuff.h"
#include "dalvik_hook.h"
#include "flowtrace.h"

// https://android.googlesource.com/platform/libnativehelper/+/brillo-m7-dev/include/nativehelper/jni.h
// You can get the method signatures from class files with javap with -s option: javap -classpath '/path/to/jre/lib/rt.jar' -s java.lang.Throwable

void LogString(JNIEnv *env, int priority, const char* fn_name, int call_line, jobject tag, jobject msg)
{
	const char *s_tag = 0;
	const char *s_msg = 0;
    char msg_log[MAX_LOG_LEN];

	if (tag) {
		s_tag = (*env)->GetStringUTFChars(env, tag, 0);
	}
	if (msg) {
		s_msg = (*env)->GetStringUTFChars(env, msg, 0);
	}
    if (!fn_name)
        fn_name = "java";

	snprintf(msg_log, sizeof(msg_log) - 1, "%s: %s\n", s_tag ? s_tag : "", s_msg ? s_msg : "");
	msg_log[sizeof(msg_log) - 1] = 0;
//    TRACE_INFO("~~~ priority %d priority %d", priority, priority2severity(priority));
    FlowTraceSendTrace(priority, LOG_FLAG_JAVA, fn_name, 0, 0, call_line, msg_log);

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
//static struct dalvik_hook_t dh_log_getStackTraceElement;
//static struct dalvik_hook_t dh_log_logTrace;
//static struct dalvik_hook_t dh_log_println_native;
//static struct dalvik_hook_t dh_log_getStackTraceString;

void print_log(JNIEnv *env, int priority, jobject tag, jobject msg, jobject tr, const char* fn_name, int call_line)
{
    if (!tag)
        tag = (*env)->NewStringUTF(env, "null");
    if (!msg)
        msg = (*env)->NewStringUTF(env, "null");

    char fn_name_buf[512] = {'j','a','v','a'};
    if (fn_name == 0)
    {
        fn_name = fn_name_buf;
        call_line = 0;
        // JNI calls can be made while an exception exists.
        //jthrowable exception = (*env)->ExceptionOccurred(env);
        //(*env)->ExceptionClear(env);

        jobjectArray stacktraces = 0;
        jclass clsThrowable = (*env)->FindClass(env, "java/lang/Throwable");
        if (clsThrowable) {
            jmethodID initThrowable = (*env)->GetMethodID(env, clsThrowable, "<init>", "()V");
            if (initThrowable) {
                jobject throwable = (*env)->NewObject(env, clsThrowable, initThrowable);
                if (throwable) {
                    jmethodID getStackTraceMethod = (*env)->GetMethodID(env, clsThrowable, "getStackTrace", "()[Ljava/lang/StackTraceElement;");
                    if (getStackTraceMethod) {
                        stacktraces = (*env)->CallObjectMethod(env, throwable, getStackTraceMethod);
                    }
                }
            }
        }

        if (stacktraces) {
            jmethodID getClassName = 0;
            jmethodID getMethodName = 0;
            jmethodID getLineNumber = 0;
            jclass clsStackTraceElement = (*env)->FindClass(env, "java/lang/StackTraceElement");
            if (clsStackTraceElement) {
                getClassName = (*env)->GetMethodID(env, clsStackTraceElement, "getClassName", "()Ljava/lang/String;");
                getMethodName = (*env)->GetMethodID(env, clsStackTraceElement, "getMethodName", "()Ljava/lang/String;");
                getLineNumber = (*env)->GetMethodID(env, clsStackTraceElement, "getLineNumber", "()I");
            }
            if (getClassName && getMethodName && getLineNumber) {
                jsize frames_length = (*env)->GetArrayLength(env, stacktraces);
                jsize i = 2;
                //for (i = 0; i < frames_length; i++)
                if (frames_length > 0)
                {
                    jobject frame = (*env)->GetObjectArrayElement(env, stacktraces, i);
                    jobject className = (*env)->CallObjectMethod(env, frame, getClassName);
                    jobject methodName = (*env)->CallObjectMethod(env, frame, getMethodName);
                    jint lineNumber = (*env)->CallIntMethod(env, frame, getLineNumber);

                    const char *s_className = (*env)->GetStringUTFChars(env, className, 0);
                    const char *s_methodName = (*env)->GetStringUTFChars(env, methodName, 0);

                    snprintf(fn_name_buf, sizeof(fn_name_buf) - 1, "%s.%s", s_className, s_methodName);
                    fn_name_buf[sizeof(fn_name_buf) - 1] = 0;
                    call_line = lineNumber;

//                char msg_log[MAX_LOG_LEN];
//                snprintf(msg_log, sizeof(msg_log) - 1, "%d/%d %s: %s %d\n", i, frames_length, s_className, s_methodName, lineNumber);
//                __android_log_write(INFO, "->", msg_log);

                    (*env)->ReleaseStringUTFChars(env, className, s_className);
                    (*env)->ReleaseStringUTFChars(env, methodName, s_methodName);
                }
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////////////
//    struct jmethod_t j_println_native;
//    resolveStaticMetod(&j_println_native, "android/util/Log",  "println_native",  "(IILjava/lang/String;Ljava/lang/String;)I", env);
//    (*env)->CallStaticIntMethodA(env, j_println_native.cls , j_println_native.mid, args);
    LogString(env, priority, fn_name, call_line, tag, msg);

//    dalvik_resolve(&dh_log_getStackTraceString, "Landroid/util/Log;",  "getStackTraceString",  "(Ljava/lang/Throwable;)Ljava/lang/String;", 0);
    struct jmethod_t j_getStackTraceString;
    if (tr && (resolveStaticMetod(&j_getStackTraceString, "android/util/Log",  "getStackTraceString",  "(Ljava/lang/Throwable;)Ljava/lang/String;", env)))
    {
        jvalue args[4];
        args[0].l = tr;
        args[1].i = priority;
        args[2].l = tag;
        args[3].l = msg;
        void *res = (*env)->CallStaticObjectMethodA(env, j_getStackTraceString.cls , j_getStackTraceString.mid, args);
        if (res) {
            args[0].i = 0;
            args[3].l = res;
//            (*env)->CallStaticIntMethodA(env, j_println_native.cls , j_println_native.mid, args);
            LogString(env, priority, fn_name, call_line, tag, res);
        }
    }
}

void print_log_old(JNIEnv *env, int priority, jobject tag, jobject msg, jobject tr)
{
    print_log(env, priority, tag, msg, tr, 0, 0);
}

static struct dalvik_hook_t dh_log_d1;
static void* hook_log_d1(JNIEnv *env, jclass cls, jobject tag, jobject msg)
{
    print_log_old(env, ANDROID_LOG_DEBUG, tag, msg, 0);
	return (void *)1;
}

static struct dalvik_hook_t dh_log_d2;
static void* hook_log_d2(JNIEnv *env, jclass cls, jobject tag, jobject msg, jobject tr)
{
    print_log_old(env, ANDROID_LOG_DEBUG, tag, msg, tr);
	return (void *)1;
}

static struct dalvik_hook_t dh_log_i1;
static void* hook_log_i1(JNIEnv *env, jclass cls, jobject tag, jobject msg)
{
    print_log_old(env, ANDROID_LOG_INFO, tag, msg, 0);
	return (void *)1;
}
static struct dalvik_hook_t dh_log_i2;
static void* hook_log_i2(JNIEnv *env, jclass cls, jobject tag, jobject msg, jobject tr)
{
    print_log_old(env, ANDROID_LOG_INFO, tag, msg, tr);
	return (void *)1;
}

static struct dalvik_hook_t dh_log_v1;
static void* hook_log_v1(JNIEnv *env, jclass cls, jobject tag, jobject msg)
{
    print_log_old(env, ANDROID_LOG_VERBOSE, tag, msg, 0);
	return (void *)1;
}
static struct dalvik_hook_t dh_log_v2;
static void* hook_log_v2(JNIEnv *env, jclass cls, jobject tag, jobject msg, jobject tr)
{
    print_log_old(env, ANDROID_LOG_VERBOSE, tag, msg, tr);
	return (void *)1;
}

static struct dalvik_hook_t dh_log_w1;
static void* hook_log_w1(JNIEnv *env, jclass cls, jobject tag, jobject msg)
{
    print_log_old(env, ANDROID_LOG_WARN, tag, msg, 0);
	return (void *)1;
}
static struct dalvik_hook_t dh_log_w2;
static void* hook_log_w2(JNIEnv *env, jclass cls, jobject tag, jobject msg, jobject tr)
{
    print_log_old(env, ANDROID_LOG_WARN, tag, msg, tr);
	return (void *)1;
}

static struct dalvik_hook_t dh_log_e1;
static void* hook_log_e1(JNIEnv *env, jclass cls, jobject tag, jobject msg)
{
    print_log_old(env, ANDROID_LOG_ERROR, tag, msg, 0);
    return (void *)1;
}

static struct dalvik_hook_t dh_log_e2;
static void* hook_log_e2(JNIEnv *env, jclass cls, jobject tag, jobject msg, jobject tr)
{
    print_log_old(env, ANDROID_LOG_ERROR, tag, msg, tr);
	return (void *)1;
}

static struct dalvik_hook_t dh_log_println;
static void* hook_log_println(JNIEnv *env, jclass cls, jint i, jobject tag, jobject msg)
{
    print_log_old(env, i, tag, msg, 0);
	return (void *)1;
}

static struct dalvik_hook_t dh_log_flowtrace;
static void* hook_log_flowtrace(JNIEnv *env, jclass cls, jint i, jobject tag, jobject msg)
{
    print_log_old(env, i, tag, msg, 0);
    return (void *)1;
}

int do_patch()
{
    int ret = 1;
//	TRACE_INFO("do_patch -> \n");

//    dalvik_dump_class(&d, "Landroid/util/Log;");
//    dalvik_dump_class(&d, "Ljava/lang/Thread;");


//    dh_log_getStackTraceElement.sm = 0;
//    dalvik_resolve(&dh_log_getStackTraceElement, "Ljava/lang/Throwable;",  "getStackTraceElement",  "(I)Ljava/lang/StackTraceElement;", 0);
//    if (dh_log_getStackTraceElement.mid == 0)
//        TRACE_ERR("Could not resolve Throwable.getStackTraceElement\n");
//    dh_log_flowtrace.sm = 1;
//    dalvik_resolve(&dh_log_logTrace, "Lproguard/inject/FlowTraceWriter;",  "logTrace",  "(ILjava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)V", 0);
//    if (dh_log_logTrace.mid == 0)
//    TRACE_ERR("Could not resolve proguard.inject.FlowTraceWriter.logTrace\n");

//    dh_log_getStackTraceString.sm = 1;
//    dalvik_resolve(&dh_log_getStackTraceString, "Landroid/util/Log;",  "getStackTraceString",  "(Ljava/lang/Throwable;)Ljava/lang/String;", 0);
//    dh_log_println_native.sm = 1;
//    dalvik_resolve(&dh_log_println_native, "Landroid/util/Log;",  "println_native",  "(IILjava/lang/String;Ljava/lang/String;)I", 0);

    dalvik_hook_setup(&dh_log_d1, "Landroid/util/Log;",  "d",  "(Ljava/lang/String;Ljava/lang/String;)I", 2, hook_log_d1);
    dalvik_hook_setup(&dh_log_d2, "Landroid/util/Log;",  "d",  "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I", 3, hook_log_d2);

    dalvik_hook_setup(&dh_log_i1, "Landroid/util/Log;",  "i",  "(Ljava/lang/String;Ljava/lang/String;)I", 2, hook_log_i1);
    dalvik_hook_setup(&dh_log_i2, "Landroid/util/Log;",  "i",  "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I", 3, hook_log_i2);

    dalvik_hook_setup(&dh_log_v1, "Landroid/util/Log;",  "v",  "(Ljava/lang/String;Ljava/lang/String;)I", 2, hook_log_v1);
    dalvik_hook_setup(&dh_log_v2, "Landroid/util/Log;",  "v",  "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I", 3, hook_log_v2);

    dalvik_hook_setup(&dh_log_w1, "Landroid/util/Log;",  "w",  "(Ljava/lang/String;Ljava/lang/String;)I", 2, hook_log_w1);
    dalvik_hook_setup(&dh_log_w2, "Landroid/util/Log;",  "w",  "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I", 3, hook_log_w2);

    dalvik_hook_setup(&dh_log_e1, "Landroid/util/Log;",  "e",  "(Ljava/lang/String;Ljava/lang/String;)I", 2, hook_log_e1);
    dalvik_hook_setup(&dh_log_e2, "Landroid/util/Log;",  "e",  "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I", 3, hook_log_e2);

    dalvik_hook_setup(&dh_log_println, "Landroid/util/Log;",  "println",  "(ILjava/lang/String;Ljava/lang/String;)I", 3, hook_log_println);

//	TRACE_INFO("do_patch <- \n");
    return ret;
}
