#ifndef CPU
#define CPU

typedef struct cpu_instance cpu_instance_t;

enum CpuResult {
	OK,
	IO_ERROR,
	MEMORY_ERROR,
	INSTRUCTION_NOT_FOUND
};

enum CpuResult cpu_create_instance(cpu_instance_t** instance);

void cpu_start(cpu_instance_t* instance, char* rom);

#endif // CPU
