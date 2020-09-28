#pragma once

#include "b_lib/Dynamic_Array.h"
#include "b_lib/Color.h"


#include "Python_Interp.h"
#include "Typer_UI.h"


#undef DELETE // Comes from some Windows header, fuck it.

constexpr char ESCAPE    = 0x1b;
constexpr char BELL      = 0x07;
constexpr char BACKSPACE = 0x08;
constexpr char DELETE    = 0x7f;

constexpr char SHIFT_IN  = 0x0f;
constexpr char SHIFT_OUT = 0x0e;


#define CLEAR_ENDING_SPACES 0



constexpr u8 TERMINAL_CHARACTER_FLAG_DEFAULT     = 0;
constexpr u8 TERMINAL_CHARACTER_FLAG_COLORED     = 1 << 0;

enum Terminal_Character_Type: u8
{
	TERMINAL_CHARACTER_TYPE_ERROR_MESSAGE,
	TERMINAL_CHARACTER_TYPE_NONE,
	
	TERMINAL_CHARACTER_TYPE_USER_INPUT,
	TERMINAL_CHARACTER_TYPE_OLD_USER_INPUT,

	TERMINAL_CHARACTER_TYPE_SENT_USER_INPUT,
	TERMINAL_CHARACTER_TYPE_CHAR_THAT_PROCESS_DID_READ,

	TERMINAL_CHARACTER_TYPE_PROCESS_OUTPUT,
};
REFLECT(Terminal_Character_Type)
	ENUM_VALUE(TERMINAL_CHARACTER_TYPE_ERROR_MESSAGE);
	ENUM_VALUE(TERMINAL_CHARACTER_TYPE_NONE);

	ENUM_VALUE(TERMINAL_CHARACTER_TYPE_USER_INPUT);
	ENUM_VALUE(TERMINAL_CHARACTER_TYPE_OLD_USER_INPUT);

	ENUM_VALUE(TERMINAL_CHARACTER_TYPE_SENT_USER_INPUT);
	ENUM_VALUE(TERMINAL_CHARACTER_TYPE_CHAR_THAT_PROCESS_DID_READ);

	ENUM_VALUE(TERMINAL_CHARACTER_TYPE_PROCESS_OUTPUT);
REFLECT_END();

struct Terminal_Character
{
	char32_t c;

	Terminal_Character_Type character_type;

	u8 flags = TERMINAL_CHARACTER_FLAG_DEFAULT;

	// This two colors are used if flags & TERMINAL_CHAR_COLORED
	rgba color;
	rgba background_color;
};



struct Terminal
{
	Dynamic_Array<Terminal_Character> characters;

	Mutex characters_mutex = TRACY_NAMED_MUTEX("buffer_mutex");

	int terminal_width  = 80;
	int terminal_height = 23;
#if OS_LINUX
	int terminal_width_pixels = 100;
	int terminal_height_pixels = 100;
#endif



// This members make sense if process is running
	bool foreground_color_is_set = false;
	bool background_color_is_set = false;

// @TODO: convert to rgb
	rgba current_foreground_color;
	rgba current_background_color;

	// 0 based coordinates, ASCII standard uses 1 based.
	int current_row    = 0;
	int current_column = 0;

	// This is accelaration structure marking line beginnings, so we can move cursor or lookup it's position faster.
	// Also useful to know count of lines.
	Dynamic_Array<int> line_heads;



	int process_caret = 0;
	int process_output_start = 0;
//


	inline int get_characters_count()
	{
		return characters.count;
	}


	inline void set_foreground_color(rgb color)
	{
		foreground_color_is_set = true;
		current_foreground_color = rgba(color, 255);
	}

	inline void set_background_color(rgb color)
	{
		background_color_is_set = true;
		current_background_color = rgba(color, 255);
	}

	inline auto conditional_colored_flag() -> decltype(TERMINAL_CHARACTER_FLAG_COLORED)
	{
		if (foreground_color_is_set || background_color_is_set)
			return TERMINAL_CHARACTER_FLAG_COLORED;

		return 0;
	}

	inline int get_line_end(int row)
	{
		int* next_line_head = line_heads.get_or_null(row + 1);

		int line_end = next_line_head ? (*next_line_head - 1) : characters.count;

		return line_end;
	}


	void init();

	void prepare_for_process();

	void update_size();



	inline Unicode_String copy_region(int start, int length, Allocator allocator)
	{			
		assert(characters_mutex.is_being_locked());

		auto builder = build_string<char32_t>(allocator, length);

		for (int i = 0; i < length; i++)
		{
			int index = start + i;
			assert(index >= 0 && index < characters.count);
			builder.append(characters[index]->c);
		}

		return builder.get_string();
	}

	inline void append_non_process(int index, Unicode_String str, Terminal_Character_Type type)
	{
		assert(characters_mutex.is_being_locked());
		assert(!python.is_running_in_non_limited_context());

		characters.ensure_capacity(str.length + characters.count);

		Terminal_Character space_character = {
			.character_type = type,
		};

		for_range(i, str.length)
		{
			space_character.c = str[i];
			characters.add_at_index(index + i, space_character);
		}
	}

	inline void remove_non_process(int index, int length)
	{
		assert(characters_mutex.is_being_locked());

		characters.remove_range(index, length);
	}


	inline int get_user_input_length()
	{
		assert(characters_mutex.is_being_locked());

		for (int i = characters.count - 1; i > 0; i--)
		{
			auto character = characters[i];
			if (character->character_type != TERMINAL_CHARACTER_TYPE_USER_INPUT)
				return (characters.count - 1) - i;
		}

		return characters.count;
	}

	inline Unicode_String copy_user_input(Allocator allocator)
	{
		assert(characters_mutex.is_being_locked());

		int length = get_user_input_length();

		return copy_region(characters.count - length, length, allocator);
	}

	inline void set_user_input(Unicode_String str)
	{
		assert(characters_mutex.is_being_locked());

		int length = get_user_input_length();

		characters.count -= length;

		typer_ui.invalidate_after(characters.count - 1);

		append_non_process(characters.count, str, TERMINAL_CHARACTER_TYPE_USER_INPUT);
	}



	void append_process_character(char32_t c);


	void clear_screen(int mode = 0);

	void move_cursor_up(int delta);
	void move_cursor_down(int delta);
	void move_cursor_left(int delta);
	void move_cursor_right(int delta);

	int get_cursor_row(bool bottom_relative_positioning = false);
	int get_cursor_column();

	void get_cursor_position(int* row, int* column, bool bottom_relative_positioning = false);
	void set_cursor_position(int row, int column, bool bottom_relative_positioning = false);

	inline void convert_row_from_bottom_relative_to_global(int* row)
	{
		int terminal_height = get_terminal_height_in_characters();

		if (line_heads.count <= terminal_height)
			return;

		*row = line_heads.count - terminal_height + *row; 
	}

	void set_cursor_row(int row, bool bottom_relative_positioning = false);
	void set_cursor_column(int column);

	void move_cursor(int row_delta, int column_delta);

	void erase_characters_after_cursor(int erase_count);
	void erase_line(int mode);

	void insert_line(int count);

	void sgr_reset();

	int  calculate_terminal_width_in_pixels();
	int  calculate_terminal_height_in_pixels();

	int  get_terminal_width_in_characters();
	int  get_terminal_height_in_characters();



	void move_line_heads_after_row_by(int after_row, int by);
};

inline Terminal terminal;
