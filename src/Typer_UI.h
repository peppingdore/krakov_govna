#pragma once


#include "UI.h"


#if OS_WINDOWS
inline bool is_inside_window_creation = false;

inline constexpr int window_border_size = 8; // 7 matches default transparent border size on Windows 10.
//  Border stuff is just fucking crazy.
//  When you maximize the window it actually get's size that is slightly bigger than the monitor,
//   to hide the border. 
#endif

inline constexpr int window_header_size = 25;
inline constexpr int window_top_border_drag_width = 4;
inline constexpr int window_header_button_width = 24;


inline rgba window_border_color = rgba(40, 40, 40, 255);




struct Renderer_Line
{
	int start;
	int length;


	bool is_wrapped;
	bool has_new_line_at_the_end;

	int left_margin;  // This is used by line wrapping



	inline int length_without_new_line()
	{
		if (has_new_line_at_the_end)
			return length - 1;

		return length;
	}
};



enum class Screen_Page
{
	Terminal = 1,
	Settings = 2,
};

enum class Drag
{
	None           = 0,
	Scrollbar      = 1,
	Text_Selection = 2,
};


struct Found_Text
{
	int start;
	int length;
};


// Really UI specific stuff should be here.
// Vaguely UI stuff is in global scope.
struct Typer_UI
{
	int  settings_toggle_button_width = window_header_button_width;
	int  settings_toggle_button_margin = 16;
	rgba settings_toggle_button_color = rgba(30, 30, 30, 255);
	rgba settings_toggle_button_line_color = rgba(255, 255, 255, 255);
	int  settings_toggle_button_line_margin = 8;

	float screen_page_transition_speed = 3.0;


	// Set every frame, usually this is window header's y_bottom,
	int y_top = 0;

	Drag drag = Drag::None;
	int drag_start_x;
	int drag_start_y;


	const static int scrollbar_width = 24;
	const static int process_bar_height = 56;

	int current_scrollbar_width = 0;
	int scroll_top_pixels_offset = 0;

	int scrollgrip_offset_drag_start = 0;

	int current_bottom_bar_height = 0;

	float autocomplete_suggestion_move_state  = 0;
	float autocomplete_suggestion_move_target = 0;



	// Search stuff
	bool is_search_bar_open = false;
	bool is_search_bar_focused = false;
	Unicode_String search_bar_text = Unicode_String::empty;

	Screen_Page screen_page = Screen_Page::Terminal;
	
	Unicode_String found_text = Unicode_String::empty;
	int next_search_start_index = 0;
	Dynamic_Array<Found_Text> found_entries;
	int current_found_entry = -1;


	UI_ID terminal_ui_id = ui_id(0);
	UI_ID search_bar_ui_id  = ui_id(0);


	bool able_to_scroll = false;

	bool was_screen_at_the_most_bottom_last_frame = false;

	bool terminal_focused = false;


	// When we use arrow keys to navigate by lines we may encounter the line that is smaller than the current one.
	// But we want to keep cursor at the same position if we jump back to bigger or equal line.
	// To remove effect of this variable set it to -1.
	int desired_precursor_pixels = -1;


	bool need_to_keep_scroll_state_this_frame = false;


	float mouse_offscreen_scroll_target = 0.0;


	Dynamic_Array<Renderer_Line> renderer_lines;
	// If char's index in buffer is >= than this, renderer lines for that char and after will be recalculated. 
	s64 first_invalid_buffer_char_index = -1;



	rgba process_caret_color  = rgba(255, 100, 0,   125);
	rgba user_cursor_color    = rgba(100, 255, 100, 125);

	int  cursor_width = 4;


	rgba selection_color = rgba(0, 120, 120, 255);

	rgba default_background_color = rgba(0, 0, 0, 255);
	rgba default_foreground_color = rgba(240, 240, 240, 255);



	const int max_typer_font_face_size = 100;
	const int min_typer_font_face_size = 2;
	const int typer_default_font_face_size = 14;


	Font::Face* typer_font_face; // @TODO: remove this.




	void invalidate_after(s64 after);
	void recalculate_renderer_lines();


	s64 find_to_which_renderer_line_position_belongs(u64 position);
	s64 find_closest_renderer_line_to_y_coordinate(int y);

	s64 find_cursor_position_for_mouse();
	s64 find_exact_cursor_position_for_mouse();

	// x - left, y - bottom
	Vector2i find_pixel_position_for_character(u64 position);


	void save_presursor_pixels_on_line(Renderer_Line line);
	void set_cursor_on_line_based_on_the_saved_precursor_pixels_on_line(Renderer_Line line);


	int get_scroll_bottom_appendix_height(int view_height);

	void scroll_to_char(int char_index, bool scroll_even_if_on_screen = false);

	void set_typer_font_face();


	bool is_header_ui_touching_cursor(int x, int y);

	int get_header_window_control_buttons_width();
	Rect get_settings_toggle_button_rect();

	void draw_window_header();

	bool header_button(Rect rect, rgba color, float* darkness, UI_ID ui_id);

#if OS_LINUX
	void do_x11_window_controlling(Rect window_control_rect);
#endif

	void draw_settings_toggle_button();


	void draw_fps();


	inline int get_terminal_view_height()
	{
		return y_top - current_bottom_bar_height;
	}


	void init();
	void do_frame();
};

inline Typer_UI typer_ui;