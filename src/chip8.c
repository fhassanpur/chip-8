#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <SDL3/SDL.h>

#define IPS 700

static unsigned int START_ADDRESS = 0x200;

typedef struct chip8_state {
    uint8_t memory[4096];
    uint16_t program_counter;
    uint16_t index_register;
    uint16_t stack[16];
    uint8_t delay_timer;
    uint8_t sound_timer;
    uint8_t registers[16];
    uint8_t vram[64 * 32];
} chip8_state_t;

static struct timespec start, end;

void chip8_init(chip8_state_t* state) {
    // Memory
    for (int i = 0; i < 4096; i++) {
        state->memory[i] = 0;
    }
    // Stack & Registers
    for (int i = 0; i < 16; i++) {
        state->stack[i] = 0;
        state->registers[i] = 0;
    }
    // VRAM
    for (int i = 0; i < 64 * 32; i++) {
        state->vram[i] = 0;
    }
    // All other values
    state->program_counter = START_ADDRESS;
    state->index_register = 0;
    state->delay_timer = 0;
    state->sound_timer = 0;

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

void chip8_op_jmp(chip8_state_t *state, uint16_t address) {
    state->program_counter = address;
    SDL_Log("1NNN: JMP %04X", address);
}

void chip8_op_set(chip8_state_t *state, uint8_t reg, uint8_t value) {
    state->registers[reg] = value;
    SDL_Log("6XNN: SET V%X, %02X", reg, value);
}

void chip8_op_add(chip8_state_t *state, uint8_t reg, uint8_t value) {
    state->registers[reg] += value;
    SDL_Log("7XNN: ADD V%X, %02X", reg, value);
}

void chip8_op_set_index(chip8_state_t *state, uint16_t value) {
    state->index_register = value;
    SDL_Log("ANNN: Set index register to %04X", value);
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

/**
 * Chip-8 Emulation
 */

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
    double remaining_time = (1.0 / IPS) - elapsed_time;
    if (remaining_time > 0) {
        struct timespec req;
        req.tv_sec = (time_t)remaining_time;
        req.tv_nsec = (long)((remaining_time - req.tv_sec) * 1e9);
        nanosleep(&req, NULL); // Sleep for the remaining time
    }
}

void chip8_execute(chip8_state_t *state, uint16_t instruction) {
    SDL_Log("Executing instruction: %04X", instruction);

    if (instruction == 0x00E0) {
        chip8_op_cls(state);
        return;
    }

    uint8_t first_nibble = (instruction >> 12) & 0x0F; // Extracts bits 12-15
    uint8_t reg;
    uint8_t value;
    
    switch(first_nibble) {
        case 0x1:
            uint16_t addr = chip8_decode_nnn(instruction);
            chip8_op_jmp(state, addr);
            break;
        case 0x6:
            reg = chip8_decode_x(instruction);
            value = chip8_decode_nn(instruction);
            chip8_op_set(state, reg, value);
            break;
        case 0x7:
            reg = chip8_decode_x(instruction);
            value = chip8_decode_nn(instruction);
            chip8_op_add(state, reg, value);
            break;
        case 0xA:
            uint16_t value16 = chip8_decode_nnn(instruction);
            chip8_op_set_index(state, value16);
            break;
        case 0xD:
            uint8_t reg_x = chip8_decode_x(instruction);
            uint8_t reg_y = chip8_decode_y(instruction);
            value = chip8_decode_n(instruction);
            chip8_op_draw(state, reg_x, reg_y, value);
            break;
        default:
            SDL_Log("Unknown or unimplemented instruction: %04X at PC: %03X", instruction, state->program_counter - 2);
            break;
    }
}