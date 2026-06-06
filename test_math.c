#include <stdio.h>
#include <stdlib.h>

int main() {
    int screen_w = 1920;
    int screen_y = 0;
    int screen_x = 0;
    int screen_h = 1080;

    int MP_COLLAPSED_W = 60;
    int MP_COLLAPSED_H = 60;
    int MP_EXPANDED_W = 320;
    int MP_EXPANDED_H = 200;

    // Simulate dragged pill off screen nicely
    int start_x = 1900;
    int start_y = 1000;
    int start_w = 60;
    int start_h = 60;

    int center_x = start_x + start_w / 2;
    int center_y = start_y + start_h / 2;

    int n_w = MP_EXPANDED_W;
    int n_h = MP_EXPANDED_H;
    int n_x = start_x;
    int n_y = start_y;

    if (center_x > screen_x + screen_w / 2)
        n_x = start_x + start_w - n_w;
    if (center_y > screen_y + screen_h / 2)
        n_y = start_y + start_h - n_h;

    printf("Pre-clamp n_x=%d, n_y=%d\n", n_x, n_y);

    if (n_x < screen_x) n_x = screen_x;
    if (n_y < screen_y) n_y = screen_y;
    if (n_x + n_w > screen_x + screen_w) n_x = screen_x + screen_w - n_w;
    if (n_y + n_h > screen_y + screen_h) n_y = screen_y + screen_h - n_h;

    printf("Post-clamp n_x=%d, n_y=%d\n", n_x, n_y);

    return 0;
}
