//
// Created by misha on 9/18/2018.
//

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <elf.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include "dalvik_globals.h"
#include "flowtrace.h"

struct dexstuff_t d;

void dalvik_dump_class(struct dexstuff_t *dex, char *clname)
{
    if (strlen(clname) > 0) {
        void *target_cls = dex->dvmFindLoadedClass_fnPtr(clname);
        if (target_cls) {
            TRACE_INFO("dvmDumpClass_fnPtr %s\n", clname);
            dex->dvmDumpClass_fnPtr(target_cls, (void*)1);
        }
        else {
            TRACE_ERR("target_cls not found %s\n", clname);
        }
    }
    else {
        TRACE_INFO("dvmDumpAllClasses_fnPtr %s\n", clname);
        dex->dvmDumpAllClasses_fnPtr(0);
    }
}

static void* mydlsym(void *hand, const char *name)
{
    void* ret = dlsym(hand, name);
    if (!ret) TRACE_ERR("addr of %s = %p\n", name, ret);
    return ret;
}

static int my_dexstuff_resolv_dvm( void )
{
    d.dvm_hand = dlopen("libdvm.so", RTLD_NOW); //libart.so libdvm.so
    if (0 == d.dvm_hand) {
        TRACE_ERR("dlopen libdvm.so fail\n");
        return 0;
    }

    d.dvmFindVirtualMethodHierByDescriptor_fnPtr = mydlsym(d.dvm_hand, "_Z36dvmFindVirtualMethodHierByDescriptorPK11ClassObjectPKcS3_");
    d.dvmFindDirectMethodByDescriptor_fnPtr = mydlsym(d.dvm_hand, "_Z31dvmFindDirectMethodByDescriptorPK11ClassObjectPKcS3_");
    d.dvmUseJNIBridge_fnPtr = mydlsym(d.dvm_hand, "_Z15dvmUseJNIBridgeP6MethodPv");
    d.dvmDumpAllClasses_fnPtr = mydlsym(d.dvm_hand, "_Z17dvmDumpAllClassesi");
    d.dvmDumpClass_fnPtr = mydlsym(d.dvm_hand, "_Z12dvmDumpClassPK11ClassObjecti");
    d.dvmFindLoadedClass_fnPtr = mydlsym(d.dvm_hand, "_Z18dvmFindLoadedClassPKc");

    return d.dvmFindVirtualMethodHierByDescriptor_fnPtr &&
           d.dvmFindDirectMethodByDescriptor_fnPtr &&
           d.dvmUseJNIBridge_fnPtr &&
           d.dvmDumpAllClasses_fnPtr &&
           d.dvmDumpClass_fnPtr &&
           d.dvmFindLoadedClass_fnPtr;
}

void init_dalvik_hook(void)
{
    //TRACE_ERR("We do not use dalvik hook\n");
//    if ( my_dexstuff_resolv_dvm() )
//        do_patch();
}
