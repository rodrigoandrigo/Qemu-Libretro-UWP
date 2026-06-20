#pragma once

#include <cstddef>
#include <cstdint>

enum : unsigned
{
	RETRO_ENVIRONMENT_SHUTDOWN = 7,
	RETRO_ENVIRONMENT_SET_PIXEL_FORMAT = 10,
	RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY = 9,
	RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY = 31,
	RETRO_ENVIRONMENT_GET_LOG_INTERFACE = 27,
	RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME = 18,
	RETRO_ENVIRONMENT_GET_CAN_DUPE = 3,
	RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK = 12,
	RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK = 22,
	RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO = 32,
	RETRO_ENVIRONMENT_SET_CONTROLLER_INFO = 35,
	RETRO_ENVIRONMENT_SET_MESSAGE = 6,
	RETRO_ENVIRONMENT_SET_MESSAGE_EXT = 60,
};

enum retro_pixel_format
{
	RETRO_PIXEL_FORMAT_0RGB1555 = 0,
	RETRO_PIXEL_FORMAT_XRGB8888 = 1,
	RETRO_PIXEL_FORMAT_RGB565 = 2,
};

enum : unsigned
{
	RETRO_DEVICE_JOYPAD = 1,
	RETRO_DEVICE_MOUSE = 2,
	RETRO_DEVICE_KEYBOARD = 3,
};

enum : unsigned
{
	RETRO_DEVICE_ID_MOUSE_X = 0,
	RETRO_DEVICE_ID_MOUSE_Y = 1,
	RETRO_DEVICE_ID_MOUSE_LEFT = 2,
	RETRO_DEVICE_ID_MOUSE_RIGHT = 3,
	RETRO_DEVICE_ID_MOUSE_WHEELUP = 4,
	RETRO_DEVICE_ID_MOUSE_WHEELDOWN = 5,
	RETRO_DEVICE_ID_MOUSE_MIDDLE = 6,
};

enum : unsigned
{
	RETROK_BACKSPACE = 8,
	RETROK_TAB = 9,
	RETROK_RETURN = 13,
	RETROK_PAUSE = 19,
	RETROK_ESCAPE = 27,
	RETROK_SPACE = 32,
	RETROK_QUOTE = 39,
	RETROK_COMMA = 44,
	RETROK_MINUS = 45,
	RETROK_PERIOD = 46,
	RETROK_SLASH = 47,
	RETROK_SEMICOLON = 59,
	RETROK_LESS = 60,
	RETROK_EQUALS = 61,
	RETROK_LEFTBRACKET = 91,
	RETROK_BACKSLASH = 92,
	RETROK_RIGHTBRACKET = 93,
	RETROK_BACKQUOTE = 96,
	RETROK_DELETE = 127,
	RETROK_KP0 = 256,
	RETROK_KP1 = 257,
	RETROK_KP2 = 258,
	RETROK_KP3 = 259,
	RETROK_KP4 = 260,
	RETROK_KP5 = 261,
	RETROK_KP6 = 262,
	RETROK_KP7 = 263,
	RETROK_KP8 = 264,
	RETROK_KP9 = 265,
	RETROK_KP_PERIOD = 266,
	RETROK_KP_DIVIDE = 267,
	RETROK_KP_MULTIPLY = 268,
	RETROK_KP_MINUS = 269,
	RETROK_KP_PLUS = 270,
	RETROK_KP_ENTER = 271,
	RETROK_KP_EQUALS = 272,
	RETROK_LSHIFT = 304,
	RETROK_RSHIFT = 303,
	RETROK_LCTRL = 306,
	RETROK_RCTRL = 305,
	RETROK_LALT = 308,
	RETROK_RALT = 307,
	RETROK_UP = 273,
	RETROK_DOWN = 274,
	RETROK_RIGHT = 275,
	RETROK_LEFT = 276,
	RETROK_INSERT = 277,
	RETROK_HOME = 278,
	RETROK_END = 279,
	RETROK_PAGEUP = 280,
	RETROK_PAGEDOWN = 281,
	RETROK_F1 = 282,
	RETROK_F2 = 283,
	RETROK_F3 = 284,
	RETROK_F4 = 285,
	RETROK_F5 = 286,
	RETROK_F6 = 287,
	RETROK_F7 = 288,
	RETROK_F8 = 289,
	RETROK_F9 = 290,
	RETROK_F10 = 291,
	RETROK_F11 = 292,
	RETROK_F12 = 293,
	RETROK_F13 = 294,
	RETROK_F14 = 295,
	RETROK_F15 = 296,
	RETROK_NUMLOCK = 300,
	RETROK_CAPSLOCK = 301,
	RETROK_SCROLLOCK = 302,
	RETROK_PRINT = 316,
	RETROK_SYSREQ = 317,
	RETROK_MENU = 319,
};

struct retro_game_geometry
{
	unsigned base_width;
	unsigned base_height;
	unsigned max_width;
	unsigned max_height;
	float aspect_ratio;
};

struct retro_system_timing
{
	double fps;
	double sample_rate;
};

struct retro_system_av_info
{
	retro_game_geometry geometry;
	retro_system_timing timing;
};

struct retro_system_info
{
	const char* library_name;
	const char* library_version;
	const char* valid_extensions;
	bool need_fullpath;
	bool block_extract;
};

struct retro_game_info
{
	const char* path;
	const void* data;
	size_t size;
	const char* meta;
};

struct retro_variable
{
	const char* key;
	const char* value;
};

typedef void (*retro_log_printf_t)(int level, const char* fmt, ...);

struct retro_log_callback
{
	retro_log_printf_t log;
};

struct retro_keyboard_callback
{
	void (*callback)(bool down, unsigned keycode, uint32_t character, uint16_t key_modifiers);
};

struct retro_audio_callback
{
	void (*callback)();
	void (*set_state)(bool enabled);
};

struct retro_message_ext
{
	const char* msg;
	unsigned duration;
	unsigned priority;
	int level;
	int target;
	int type;
	int progress;
};

struct retro_message
{
	const char* msg;
	unsigned frames;
};

typedef bool (*retro_environment_t)(unsigned cmd, void* data);
typedef void (*retro_video_refresh_t)(const void* data, unsigned width, unsigned height, size_t pitch);
typedef void (*retro_audio_sample_t)(int16_t left, int16_t right);
typedef size_t (*retro_audio_sample_batch_t)(const int16_t* data, size_t frames);
typedef void (*retro_input_poll_t)();
typedef int16_t (*retro_input_state_t)(unsigned port, unsigned device, unsigned index, unsigned id);
