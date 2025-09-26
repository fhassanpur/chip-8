#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <SDL3/SDL.h>

#define IPS 700
#define TIMER_DECREMENT_INTERVAL (1.0 / 60.0) // 60 Hz

static unsigned int START_ADDRESS = 0x200;

typedef struct chip8_state {
    uint8_t memory[4096];
    uint16_t program_counter;
    uint16_t index_register;
    uint16_t stack[16];
    uint8_t stack_ptr;
    uint8_t delay_timer;
    uint8_t sound_timer;
    uint8_t registers[16];
    uint8_t vram[64 * 32];
    uint8_t keypad[16];
} chip8_state_t;

static struct timespec start, end;
static double last_timer_update = 0.0;

void chip8_init(chip8_state_t* state) {
    // Memory
    for (int i = 0; i < 4096; i++) {
        state->memory[i] = 0;
    }
    // Stack, registers, keypad
    for (int i = 0; i < 16; i++) {
        state->stack[i] = 0;
        state->registers[i] = 0;
        state->keypad[i] = 0;
    }
    // VRAM
    for (int i = 0; i < 64 * 32; i++) {
        state->vram[i] = 0;
    }
    // All other values
    state->program_counter = START_ADDRESS;
    state->index_register = 0;
    state->stack_ptr = 0;
    state->delay_timer = 0;
    state->sound_timer = 0;

    // Initalize randomness
    srand(time(NULL));

    SDL_Log("Initialized chip-8 state");
}

void chip8_load_rom(chip8_state_t* state, char const* filename) {
    // Open the file
    FILE *fptr = fopen(filename, "rb");

    // Check if file exists / have permissions
    if (fptr == NULL) {
        SDL_Log("Error opening file: %s", filename);
        exit(1);
    }

    // Load ROM into memory
    int i = 0;
    int buffer;
    while ((buffer = fgetc(fptr)) != EOF) {
        uint8_t byte = (uint8_t)buffer;
        state->memory[START_ADDRESS + i] = byte;
        i++;
    }

    fclose(fptr);
}

/**
 * Chip-8 Instruction Decoding
 */
uint8_t chip8_decode_x(uint16_t instruction) {
    return (instruction >> 8) & 0x0F; // Extracts bits 8-11
}

uint8_t chip8_decode_y(uint16_t instruction) {
    return (instruction >> 4) & 0x0F; // Extracts bits 4-7
}

uint8_t chip8_decode_n(uint16_t instruction) {
    return instruction & 0x0F; // Extracts bits 0-3
}

uint8_t chip8_decode_nn(uint16_t instruction) {
    return instruction & 0xFF; // Extracts bits 0-7 (full NN)
}

uint16_t chip8_decode_nnn(uint16_t instruction) {
    return instruction & 0x0FFF; // Extracts bits 0-11 (NNN)
}


/**
 * Chip-8 Instruction Execution
 */

void chip8_op_cls(chip8_state_t *state) {
    for (int i = 0; i < sizeof(state->vram); i++) {
        state->vram[i] = 0;
    }
    SDL_Log("00E0: Clear screen");
}

void chip8_op_pop(chip8_state_t *state) {
    if (state->stack_ptr == 0) {
        SDL_Log("Stack underflow!");
        return;
    }
    state->program_counter = state->stack[--state->stack_ptr];
    SDL_Log("00EE: POP");
}

void chip8_op_jmp(chip8_state_t *state, uint16_t address) {
    state->program_counter = address;
    SDL_Log("1NNN: JMP %04X", address);
}

void chip8_op_push(chip8_state_t *state, uint16_t address) {
    if (state->stack_ptr >= sizeof(state->stack)) {
        SDL_Log("Stack overflow!");
        return;
    }
    state->stack[state->stack_ptr++] = state->program_counter;
    state->program_counter = address;
    SDL_Log("2NNN: PUSH %04X", address);
}

void chip8_op_skip_equal(chip8_state_t *state, uint8_t reg, uint8_t value) {
    if (state->registers[reg] == value) {
        state->program_counter += 2;
    }
    SDL_Log("3XNN: SKIP IF V%X == %04X", reg, value);
}

void chip8_op_skip_not_equal(chip8_state_t *state, uint8_t reg, uint8_t value) {
    if (state->registers[reg] != value) {
        state->program_counter += 2;
    }
    SDL_Log("4XNN: SKIP IF V%X != %04X", reg, value);
}

void chip8_op_skip_reg_equal(chip8_state_t *state, uint8_t reg_x, uint8_t reg_y) {
    if (state->registers[reg_x] == state->registers[reg_y]) {
        state->program_counter += 2;
    }
    SDL_Log("5XY0: SKIP IF V%X == V%X", reg_x, reg_y);
}

void chip8_op_set(chip8_state_t *state, uint8_t reg, uint8_t value) {
    state->registers[reg] = value;
    SDL_Log("6XNN: SET V%X, %02X", reg, value);
}

void chip8_op_add(chip8_state_t *state, uint8_t reg, uint8_t value) {
    state->registers[reg] += value;
    SDL_Log("7XNN: ADD V%X, %02X", reg, value);
}

void chip8_op_set_reg(chip8_state_t *state, uint8_t reg_x, uint8_t reg_y) {
    state->registers[reg_x] = state->registers[reg_y];
    SDL_Log("8XY0: SET V%X, V%X", reg_x, reg_y);
}

void chip8_op_or(chip8_state_t *state, uint8_t reg_x, uint8_t reg_y) {
    state->registers[reg_x] |= state->registers[reg_y];
    SDL_Log("8XY1: OR V%X, V%X", reg_x, reg_y);
}

void chip8_op_and(chip8_state_t *state, uint8_t reg_x, uint8_t reg_y) {
    state->registers[reg_x] &= state->registers[reg_y];
    SDL_Log("8XY2: AND V%X, V%X", reg_x, reg_y);
}

void chip8_op_xor(chip8_state_t *state, uint8_t reg_x, uint8_t reg_y) {
    state->registers[reg_x] ^= state->registers[reg_y];
    SDL_Log("8XY3: XOR V%X, V%X", reg_x, reg_y);
}

void chip8_op_add_reg(chip8_state_t *state, uint8_t reg_x, uint8_t reg_y) {
    uint16_t sum = state->registers[reg_x] + state->registers[reg_y];
    state->registers[reg_x] += state->registers[reg_y];
    state->registers[0xF] = sum > 255 ? 1 : 0;
    SDL_Log("8XY4: ADD V%X, V%X", reg_x, reg_y);
}

void chip8_op_subtract_xy(chip8_state_t *state, uint8_t reg_x, uint8_t reg_y) {
    uint8_t flag = state->registers[reg_x] >= state->registers[reg_y] ? 1 : 0;
    state->registers[reg_x] -= state->registers[reg_y];
    state->registers[0xF] = flag;
    SDL_Log("8XY5: SUB V%X, V%X", reg_x, reg_y);
}

void chip8_op_shr(chip8_state_t *state, uint8_t reg_x, uint8_t reg_y) {
    state->registers[reg_x] = state->registers[reg_y];
    uint8_t shifted_value = state->registers[reg_x] & 1;
    state->registers[reg_x] >>= 1;
    state->registers[0xF] = shifted_value & 1;
    SDL_Log("8XY6: SHR V%X", reg_y);
}

void chip8_op_subtract_yx(chip8_state_t *state, uint8_t reg_x, uint8_t reg_y) {
    uint8_t flag = state->registers[reg_y] >= state->registers[reg_x] ? 1 : 0;
    state->registers[reg_x] = state->registers[reg_y] - state->registers[reg_x];
    state->registers[0xF] = flag;
    SDL_Log("8XY7: SUB V%X, V%X", reg_x, reg_y);
}

void chip8_op_shl(chip8_state_t *state, uint8_t reg_x, uint8_t reg_y) {
    state->registers[reg_x] = state->registers[reg_y];
    uint8_t shifted_value = (state->registers[reg_x] >> (sizeof(state->registers[reg_x]) * 8 - 1)) & 1;
    state->registers[reg_x] <<= 1;
    state->registers[0xF] = shifted_value & 1;
    SDL_Log("8XYE: SHR V%X", reg_y);
}

void chip8_op_skip_reg_not_equal(chip8_state_t *state, uint8_t reg_x, uint8_t reg_y) {
    if (state->registers[reg_x] != state->registers[reg_y]) {
        state->program_counter += 2;
    }
    SDL_Log("9XY0: SKIP IF V%X == V%X", reg_x, reg_y);
}

void chip8_op_set_index(chip8_state_t *state, uint16_t value) {
    state->index_register = value;
    SDL_Log("ANNN: Set index register to %04X", value);
}

void chip8_op_jmp_offset(chip8_state_t *state, uint16_t value) {
    state->program_counter = state->registers[0x0] + value;
    SDL_Log("BNNN: JMP V0, %04X", value);
}

void chip8_op_rand(chip8_state_t *state, uint8_t reg_x, uint8_t value) {
    state->registers[reg_x] = rand() & value;
    SDL_Log("CXNN: RAND V%X, %04X", reg_x, value);
}

void chip8_op_draw(chip8_state_t *state, uint8_t reg_x, uint8_t reg_y, uint8_t value) {
    uint8_t x = state->registers[reg_x] % 64;
    uint8_t y = state->registers[reg_y] % 32;

    state->registers[0xF] = 0;

    for (int row = 0; row < value; row++) {
        uint8_t sprite = state->memory[state->index_register + row];

        for (int col = 0; col < 8; col++) {
            if ((x + col) >= 64 || (y + row) >= 32) continue; // Optional: skip out-of-bounds

            uint8_t sprite_pixel = (sprite >> (7 - col)) & 0x1;
            uint16_t vram_index = (y + row) * 64 + (x + col);

            if (sprite_pixel) {
                if (state->vram[vram_index] == 1) {
                    state->registers[0xF] = 1;
                }
                state->vram[vram_index] ^= 1;
            }
        }
    }
    SDL_Log("DXYN: Draw sprite at (%X, %X) with height %X", x, y, value);
}

void chip8_op_skip_key(chip8_state_t *state, uint8_t reg_x) {
    if (state->keypad[state->registers[reg_x]]) {
        state->program_counter += 2;
    }
    SDL_Log("EX9E: SKIP V%X", reg_x);
}

void chip8_op_skip_not_key(chip8_state_t *state, uint8_t reg_x) {
    if (!state->keypad[state->registers[reg_x]]) {
        state->program_counter += 2;
    }
    SDL_Log("EXA1: SKIP NOT V%X", reg_x);
}

void chip8_op_get_delay_timer(chip8_state_t *state, uint8_t reg_x) {
    state->registers[reg_x] = state->delay_timer;
    SDL_Log("FX07: SET V%X, DLY", reg_x);
}

void chip8_op_halt_key(chip8_state_t *state, uint8_t reg_x) {
    SDL_Log("FX0A: HALT KEY V%X", reg_x);

    for (int i = 0; i < 16; i++) {
        if (state->keypad[i]) {
            state->registers[reg_x] = i;
            return;
        }
    }
    state->program_counter -= 2; // Repeat this instruction
}

void chip8_op_set_delay_timer(chip8_state_t *state, uint8_t reg_x) {
    state->delay_timer = state->registers[reg_x];
    SDL_Log("FX15: SET DLY, V%X", reg_x);
}

void chip8_op_set_sound_timer(chip8_state_t *state, uint8_t reg_x) {
    state->sound_timer = state->registers[reg_x];
    SDL_Log("FX18: SET SND, V%X", reg_x);
}

void chip8_op_add_index(chip8_state_t *state, uint8_t reg_x) {
    state->index_register += state->registers[reg_x];
    SDL_Log("FX1E: ADDINDEX V%X", reg_x);
}

void chip8_op_coded_conversion(chip8_state_t *state, uint8_t reg_x) {
    state->memory[state->index_register] = (state->registers[reg_x] / 100) % 10;
    state->memory[state->index_register + 1] = (state->registers[reg_x] / 10) % 10;
    state->memory[state->index_register + 2] = state->registers[reg_x] % 10;
    SDL_Log("FX33: CONVERT V%X", reg_x);
}

void chip8_op_store_memory(chip8_state_t *state, uint8_t reg_x) {
    for (int i = 0; i <= reg_x; i++) {
        state->memory[state->index_register + i] = state->registers[i];
    }
    SDL_Log("FX55: STORE V0->V%X", reg_x);
}

void chip8_op_load_memory(chip8_state_t *state, uint8_t reg_x) {
    for (int i = 0; i <= reg_x; i++) {
        state->registers[i] = state->memory[state->index_register + i];
    }
    SDL_Log("FX65: LOAD V0->V%X", reg_x);
}

/**
 * Chip-8 Emulation
 */

void chip8_execute(chip8_state_t *state, uint16_t instruction) {
    uint8_t first_nibble = (instruction >> 12) & 0x0F; // Extracts bits 12-15
    
    uint8_t reg_x = chip8_decode_x(instruction);
    uint8_t reg_y = chip8_decode_y(instruction);
    uint8_t value_n = chip8_decode_n(instruction);
    uint8_t value_nn = chip8_decode_nn(instruction);
    uint16_t value_nnn = chip8_decode_nnn(instruction);
    
    switch(first_nibble) {
        case 0x0:
            switch (value_nn) {
                case 0xE0:
                    chip8_op_cls(state);
                    break;
                case 0xEE:
                    chip8_op_pop(state);
                    break;
                default:
                    break;
            }
            break;
        case 0x1:
            chip8_op_jmp(state, value_nnn);
            break;
        case 0x2:
            chip8_op_push(state, value_nnn);
            break;
        case 0x3:
            chip8_op_skip_equal(state, reg_x, value_nn);
            break;
        case 0x4:
            chip8_op_skip_not_equal(state, reg_x, value_nn);
            break;
        case 0x5:
            chip8_op_skip_reg_equal(state, reg_x, reg_y);
            break;
        case 0x6:
            chip8_op_set(state, reg_x, value_nn);
            break;
        case 0x7:
            chip8_op_add(state, reg_x, value_nn);
            break;
        case 0x8:
            switch (value_n) {
                case 0x0:
                    chip8_op_set_reg(state, reg_x, reg_y);
                    break;
                case 0x1:
                    chip8_op_or(state, reg_x, reg_y);
                    break;
                case 0x2:
                    chip8_op_and(state, reg_x, reg_y);
                    break;
                case 0x3:
                    chip8_op_xor(state, reg_x, reg_y);
                    break;
                case 0x4:
                    chip8_op_add_reg(state, reg_x, reg_y);
                    break;
                case 0x5:
                    chip8_op_subtract_xy(state, reg_x, reg_y);
                    break;
                case 0x6:
                    chip8_op_shr(state, reg_x, reg_y);
                    break;
                case 0x7:
                    chip8_op_subtract_yx(state, reg_x, reg_y);
                    break;
                case 0xE:
                    chip8_op_shl(state, reg_x, reg_y);
                    break;
                default:
                    break;
            }
            break;
        case 0x9:
            chip8_op_skip_reg_not_equal(state, reg_x, reg_y);
            break;
        case 0xA:
            chip8_op_set_index(state, value_nnn);
            break;
        case 0xB:
            chip8_op_jmp_offset(state, value_nnn);
            break;
        case 0xC:
            chip8_op_rand(state, reg_x, value_nn);
            break;
        case 0xD:
            chip8_op_draw(state, reg_x, reg_y, value_n);
            break;
        case 0xE:
            switch(value_nn) {
                case 0x9E:
                    chip8_op_skip_key(state, reg_x);
                    break;
                case 0xA1:
                    chip8_op_skip_not_key(state, reg_x);
                    break;
                default:
                    break;
            }
            break;
        case 0xF:
            switch (value_nn) {
                case 0x07:
                    chip8_op_get_delay_timer(state, reg_x);
                    break;
                case 0x0A:
                    chip8_op_halt_key(state, reg_x);
                    break;
                case 0x15:
                    chip8_op_set_delay_timer(state, reg_x);
                    break;
                case 0x18:
                    chip8_op_set_sound_timer(state, reg_x);
                    break;
                case 0x1E:
                    chip8_op_add_index(state, reg_x);
                    break;
                case 0x33:
                    chip8_op_coded_conversion(state, reg_x);
                    break;
                case 0x55:
                    chip8_op_store_memory(state, reg_x);
                    break;
                case 0x65:
                    chip8_op_load_memory(state, reg_x);
                    break;
                default:
                    break;
            }
            break;
        default:
            SDL_Log("Unknown or unimplemented instruction: %04X at PC: %03X", instruction, state->program_counter - 2);
            break;
    }
}

void chip8_tick(chip8_state_t* state) {
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Fetch instruction
    uint8_t byte_a = state->memory[state->program_counter];
    uint8_t byte_b = state->memory[state->program_counter + 1];
    uint16_t instruction = (byte_a << 8) | byte_b;
    state->program_counter += 2;

    // Execute instruction
    chip8_execute(state, instruction);

    // Calculate remaining time to sleep
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    // Update the timers
    last_timer_update += elapsed_time;
    if (last_timer_update >= TIMER_DECREMENT_INTERVAL) {
        if (state->delay_timer > 0) {
            state->delay_timer--;
        }
        if (state->sound_timer > 0) {
            state->sound_timer--;
        }
        last_timer_update -= TIMER_DECREMENT_INTERVAL; // Reset the timer update
    }

    double remaining_time = (1.0 / IPS) - (elapsed_time + last_timer_update);
    if (remaining_time > 0) {
        struct timespec req;
        req.tv_sec = (time_t)remaining_time;
        req.tv_nsec = (long)((remaining_time - req.tv_sec) * 1e9);
        nanosleep(&req, NULL); // Sleep for the remaining time
    }
}