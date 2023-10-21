#ifndef CPU_H
#define CPU_H

#include "image.h"
#include "sdl_wrapper.h"

typedef struct cpu_instance cpu_instance_t;

enum CpuResult {
	OK,
	IO_ERROR,
	MEMORY_ERROR,
	INSTRUCTION_NOT_FOUND,
	THREAD_ERROR,
	INVALID_STATE
};

enum CpuResult cpu_create_instance(cpu_instance_t** instance);

enum CpuResult cpu_start(cpu_instance_t* instance);

enum CpuResult cpu_init(cpu_instance_t* cpu, char* rom, void(* frame_callback)(int, uint8_t*, sdl_view_t*, image_t*, pthread_mutex_t*), uint8_t* rgb24,
		sdl_view_t* view, pthread_mutex_t* mu);

enum CpuResult cpu_stop(cpu_instance_t* instance);

image_t* cpu_get_image_inst(cpu_instance_t* instance);

#endif // CPU_H
