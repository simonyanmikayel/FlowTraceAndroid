/*
 *  Collin's Dynamic Dalvik Instrumentation Toolkit for Android
 *  Collin Mulliner <collin[at]mulliner.org>
 *
 *  (c) 2012,2013
 *
 *  License: LGPL v2.1
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>

#include "dexstuff.h"
#include "dalvik_hook.h"
#include "flowtrace.h"

static void* dalvik_hook(struct dalvik_hook_t *h)
{
	if (h->debug_me)
		TRACE_INFO("dalvik_hook: class %s\n", h->clname);
	
	void *target_cls = d.dvmFindLoadedClass_fnPtr(h->clname);
	if (h->debug_me)
		TRACE_INFO("class = 0x%x\n", target_cls);

	// print class in logcat
	if (h->dump && target_cls)
		d.dvmDumpClass_fnPtr(target_cls, (void*)1);

	if (!target_cls) {
		if (h->debug_me)
			TRACE_INFO("target_cls == 0\n");
		return (void*)0;
	}

	h->method = d.dvmFindVirtualMethodHierByDescriptor_fnPtr(target_cls, h->method_name, h->method_sig);
	if (h->method == 0) {
		h->method = d.dvmFindDirectMethodByDescriptor_fnPtr(target_cls, h->method_name, h->method_sig);
	}

	// constrcutor workaround, see "dalvik_prepare" below
	if (!h->resolvm) {
		h->cls = target_cls;
		h->mid = (void*)h->method;
	}

	if (h->debug_me)
		TRACE_INFO("%s(%s) = 0x%x\n", h->method_name, h->method_sig, h->method);

	if (h->method) {
		h->insns = h->method->insns;

		if (h->debug_me) {
			TRACE_INFO("nativeFunc %x\n", h->method->nativeFunc);
		
			TRACE_INFO("insSize = 0x%x  registersSize = 0x%x  outsSize = 0x%x\n", h->method->insSize, h->method->registersSize, h->method->outsSize);
		}

		h->iss = h->method->insSize;
		h->rss = h->method->registersSize;
		h->oss = h->method->outsSize;
	
		h->method->insSize = h->n_iss;
		h->method->registersSize = h->n_rss;
		h->method->outsSize = h->n_oss;

		if (h->debug_me) {
			TRACE_INFO("shorty %s\n", h->method->shorty);
			TRACE_INFO("name %s\n", h->method->name);
			TRACE_INFO("arginfo %x\n", h->method->jniArgInfo);
		}
		h->method->jniArgInfo = 0x80000000; // <--- also important
		if (h->debug_me) {
			TRACE_INFO("noref %c\n", h->method->noRef);
			TRACE_INFO("access %x\n", h->method->a);
		}
		h->access_flags = h->method->a;
		h->method->a = h->method->a | h->af; // make method native
		if (h->debug_me)
			TRACE_INFO("access %x\n", h->method->a);
	
		d.dvmUseJNIBridge_fnPtr(h->method, h->native_func);
		
		if (h->debug_me)
			TRACE_INFO("patched %s to: 0x%x\n", h->method_name, h->native_func);

		return (void*)1;
	}
	else {
		if (h->debug_me)
			TRACE_INFO("could NOT patch %s\n", h->method_name);
	}

	return (void*)0;
}

int dalvik_hook_setup(struct dalvik_hook_t *h, char *clname, char *meth, char *sig, int ns, void *func)
{
	void* res;
	strcpy(h->clname, clname);
	strncpy(h->clnamep, clname+1, strlen(clname)-2);
	strcpy(h->method_name, meth);
	strcpy(h->method_sig, sig);
	h->n_iss = ns;
	h->n_rss = ns;
	h->n_oss = 0;
	h->native_func = func;

	//h->sm = 0; // set by hand if needed

	h->af = 0x0100; // native, modify by hand if needed

	h->resolvm = 0; // don't resolve method on-the-fly, change by hand if needed

	//h->debug_me = 0;

	res = dalvik_hook(h);

	if (h->debug_me)
		TRACE_INFO("[%s %s] cls = 0x%x mid = 0x%x\n", clname, meth, h->cls, h-> mid);

	return 0 != res;
}

int resolveStaticMetod(struct jmethod_t *h, char *szCls, char *szMeth, char *szSig, JNIEnv *env)
{
	h->mid = 0;
	h->cls = (*env)->FindClass(env, szCls);
	if (h->cls)
		h->mid = (*env)->GetStaticMethodID(env, h->cls, szMeth, szSig);
    return h->mid != 0;
}

int resolveDynamicMetod(struct jmethod_t *h, char *szCls, char *szMeth, char *szSig, JNIEnv *env)
{
	h->mid = 0;
	h->cls = (*env)->FindClass(env, szCls);
	if (h->cls)
		h->mid = (*env)->GetMethodID(env, h->cls, szMeth, szSig);
    return h->mid != 0;
}

int dalvik_resolve(struct dalvik_hook_t *h, char *clname, char *meth, char *sig, JNIEnv *env)
{
	do
	{
//		if (env)
//			enter_critical_section(__FUNCTION__, __LINE__);
		if (h->mid == 0)
		{
			strcpy(h->clname, clname);
			strncpy(h->clnamep, clname+1, strlen(clname)-2);
			strcpy(h->method_name, meth);
			strcpy(h->method_sig, sig);

			if (env)
			{
				h->cls = (*env)->FindClass(env, h->clnamep);
				if (h->cls) {
					if (h->sm)
						h->mid = (*env)->GetStaticMethodID(env, h->cls, h->method_name, h->method_sig);
					else
						h->mid = (*env)->GetMethodID(env, h->cls, h->method_name, h->method_sig);
				}
			}
			else
			{
				h->cls = d.dvmFindLoadedClass_fnPtr(h->clname);
				if (h->cls) {
					h->mid = (void*)d.dvmFindVirtualMethodHierByDescriptor_fnPtr(h->cls, h->method_name, h->method_sig);
					if (h->method == 0) {
						h->mid = (void*)d.dvmFindDirectMethodByDescriptor_fnPtr(h->cls, h->method_name, h->method_sig);
					}
				}
			}
		}
//		if (env)
//			leave_critical_section(__FUNCTION__, __LINE__);
	} while(0);

	if (h->debug_me)
		TRACE_INFO("[%s %s] cls = 0x%x mid = 0x%x\n", clname, meth, h->cls, h-> mid);

	return h->mid != 0;
}

int dalvik_prepare(struct dalvik_hook_t *h, JNIEnv *env)
{

	// this seems to crash when hooking "constructors"

	if (h->resolvm) {
		h->cls = (*env)->FindClass(env, h->clnamep);
		if (h->debug_me)
			TRACE_INFO("cls = 0x%x\n", h->cls);
		if (!h->cls)
			return 0;
		if (h->sm)
			h->mid = (*env)->GetStaticMethodID(env, h->cls, h->method_name, h->method_sig);
		else
			h->mid = (*env)->GetMethodID(env, h->cls, h->method_name, h->method_sig);
		if (h->debug_me)
			TRACE_INFO("mid = 0x%x\n", h-> mid);
		if (!h->mid)
			return 0;
	}

	h->method->insSize = h->iss;
	h->method->registersSize = h->rss;
	h->method->outsSize = h->oss;
	h->method->a = h->access_flags;
	h->method->jniArgInfo = 0;
	h->method->insns = h->insns;
    return 0;
}

void dalvik_postcall(struct dalvik_hook_t *h)
{
	h->method->insSize = h->n_iss;
	h->method->registersSize = h->n_rss;
	h->method->outsSize = h->n_oss;

	h->method->jniArgInfo = 0x80000000;
	h->access_flags = h->method->a;
	h->method->a = h->method->a | h->af;

	d.dvmUseJNIBridge_fnPtr(h->method, h->native_func);
	
	if (h->debug_me)
		TRACE_INFO("patched BACK %s to: 0x%x\n", h->method_name, h->native_func);
}
