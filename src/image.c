#include "image.h"

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

struct image {
	int cols;
	int rows;
	uint8_t* data;
};

image_t* image_create(int r, int c) {
	image_t* i;

	i = malloc(sizeof(image_t));
	i->rows = r;
	i->cols = c;
	i->data = malloc(sizeof(uint8_t) * r * c);

	return i;
}

uint8_t* image_row(image_t* inst, int r) {
	return &inst->data[r * inst->cols];
}

static bool xor(image_t* inst, int c, int r, uint8_t value) {
	uint8_t* current_value;
	uint8_t prev_value;

	current_value = image_at(inst, c, r);
	prev_value = *current_value;
	*current_value ^= value;
	return current_value == 0 && prev_value > 0;
}

bool image_xor_sprite(image_t *inst, int c, int r, int height, uint8_t *sprite) {
	bool pixel_disabled;
	int y, x;
	int cur_r, cur_c;
	uint8_t sprite_byte;
	uint8_t sprite_val;

	pixel_disabled = false;
	for (y = 0; y < height; y++) {
		cur_r = r + y;
		while (cur_r >= inst->rows) {
			cur_r -= inst->rows;
		}
		sprite_byte = sprite[y];
		for (x = 0; x < 8; x++) {
			cur_c = c + x;
			while (cur_c >= inst->cols) {
				cur_c -= inst->cols;
			}
			//scanning sprite byte from msb to lsb and check if it is disabled§
			//getting x bit from sprite_byte
			sprite_val = (sprite_byte & (0x80 >> x)) >> (7 - x);
			pixel_disabled |= xor(inst, cur_c, cur_r, sprite_val);
		}
	}
	return pixel_disabled;
}


void image_draw_to_stdout(image_t* inst) {
	int r, c;

	for (r = 0; r < inst->rows; r++) {
		for (c = 0; c < inst->cols; c++) {
			if (*image_at(inst, c, r) > 0) {
				printf("X");
			} else {
				printf(" ");
			}
		}
		printf("\n");
	}
	printf("\n");
}

uint8_t* image_at(image_t* inst, int c, int r) {
	return &image_row(inst, r)[c];
}

void image_set_all(image_t *inst, uint8_t value) {
	memset(inst->data, value, inst->rows * inst->cols);
}

void image_destroy(image_t* inst) {
	free(inst->data);
	free(inst);
}
