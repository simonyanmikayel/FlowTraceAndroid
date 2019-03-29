/*
 * corkscrew.c
 *
 *  Created on: Aug 8, 2018
 *      Author: misha
 */
#include <jni.h>
#include <android/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <dlfcn.h>
#include "corkscrew.h"
#include "flowtrace.h"

typedef void (*t_get_backtrace_symbols)(const backtrace_frame_t* backtrace, unsigned int frames, backtrace_symbol_t* symbols);
//typedef unsigned int (*t_unwind_backtrace_signal_arch)(siginfo_t* si, void* sc, const map_info_t* lst, backtrace_frame_t* bt, size_t ignore_depth, size_t max_depth);
typedef unsigned int (*t_unwind_backtrace)(backtrace_frame_t* bt, size_t ignore_depth, size_t max_depth);

t_get_backtrace_symbols _get_backtrace_symbols;
//t_unwind_backtrace_signal_arch unwind_backtrace_signal_arch;
t_unwind_backtrace _unwind_backtrace;
int corkscrew_initialised;

int init_corkscrew( void )
{
	void * libcorkscrew = dlopen("libcorkscrew.so", RTLD_NOW);
	if (libcorkscrew) {
//		unwind_backtrace_signal_arch = (t_unwind_backtrace_signal_arch) dlsym(libcorkscrew, "unwind_backtrace_signal_arch");
		_unwind_backtrace = (t_unwind_backtrace) dlsym(libcorkscrew, "unwind_backtrace");
//		acquire_my_map_info_list = (t_acquire_my_map_info_list) dlsym(libcorkscrew, "acquire_my_map_info_list");
//		release_my_map_info_list = (t_release_my_map_info_list) dlsym(libcorkscrew, "release_my_map_info_list");
		_get_backtrace_symbols  = (t_get_backtrace_symbols) dlsym(libcorkscrew, "get_backtrace_symbols");
//		free_backtrace_symbols = (t_free_backtrace_symbols) dlsym(libcorkscrew, "free_backtrace_symbols");
	}

	corkscrew_initialised = (_get_backtrace_symbols != 0) && (_unwind_backtrace != 0);
    TRACE_INFO("corkscrew_initialised =  %d\n", corkscrew_initialised);
	return 1;
}

unsigned int get_backtrace(backtrace_frame_t* frames, backtrace_symbol_t* symbols, unsigned int stack_size)
{
	if (!corkscrew_initialised)
		return 0;

	unsigned int frame_count = _unwind_backtrace(frames, 2, stack_size);
    TRACE_INFO("get_backtrace: frame_count =  %d\n", frame_count);

	_get_backtrace_symbols(frames, frame_count, symbols);
	unsigned int i;
	for (i = 0; i < frame_count; ++i) {
		const char *method = symbols[i].demangled_name;
		if (!method)
			method = symbols[i].symbol_name;
		if (!method)
			method = "?";
		//__android_log_print(ANDROID_LOG_ERROR, "DUMP", "%s", method);
		const char *file = symbols[i].map_name;
		if (!file)
			file = "-";
        TRACE_INFO("get_backtrace: %d file: %s, method: %s\n", i, file, method);
    }


	return frame_count;
}

//void print_backtrace()
//{
//    backtrace_frame_t frames[256];
//    ssize_t count = unwind_backtrace(frames, 2, sizeof(frames));
//    backtrace_symbol_t symbols[count];
//
//    get_backtrace_symbols(frames, count, symbols);
//    for(size_t i = 0; i < count; i++){
//        char line[1024] = {0};
//        format_backtrace_line(i, frames + i, symbols +i, line, 1024);
//        log(LOG_INFO, " #%d %s\n", i,  line); /* use your logging here */
//    }
//    free_backtrace_symbols(symbols, count);
//}

