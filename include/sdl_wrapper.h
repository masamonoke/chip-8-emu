#ifndef SDL_WRAPPER_H
#define SDL_WRAPPER_H

#include <SDL2/SDL_events.h>

typedef struct sdl_view sdl_view_t;

sdl_view_t* sdl_wrapper_create_view(char* title, int width, int height, int window_scale);

void sdl_wrapper_destroy_view(sdl_view_t* view);

SDL_Event* sdl_wrapper_update(sdl_view_t* view, int* events_count);

void sdl_wrapper_set_frame_rgb24(sdl_view_t* view, uint8_t* rgb24, int height);

#endif // SDL_WRAPPER_H
