#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct image image_t;

image_t* image_create(int r, int c);

uint8_t* image_row(image_t* inst, int r);

uint8_t* image_at(image_t* inst, int c, int r);

void image_set_all(image_t* inst, uint8_t value);

bool image_xor_sprite(image_t* inst, int c, int r, int height, uint8_t* sprite);

void image_copy_to_rgb24(image_t* inst, uint8_t* dst, int red_scale, int green_scale, int blue_scale);

void image_draw_to_stdout(image_t* inst);

void image_destroy(image_t* inst);

#endif // IMAGE_H
