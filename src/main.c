#include <stdlib.h>

#include <log.h>

#include <cpu.h>
#include <sdl_wrapper.h>

void run(cpu_instance_t* inst, char* rom) {
	/* int width, height; */
	/* sdl_view_t* view; */
	/* uint8_t* rgb24; */

	/* width = 64; */
	/* height = 32; */
	/* view = sdl_wrapper_create_view("CHIP-8", 64, 32, 8); */
	/* rgb24 = calloc(width * height * 3, sizeof(uint8_t)); */
	/* sdl_wrapper_set_frame_rgb24(view, rgb24, height); */
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
