static SDL_HitTestResult SDLCALL mp_hit_test_callback(SDL_Window *win,
                                                      const SDL_Point *area,
                                                      void *data) {
  (void)win; (void)data;
  int mx = area->x; int my = area->y;
  int current_w = g_mp.flyout_active ? g_mp.flyout_current_w : MP_COLLAPSED_W;
  int current_h = MP_COLLAPSED_H;
  int offset_x = get_bounds_offset_x(current_w);
  int offset_y = get_bounds_offset_y(current_h);

  /* Ensure within window logical area */
  if (mx < offset_x || mx > offset_x + current_w || my < offset_y || my > offset_y + current_h) {
    return SDL_HITTEST_NORMAL;
  }

  int cd_radius = MP_COLLAPSED_W / 2;
  int cd_cx = offset_x + cd_radius;
  if (g_mp.flyout_active && g_mp.flyout_direction == -1) {
    cd_cx += (current_w - MP_COLLAPSED_W);
  }
  int cd_cy = offset_y + cd_radius;

  int dx = mx - cd_cx;
  int dy = my - cd_cy;
  int dist_sq = dx * dx + dy * dy;

  /* If inside the CD itself */
  if (dist_sq <= cd_radius * cd_radius) {
    /* If the user clicks the inner 25px radius (50px diameter), it's a CLICK. */
    if (dist_sq <= 25 * 25) {
      return SDL_HITTEST_NORMAL;
    }
  }

  /* Any other part of the mini player (the CD's outer edge OR the flyout body) is heavily draggable */
  return SDL_HITTEST_DRAGGABLE;
}
