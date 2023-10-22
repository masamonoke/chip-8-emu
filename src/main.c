#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include <log.h>

#include <cpu.h>
#include <sdl_wrapper.h>
#include <utils.h>

// WARNING: somehow it works but some game events not trigger

void frame_callback(int height, uint8_t* rgb24, sdl_view_t* view, image_t* image, pthread_mutex_t* mu) {
	pthread_mutex_lock(mu);
	image_copy_to_rgb24(image, rgb24, 255, 20, 20);
	sdl_wrapper_set_frame_rgb24(view, rgb24, height);
	pthread_mutex_unlock(mu);
}

void key_callback(sdl_view_t* view, pthread_mutex_t* mu, uint8_t* keypad) {
	int i;
	int events_count;
	SDL_Event* events;
	SDL_Event e;
	int key;

	pthread_mutex_lock(mu);
	events_count = sdl_wrapper_get_events_count(view);
	events = sdl_wrapper_get_events(view);
	for (i = 0; i < events_count; i++) {
		e = events[i];
		if (e.type == SDL_KEYDOWN) {
			switch (e.key.keysym.sym) {
				case SDLK_1:
					key = 0;
					break;
				case SDLK_2:
					key = 1;
					break;
				case SDLK_3:
					key = 2;
					break;
				case SDLK_4:
					key = 3;
					break;
				case SDLK_q:
					key = 4;
					break;
				case SDLK_w:
					key = 5;
					break;
				case SDLK_e:
					key = 6;
					break;
				case SDLK_r:
					key = 7;
					break;
				case SDLK_a:
					key = 8;
					break;
				case SDLK_s:
					key = 9;
					break;
				case SDLK_d:
					key = 10;
					break;
				case SDLK_f:
					key = 11;
					break;
				case SDLK_z:
					key = 12;
					break;
				case SDLK_x:
					key = 13;
					break;
				case SDLK_c:
					key = 14;
					break;
				case SDLK_v:
					key = 15;
					break;
				default:
					log_info("Not keypad key pressed");
					continue;
			}
			log_info("Key %d is pressed", key);
			keypad[key] = 1;
		}
	}
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
	pthread_mutex_t cpu_mu;
	enum CpuResult cpu_res;
	pthread_mutex_t event_mu;

	width = 64;
	height = 32;
	rgb24 = calloc(width * height * 3, sizeof(uint8_t));
	view = sdl_wrapper_create_view("CHIP-8", width, height, window_scale);
	if (pthread_mutex_init(&cpu_mu, NULL) != 0) {
		log_error("Mutex init failed");
		exit(1);
	}
	if (pthread_mutex_init(&event_mu, NULL) != 0) {
		log_error("Mutex init failed");
		exit(1);
	}
	cpu_res = cpu_init(inst, rom, frame_callback, rgb24, view, &cpu_mu, key_callback);
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
		int tmp = 0;
		sdl_wrapper_update(view, &tmp);
		events_count = sdl_wrapper_get_events_count(view);
		new_events = sdl_wrapper_get_events(view);
		for (i = 0; i < events_count; i++) {
			if (new_events[i].type == SDL_QUIT) {
				quit = true;
				continue;
			}
		}
		pthread_mutex_lock(&event_mu);
		sdl_wrapper_set_events(view, new_events, events_count);
		pthread_mutex_unlock(&event_mu);
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
