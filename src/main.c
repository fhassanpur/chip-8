#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "chip8.c"

int main(int argc, char *argv[])
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Event event;

    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Create Window and Renderer
    if (!SDL_CreateWindowAndRenderer("Chip-8 Emulator", 768, 384, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create window and renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Create emulator state
    chip8_state_t chip8_state;
    chip8_init(&chip8_state);

    // Load ROM
    if (argc < 2) {
        SDL_Log("Usage: %s <ROM file>", argv[0]);
        return SDL_APP_FAILURE;
    }
    chip8_load_rom(&chip8_state, argv[1]);

    // Main emulator loop
    while (true) {

        // Check for quit
        SDL_PollEvent(&event);
        if (event.type == SDL_EVENT_QUIT) {
            break;
        }

        // Tick emulator
        chip8_tick(&chip8_state);

        /* Draw Chip-8 VRAM buffer */
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

        int scale_x = 768 / 64;
        int scale_y = 384 / 32;

        for (int i = 0; i < 64; i++) {
            for (int j = 0; j < 32; j++) {
                int index = (j * 64) + i;
                uint8_t pixel = chip8_state.vram[index];

                if (pixel) {
                    SDL_FRect rect = {
                        .x = i * scale_x,
                        .y = j * scale_y,
                        .w = scale_x,
                        .h = scale_y
                    };
                    SDL_RenderFillRect(renderer, &rect);
                }
            }
        }
        
        SDL_RenderPresent(renderer);
    }

    // Destroy resources
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}