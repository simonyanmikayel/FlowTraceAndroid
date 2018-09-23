/*
 * corkscrew.h
 *
 *  Created on: Aug 8, 2018
 *      Author: misha
 */

#ifndef LIBRARIES_UDP_TRACING_UDP_TRACING_INC_CORKSCREW_H_
#define LIBRARIES_UDP_TRACING_UDP_TRACING_INC_CORKSCREW_H_

typedef struct map_info_t map_info_t;
typedef struct {
    void* absolute_pc;
    void* stack_top;
    unsigned int stack_size;
} backtrace_frame_t;
typedef struct {
	void* relative_pc;
	void* relative_symbol_addr;
    char* map_name;
    char* symbol_name;
    char* demangled_name;
} backtrace_symbol_t;

int init_corkscrew( void );
unsigned int get_backtrace(backtrace_frame_t* frames, backtrace_symbol_t* symbols, unsigned int stack_size);

#endif /* LIBRARIES_UDP_TRACING_UDP_TRACING_INC_CORKSCREW_H_ */
