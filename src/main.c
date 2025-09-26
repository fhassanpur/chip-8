#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "chip8.c"
#include "input.c"

#define WINDOW_WIDTH 768
#define WINDOW_HEIGHT 384

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Event event;

static SDL_AudioStream *stream;
static int cur_sine_sample = 0;

static int scale_x = WINDOW_WIDTH / 64;
static int scale_y = WINDOW_HEIGHT / 32;

void process_audio(uint8_t sound_timer) {
    const int min_audio = 8000;

    if (SDL_GetAudioStreamQueued(stream) < min_audio && sound_timer > 0) {
        static float samples[512];  /* this will feed 512 samples each frame until we get to our maximum. */

        /* generate a 440Hz pure tone */
        for (int i = 0; i < SDL_arraysize(samples); i++) {
            const int freq = 440;
            const float phase = cur_sine_sample * freq / 8000.0f;
            samples[i] = SDL_sinf(phase * 2 * SDL_PI_F);
            cur_sine_sample++;
        }

        /* wrapping around to avoid floating-point errors */
        cur_sine_sample %= 8000;

        /* feed the new data to the stream. It will queue at the end, and trickle out as the hardware needs more data. */
        SDL_PutAudioStreamData(stream, samples, sizeof (samples));
    }
}

void render(uint8_t *vram) {
    /* Draw Chip-8 VRAM buffer */
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 32; j++) {
            int index = (j * 64) + i;
            uint8_t pixel = vram[index];

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

int main(int argc, char *argv[]) {
    
    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Create Window and Renderer
    if (!SDL_CreateWindowAndRenderer("Chip-8 Emulator", WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create window and renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Setup audio
    SDL_AudioSpec audio_spec;
    audio_spec.channels = 1;
    audio_spec.format = SDL_AUDIO_F32;
    audio_spec.freq = 8000;
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, NULL, NULL);
    if (!stream) {
        SDL_Log("Couldn't create audio stream: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_ResumeAudioStreamDevice(stream);

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

        get_input(chip8_state.keypad);
        chip8_tick(&chip8_state);
        process_audio(chip8_state.sound_timer);
        render(chip8_state.vram);
    }

    // Destroy resources
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}