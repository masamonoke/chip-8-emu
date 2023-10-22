#include "sdl_wrapper.h"

#include <pthread.h>

#include <SDL2/SDL.h>
#include <log.h>

struct sdl_view {
	SDL_Window* window;
	SDL_Renderer* renderer;
	SDL_Texture* window_texture;
	SDL_Event* events;
	size_t events_count;
	char* title;
	pthread_mutex_t mu;
	int width;
	int height;
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

	view->events = malloc(sizeof(SDL_Event) * EVENTS_COUNT);
	pthread_mutex_init(&view->mu, NULL);
	view->width = width;
	view->height = height;
	view->events_count = 0;

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

	if (!view->window_texture) {
		log_error("Need to set the frame before calling update.");
		exit(1);
	}
	i = 0;
	while (SDL_PollEvent(&e)) {
		view->events[i++] = e;
	}

	if (i != 0) {
		view->events_count = i - 1;
	}

	SDL_RenderCopy(view->renderer, view->window_texture, NULL, NULL);
	SDL_RenderPresent(view->renderer);

	char title[255];
	sprintf(title, "%s", view->title);
	SDL_SetWindowTitle(view->window, title);

	*events_count = view->events_count;
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

int sdl_wrapper_get_view_height(sdl_view_t* view) {
	return view->height;
}

// TODO: probably need mutex on read value or make atomic
SDL_Event* sdl_wrapper_get_events(sdl_view_t* view) {
	return view->events;
}

size_t sdl_wrapper_get_events_count(sdl_view_t* view) {
	int events_count;

	events_count = view->events_count;
	view->events_count = 0;
	return events_count;
}

void sdl_wrapper_set_events(sdl_view_t* view, SDL_Event* events, int events_count) {
	int i, j;
	int end;

	if (events_count != 0) {
		if (events_count + view->events_count < EVENTS_COUNT) {
			i = view->events_count;
			end = events_count + view->events_count;
		} else {
			i = 0;
			end = events_count;
		}

		for (j = 0; i < end; i++, j++) {
			view->events[i] = events[j];
		}
		view->events_count = end;
	}
}

int sdl_wrapper_get_view_width(sdl_view_t* view) {
	return view->width;
}
