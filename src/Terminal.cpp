#include "Terminal.h"

#include "b_lib/Math.h"

#include "Terminal_IO.h"
#include "Main.h"


void validate_process_caret()
{
	assert(terminal.process_caret >= terminal.process_output_start && terminal.process_caret <= terminal.characters.count);
}

void validate_line_heads()
{
	assert(terminal.line_heads.count > 0);

	for (int head: terminal.line_heads)
	{
		assert(head == terminal.process_output_start || terminal.characters[head - 1]->c == '\n');
	}

	for (int i = terminal.process_output_start; i < terminal.characters.count; i++)
	{
		if (terminal.characters[i]->c == '\n')
		{
			assert(terminal.line_heads.contains(i + 1));
		}
	}
}

void validate()
{
#if DEBUG
	// return

	validate_process_caret();
	validate_line_heads();

#endif
}


void Terminal::init()
{
	ZoneScoped;

	characters = make_array<Terminal_Character>(128, c_allocator);
	line_heads = make_array<int>(64, c_allocator);
}

void Terminal::prepare_for_process()
{
	background_color_is_set = false;
	foreground_color_is_set = false;

	current_row  = 0;
	current_column = 0;

	process_caret = characters.count;
	process_output_start = process_caret;

	current_foreground_color = typer_ui.default_foreground_color;
	current_background_color = typer_ui.default_background_color;
	
	line_heads.clear();
	line_heads.add(process_output_start);
}

void Terminal::update_size()
{
	ZoneScoped;

	if (!typer_ui.typer_font_face) return;

	int pixel_width  = calculate_terminal_width_in_pixels();
	int pixel_height = calculate_terminal_height_in_pixels();

	int new_terminal_width  = pixel_width  / typer_ui.typer_font_face->space_width;
	int new_terminal_height = pixel_height / typer_ui.typer_font_face->line_spacing;

#if OS_LINUX
	if (new_terminal_width == terminal_width && new_terminal_height == terminal_height && 
		pixel_width == terminal_width_pixels && pixel_height == terminal_height_pixels)
		return;
#else
	if (new_terminal_width == terminal_width && new_terminal_height == terminal_height)
		return;
#endif

	
	terminal.terminal_width  = new_terminal_width;
	terminal.terminal_height = new_terminal_height;

#if OS_LINUX
	terminal.terminal_width_pixels  = pixel_width;
	terminal.terminal_height_pixels = pixel_height;
#endif


	terminal_io.update_pty_size();
}

void Terminal::append_process_character(char32_t c)
{
	ZoneScoped;
	
	typer_ui.invalidate_after(process_caret);


	// @TODO: continue??
	// @TODO: clearing after erase line doesn't seem to be working consistenly
	// @BugRepro: try wsl -> git status in fullscreen mode and then select the output.
	auto clear_ending_colorless_spaces = [&]()
	{ 
		if (!CLEAR_ENDING_SPACES) return;
		// :ClearingEndingSpaces
		// Clear current line's ending spacing, before moving to next line.
		// @TODO: does this exact code lead to unwanted consuquences???
			
		int line_end = get_line_end(current_row);

		Terminal_Character* next_character = characters.get_or_null(line_end);

		// Make sure, that cursor is at the end of the line
		if (!next_character || next_character->c == '\n')
		{
			int delete_characters_count = 0;

			for (int i = line_end - 1; i > process_output_start; i--)
			{
				Terminal_Character* character = characters.get_or_null(i);

				if (!character) continue;

				if (character->c == ' ' &&
					((character->flags & TERMINAL_CHARACTER_FLAG_COLORED) == 0))
				{
					delete_characters_count += 1;
				}
				else
				{
					break;
				}
			}

			int clear_start = line_end - delete_characters_count;

			characters.remove_range(clear_start, delete_characters_count);
			move_line_heads_after_row_by(current_row, -delete_characters_count);



			assert(current_column == (process_caret - *line_heads[current_row]));

			if (process_caret >= clear_start)
			{
				process_caret  = clear_start;
				current_column = process_caret - *line_heads[current_row];
			}
		}
	};

	switch (c)
	{
		case '\n':
		{
			clear_ending_colorless_spaces();			

			int row, column;
			get_cursor_position(&row, &column);

			assert(line_heads.count);

		#if IS_POSIX
			set_cursor_position(row + 1, 0);
		#elif OS_WINDOWS
			// Special case.
			if (row == line_heads.count - 1)
			{
				set_cursor_position(row + 1, 0);
			}
			else
			{
				set_cursor_position(row + 1, column);
			}
		#else
			static_assert(false);
		#endif
		}
		break;

		case '\r':
		{
			clear_ending_colorless_spaces();			
			
			int row, column;
			get_cursor_position(&row, &column);
			set_cursor_position(row, 0);
		}
		break;

		case DELETE:
		{
			if (process_caret >= process_output_start && process_caret < get_characters_count())
			{
				Terminal_Character* existing_character = characters[process_caret];

				// Maybe better handle this situation
				if (existing_character->c != '\n')
				{
					*existing_character = {
						.c = ' ',

						.character_type = TERMINAL_CHARACTER_TYPE_PROCESS_OUTPUT,
					};
				}
			}

			validate();
		}
		break;

		case BACKSPACE:
		{
			int previous_index = process_caret - 1;

			if (previous_index >= process_output_start && previous_index < get_characters_count())
			{
				Terminal_Character* existing_character = characters[previous_index];

				// Maybe better handle this situation
				if (existing_character->c != '\n')
				{
					*existing_character = {
						.c = ' ',

						.character_type = TERMINAL_CHARACTER_TYPE_PROCESS_OUTPUT,
					};

					process_caret  -= 1;
					current_column -= 1;

					assert(process_caret == previous_index);
				}
			}

			validate();
		}
		break;

		default:
		{
			Terminal_Character character = {
				.c = c,

				.character_type = TERMINAL_CHARACTER_TYPE_PROCESS_OUTPUT,

				.flags = conditional_colored_flag(),
				
				.color            = current_foreground_color,
				.background_color = current_background_color,
			};


			Terminal_Character* existing_character = characters.get_or_null(process_caret);

			// @TODO: think about better handling this situation.
			if (existing_character)
			{
				if (existing_character->c == '\n')
				{
					characters.add_at_index(process_caret, character);
					move_line_heads_after_row_by(current_row, 1);
				}
				else
				{
					*existing_character = character;
				}
			}
			else
			{
				assert(process_caret == characters.count);
				characters.add(character);
			}

			process_caret  += 1;
			current_column += 1;

			validate();
		}
	}
}

void Terminal::clear_screen(int mode)
{
	ZoneScoped;

	assert(characters_mutex.is_being_locked());

	// @TODO: fill with spaces???
	// @TODO: do selection_length movement

	// Modes:
	// 0 - clear to the end
	// 1 - clear to the beginning
	// 2 - clear entire screen
	// 3 - clear entire screen and scrollback


	// @TODO: do user cursor stuff ??


	auto remove_line_heads_after = [&](int after)
	{
		for_range(i, line_heads.count)
		{
			int head = *line_heads[i];

			if (head > after)
			{
				line_heads.count = i;
				break;
			}
		}
	};

	switch (mode)
	{
		case 0:
		{
			typer_ui.invalidate_after(process_caret);
			characters.count = process_caret;

			remove_line_heads_after(process_caret);
		}
		break;

		case 1:
		{
			typer_ui.invalidate_after(process_output_start);
			characters.remove_range(process_output_start, process_caret - process_output_start);

			for (int i = process_caret - 1; i >= process_output_start; i--)
			{
				Terminal_Character* character = characters[i];
				if (character->c != '\n' &&
					character->c != '\t')
				{
					character->c = ' ';
					character->flags = TERMINAL_CHARACTER_FLAG_DEFAULT;
				}
			}

			process_caret = process_output_start;
		}
		break;

		case 2:
		{
			process_caret = process_output_start;
			
			typer_ui.invalidate_after(process_output_start);
			characters.remove_range(process_output_start, characters.count - process_output_start);

			remove_line_heads_after(process_caret);
		}
		break;

		case 3:
		{
			typer_ui.invalidate_after(-1);
			characters.clear();

			process_output_start = 0;
			process_caret = 0;

			line_heads.count = 1; // Leave only starting line head
			assert(line_heads.capacity >= 1);
			*line_heads[0] = process_output_start;
		}
		break;
	}


	// Memory wouldn't free itself.
	if (characters.count + 10000 < characters.capacity)
	{
		characters.shrink_to_capacity(characters.count + 200);
	}

	validate();
}

void Terminal::move_cursor_up(int delta)
{
	move_cursor(-delta, 0);
}

void Terminal::move_cursor_down(int delta)
{
	move_cursor(delta, 0);
}

void Terminal::move_cursor_left(int delta)
{
	move_cursor(0, -delta);
}

void Terminal::move_cursor_right(int delta)
{
	move_cursor(0, delta);
}

int Terminal::get_cursor_row(bool bottom_relative_positioning)
{
	int row;
	int column;

	get_cursor_position(&row, &column);

	if (bottom_relative_positioning)
		convert_row_from_bottom_relative_to_global(&row);

	return row;
}

int Terminal::get_cursor_column()
{
	int row;
	int column;

	get_cursor_position(&row, &column);

	return column;
}

void Terminal::get_cursor_position(int* row, int* column, bool bottom_relative_positioning)
{
	ZoneScoped;

	*row    = current_row;
	if (bottom_relative_positioning)
		convert_row_from_bottom_relative_to_global(row);

	*column = current_column;
}

void Terminal::set_cursor_position(int row, int column, bool bottom_relative_positioning)
{
	ZoneScoped;

	if (column < 0)
	{
		column = 0;
	}
	if (row < 0)
	{
		row = 0;
	}

	validate();

	if (bottom_relative_positioning)
		convert_row_from_bottom_relative_to_global(&row);



	// First, go to required line.
	{
		if (row >= line_heads.count)
		{
			int lines_to_append = row - line_heads.count + 1;

			Terminal_Character new_line_character = {
				.c = '\n',

				.character_type = TERMINAL_CHARACTER_TYPE_PROCESS_OUTPUT,
			};

			for_range(i, lines_to_append)
			{
				characters.add(new_line_character);
			
				line_heads.add(characters.count);
			}

			process_caret = characters.count;
		}
		else
		{
			process_caret = *line_heads[row];
		}
		
		current_row = row;

		assert(process_caret == 0 || characters[process_caret - 1]->c == '\n');

		validate();
	}


	// Second, go to required row. process_caret is at the beginning of the line
	{
		assert(process_caret == *line_heads[current_row]);

		int terminal_width = get_terminal_width_in_characters();

		if (column > terminal_width)
			column = terminal_width;


		int line_end = get_line_end(current_row);

		int characters_on_this_line = line_end - process_caret;
		assert(characters_on_this_line >= 0);


		// '|adssadddsdss{                  }|'
		//  ↑               ↑                ↑
		//  process_caret   Empty space.     Where caret needs to be place

		// Fill empty space
		{
			int to_fill = column - characters_on_this_line;
			if (to_fill < 0)
				to_fill = 0;


			Terminal_Character space_character = {
				.c = ' ',

				.character_type = TERMINAL_CHARACTER_TYPE_PROCESS_OUTPUT,
			};

			for (int i = 0; i < to_fill; i++)
			{
				characters.add_at_index(line_end, space_character);
			}

			move_line_heads_after_row_by(current_row, to_fill);
		}


		process_caret += column;
		current_column = column;

		assert(process_caret == (*line_heads[current_row] + current_column));

		validate();
	}
}

void Terminal::move_line_heads_after_row_by(int after_row, int by)
{
	if (by == 0) return;

	for (int i = after_row + 1; i < line_heads.count; i++)
	{
		line_heads.data[i] += by;
	}
}


void Terminal::set_cursor_row(int row, bool bottom_relative_positioning)
{
	ZoneScoped;

	int current_row;
	int current_column;

	get_cursor_position(&current_row, &current_column, bottom_relative_positioning);
	set_cursor_position(row, current_column);
}

void Terminal::set_cursor_column(int column)
{
	ZoneScoped;

	int current_row;
	int current_column;

	get_cursor_position(&current_row, &current_column);
	set_cursor_position(current_row, column);
}

void Terminal::move_cursor(int row_delta, int column_delta)
{
	int row, column;
	get_cursor_position(&row, &column);

	set_cursor_position(row + row_delta, column + column_delta);
}

void Terminal::erase_characters_after_cursor(int erase_count)
{
	ZoneScoped;


	validate();


	int terminal_width = get_terminal_width_in_characters();


	int erase_count_max = terminal_width - current_column;

	erase_count = clamp(0, erase_count_max, erase_count);


	bool erasing_till_the_end_of_the_line = erase_count == erase_count_max;

	Terminal_Character space_character = {
		.c = ' ',

		.character_type = TERMINAL_CHARACTER_TYPE_PROCESS_OUTPUT,

		.flags = conditional_colored_flag(),
		
		.color            = current_foreground_color,
		.background_color = current_background_color,
	};


	// :ClearingEndingSpaces
	// @Hack: special case again, if erasing till the end and if no background color is set do not fill with spaces
	if (erasing_till_the_end_of_the_line && !background_color_is_set && CLEAR_ENDING_SPACES)
	{
		int line_end = get_line_end(current_row);

		characters.remove_range(process_caret, line_end - process_caret);
		move_line_heads_after_row_by(current_row, -(line_end - process_caret));
	}
	else
	{
		int line_head_move_count = 0;

		for (int i = 0; i < erase_count; i++)
		{
			int index = process_caret + i;


			if (index >= characters.count)
			{
				assert(index == characters.count);
				characters.add(space_character);

				// No line head should be after current line
				assert(current_row == line_heads.count - 1);
			}
			else if (characters[index]->c == '\n')
			{
				characters.add_at_index(index, space_character);
				line_head_move_count += 1;
			}
			else
			{
				characters.data[index] = space_character;
			}
		}

		move_line_heads_after_row_by(current_row, line_head_move_count);
	}

	validate();
}

// @TODO: test all modes
void Terminal::erase_line(int mode)
{
	// Modes:  
	//  0 - clear from cursor to the end of the line.
	//  1 - clear from cursor to the beginning of the line.
	//  2 - clear entire line.

	ZoneScoped;

	assert(line_heads.count > 0);

	int line_start = *line_heads[current_row];


	validate();


	Terminal_Character space_character = {
		.c = ' ',

		.character_type = TERMINAL_CHARACTER_TYPE_PROCESS_OUTPUT,

		.flags = conditional_colored_flag(),
		
		.color            = current_foreground_color,
		.background_color = current_background_color,
	};


	// Clear toward the beginning
	if (mode == 1 || mode == 2)
	{
		for (int i = process_caret - 1; i >= line_start; i--)
		{
			assert(characters[i]->c != '\n');

			*characters[i] = space_character;
		}
	}

	// Clear toward the ending
	if (mode == 0 || mode == 2)
	{
		// If the line is bigger than terminal width, we must truncate it.
		int line_end = get_line_end(current_row);

		assert(line_end >= process_caret);

		characters.remove_range(process_caret, line_end - process_caret);
		move_line_heads_after_row_by(current_row, -(line_end - process_caret));

		
		// :ClearingEndingSpaces
		// @Hack: if background color is default, do not fill with spaces, but just keep erased line.
		if (background_color_is_set || !CLEAR_ENDING_SPACES)
		{
			validate();

			int erase_count = get_terminal_width_in_characters() - current_column;
			erase_characters_after_cursor(erase_count);
		}
	}

	validate();

}

void Terminal::insert_line(int count)
{
	ZoneScoped;

	validate();


	int line_start = *line_heads[current_row];


	Terminal_Character character = {
		.c = '\n',

		.character_type = TERMINAL_CHARACTER_TYPE_PROCESS_OUTPUT,

		.flags = TERMINAL_CHARACTER_FLAG_DEFAULT,
	};

	for (int i = 0; i < count; i++)
	{
		line_heads.add(line_start + i);
		characters.add_at_index(line_start, character);
	}

	move_line_heads_after_row_by(current_row + count, count);

	set_cursor_position(current_row + count, current_column);

	validate();
}

void Terminal::sgr_reset()
{
	ZoneScoped;

	background_color_is_set = false;
	foreground_color_is_set = false;

	current_background_color = typer_ui.default_background_color;
	current_foreground_color = typer_ui.default_foreground_color;
}



int Terminal::calculate_terminal_width_in_pixels()
{
	return get_active_width() - typer_ui.current_scrollbar_width - (typer_ui.current_scrollbar_width == 0 ? 0 : renderer.scaled(line_margin_from_scrollbar_gradient));
}
int Terminal::calculate_terminal_height_in_pixels()
{
	return renderer.height - typer_ui.current_bottom_bar_height - renderer.scaled(window_header_size);
}


int Terminal::get_terminal_width_in_characters()
{
#if 1
	return terminal_width;
#else
	return calculate_terminal_width_in_pixels() / typer_font_face->space_width;
#endif
}
int Terminal::get_terminal_height_in_characters()
{
#if 1
	return terminal_height;
#else
	return calculate_terminal_height_in_pixels() / typer_font_face->line_spacing;
#endif
}