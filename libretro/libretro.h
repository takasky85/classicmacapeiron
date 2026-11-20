/*
 * libretro.h - the part of the libretro API this core uses.
 *
 * The full upstream header is several thousand lines and declares a great deal the core does not
 * touch. This is a trimmed copy: the same structures, the same enum values and the same function
 * signatures, because the frontend links against them by ABI and a wrong number here would be a
 * wrong number at run time. Anything the core does not use is simply absent rather than changed.
 *
 * Upstream is libretro/libretro-common, under a permissive licence that allows this.
 */
#ifndef APEIRON_LIBRETRO_H
#define APEIRON_LIBRETRO_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RETRO_CALLCONV
#  define RETRO_CALLCONV
#endif
#ifndef RETRO_API
#  if defined(_WIN32)
#    define RETRO_API __declspec(dllexport)
#  elif defined(__GNUC__)
#    define RETRO_API __attribute__((visibility("default")))
#  else
#    define RETRO_API
#  endif
#endif

#define RETRO_API_VERSION 1

/* ---- devices ------------------------------------------------------------------------------- */
#define RETRO_DEVICE_TYPE_SHIFT 8
#define RETRO_DEVICE_MASK       ((1 << RETRO_DEVICE_TYPE_SHIFT) - 1)

#define RETRO_DEVICE_NONE      0
#define RETRO_DEVICE_JOYPAD    1
#define RETRO_DEVICE_MOUSE     2
#define RETRO_DEVICE_KEYBOARD  3
#define RETRO_DEVICE_LIGHTGUN  4
#define RETRO_DEVICE_ANALOG    5
#define RETRO_DEVICE_POINTER   6

#define RETRO_DEVICE_ID_JOYPAD_B       0
#define RETRO_DEVICE_ID_JOYPAD_Y       1
#define RETRO_DEVICE_ID_JOYPAD_SELECT  2
#define RETRO_DEVICE_ID_JOYPAD_START   3
#define RETRO_DEVICE_ID_JOYPAD_UP      4
#define RETRO_DEVICE_ID_JOYPAD_DOWN    5
#define RETRO_DEVICE_ID_JOYPAD_LEFT    6
#define RETRO_DEVICE_ID_JOYPAD_RIGHT   7
#define RETRO_DEVICE_ID_JOYPAD_A       8
#define RETRO_DEVICE_ID_JOYPAD_X       9
#define RETRO_DEVICE_ID_JOYPAD_L      10
#define RETRO_DEVICE_ID_JOYPAD_R      11
#define RETRO_DEVICE_ID_JOYPAD_L2     12
#define RETRO_DEVICE_ID_JOYPAD_R2     13
#define RETRO_DEVICE_ID_JOYPAD_L3     14
#define RETRO_DEVICE_ID_JOYPAD_R3     15

#define RETRO_DEVICE_INDEX_ANALOG_LEFT   0
#define RETRO_DEVICE_INDEX_ANALOG_RIGHT  1
#define RETRO_DEVICE_INDEX_ANALOG_BUTTON 2
#define RETRO_DEVICE_ID_ANALOG_X         0
#define RETRO_DEVICE_ID_ANALOG_Y         1

#define RETRO_DEVICE_ID_MOUSE_X             0
#define RETRO_DEVICE_ID_MOUSE_Y             1
#define RETRO_DEVICE_ID_MOUSE_LEFT          2
#define RETRO_DEVICE_ID_MOUSE_RIGHT         3

#define RETRO_DEVICE_ID_POINTER_X       0
#define RETRO_DEVICE_ID_POINTER_Y       1
#define RETRO_DEVICE_ID_POINTER_PRESSED 2
#define RETRO_DEVICE_ID_POINTER_COUNT   3

/* ---- environment --------------------------------------------------------------------------- */
#define RETRO_ENVIRONMENT_SET_MESSAGE             6
#define RETRO_ENVIRONMENT_SHUTDOWN                7
#define RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY    9
#define RETRO_ENVIRONMENT_SET_PIXEL_FORMAT       10
#define RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS  11
#define RETRO_ENVIRONMENT_GET_VARIABLE           15
#define RETRO_ENVIRONMENT_SET_VARIABLES          16
#define RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE    17
#define RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME    18
#define RETRO_ENVIRONMENT_GET_LOG_INTERFACE      27
#define RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY     31

enum retro_pixel_format {
   RETRO_PIXEL_FORMAT_0RGB1555 = 0,
   RETRO_PIXEL_FORMAT_XRGB8888 = 1,
   RETRO_PIXEL_FORMAT_RGB565   = 2,
   RETRO_PIXEL_FORMAT_UNKNOWN  = INT32_MAX
};

enum retro_log_level {
   RETRO_LOG_DEBUG = 0, RETRO_LOG_INFO, RETRO_LOG_WARN, RETRO_LOG_ERROR, RETRO_LOG_DUMMY = INT32_MAX
};

typedef void (RETRO_CALLCONV *retro_log_printf_t)(enum retro_log_level level, const char *fmt, ...);
struct retro_log_callback { retro_log_printf_t log; };

struct retro_variable { const char *key; const char *value; };

struct retro_message { const char *msg; unsigned frames; };

struct retro_input_descriptor {
   unsigned port, device, index, id;
   const char *description;
};

struct retro_system_info {
   const char *library_name;
   const char *library_version;
   const char *valid_extensions;
   bool need_fullpath;
   bool block_extract;
};

struct retro_game_geometry {
   unsigned base_width, base_height, max_width, max_height;
   float aspect_ratio;
};

struct retro_system_timing { double fps; double sample_rate; };

struct retro_system_av_info {
   struct retro_game_geometry geometry;
   struct retro_system_timing timing;
};

struct retro_game_info {
   const char *path;
   const void *data;
   size_t size;
   const char *meta;
};

/* ---- the callbacks a frontend hands us ------------------------------------------------------ */
typedef bool (RETRO_CALLCONV *retro_environment_t)(unsigned cmd, void *data);
typedef void (RETRO_CALLCONV *retro_video_refresh_t)(const void *data, unsigned width,
                                                     unsigned height, size_t pitch);
typedef void (RETRO_CALLCONV *retro_audio_sample_t)(int16_t left, int16_t right);
typedef size_t (RETRO_CALLCONV *retro_audio_sample_batch_t)(const int16_t *data, size_t frames);
typedef void (RETRO_CALLCONV *retro_input_poll_t)(void);
typedef int16_t (RETRO_CALLCONV *retro_input_state_t)(unsigned port, unsigned device,
                                                      unsigned index, unsigned id);

/* ---- what a core must export ---------------------------------------------------------------- */
RETRO_API void retro_set_environment(retro_environment_t);
RETRO_API void retro_set_video_refresh(retro_video_refresh_t);
RETRO_API void retro_set_audio_sample(retro_audio_sample_t);
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t);
RETRO_API void retro_set_input_poll(retro_input_poll_t);
RETRO_API void retro_set_input_state(retro_input_state_t);

RETRO_API void retro_init(void);
RETRO_API void retro_deinit(void);
RETRO_API unsigned retro_api_version(void);
RETRO_API void retro_get_system_info(struct retro_system_info *info);
RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info);
RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device);
RETRO_API void retro_reset(void);
RETRO_API void retro_run(void);
RETRO_API size_t retro_serialize_size(void);
RETRO_API bool retro_serialize(void *data, size_t size);
RETRO_API bool retro_unserialize(const void *data, size_t size);
RETRO_API void retro_cheat_reset(void);
RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code);
RETRO_API bool retro_load_game(const struct retro_game_info *game);
RETRO_API bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num);
RETRO_API void retro_unload_game(void);
RETRO_API unsigned retro_get_region(void);
RETRO_API void *retro_get_memory_data(unsigned id);
RETRO_API size_t retro_get_memory_size(unsigned id);

#ifdef __cplusplus
}
#endif
#endif
