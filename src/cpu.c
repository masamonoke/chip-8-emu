#include "cpu.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <memory.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include <log.h>

#include <utils.h>
#include <image.h>

static const int refresh_rate_hz = 60;
static const int cycle_speed_hz = refresh_rate_hz * 9;
static const int cycles_per_frame = cycle_speed_hz / refresh_rate_hz;

#ifdef DEBUG
#define dbg(...) log_debug(__VA_ARGS__);
#else
#define dbg(...)
#endif

struct cpu_instance {
	uint16_t current_opcode_;
	uint8_t memory_[4096];
	uint8_t v_registers_[16];
	uint16_t index_register_;
	uint16_t program_counter_;
	uint8_t delay_timer_;
	uint8_t sound_timer_;
	uint16_t stack_[16];
	uint16_t stack_pointer_;
	uint8_t keypad_state_[16];
	uint64_t num_cycles_;
	_Atomic(bool) is_running_;
	image_t* image;
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
	memcpy(inst->memory_ + 0x200, buf, len);
	dbg("Loaded %d bytes size rom", len);
	return res;
}

static enum CpuResult init(cpu_instance_t* inst, char* rom) {
	memset(inst->memory_, 0, sizeof(inst->memory_));
	memset(inst->v_registers_, 0, sizeof(inst->v_registers_));
	memset(inst->keypad_state_, 0, sizeof(inst->keypad_state_));
	memset(inst->stack_, 0, sizeof(inst->keypad_state_));
	inst->current_opcode_ = 0;
	inst->index_register_ = 0;
	inst->program_counter_ = 0x200;
	inst->delay_timer_ = 0;
	inst->sound_timer_ = 0;
	inst->stack_pointer_ = 0;
	inst->num_cycles_ = 0;
	if (atomic_load(&inst->is_running_)) {
		log_error("Cannot start cpu twice");
		exit(1);
	}
	atomic_init(&inst->is_running_, true);
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
	memcpy(inst->memory_ + 0x50, fontset, sizeof(fontset));
	inst->image = image_create(32, 64);

	return load_rom(inst, rom);
}

/* 1nnn - JP addr */
/* Jump to location nnn. */
/* The interpreter sets the program counter to nnn. */
static void jp(cpu_instance_t* inst, uint16_t addr) {
	inst->program_counter_ = addr;
	log_info("JP %d", addr);
}

/* 2nnn - CALL addr */
/* Call subroutine at nnn. */
/* The interpreter increments the stack pointer, then puts the current PC on the top of the stack. The PC is then set to nnn. */
static void call(cpu_instance_t* inst, uint16_t addr) {
	inst->stack_[inst->stack_pointer_++] = inst->program_counter_;
	dbg("CALL 0x%X - PUSH 0x%X onto stack", addr, inst->stack_[inst->stack_pointer_ - 1]);
	inst->program_counter_ = addr;
}

static void skip(cpu_instance_t* inst) {
	inst->program_counter_ += 4;
	dbg("SKIP from 0x%X to 0x%X", inst->program_counter_ - 4, inst->program_counter_);
}

static void next(cpu_instance_t* inst) {
	inst->program_counter_ += 2;
	dbg("NEXT from 0x%X to 0x%X", inst->program_counter_ - 2, inst->program_counter_);
}

/* 3xkk - SE Vx, byte */
/* Skip next instruction if Vx = kk. */
/* The interpreter compares register Vx to kk, and if they are equal, increments the program counter by 2. */
static void se(cpu_instance_t* inst, uint8_t reg, uint8_t value) {
	dbg("SE V%d, kk");
	inst->v_registers_[reg] == value ? skip(inst) : next(inst);
}

// Skip next instruction if Vx != kk.
static void sne(cpu_instance_t* inst, uint8_t reg, uint8_t value) {
	inst->v_registers_[reg] != value ? skip(inst) : next(inst);
}

// Skip next instruction if Vx = Vy. (5xy0 - SE Vx, Vy)
static void sereg(cpu_instance_t* inst, uint8_t reg_x, uint8_t reg_y) {
	inst->v_registers_[reg_x] == inst->v_registers_[reg_y] ? skip(inst) : next(inst);
}

// 6xkk - LD Vx, byte
// Set Vx = kk.
static void ldim(cpu_instance_t* inst, uint8_t reg, uint8_t value) {
	dbg("V%x <== 0x%X", reg, reg, value);
	inst->v_registers_[reg] = value;
	next(inst);
}

// 7xkk - ADD Vx, byte
// Set Vx = Vx + kk.
static void addim(cpu_instance_t* inst, uint8_t reg, uint8_t value) {
	dbg("V%d <== V%d + 0x%X", reg, reg, value);
	inst->v_registers_[reg] += value;
	next(inst);
}

// 8xy0 - LD Vx, Vy
// Set Vx = Vy.
static void ldv(cpu_instance_t* inst, uint8_t reg_x, uint8_t reg_y) {
	inst->v_registers_[reg_x] = inst->v_registers_[reg_y];
	next(inst);
}

// 8xy1 - OR Vx, Vy
// Set Vx = Vx OR Vy.
static void or(cpu_instance_t* inst, uint8_t reg_x, uint8_t reg_y) {
	inst->v_registers_[reg_x] |= inst->v_registers_[reg_y];
	next(inst);
}
// 8xy2 - AND Vx, Vy
// Set Vx = Vx AND Vy.
static void and(cpu_instance_t* inst, uint8_t reg_x, uint8_t reg_y) {
	inst->v_registers_[reg_x] &= inst->v_registers_[reg_y];
	next(inst);
}

// 8xy3 - XOR Vx, Vy
// Set Vx = Vx XOR Vy.
static void xor(cpu_instance_t* inst, uint8_t reg_x, uint8_t reg_y) {
	inst->v_registers_[reg_x] ^= inst->v_registers_[reg_y];
	next(inst);
}

// 8xy4 - ADD Vx, Vy
// Set Vx = Vx + Vy, set VF = carry.
// The values of Vx and Vy are added together. If the result is greater than 8 bits (i.e., > 255,)
// VF is set to 1, otherwise 0. Only the lowest 8 bits of the result are kept, and stored in Vx.
static void add(cpu_instance_t* inst, uint8_t reg_x, uint8_t reg_y) {
	uint16_t res;
	res = inst->v_registers_[reg_x] += inst->v_registers_[reg_y];
	inst->v_registers_[0xF] = res > 0xFF;
	inst->v_registers_[reg_x] = res;
	next(inst);
}

/* 8xy5 - SUB Vx, Vy */
/* Set Vx = Vx - Vy, set VF = NOT borrow. */
/* If Vx > Vy, then VF is set to 1, otherwise 0. Then Vy is subtracted from Vx, and the results stored in Vx. */
static void sub(cpu_instance_t* inst, uint8_t reg_x, uint8_t reg_y) {
	inst->v_registers_[0xF] = inst->v_registers_[reg_x] > inst->v_registers_[reg_y];
	inst->v_registers_[reg_x] -= inst->v_registers_[reg_y];
	next(inst);
}

/* 8xy6 - SHR Vx {, Vy} */
/* Set Vx = Vx SHR 1. */
/* If the least-significant bit of Vx is 1, then VF is set to 1, otherwise 0. Then Vx is divided by 2. */
/* SHR is shift right */
static void shr(cpu_instance_t* inst, uint8_t reg_x) {
	inst->v_registers_[0xF] = inst->v_registers_[reg_x] & 1;
	inst->v_registers_[reg_x] >>= 1;
	next(inst);
}

/* 8xy7 - SUBN Vx, Vy */
/* Set Vx = Vy - Vx, set VF = NOT borrow. */
/* If Vy > Vx, then VF is set to 1, otherwise 0. Then Vx is subtracted from Vy, and the results stored in Vx. */
static void subn(cpu_instance_t* inst, uint8_t reg_x, uint8_t reg_y) {
	inst->v_registers_[0xF] = inst->v_registers_[reg_y] > inst->v_registers_[reg_x];
	inst->v_registers_[reg_x] = inst->v_registers_[reg_y] - inst->v_registers_[reg_x];
	next(inst);
}

/* 8xyE - SHL Vx {, Vy} */
/* Set Vx = Vx SHL 1. */
/* If the most-significant bit of Vx is 1, then VF is set to 1, otherwise to 0. Then Vx is multiplied by 2. */
static void shl(cpu_instance_t* inst, uint8_t reg) {
	inst->v_registers_[0xF] = inst->v_registers_[reg] > 0x80;
	inst->v_registers_[reg] <<= 1;
	next(inst);
}

/* 9xy0 - SNE Vx, Vy */
/* Skip next instruction if Vx != Vy. */
/* The values of Vx and Vy are compared, and if they are not equal, the program counter is increased by 2. */
static void snereg(cpu_instance_t* inst, uint8_t reg_x, uint8_t reg_y) {
	inst->v_registers_[reg_x] != inst->v_registers_[reg_y] ? skip(inst) : next(inst);
}

/* Annn - LD I, addr */
/* Set I = nnn. */
/* The value of register I is set to nnn. */
static void ldi(cpu_instance_t* inst, uint16_t addr) {
	inst->index_register_ = addr;
	dbg("I <== 0x%X", inst->index_register_, addr);
	next(inst);
}

/* Bnnn - JP V0, addr */
/* Jump to location nnn + V0. */
/* The program counter is set to nnn plus the value of V0. */
static void jpreg(cpu_instance_t* inst, uint16_t addr) {
	inst->program_counter_ = inst->v_registers_[0] + addr;
}

/* Cxkk - RND Vx, byte */
/* Set Vx = random byte AND kk. */
/* The interpreter generates a random number from 0 to 255, which is then ANDed with the value kk. */
/* The results are stored in Vx. See instruction 8xy2 for more information on AND. */
static void rnd(cpu_instance_t* inst, uint8_t reg_x, uint8_t value) {
	inst->v_registers_[reg_x] = (rand() % 256) & value;
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

	x = inst->v_registers_[reg_x];
	y = inst->v_registers_[reg_y];
	pixels_unset = image_xor_sprite(inst->image, x, y, n_rows, inst->memory_ + inst->index_register_);
	inst->v_registers_[0xF] = pixels_unset;
	next(inst);
}

/* Ex9E - SKP Vx */
/* Skip next instruction if key with the value of Vx is pressed. */
/* Checks the keyboard, and if the key corresponding to the value of Vx is currently in the down position, PC is increased by 2. */
static void skey(cpu_instance_t* inst, uint8_t reg_x) {
	inst->keypad_state_[inst->v_registers_[reg_x]] ? skip(inst) : next(inst);
}

/* ExA1 - SKNP Vx */
/* Skip next instruction if key with the value of Vx is not pressed. */
/* Checks the keyboard, and if the key corresponding to the value of Vx is currently in the up position, PC is increased by 2. */
static void snkey(cpu_instance_t* inst, uint8_t reg) {
	inst->keypad_state_[inst->v_registers_[reg]] ? next(inst) : skip(inst);
}

/* Fx07 - LD Vx, DT */
/* Set Vx = delay timer value. */
/* The value of DT is placed into Vx. */
static void rdelay(cpu_instance_t* inst, uint8_t reg) {
	inst->v_registers_[reg] = inst->delay_timer_;
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
	inst->delay_timer_ = inst->v_registers_[reg];
	next(inst);
}

/* Fx18 - LD ST, Vx */
/* Set sound timer = Vx. */
/* ST is set equal to the value of Vx. */
static void wsound(cpu_instance_t* inst, uint8_t reg) {
	inst->sound_timer_ = inst->v_registers_[reg];
	next(inst);
}

/* Fx1E - ADD I, Vx */
/* Set I = I + Vx. */
/* The values of I and Vx are added, and the results are stored in I. */
static void addi(cpu_instance_t* inst, uint8_t reg) {
	inst->index_register_ += inst->v_registers_[reg];
	next(inst);
}

/* Fx29 - LD F, Vx */
/* Set I = location of sprite for digit Vx. */
/* The value of I is set to the location for the hexadecimal sprite corresponding to the value of Vx. */
static void ldsprite(cpu_instance_t* inst, uint8_t reg) {
	uint8_t digit;

	digit = inst->v_registers_[reg];
	inst->index_register_ = 0x50 + (5 * digit);
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

	value = inst->v_registers_[reg];
	hundreds = value / 100;
	tens = (value / 10) % 10;
	ones = (value % 100) % 10;
	i = inst->index_register_;
	inst->memory_[i] = hundreds;
	inst->memory_[i + 1] = tens;
	inst->memory_[i + 2] = ones;
	dbg("LD (store BCD) value: %d, res: %d%d%d", value, hundreds, tens, ones);
	next(inst);
}

/* Fx55 - LD [I], Vx */
/* Store registers V0 through Vx in memory starting at location I. */
/* The interpreter copies the values of registers V0 through Vx into memory, starting at the address in I. */
static void streg(cpu_instance_t* inst, uint8_t reg) {
	uint8_t v;

	for (v = 0; v <= reg; v++) {
		inst->memory_[inst->index_register_ + v] = inst->v_registers_[v];
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
		dbg("(V%d <== M[%X] {%d})", v, inst->index_register_ + v, inst->memory_[inst->index_register_ + v]);
		inst->v_registers_[v] = inst->memory_[inst->index_register_ + v];
	}
	next(inst);
}

static void cls(cpu_instance_t* inst) {
	NOT_IMPLEMENTED("cls");
	next(inst);
}

static void ret(cpu_instance_t* inst) {
	inst->program_counter_ = inst->stack_[inst->stack_pointer_--] + 2;
	log_info("RET -- POPPED pc=0x%X off the stack.", inst->program_counter_);
}

static enum CpuResult execute_instruction(cpu_instance_t* inst) {
	uint16_t opcode;
	uint16_t nnn; // nnn or addr - A 12-bit value, the lowest 12 bits of the instruction
	uint8_t kk;   // kk or byte - An 8-bit value, the lowest 8 bits of the instruction
	uint8_t x;    // x - A 4-bit value, the lower 4 bits of the high byte of the instruction
	uint8_t y;    // y - A 4-bit value, the upper 4 bits of the low byte of the instruction
	uint8_t n;    // n or nibble - A 4-bit value, the lowest 4 bits of the instruction

	opcode = inst->current_opcode_;
	nnn = opcode & 0x0FFF;
	kk = opcode & 0x00FF;
	x = (opcode & 0x0F00) >> 8;
	y = (opcode & 0x00F0) >> 4;
	n = opcode & 0x000F;

	if ( (opcode & 0xF000) == 0x1000 ) {
		log_info("JP");
		jp(inst, nnn);
		return OK;
	} else if ( (opcode & 0xF000) == 0x2000 ) {
		log_info("CALL");
		call(inst, nnn);
		return OK;
	} else if ( (opcode & 0xF000) == 0x3000 ) {
		log_info("SE");
		se(inst, x, kk);
		return OK;
	} else if ( (opcode & 0xF000) == 0x4000 ) {
		log_info("SNE");
		sne(inst, x, kk);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x5000 ) {
		log_info("SEREG");
		sereg(inst, x, y);
		return OK;
	} else if ( (opcode & 0xF000) == 0x6000 ) {
		log_info("LDIM");
		ldim(inst, x, kk);
		return OK;
	} else if ( (opcode & 0xF000) == 0x7000 ) {
		log_info("ADDIM");
		addim(inst, x, kk);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x8000 ) {
		log_info("LDV");
		ldv(inst, x, y);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x8001 ) {
		log_info("OR");
		or(inst, x, y);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x8002 ) {
		log_info("AND");
		and(inst, x, y);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x8003 ) {
		log_info("XOR");
		xor(inst, x, y);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x8004 ) {
		log_info("ADD");
		add(inst, x, y);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x8005 ) {
		log_info("SUB");
		sub(inst, x, y);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x8006 ) {
		log_info("SHR");
		shr(inst, x);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x8007 ) {
		log_info("SUBN");
		subn(inst, x, y);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x800E ) {
		log_info("SHL");
		shl(inst, x);
		return OK;
	} else if ( (opcode & 0xF00F) == 0x9000 ) {
		log_info("SNEREG");
		snereg(inst, x, y);
		return OK;
	} else if ( (opcode & 0xF000) == 0xA000 ) {
		log_info("LDI");
		ldi(inst, nnn);
		return OK;
	} else if ( (opcode & 0xF000) == 0xB000 ) {
		log_info("JPREG");
		jpreg(inst, nnn);
		return OK;
	} else if ( (opcode & 0xF000) == 0xC000 ) {
		log_info("RND");
		rnd(inst, x, kk);
		return OK;
	} else if ( (opcode & 0xF000) == 0xD000 ) {
		log_info("DRAW");
		draw(inst, x, y, n);
		return OK;
	} else if ( (opcode & 0xF0FF) == 0xE09E ) {
		log_info("SKEY");
		skey(inst, x);
		return OK;
	} else if ( (opcode & 0xF0FF) == 0xE0A1 ) {
		log_info("SNKEY");
		snkey(inst, x);
		return OK;
	} else if ( (opcode & 0xF0FF) == 0xF007 ) {
		log_info("RDELAY");
      	rdelay(inst, x);
		return OK;
    } else if ( (opcode & 0xF0FF) == 0xF00A ) {
		log_info("WAIT");
      	waitkey(inst, x);
		return OK;
    } else if ( (opcode & 0xF0FF) == 0xF015 ) {
		log_info("DELAY");
      	wdelay(inst, x);
		return OK;
    } else if ( (opcode & 0xF0FF) == 0xF018 ) {
		log_info("SOUND");
      	wsound(inst, x);
		return OK;
    } else if ( (opcode & 0xF0FF) == 0xF01E ) {
		log_info("ADDI");
      	addi(inst, x);
		return OK;
    } else if ( (opcode & 0xF0FF) == 0xF029 ) {
		log_info("LDSPRITE");
      	ldsprite(inst, x);
		return OK;
    } else if ( (opcode & 0xF0FF) == 0xF033 ) {
		log_info("STBCD");
      	stbcd(inst, x);
		return OK;
    } else if ((opcode & 0xF0FF) == 0xF055 ) {
		log_info("STREG");
      	streg(inst, x);
		return OK;
    } else if ( (opcode & 0xF0FF) == 0xF065 ) {
		log_info("LDREG");
      	ldreg(inst, x);
		return OK;
    } else if (opcode == 0x00E0) {
		log_info("CLS");
		cls(inst);
		return OK;
	} else if (opcode == 0x00EE) {
		log_info("RET");
		ret(inst);
		return OK;
	} else if (opcode == 0x0) {
		return OK;
	}

	return INSTRUCTION_NOT_FOUND;
}

static void run_cycle(cpu_instance_t* inst) {
	enum CpuResult res;

	inst->current_opcode_ = inst->memory_[inst->program_counter_] << 8 | inst->memory_[inst->program_counter_ + 1];
	//dbg("0x%X - 0x%X\t", inst->program_counter_, inst->current_opcode_);
	res = execute_instruction(inst);
	if (res != OK) {
		log_error("Instruction not found for opcode 0x%X", inst->current_opcode_);
	}
	inst->num_cycles_++;
	if (inst->num_cycles_ % cycles_per_frame == 0) {
		if (inst->delay_timer_ > 0) {
			inst->delay_timer_--;
		}
		if (inst->sound_timer_ > 0) {
			log_info("Beeping");
			inst->sound_timer_--;
		}
	}
}

static void loop(cpu_instance_t* inst) {
	struct timespec start_time;
	struct timespec frame_start_time;
	struct timespec millis_15;
	struct timespec now;
	struct timespec diff;
	int vsync;
	int cycle;

	while (atomic_load(&inst->is_running_)) {
		timespec_get(&start_time, TIME_UTC);
		for (vsync = 0; vsync < refresh_rate_hz; vsync++) {
			timespec_get(&frame_start_time, TIME_UTC);
			for (cycle = 0; cycle < cycles_per_frame; cycle++) {
				run_cycle(inst);
			}
			timespec_get(&now, TIME_UTC);
			millis_15.tv_sec = 15 / 1000;
			millis_15.tv_nsec = (15 % 1000) * 1000000;
			struct timespec to_vsync = {
				.tv_sec = millis_15.tv_sec - (now.tv_sec - frame_start_time.tv_sec),
				.tv_nsec = millis_15.tv_nsec - (now.tv_nsec - frame_start_time.tv_nsec)
			};
			if (to_vsync.tv_nsec < 0) {
				to_vsync.tv_nsec += 1000000000;
				to_vsync.tv_sec--;
			}
			if (to_vsync.tv_nsec > 0 || to_vsync.tv_sec > 0) {
				nanosleep(&to_vsync, NULL);
			}
		}

		timespec_get(&now, TIME_UTC);
		diff.tv_sec = now.tv_sec - start_time.tv_sec;
		diff.tv_nsec = now.tv_nsec - start_time.tv_nsec;
		if (diff.tv_nsec < 0) {
			diff.tv_nsec += 1000000000;
			diff.tv_sec--;
		}
		if (diff.tv_sec > 0 || diff.tv_nsec > 0) {
			log_info("CPU sleeping for %lld.%.9ld", (long long) diff.tv_sec, diff.tv_nsec);
			nanosleep(&diff, NULL);
		}
	}
}

struct thread_data {
	cpu_instance_t* inst;
	char* rom;
};

static void* thread_routine(void* data) {
	struct thread_data* th_data;
	cpu_instance_t* inst;
	char* rom;

	th_data = (struct thread_data*) data;
	inst = th_data->inst;
	rom = th_data->rom;
	init(inst, rom);
	loop(inst);
	pthread_exit(NULL);
}


void cpu_start(cpu_instance_t* instance, char* rom) {
	pthread_t thread;
	struct thread_data th_data;

	th_data.inst = instance;
	th_data.rom = rom;
	pthread_create(&thread, NULL, thread_routine, (void*) &th_data);
	pthread_join(thread, NULL);
	/* init(instance, rom); */
	/* loop(instance); */
}
