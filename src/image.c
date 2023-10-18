#include "image.h"

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#include <log.h>

struct image {
	int cols;
	int rows;
	uint8_t* data;
};

image_t* image_create(int r, int c) {
	image_t* i;

	i = malloc(sizeof(struct image));
	i->rows = r;
	i->cols = c;
	i->data = malloc(sizeof(uint8_t) * r * c);

	return i;
}

uint8_t* image_row(image_t* inst, int r) {
	if (r < 0 || r > inst->rows) {
		log_error("Row=%d out of bounds", r);
		exit(1);
	}
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
	if (c < 0 || c >= inst->cols) {
		log_error("Column=%d is out of bounds", c);
		exit(1);
	}
	return &image_row(inst, r)[c];
}

void image_set_all(image_t *inst, uint8_t value) {
	memset(inst->data, value, inst->rows * inst->cols);
}

void image_destroy(image_t* inst) {
	free(inst->data);
	free(inst);
}

void image_copy_to_rgb24(image_t* inst, uint8_t* dst, int red_scale, int green_scale, int blue_scale) {
	int row, col;

	for (row = 0; row < inst->rows; row++) {
		for (col = 0; col < inst->cols; col++) {
			dst[(row * inst->cols + col) * 3] = *image_at(inst, col, row) * red_scale;
			dst[(row * inst->cols + col) * 3 + 1] = *image_at(inst, col, row) * green_scale;
			dst[(row * inst->cols + col) * 3 + 2] = *image_at(inst, col, row) * blue_scale;
		}
	}
}
