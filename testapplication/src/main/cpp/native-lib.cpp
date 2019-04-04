#include <jni.h>
#include <stdio.h>
#include <string>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>

typedef void (*pfn_FlowTraceLogWrite) ( int logPriority, const char *fn_name, int call_line, const char *fmt, ... );

static     pfn_FlowTraceLogWrite flowTraceLogWrite = 0;

extern "C" JNIEXPORT jstring
JNICALL
Java_com_example_testapplication_MainActivity_stringFromJNI(
        JNIEnv *env,
        jobject /* this */)
{
    static char str[256];
    void* handle = dlopen("libflowtrace.so", RTLD_NOW);
    if (handle)
        flowTraceLogWrite = (pfn_FlowTraceLogWrite)dlsym(handle, "AndroidLogWrite");
    sprintf(str, "libflowtrace: %p flowTraceLogWrite: %p\n", handle, flowTraceLogWrite);
    if (flowTraceLogWrite)
        flowTraceLogWrite(0, __FUNCTION__, __LINE__, "libflowtrace: %p flowTraceLogWrite: %p\n", handle, flowTraceLogWrite);
    std::string hello = "Hello from Cpp";
    return env->NewStringUTF(str);
}
