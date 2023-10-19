#include "sdl_wrapper.h"

#include <pthread.h>

#include <SDL2/SDL.h>
#include <log.h>

struct sdl_timer {
	uint32_t start_ticks;
};

static void sdl_timer_start(struct sdl_timer* timer) {
	timer->start_ticks = SDL_GetTicks();
}

static uint32_t sdl_timer_ms(struct sdl_timer* timer) {
	return SDL_GetTicks() - timer->start_ticks;
}

static void sdl_timer_init(struct sdl_timer* timer) {
	timer->start_ticks = 0;
}

struct sdl_view {
	SDL_Window* window;
	SDL_Renderer* renderer;
	SDL_Texture* window_texture;
	SDL_Event* events;
	struct sdl_timer timer;
	int updates_count;
	char* title;
	pthread_mutex_t mu;
};

#define EVENTS_COUNT 10000
sdl_view_t* sdl_wrapper_create_view(char* title, int width, int height, int window_scale) {
	sdl_view_t* view;

	view = malloc(sizeof(struct sdl_view));
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		log_error("%s", SDL_GetError());
		exit(1);
	}

	view->title = title;
	view->window = SDL_CreateWindow(
		title,
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		width * window_scale,
		height * window_scale,
		SDL_WINDOW_SHOWN);
	if (!view->window) {
		log_error("%s", SDL_GetError());
		exit(1);
	}

	view->renderer = SDL_CreateRenderer(view->window, -1, SDL_RENDERER_ACCELERATED / SDL_RENDERER_PRESENTVSYNC);
	if (!view->renderer) {
		log_error("%s", SDL_GetError());
		exit(1);
	}
	SDL_SetRenderDrawColor(view->renderer, 0xFF, 0xFF, 0xFF, 0xFF);

	view->window_texture = SDL_CreateTexture(view->renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, width, height);
	if (!view->window_texture) {
		log_error("%s", SDL_GetError());
		exit(1);
	}

	sdl_timer_init(&view->timer);
	sdl_timer_start(&view->timer);
	view->updates_count = 0;

	view->events = malloc(sizeof(SDL_Event) * EVENTS_COUNT);
	pthread_mutex_init(&view->mu, NULL);

	return view;
}

void sdl_wrapper_destroy_view(sdl_view_t* view) {
	SDL_DestroyTexture(view->window_texture);
	SDL_DestroyRenderer(view->renderer);
	SDL_DestroyWindow(view->window);
	SDL_Quit();
}

SDL_Event* sdl_wrapper_update(sdl_view_t* view, int* events_count) {
	pthread_mutex_lock(&view->mu);
	size_t i;
	SDL_Event e;
	float avg_fps;

	if (!view->window_texture) {
		log_error("Need to set the frame before calling update.");
		exit(1);
	}
	i = 0;
	while (SDL_PollEvent(&e)) {
		view->events[i++] = e;
	}

	SDL_RenderCopy(view->renderer, view->window_texture, NULL, NULL);
	SDL_RenderPresent(view->renderer);

	view->updates_count++;
	avg_fps = view->updates_count / (sdl_timer_ms(&view->timer) / 1000.f);

	char title[255];
	sprintf(title, "%s - %d fps", view->title, (int) avg_fps);
	SDL_SetWindowTitle(view->window, title);
	if (sdl_timer_ms(&view->timer) >= 1000) {
		view->updates_count = 0;
		sdl_timer_start(&view->timer);
	}

	*events_count = i - 1;
	pthread_mutex_unlock(&view->mu);
	return view->events;
}

void sdl_wrapper_set_frame_rgb24(sdl_view_t* view, uint8_t* rgb24, int height) {
	pthread_mutex_lock(&view->mu);
	void* pixel_data;
	int pitch;

	SDL_LockTexture(view->window_texture, NULL, &pixel_data, &pitch);
	memcpy(pixel_data, rgb24, pitch * height);
	SDL_UnlockTexture(view->window_texture);
	pthread_mutex_unlock(&view->mu);
}
