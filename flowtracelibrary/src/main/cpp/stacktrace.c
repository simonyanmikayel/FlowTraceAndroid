//
// Created by misha on 9/18/2020.
//
#include <jni.h>
#include <android/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <dlfcn.h>
#include "flowtrace.h"
#include <ucontext.h>
#include "stacktrace.h"

////////////////////////////////////////////////////////
// from sigsegv.c
////////////////////////////////////////////////////////
static void signal_segv(int signum, siginfo_t* info, void*ptr) {

    static const char *si_codes[3] = {"", "SEGV_MAPERR", "SEGV_ACCERR"};
//    ucontext_t *ucontext = (ucontext_t*)ptr;

    signal(signum,SIG_DFL);   // Re Register signal handler for default action

    TRACE_ERR("sigaction: info.si_signo = %d", signum);
    TRACE_ERR("sigaction: info.si_errno = %d", info->si_errno);
    if (info->si_code >= 0 && info->si_code <= 2)
        {TRACE_ERR("sigaction: info.si_code  = %d (%s)", info->si_code, si_codes[info->si_code]);}
    else
        {TRACE_ERR("sigaction: info.si_code  = %d ", info->si_code);}
    TRACE_ERR("sigaction: info.si_addr  = %p", info->si_addr);

    FlowTraceSendTrace(0,0,"",0,0,0, "sigaction: info.si_signo = %d\n", signum);
    FlowTraceSendTrace(0,0,"",0,0,0, "sigaction: info.si_errno = %d\n", info->si_errno);
    FlowTraceSendTrace(0,0,"",0,0,0, "sigaction: info.si_addr  = %p\n", info->si_addr);

    flush_log();

    flush_log();
    SIG_DFL(signum);
    //exit (-1);
}

void init_sigaction()
{
    struct sigaction action;
    TRACE_INFO("sigaction: init_sigaction\n");
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = signal_segv;
    action.sa_flags = SA_SIGINFO;
    if(sigaction(SIGSEGV, &action, NULL) < 0)
        TRACE_ERR("sigaction: error on sigaction\n");
}
