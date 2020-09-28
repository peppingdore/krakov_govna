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

#if OS_LINUX
#include "X11_Header.h"
#endif


#include "Python_Interp.h"
#include "Python_Debugger.h"

#include "Typer_UI.h"



inline Unicode_String version_string = U"2.0.0 β";




#define TYPER_SIMD 1 // @TODO: use this switch at another places


#define USE_HIDDEN_CONSOLE 1
#ifdef NDEBUG
static_assert(USE_HIDDEN_CONSOLE);
#endif


#if OS_WINDOWS
inline Semaphore alloc_console_finished_semaphore = create_semaphore();
#endif
inline Semaphore renderer_init_finished_semaphore = create_semaphore();




constexpr int wrapped_line_margin = 24;
constexpr int line_margin_from_scrollbar_gradient = 4;


inline double uptime = 0.0;

inline double frame_time;
inline double frame_time_us;
inline double frame_time_ms;


inline u64 frame_index = 0;

inline float frame_time_accum = 0.0;
inline int   frame_time_accum_count = 0;
inline float fps = 0;
inline const int fps_update_frames_count = 25;

inline u64 next_redraw_frame = 0;
inline void need_to_redraw_next_frame(Code_Location code_location)
{
	assert(threading.is_main_thread());
	next_redraw_frame = frame_index + 1;
}
inline b32 non_main_thread_told_to_redraw = false;
inline void need_to_redraw_next_frame_from_nonmain_thread()
{
	// assert(!threading.is_main_thread());
	atomic_set<b32>(&non_main_thread_told_to_redraw, true);
}


inline bool redraw_current_frame = false;



inline Time_Measurer time_to_from_start_to_first_frame_done = create_time_measurer();



inline FILE*  typer_initial_stdout;
inline Logger typer_logger;
inline Mutex  typer_logger_mutex;


#if DEBUG
#define LOG_PROCESS_OUTPUT_TO_HUYE_TXT 0 && OS_WINDOWS // You can change this
#else
#define LOG_PROCESS_OUTPUT_TO_HUYE_TXT 0 // But do not touch this.
#endif


#if LOG_PROCESS_OUTPUT_TO_HUYE_TXT
inline File huye_file; // @TODO: remove
#endif


inline bool is_process_elevated = false;




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

#if OS_WINDOWS
inline void close_handle_if_its_valid(HANDLE handle)
{
#if !DEBUG
	DWORD dummy;
	if (GetHandleInformation(handle, &dummy))
	{
		CloseHandle(handle);
	}
#endif
};

inline bool have_to_inform_window_about_changes = false;

#endif


struct Console_Settings
{
	bool auto_scroll;
	bool scrollbar_enabled;

	inline void reset()
	{
		auto_scroll       = true;
		scrollbar_enabled = true;
	}
};

inline Console_Settings console_settings;



inline s64 user_cursor;
inline s64 selection_length;

inline bool enable_slow_output_processing = false && DEBUG;
constexpr int slow_output_processing_sleep_time_ms = 10;

inline bool buffer_changed = false;

inline Time_Measurer command_running_time = create_time_measurer();



#if OS_LINUX

enum Dragging_Edge: u32
{
	DRAGGING_EDGE_NONE = 0,

	DRAGGING_EDGE_TOP    = 1,
	DRAGGING_EDGE_LEFT   = 1 << 1,
	DRAGGING_EDGE_RIGHT  = 1 << 2,
	DRAGGING_EDGE_BOTTOM = 1 << 3,

	DRAGGING_EDGE_TOP_LEFT     = DRAGGING_EDGE_TOP | DRAGGING_EDGE_LEFT,
	DRAGGING_EDGE_TOP_RIGHT    = DRAGGING_EDGE_TOP | DRAGGING_EDGE_RIGHT,	
	DRAGGING_EDGE_BOTTOM_LEFT  = DRAGGING_EDGE_BOTTOM | DRAGGING_EDGE_LEFT,
	DRAGGING_EDGE_BOTTOM_RIGHT = DRAGGING_EDGE_BOTTOM | DRAGGING_EDGE_RIGHT,
};

inline float double_click_threshold = 0.5;
inline int window_resize_border_size = 8;

inline float time_from_window_header_click = 0.0;

inline int drag_start_window_x;
inline int drag_start_window_y;

inline int drag_start_window_width;
inline int drag_start_window_height;

inline int drag_start_mouse_x;
inline int drag_start_mouse_y;


inline bool dragging_window = false;
inline bool resizing_window = false;

inline u32 dragging_edge = DRAGGING_EDGE_NONE;


#endif


inline int out_of_border_scrolling_threshold = 36;



inline int get_active_width()
{
	return (python_debugger.is_open ? renderer.width - python_debugger.get_width() : renderer.width);
}

inline int get_active_height()
{
	return renderer.height - renderer.scaled(window_header_size);
}

inline String_Builder<char32_t> running_macro;


bool can_user_modify_buffer_at(u64 index);



void copy_buffer_region_to_os_clipboard(int start, int length);

void copy_selection_to_clipboard();
void paste_from_clipboard_to_user_cursor();
void cut_from_clipboard_to_user_cursor();


bool is_typing_command();




inline Unicode_String typer_directory;


bool is_character_at_index_is_user_input(u64 index);
bool process_user_input();


// :LimitedExecutionContext
void prepare_for_running_command(bool limited_context = false);
void cleanup_after_command(bool limited_context = false);


struct Typer
{
	Time_Measurer time_measurer;



	Mutex     terminal_mutex = TRACY_NAMED_MUTEX("terminal_mutex");


	void init();


	void tell_python_to_run_prompt();

	void if_executing_command_do_input_output_and_check_if_its_alive();
	
	void terminate_running_process();


	void do_frame();

	void do_terminal_frame();

    void terminate();
};
inline Typer typer;




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

#elif OS_LINUX

inline void capture_mouse()
{
    XGrabPointer(x11.display, x11.window, false,
               ButtonPressMask |
                 ButtonReleaseMask |
                 PointerMotionMask |
                 FocusChangeMask |
                 EnterWindowMask |
                  LeaveWindowMask,
               GrabModeAsync,
               GrabModeAsync,
               RootWindow(x11.display, DefaultScreen(x11.display)),
               0,
               CurrentTime);
}

inline void release_mouse()
{
    XUngrabPointer(x11.display, CurrentTime);
}

#elif OS_DARWIN


inline void capture_mouse()
{
	// @TODO: implement
}

inline void release_mouse()
{
	// @TODO: implement
}


#endif



bool is_mouse_over_terminal();


#if OS_DARWIN
int typer_main_for_osx();
void typer_do_frame_osx();

void osx_get_window_size(int* out_width, int* out_height);

#endif