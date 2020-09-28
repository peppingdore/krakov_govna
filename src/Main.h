#pragma once

#include "Tracy_Header.h"

#include "b_lib/Basic.h"
#include "b_lib/String.h"
#include "b_lib/Reflection.h"
#include "b_lib/Format_String.h"
#include "b_lib/Log.h"
#include "b_lib/Font.h"
#include "b_lib/Threading.h"
#include "b_lib/Text_Editor.h"
#include "b_lib/Time_Measurer.h"
#include "b_lib/OS_Clipboard.h"
#include "b_lib/Cursor.h"
#include "b_lib/Arena_Allocator.h"

#include "Renderer.h"
#include "Settings.h"

#if OS_WINDOWS

#include <windowsx.h>
#endif



inline double uptime = 0.0;

inline double frame_time;
inline double frame_time_us;
inline double frame_time_ms;


inline u64 frame_index = 0;

inline float frame_time_accum = 0.0;
inline int   frame_time_accum_count = 0;
inline float fps = 0;
inline const int fps_update_frames_count = 25;



inline Logger main_logger;
inline Mutex  main_logger_mutex;


inline bool has_window_focus = false;

inline int window_width  = 1000;
inline int window_height = 500;

inline int window_min_width  = 600;
inline int window_min_height = 400;


inline bool window_size_changed = false;


inline Cursor_Type desired_cursor_type = Cursor_Type::Normal;

inline void handle_os_resize_event(int new_width, int new_height);


#if OS_WINDOWS
struct Windows
{
	HDC dc;
	HWND hwnd;

	HINSTANCE hinstance;

	UINT window_dpi;
	float window_scaling;
};

inline Windows windows;

#endif

inline bool have_to_inform_window_about_changes = false;

inline bool is_inside_window_creation = true;

inline Unicode_String executable_directory;


inline Time_Measurer time_measurer;



int main();
void do_frame();
void terminate();



inline File log_file;

inline Arena_Allocator frame_allocator;





#if OS_WINDOWS
inline void capture_mouse()
{
    SetCapture(windows.hwnd);
}

inline void release_mouse()
{
    SetCapture(NULL);
}

#endif

