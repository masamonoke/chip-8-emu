#include <stdlib.h>

#include <log.h>

#include <cpu.h>

void run(cpu_instance_t* inst, char* rom) {
	cpu_start(inst, rom);
}

int main(int argc, char** argv) {
	cpu_instance_t* cpu_instance;
	enum CpuResult res;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <path to executable>\n", argv[0]);
		return 1;
	}

	cpu_instance = NULL;
	res = cpu_create_instance(&cpu_instance);
	if (res == MEMORY_ERROR) {
		log_error("Memory error");
		exit(1);
	}

	run(cpu_instance, argv[1]);

	return 0;
}
