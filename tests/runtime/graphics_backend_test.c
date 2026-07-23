#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

extern bool janus_graphics_available(void);
extern bool janus_graphics_init_window(int width, int height, const void *title);
extern bool janus_graphics_window_should_close(void);
extern void janus_graphics_close_window(void);
extern void janus_graphics_set_target_fps(int frames_per_second);
extern void janus_graphics_begin_drawing(void);
extern void janus_graphics_end_drawing(void);
extern void janus_graphics_clear_background(uint32_t color);
extern void janus_graphics_draw_pixel(int x, int y, uint32_t color);
extern void janus_graphics_draw_line(int start_x, int start_y, int end_x,
                                     int end_y, uint32_t color);
extern void janus_graphics_draw_circle(int center_x, int center_y, float radius,
                                       uint32_t color);
extern void janus_graphics_draw_rectangle(int x, int y, int width, int height,
                                          uint32_t color);
extern void janus_graphics_draw_text(const void *text, int x, int y,
                                     int font_size, uint32_t color);
extern void *janus_graphics_load_texture(const void *file_name);
extern bool janus_graphics_texture_is_valid(const void *handle);
extern int janus_graphics_texture_width(const void *handle);
extern int janus_graphics_texture_height(const void *handle);
extern void janus_graphics_unload_texture(void *handle);
extern void janus_graphics_draw_texture(const void *handle, int x, int y,
                                        uint32_t tint);
extern bool janus_graphics_init_audio(void);
extern void janus_graphics_close_audio(void);
extern void janus_graphics_set_master_volume(float volume);
extern void *janus_graphics_load_sound(const void *file_name);
extern bool janus_graphics_sound_is_valid(const void *handle);
extern void janus_graphics_unload_sound(void *handle);
extern void janus_graphics_play_sound(const void *handle);
extern bool janus_graphics_sound_is_playing(const void *handle);
extern void *janus_graphics_load_music(const void *file_name);
extern bool janus_graphics_music_is_valid(const void *handle);
extern void janus_graphics_unload_music(void *handle);
extern void janus_graphics_play_music(const void *handle);
extern void janus_graphics_update_music(const void *handle);
extern bool janus_graphics_music_is_playing(const void *handle);
extern bool janus_graphics_is_key_down(int key);
extern bool janus_graphics_is_key_pressed(int key);
extern int janus_graphics_mouse_x(void);
extern int janus_graphics_mouse_y(void);
extern bool janus_graphics_is_mouse_button_down(int button);
extern bool janus_graphics_is_mouse_button_pressed(int button);

int main(void) {
  if (!janus_graphics_available() ||
      !janus_graphics_init_window(800, 450, "test") ||
      janus_graphics_window_should_close()) {
    fputs("graphics backend did not initialize the configured library\n",
          stderr);
    return 1;
  }

  janus_graphics_set_target_fps(60);
  janus_graphics_begin_drawing();
  janus_graphics_clear_background(UINT32_C(0x182000ff));
  janus_graphics_draw_pixel(1, 2, UINT32_C(0xffffffff));
  janus_graphics_draw_line(1, 2, 3, 4, UINT32_C(0xffffffff));
  janus_graphics_draw_circle(5, 6, 7.0f, UINT32_C(0xffffffff));
  janus_graphics_draw_rectangle(8, 9, 10, 11, UINT32_C(0xffffffff));
  janus_graphics_draw_text("Janus", 12, 13, 14, UINT32_C(0xffffffff));
  void *texture = janus_graphics_load_texture("sprite.png");
  if (!janus_graphics_texture_is_valid(texture) ||
      janus_graphics_texture_width(texture) != 64 ||
      janus_graphics_texture_height(texture) != 32) {
    fputs("graphics backend did not load texture metadata\n", stderr);
    return 1;
  }
  janus_graphics_draw_texture(texture, 15, 16, UINT32_C(0xffffffff));
  janus_graphics_unload_texture(texture);
  janus_graphics_end_drawing();

  if (!janus_graphics_init_audio()) {
    fputs("graphics backend did not initialize audio\n", stderr);
    return 1;
  }
  janus_graphics_set_master_volume(0.5f);
  void *sound = janus_graphics_load_sound("effect.wav");
  void *music = janus_graphics_load_music("music.ogg");
  if (!janus_graphics_sound_is_valid(sound) ||
      !janus_graphics_music_is_valid(music)) {
    fputs("graphics backend did not load audio resources\n", stderr);
    return 1;
  }
  janus_graphics_play_sound(sound);
  janus_graphics_play_music(music);
  janus_graphics_update_music(music);
  if (!janus_graphics_sound_is_playing(sound) ||
      !janus_graphics_music_is_playing(music)) {
    fputs("graphics backend did not forward audio playback\n", stderr);
    return 1;
  }
  janus_graphics_unload_sound(sound);
  janus_graphics_unload_music(music);
  janus_graphics_close_audio();

  if (!janus_graphics_is_key_down(263) ||
      !janus_graphics_is_key_pressed(256) ||
      janus_graphics_mouse_x() != 123 || janus_graphics_mouse_y() != 234 ||
      !janus_graphics_is_mouse_button_down(0) ||
      !janus_graphics_is_mouse_button_pressed(1)) {
    fputs("graphics backend did not forward input through raylib\n", stderr);
    return 1;
  }

  janus_graphics_close_window();
  return 0;
}
