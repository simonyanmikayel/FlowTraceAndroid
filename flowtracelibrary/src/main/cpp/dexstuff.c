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
#include "corkscrew.h"
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
    int ret = 1;
    d.dvm_hand = dlopen("libdvm.so", RTLD_NOW);
    if (0 == d.dvm_hand) {
        TRACE_ERR("dlopen libdvm.so fail\n");
        return 0;
    }

    //ret = ret && ( d.dvmThreadSelf_fnPtr = mydlsym(d.dvm_hand, "_Z13dvmThreadSelfv") );
    //ret = ret && ( d.dvmStringFromCStr_fnPtr = mydlsym(d.dvm_hand, "_Z32dvmCreateStringFromCstrAndLengthPKcj") );
    //ret = ret && ( d.dvmGetSystemClassLoader_fnPtr = mydlsym(d.dvm_hand, "_Z23dvmGetSystemClassLoaderv") );
    //ret = ret && ( d.dvmIsClassInitialized_fnPtr = mydlsym(d.dvm_hand, "_Z21dvmIsClassInitializedPK11ClassObject") );
    //ret = ret && ( d.dvmInitClass_fnPtr = mydlsym(d.dvm_hand, "dvmInitClass") );
    ret = ret && ( d.dvmFindVirtualMethodHierByDescriptor_fnPtr = mydlsym(d.dvm_hand, "_Z36dvmFindVirtualMethodHierByDescriptorPK11ClassObjectPKcS3_") );
    ret = ret && ( d.dvmFindDirectMethodByDescriptor_fnPtr = mydlsym(d.dvm_hand, "_Z31dvmFindDirectMethodByDescriptorPK11ClassObjectPKcS3_") );
    //ret = ret && ( d.dvmIsStaticMethod_fnPtr = mydlsym(d.dvm_hand, "_Z17dvmIsStaticMethodPK6Method") );
    //ret = ret && ( d.dvmAllocObject_fnPtr = mydlsym(d.dvm_hand, "dvmAllocObject") );
    //ret = ret && ( d.dvmCallMethodV_fnPtr = mydlsym(d.dvm_hand, "_Z14dvmCallMethodVP6ThreadPK6MethodP6ObjectbP6JValueSt9__va_list") );
    //ret = ret && ( d.dvmCallMethodA_fnPtr = mydlsym(d.dvm_hand, "_Z14dvmCallMethodAP6ThreadPK6MethodP6ObjectbP6JValuePK6jvalue") );
    //ret = ret && ( d.dvmAddToReferenceTable_fnPtr = mydlsym(d.dvm_hand, "_Z22dvmAddToReferenceTableP14ReferenceTableP6Object") );
    //ret = ret && ( d.dvmSetNativeFunc_fnPtr = mydlsym(d.dvm_hand, "_Z16dvmSetNativeFuncP6MethodPFvPKjP6JValuePKS_P6ThreadEPKt") );
    ret = ret && ( d.dvmUseJNIBridge_fnPtr = mydlsym(d.dvm_hand, "_Z15dvmUseJNIBridgeP6MethodPv") );
    ret = ret && ( d.dvmDecodeIndirectRef_fnPtr =  mydlsym(d.dvm_hand, "_Z20dvmDecodeIndirectRefP6ThreadP8_jobject") );
    //ret = ret && ( d.dvmLinearSetReadWrite_fnPtr = mydlsym(d.dvm_hand, "_Z21dvmLinearSetReadWriteP6ObjectPv") );
    //ret = ret && ( d.dvmGetCurrentJNIMethod_fnPtr = mydlsym(d.dvm_hand, "_Z22dvmGetCurrentJNIMethodv") );
    //ret = ret && ( d.dvmFindInstanceField_fnPtr = mydlsym(d.dvm_hand, "_Z20dvmFindInstanceFieldPK11ClassObjectPKcS3_") );
    //ret = ret && ( d.dvmCallJNIMethod_fnPtr = mydlsym(d.dvm_hand, "_Z16dvmCallJNIMethodPKjP6JValuePK6MethodP6Thread") );
    ret = ret && ( d.dvmDumpAllClasses_fnPtr = mydlsym(d.dvm_hand, "_Z17dvmDumpAllClassesi") );
    ret = ret && ( d.dvmDumpClass_fnPtr = mydlsym(d.dvm_hand, "_Z12dvmDumpClassPK11ClassObjecti") );
    ret = ret && ( d.dvmFindLoadedClass_fnPtr = mydlsym(d.dvm_hand, "_Z18dvmFindLoadedClassPKc") );
    //ret = ret && ( d.dvmHashForeach_fnPtr = mydlsym(d.dvm_hand, "_Z14dvmHashForeachP9HashTablePFiPvS1_ES1_") );
    ret = ret && ( d.gDvm = mydlsym(d.dvm_hand, "gDvm") );

    return ret;
}

void init_dalvik_hook(void)
{
    if ( my_dexstuff_resolv_dvm() )
        if( do_patch() )
            //if( init_corkscrew() )
            ;
}
