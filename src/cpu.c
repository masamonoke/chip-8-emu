#include "cpu.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <memory.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>

#include <log.h>

#include <utils.h>
#include <image.h>
#include "sdl_wrapper.h"

static const int refresh_rate_hz = 60;
static const int cycle_speed_hz = refresh_rate_hz * 9;
static const int cycles_per_frame = cycle_speed_hz / refresh_rate_hz;

#ifdef DEBUG
#define dbg(...) log_debug(__VA_ARGS__);
#else
#define dbg(...)
#endif

struct cpu_instance {
	uint16_t current_opcode;
	uint8_t memory[4096];
	uint8_t v_registers[16];
	uint16_t index_register;
	uint16_t program_counter;
	uint8_t delay_timer;
	uint8_t sound_timer;
	uint16_t stack[16];
	uint16_t stack_pointer;
	uint8_t keypad_state[16];
	uint64_t num_cycles;
	_Atomic(bool) is_running;
	image_t* image;
	pthread_t thread;
	void (*frame_callback)(int, uint8_t*, sdl_view_t*, image_t*, pthread_mutex_t*);
	uint8_t* rgb24;
	sdl_view_t* view;
	pthread_mutex_t* frame_mutex;
};

enum CpuResult cpu_create_instance(cpu_instance_t** inst) {
	*inst = malloc(sizeof(struct cpu_instance));
	if (*inst == NULL) {
		return MEMORY_ERROR;
	}
	return OK;
}

static enum CpuResult read_file(char* filename, char** buf, unsigned long* len) {
	FILE* f;
	unsigned long file_len;
	f = fopen(filename, "rb");
	if (!f) {
		log_error("Unable to open file %s", filename);
		return IO_ERROR;
	}
	fseek(f, 0, SEEK_END);
	file_len = ftell(f);
	fseek(f, 0, SEEK_SET);
	*buf = (char*) malloc(file_len + 1);
	if (!buf) {
		log_error("Memory allocation error");
		fclose(f);
		return MEMORY_ERROR;
	}
	fread(*buf, file_len, 1, f);
	fclose(f);
	if (len != NULL) {
		*len = file_len;
	}
	return OK;
}

static enum CpuResult load_rom(cpu_instance_t* inst, char* rom) {
	char* buf;
	unsigned long len;
	enum CpuResult res;
	res = read_file(rom, &buf, &len);
	if (res != OK) {
		free(inst);
		return res;
	}
	memcpy(inst->memory + 0x200, buf, len);
	log_info("Loaded %d bytes size rom", len);
	return res;
}

enum CpuResult cpu_init(cpu_instance_t* inst, char* rom, void(* frame_callback)(int, uint8_t*, sdl_view_t*, image_t*, pthread_mutex_t*), uint8_t* rgb24,
		sdl_view_t* view, pthread_mutex_t* mu) {
	memset(inst->memory, 0, sizeof(inst->memory));
	memset(inst->v_registers, 0, sizeof(inst->v_registers));
	memset(inst->keypad_state, 0, sizeof(inst->keypad_state));
	memset(inst->stack, 0, sizeof(inst->keypad_state));
	inst->current_opcode = 0;
	inst->index_register = 0;
	inst->program_counter = 0x200;
	inst->delay_timer = 0;
	inst->sound_timer = 0;
	inst->stack_pointer = 0;
	inst->num_cycles = 0;

	atomic_init(&inst->is_running, false);

	// for 0:
	// 0xF0 is 1111 0000 -> XXXX
	// 0x90 is 1001 0000 -> X  X
	// and etc
	uint8_t fontset[80] = {
		0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
		0x20, 0x60, 0x20, 0x20, 0x70, // 1
		0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
		0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
		0x90, 0x90, 0xF0, 0x10, 0x10, // 4
		0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
		0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
		0xF0, 0x10, 0x20, 0x40, 0x40, // 7
		0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
		0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
		0xF0, 0x90, 0xF0, 0x90, 0x90, // A
		0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
		0xF0, 0x80, 0x80, 0x80, 0xF0, // C
		0xE0, 0x90, 0x90, 0x90, 0xE0, // D
		0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
		0xF0, 0x80, 0xF0, 0x80, 0x80  // F
	};
	memcpy(inst->memory + 0x50, fontset, sizeof(fontset));

	inst->image = image_create(32, 64);
	image_set_all(inst->image, 0);
	inst->frame_callback = frame_callback;
	inst->rgb24 = rgb24;
	inst->view = view;
	inst->frame_mutex = mu;

	return load_rom(inst, rom);
}

/* 1nnn - JP addr */
/* Jump to location nnn. */
/* The interpreter sets the program counter to nnn. */
static void jp(cpu_instance_t* inst, uint16_t addr) {
	inst->program_counter = addr;
	dbg("JP %d", addr);
}

/* 2nnn - CALL addr */
/* Call subroutine at nnn. */
/* The interpreter increments the stack pointer, then puts the current PC on the top of the stack. The PC is then set to nnn. */
static void call(cpu_instance_t* inst, uint16_t addr) {
	inst->stack[inst->stack_pointer++] = inst->program_counter;
	dbg("CALL 0x%X - PUSH 0x%X onto stack", addr, inst->stack[inst->stack_pointer - 1]);
	inst->program_counter = addr;
}

static void skip(cpu_instance_t* inst) {
	inst->program_counter += 4;
	dbg("SKIP from 0x%X to 0x%X", inst->program_counter - 4, inst->program_counter);
}

static void next(cpu_instance_t* inst) {
	inst->program_counter += 2;
	dbg("NEXT from 0x%X to 0x%X", inst->program_counter - 2, inst->program_counter);
}

/* 3xkk - SE Vx, byte */
/* Skip next instruction if Vx = kk. */
/* The interpreter compares register Vx to kk, and if they are equal, increments the program counter by 2. */
static void se(cpu_instance_t* inst, uint8_t reg, uint8_t value) {
	dbg("SE V%d, kk");
	inst->v_registers[reg] == value ? skip(inst) : next(inst);
}

// Skip next instruction if Vx != kk.
static void sne(cpu_instance_t* inst, uint8_t reg, uint8_t value) {
	inst->v_registers[reg] != value ? skip(inst) : next(inst);
}

// Skip next instruction if Vx = Vy. (5xy0 - SE Vx, Vy)
static void sereg(cpu_instance_t* inst, uint8_t reg_x, uint8_t reg_y) {
	inst->v_registers[reg_x] == inst->v_registers[reg_y] ? skip(inst) : next(inst);
}

// 6xkk - LD Vx, byte
// Set Vx = kk.
static void ldim(cpu_instance_t* inst, uint8_t reg, uint8_t value) {
	dbg("V%x <== 0x%X", reg, reg, value);
	inst->v_registers[reg] = value;
	next(inst);
}

// 7xkk - ADD Vx, byte
// Set Vx = Vx + kk.
static void addim(cpu_instance_t* inst, uint8_t reg, uint8_t value) {
	dbg("V%d <== V%d + 0x%X", reg, reg, value);
	inst->v_registers[reg] += value;
	next(inst);
}

// 8xy0 - LD Vx, Vy
// Set Vx = Vy.
static void ldv(cpu_instance_t* inst, uint8_t reg_x, uint8_t reg_y) {
	inst->v_registers[reg_x] = inst->v_registers[reg_y];
	next(inst);
}

// 8xy1 - OR Vx, Vy
// Set Vx = Vx OR Vy.
static void or(cpu_instance_t* inst, uint8_t reg_x, uint8_t reg_y) {
	inst->v_registers[reg_x] |= inst->v_registers[reg_y];
	next(inst);
}
// 8xy2 - AND Vx, Vy
// Set Vx = Vx AND Vy.
static void and(cpu_instance_t* inst, uint8_t reg_x, uint8_t reg_y) {
	inst->v_registers[reg_x] &= inst->v_registers[reg_y];
	next(inst);
}

// 8xy3 - XOR Vx, Vy
// Set Vx = Vx XOR Vy.
static void xor(cpu_instance_t* inst, uint8_t reg_x, uint8_t reg_y) {
	inst->v_registers[reg_x] ^= inst->v_registers[reg_y];
	next(inst);
}

// 8xy4 - ADD Vx, Vy
// Set Vx = Vx + Vy, set VF = carry.
// The values of Vx and Vy are added together. If the result is greater than 8 bits (i.e., > 255,)
// VF is set to 1, otherwise 0. Only the lowest 8 bits of the result are kept, and stored in Vx.
static void add(cpu_instance_t* inst, uint8_t reg_x, uint8_t reg_y) {
	uint16_t res;
	res = inst->v_registers[reg_x] += inst->v_registers[reg_y];
	inst->v_registers[0xF] = res > 0xFF;
	inst->v_registers[reg_x] = res;
	next(inst);
}

/* 8xy5 - SUB Vx, Vy */
/* Set Vx = Vx - Vy, set VF = NOT borrow. */
/* If Vx > Vy, then VF is set to 1, otherwise 0. Then Vy is subtracted from Vx, and the results stored in Vx. */
static void sub(cpu_instance_t* inst, uint8_t reg_x, uint8_t reg_y) {
	inst->v_registers[0xF] = inst->v_registers[reg_x] > inst->v_registers[reg_y];
	inst->v_registers[reg_x] -= inst->v_registers[reg_y];
	next(inst);
}

/* 8xy6 - SHR Vx {, Vy} */
/* Set Vx = Vx SHR 1. */
/* If the least-significant bit of Vx is 1, then VF is set to 1, otherwise 0. Then Vx is divided by 2. */
/* SHR is shift right */
static void shr(cpu_instance_t* inst, uint8_t reg_x) {
	inst->v_registers[0xF] = inst->v_registers[reg_x] & 1;
	inst->v_registers[reg_x] >>= 1;
	next(inst);
}

/* 8xy7 - SUBN Vx, Vy */
/* Set Vx = Vy - Vx, set VF = NOT borrow. */
/* If Vy > Vx, then VF is set to 1, otherwise 0. Then Vx is subtracted from Vy, and the results stored in Vx. */
static void subn(cpu_instance_t* inst, uint8_t reg_x, uint8_t reg_y) {
	inst->v_registers[0xF] = inst->v_registers[reg_y] > inst->v_registers[reg_x];
	inst->v_registers[reg_x] = inst->v_registers[reg_y] - inst->v_registers[reg_x];
	next(inst);
}

/* 8xyE - SHL Vx {, Vy} */
/* Set Vx = Vx SHL 1. */
/* If the most-significant bit of Vx is 1, then VF is set to 1, otherwise to 0. Then Vx is multiplied by 2. */
static void shl(cpu_instance_t* inst, uint8_t reg) {
	inst->v_registers[0xF] = inst->v_registers[reg] > 0x80;
	inst->v_registers[reg] <<= 1;
	next(inst);
}

/* 9xy0 - SNE Vx, Vy */
/* Skip next instruction if Vx != Vy. */
/* The values of Vx and Vy are compared, and if they are not equal, the program counter is increased by 2. */
static void snereg(cpu_instance_t* inst, uint8_t reg_x, uint8_t reg_y) {
	inst->v_registers[reg_x] != inst->v_registers[reg_y] ? skip(inst) : next(inst);
}

/* Annn - LD I, addr */
/* Set I = nnn. */
/* The value of register I is set to nnn. */
static void ldi(cpu_instance_t* inst, uint16_t addr) {
	inst->index_register = addr;
	dbg("I <== 0x%X", inst->index_register, addr);
	next(inst);
}

/* Bnnn - JP V0, addr */
/* Jump to location nnn + V0. */
/* The program counter is set to nnn plus the value of V0. */
static void jpreg(cpu_instance_t* inst, uint16_t addr) {
	inst->program_counter = inst->v_registers[0] + addr;
}

/* Cxkk - RND Vx, byte */
/* Set Vx = random byte AND kk. */
/* The interpreter generates a random number from 0 to 255, which is then ANDed with the value kk. */
/* The results are stored in Vx. See instruction 8xy2 for more information on AND. */
static void rnd(cpu_instance_t* inst, uint8_t reg_x, uint8_t value) {
	inst->v_registers[reg_x] = (rand() % 256) & value;
	next(inst);
}

/* Dxyn - DRW Vx, Vy, nibble */
/* Display n-byte sprite starting at memory location I at (Vx, Vy), set VF = collision. */
/* The interpreter reads n bytes from memory, starting at the address stored in I. */
/* These bytes are then displayed as sprites on screen at coordinates (Vx, Vy). Sprites are XORed onto the existing screen. */
/* If this causes any pixels to be erased, VF is set to 1, otherwise it is set to 0. */
/* If the sprite is positioned so part of it is outside the coordinates of the display, it wraps around to the opposite side of the screen. */
/* See instruction 8xy3 for more information on XOR, and section 2.4, Display, for more information on the Chip-8 screen and sprites. */
static void draw(cpu_instance_t* inst, uint8_t reg_x, uint8_t reg_y, uint8_t n_rows) {
	uint8_t x;
	uint8_t y;
	bool pixels_unset;

	x = inst->v_registers[reg_x];
	y = inst->v_registers[reg_y];
	pixels_unset = image_xor_sprite(inst->image, x, y, n_rows, inst->memory + inst->index_register);
	inst->v_registers[0xF] = pixels_unset;
	next(inst);
}

/* Ex9E - SKP Vx */
/* Skip next instruction if key with the value of Vx is pressed. */
/* Checks the keyboard, and if the key corresponding to the value of Vx is currently in the down position, PC is increased by 2. */
static void skey(cpu_instance_t* inst, uint8_t reg_x) {
	inst->keypad_state[inst->v_registers[reg_x]] ? skip(inst) : next(inst);
}

/* ExA1 - SKNP Vx */
/* Skip next instruction if key with the value of Vx is not pressed. */
/* Checks the keyboard, and if the key corresponding to the value of Vx is currently in the up position, PC is increased by 2. */
static void snkey(cpu_instance_t* inst, uint8_t reg) {
	inst->keypad_state[inst->v_registers[reg]] ? next(inst) : skip(inst);
}

/* Fx07 - LD Vx, DT */
/* Set Vx = delay timer value. */
/* The value of DT is placed into Vx. */
static void rdelay(cpu_instance_t* inst, uint8_t reg) {
	inst->v_registers[reg] = inst->delay_timer;
	next(inst);
}

/* Fx0A - LD Vx, K */
/* Wait for a key press, store the value of the key in Vx. */
/* All execution stops until a key is pressed, then the value of that key is stored in Vx. */
static void waitkey(cpu_instance_t* inst, uint8_t reg) {
	NOT_IMPLEMENTED("waitkey");
	UNUSED(reg);
	next(inst);
}

/* Fx15 - LD DT, Vx */
/* Set delay timer = Vx. */
/* DT is set equal to the value of Vx. */
static void wdelay(cpu_instance_t* inst, uint8_t reg) {
	inst->delay_timer = inst->v_registers[reg];
	next(inst);
}

/* Fx18 - LD ST, Vx */
/* Set sound timer = Vx. */
/* ST is set equal to the value of Vx. */
static void wsound(cpu_instance_t* inst, uint8_t reg) {
	inst->sound_timer = inst->v_registers[reg];
	next(inst);
}

/* Fx1E - ADD I, Vx */
/* Set I = I + Vx. */
/* The values of I and Vx are added, and the results are stored in I. */
static void addi(cpu_instance_t* inst, uint8_t reg) {
	inst->index_register += inst->v_registers[reg];
	next(inst);
}

/* Fx29 - LD F, Vx */
/* Set I = location of sprite for digit Vx. */
/* The value of I is set to the location for the hexadecimal sprite corresponding to the value of Vx. */
static void ldsprite(cpu_instance_t* inst, uint8_t reg) {
	uint8_t digit;

	digit = inst->v_registers[reg];
	inst->index_register = 0x50 + (5 * digit);
	dbg("LD (sprite) digit %d. I <== 0x%X", digit, 0x50 + (5 * digit));
	next(inst);
}

/* Fx33 - LD B, Vx */
/* Store BCD representation of Vx in memory locations I, I+1, and I+2. */
/* The interpreter takes the decimal value of Vx, and places the hundreds digit in memory at location in I, */
/* the tens digit at location I+1, and the ones digit at location I+2. */
static void stbcd(cpu_instance_t* inst, uint8_t reg) {
	uint8_t value;
	uint8_t hundreds;
	uint8_t tens;
	uint8_t ones;
	uint16_t i;

	value = inst->v_registers[reg];
	hundreds = value / 100;
	tens = (value / 10) % 10;
	ones = (value % 100) % 10;
	i = inst->index_register;
	inst->memory[i] = hundreds;
	inst->memory[i + 1] = tens;
	inst->memory[i + 2] = ones;
	dbg("LD (store BCD) value: %d, res: %d%d%d", value, hundreds, tens, ones);
	next(inst);
}

/* Fx55 - LD [I], Vx */
/* Store registers V0 through Vx in memory starting at location I. */
/* The interpreter copies the values of registers V0 through Vx into memory, starting at the address in I. */
static void streg(cpu_instance_t* inst, uint8_t reg) {
	uint8_t v;

	for (v = 0; v <= reg; v++) {
		inst->memory[inst->index_register + v] = inst->v_registers[v];
	}
	next(inst);
}

/* Fx65 - LD Vx, [I] */
/* Read registers V0 through Vx from memory starting at location I. */
/* The interpreter reads values from memory starting at location I into registers V0 through Vx. */
static void ldreg(cpu_instance_t* inst, uint8_t reg) {
	uint8_t v;

	dbg("LD (Fx65) ");
	for (v = 0; v <= reg; v++) {
		dbg("(V%d <== M[%X] {%d})", v, inst->index_register + v, inst->memory[inst->index_register + v]);
		inst->v_registers[v] = inst->memory[inst->index_register + v];
	}
	next(inst);
}

static void cls(cpu_instance_t* inst) {
	NOT_IMPLEMENTED("cls");
	next(inst);
}

static void ret(cpu_instance_t* inst) {
	inst->program_counter = inst->stack[inst->stack_pointer--] + 2;
	dbg("RET -- POPPED pc=0x%X off the stack.", inst->program_counter);
}

static enum CpuResult execute_instruction(cpu_instance_t* inst) {
	uint16_t opcode;
	uint16_t nnn; // nnn or addr - A 12-bit value, the lowest 12 bits of the instruction
	uint8_t kk;   // kk or byte - An 8-bit value, the lowest 8 bits of the instruction
	uint8_t x;    // x - A 4-bit value, the lower 4 bits of the high byte of the instruction
	uint8_t y;    // y - A 4-bit value, the upper 4 bits of the low byte of the instruction
	uint8_t n;    // n or nibble - A 4-bit value, the lowest 4 bits of the instruction

	opcode = inst->current_opcode;
	nnn = opcode & 0x0FFF;
	kk = opcode & 0x00FF;
	x = (opcode & 0x0F00) >> 8;
	y = (opcode & 0x00F0) >> 4;
	n = opcode & 0x000F;

	if ( (opcode & 0xF000) == 0x1000 ) {
		dbg("JP");
		jp(inst, nnn);
		return OK;
	} else if ( (opcode & 0xF000) == 0x2000 ) {
		dbg("CALL");
		call(inst, nnn);
		return OK;
	} else if ( (opcode & 0xF000) == 0x3000 ) {
		dbg("SE");
		se(inst, x, kk);
		return OK;
	} else if ( (opcode & 0xF000) == 0x4000 ) {
		dbg("SNE");
		sne(inst, x, kk);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x5000 ) {
		dbg("SEREG");
		sereg(inst, x, y);
		return OK;
	} else if ( (opcode & 0xF000) == 0x6000 ) {
		dbg("LDIM");
		ldim(inst, x, kk);
		return OK;
	} else if ( (opcode & 0xF000) == 0x7000 ) {
		dbg("ADDIM");
		addim(inst, x, kk);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x8000 ) {
		dbg("LDV");
		ldv(inst, x, y);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x8001 ) {
		dbg("OR");
		or(inst, x, y);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x8002 ) {
		dbg("AND");
		and(inst, x, y);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x8003 ) {
		dbg("XOR");
		xor(inst, x, y);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x8004 ) {
		dbg("ADD");
		add(inst, x, y);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x8005 ) {
		dbg("SUB");
		sub(inst, x, y);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x8006 ) {
		dbg("SHR");
		shr(inst, x);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x8007 ) {
		dbg("SUBN");
		subn(inst, x, y);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x800E ) {
		dbg("SHL");
		shl(inst, x);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x9000 ) {
		dbg("SNEREG");
		snereg(inst, x, y);
		return OK;
	} else if ( (opcode & 0xF000) == 0xA000 ) {
		dbg("LDI");
		ldi(inst, nnn);
		return OK;
	} else if ( (opcode & 0xF000) == 0xB000 ) {
		dbg("JPREG");
		jpreg(inst, nnn);
		return OK;
	} else if ( (opcode & 0xF000) == 0xC000 ) {
		dbg("RND");
		rnd(inst, x, kk);
		return OK;
	} else if ( (opcode & 0xF000) == 0xD000 ) {
		dbg("DRAW");
		draw(inst, x, y, n);
		return OK;
	} else if ( (opcode & 0xF0FF) == 0xE09E ) {
		dbg("SKEY");
		skey(inst, x);
		return OK;
	} else if ( (opcode & 0xF0FF) == 0xE0A1 ) {
		dbg("SNKEY");
		snkey(inst, x);
		return OK;
	} else if ( (opcode & 0xF0FF) == 0xF007 ) {
		dbg("RDELAY");
      	rdelay(inst, x);
		return OK;
    } else if ( (opcode & 0xF0FF) == 0xF00A ) {
		dbg("WAIT");
      	waitkey(inst, x);
		return OK;
    } else if ( (opcode & 0xF0FF) == 0xF015 ) {
		dbg("DELAY");
      	wdelay(inst, x);
		return OK;
    } else if ( (opcode & 0xF0FF) == 0xF018 ) {
		dbg("SOUND");
      	wsound(inst, x);
		return OK;
    } else if ( (opcode & 0xF0FF) == 0xF01E ) {
		dbg("ADDI");
      	addi(inst, x);
		return OK;
    } else if ( (opcode & 0xF0FF) == 0xF029 ) {
		dbg("LDSPRITE");
      	ldsprite(inst, x);
		return OK;
    } else if ( (opcode & 0xF0FF) == 0xF033 ) {
		dbg("STBCD");
      	stbcd(inst, x);
		return OK;
    } else if ((opcode & 0xF0FF) == 0xF055 ) {
		dbg("STREG");
      	streg(inst, x);
		return OK;
    } else if ( (opcode & 0xF0FF) == 0xF065 ) {
		dbg("LDREG");
      	ldreg(inst, x);
		return OK;
    } else if (opcode == 0x00E0) {
		dbg("CLS");
		cls(inst);
		return OK;
	} else if (opcode == 0x00EE) {
		dbg("RET");
		ret(inst);
		return OK;
	} else if (opcode == 0x0) {
		return OK;
	}

	return INSTRUCTION_NOT_FOUND;
}

static void run_cycle(cpu_instance_t* inst) {
	enum CpuResult res;

	inst->current_opcode = inst->memory[inst->program_counter] << 8 | inst->memory[inst->program_counter + 1];
	//dbg("0x%X - 0x%X\t", inst->program_counter, inst->current_opcode);
	res = execute_instruction(inst);
	if (res != OK) {
		log_error("Instruction not found for opcode 0x%X", inst->current_opcode);
	}
	inst->num_cycles++;
	if (inst->num_cycles % cycles_per_frame == 0) {
		if (inst->delay_timer > 0) {
			inst->delay_timer--;
		}
		if (inst->sound_timer > 0) {
			log_info("Beeping");
			inst->sound_timer--;
		}
	}
}

static struct timespec diff_timespec(struct timespec t1, struct timespec t2) {
	struct timespec diff;

	diff.tv_sec = t1.tv_sec - t2.tv_sec;
	diff.tv_nsec = t1.tv_nsec - t2.tv_nsec;
	if (diff.tv_nsec < 0) {
		diff.tv_nsec += 1000000000; // nsec/sec
    	diff.tv_sec--;
	}
	return diff;
}

static void loop(cpu_instance_t* inst) {
	struct timespec start_time;
	struct timespec frame_start_time;
	struct timespec now;
	struct timespec delta;
	struct timespec delay;
	int vsync;
	int cycle;
	int height;

	while (atomic_load(&inst->is_running)) {
		clock_gettime(CLOCK_MONOTONIC_RAW, &start_time);
		for (vsync = 0; vsync < refresh_rate_hz; vsync++) {
			clock_gettime(CLOCK_MONOTONIC_RAW, &frame_start_time);
			for (cycle = 0; cycle < cycles_per_frame; cycle++) {
				run_cycle(inst);
			}
			height = sdl_wrapper_get_view_height(inst->view);
			inst->frame_callback(height, inst->rgb24, inst->view, inst->image, inst->frame_mutex);
			clock_gettime(CLOCK_MONOTONIC_RAW, &now);
			delta = diff_timespec(now, frame_start_time);
			delay.tv_sec = 0;
			delay.tv_nsec = 15000000;
			delta = diff_timespec(delay, delta);
			nanosleep(&delta, NULL);
		}

		clock_gettime(CLOCK_MONOTONIC_RAW, &now);
		delta = diff_timespec(now, start_time);
		delay.tv_sec = 1;
		delta = diff_timespec(delay, delta);
		if (delta.tv_sec > 0 || delta.tv_nsec > 0) {
			dbg("CPU sleeping for %lld.%.9ld", (long long) delta.tv_sec, delta.tv_nsec);
			nanosleep(&delta, NULL);
		}
	}
}


static void* thread_routine(void* data) {
	cpu_instance_t* inst;

	inst = (cpu_instance_t*) data;
	log_info("Starting emulation loop");
	atomic_store(&inst->is_running, true);
	loop(inst);
	pthread_exit(NULL);
}

enum CpuResult cpu_start(cpu_instance_t* instance) {
	int res;

	if (atomic_load(&instance->is_running)) {
		log_error("CPU is already running");
		return INVALID_STATE;
	}
	atomic_store(&instance->is_running, true);
	log_info("Starting CPU...");
	res = pthread_create(&instance->thread, NULL, thread_routine, (void*) instance);
	if (res != 0) {
		log_error("CPU thread start error");
		return THREAD_ERROR;
	}
	return OK;
}

enum CpuResult cpu_stop(cpu_instance_t* instance) {
	int res;

	if (!atomic_load(&instance->is_running)) {
		log_error("CPU must start() before stopping");
		return INVALID_STATE;
	}
	atomic_store(&instance->is_running, false);
	res = pthread_join(instance->thread, NULL);
	if (res != 0) {
		log_error("CPU thred join error");
		return THREAD_ERROR;
	}
	return OK;
}

image_t* cpu_get_image_inst(cpu_instance_t* instance) {
	return instance->image;
}
