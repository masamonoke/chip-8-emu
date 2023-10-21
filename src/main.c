#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include <log.h>

#include <cpu.h>
#include <sdl_wrapper.h>
#include <utils.h>

void frame_callback(int height, uint8_t* rgb24, sdl_view_t* view, image_t* image, pthread_mutex_t* mu) {
	pthread_mutex_lock(mu);
	image_copy_to_rgb24(image, rgb24, 255, 20, 20);
	sdl_wrapper_set_frame_rgb24(view, rgb24, height);
	pthread_mutex_unlock(mu);
}

void run(cpu_instance_t* inst, char* rom) {
	bool quit;
	sdl_view_t* view = NULL;
	uint8_t* rgb24 = NULL;
	SDL_Event* new_events;
	int width, height;
	int events_count;
	int i;
	int window_scale = 8;
	pthread_mutex_t mu;
	enum CpuResult cpu_res;

	width = 64;
	height = 32;
	rgb24 = calloc(width * height * 3, sizeof(uint8_t));
	view = sdl_wrapper_create_view("CHIP-8", width, height, window_scale);
	if (pthread_mutex_init(&mu, NULL) != 0) {
		log_error("Mutex init failed");
		exit(1);
	}
	cpu_res = cpu_init(inst, rom, frame_callback, rgb24, view, &mu);
	if (cpu_res != OK) {
		log_error("Error initializing CPU instance");
		exit(1);
	}
	cpu_res = cpu_start(inst);
	if (cpu_res != OK) {
		exit(1);
	}
	quit = false;
	while (!quit) {
		new_events = sdl_wrapper_update(view, &events_count);
		for (i = 0; i < events_count; i++) {
			if (new_events[i].type == SDL_QUIT) {
				quit = true;
			}
		}
		usleep(10);
	}
	cpu_stop(inst);
	sdl_wrapper_destroy_view(view);
	free(rgb24);
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
		log_error("CPU memory error");
		exit(1);
	}
	if (cpu_instance == NULL) {
		log_error("CPU instance is not initialized");
		exit(1);
	}

	run(cpu_instance, argv[1]);

	return EXIT_SUCCESS;
}
