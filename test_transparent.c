#include <SDL2/SDL.h>
#include <stdio.h>
int main() {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* window = SDL_CreateWindow("Transparent", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 400, 400, SDL_WINDOW_TRANSPARENT | SDL_WINDOW_BORDERLESS);
    if (!window) {
        printf("Failed to create window\n");
        return 1;
    }
    printf("Success\n");
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
