#include "Output_Processor.h"

#include "Terminal.h"
#include "Main.h"
#include "Terminal_IO.h"

#include "ASCII_Sequence_Stuff.h"

void Output_Processor::init()
{
	escape_sequence_payload = make_array<char32_t>(32, c_allocator);

	create_arena_allocator(&print_output_arena, c_allocator, 4 * 1024);
#ifdef ALLOCATOR_NAMES
	print_output_arena.name = "print_output_arena";
#endif
}

void Output_Processor::clean()
{
	is_output_interactive = false;
	process_output_interactive_id = 0;
}

void Output_Processor::process_output(char* output_buffer, int output_buffer_length)
{
	ZoneScoped;

	if (output_buffer_length == 0) return;


	print_output_arena.reset();

	Scoped_Lock buffer_lock(terminal.characters_mutex);


	need_to_redraw_next_frame_from_nonmain_thread();

	assert(terminal.process_caret <= terminal.characters.count);

	typer_ui.invalidate_after(terminal.process_caret - 1);


	for (int i = 0; i < output_buffer_length; i++)
	{
		char32_t c = output_buffer[i];

#if LOG_PROCESS_OUTPUT_TO_HUYE_TXT
		huye_file.write(String(&output_buffer[i], 1));
#if OS_WINDOWS
		huye_file.flush();
#endif
#endif

		{
			// Detect utf-8 sequence
			if (utf_code_units_to_read)
			{
				utf_code_units_to_read -= 1;

				utf8_to_utf32_accumulator |= (c & 0b0011'1111) << (utf_code_units_to_read * 6);

				if (utf_code_units_to_read == 0)
				{
					c = utf8_to_utf32_accumulator;
				}
				else
				{
					continue;
				}
			}
			else
			{
				if (c & 0b1000'0000)
				{
					utf8_to_utf32_accumulator = (char32_t) 0;

					if ((c & 0b1110'0000) == 0b1100'0000)
					{
						utf8_to_utf32_accumulator |= (c & 0b0001'1111) << 6;
						
						utf_code_units_to_read = 1;
						continue;
					}
					else if ((c & 0b1111'0000) == 0b1110'0000)
					{
						utf8_to_utf32_accumulator |= (c & 0b0000'1111) << 12;
						
						utf_code_units_to_read = 2;
						continue;
					}
					else if ((c & 0b1111'1000) == 0b1111'0000)
					{
						utf8_to_utf32_accumulator |= (c & 0b0000'0111) << 18;

						utf_code_units_to_read = 3;
						continue;
					}
				}
			}
		}	



		defer { process_last_character = c; };

		if (c == SHIFT_OUT || c == SHIFT_IN)
		{
			// @TODO: do not ignore. 
			continue;
		}


	#if LOG_PROCESS_OUTPUT_TO_HUYE_TXT
		#define WRITE_DESC(x) huye_file.write(x)
	#else
		#define WRITE_DESC(x)
	#endif

		if (in_escape_sequence())
		{
			escape_sequence_payload.add(c);

			// Check for "escape sequence final byte".
			// 0x07 is BEL that iTerm2 uses to terminate it's private sequences.
			//  But i'm not sure that we should support them...

			if ((c == ']' && process_last_character == '[' && escape_sequence_payload.count == 1))
			{
				sequence_type = Sequence_Type::Typer_Private;
				// iTerm2 escape sequences begin like this as well, we can use number after to distinguish. iTerm2 uses 1337 if i remember correctly.
			}

			bool should_escape_sequence_end = false;

			if (sequence_type == Sequence_Type::Typer_Private ||
				sequence_type == Sequence_Type::OSC)
			{
				should_escape_sequence_end = c == BELL;
			}
			else if (sequence_type == Sequence_Type::CSI)
			{
				should_escape_sequence_end = c >= 0x40 && c <= 0x7E;
			}
			else if (sequence_type == Sequence_Type::ESC)
			{
				assert(escape_sequence_payload.count > 0);
				char32_t first_char = escape_sequence_payload.data[0];

				if (first_char == '%' ||
					first_char == '(' ||
					first_char == ')')
				{
					should_escape_sequence_end = escape_sequence_payload.count > 1;
				}
				else
				{
					should_escape_sequence_end = true;
				}

				assert(first_char != '[' && first_char != ']');
			}
			else
			{
				assert(false);
			}

			if (should_escape_sequence_end)
			{
				Unicode_String payload_unicode = Unicode_String(escape_sequence_payload.data, escape_sequence_payload.count);

				// log(ctx.logger, U"Escape sequence: %", payload_unicode);

				String payload = payload_unicode.to_utf8_but_ascii(print_output_arena);

				assert(payload.length);

				switch(sequence_type)
				{
					case Sequence_Type::Typer_Private:
					{
						if (payload.advance_if_starts_with("]322"))
						{
							process_typer_sequence(payload);
						}
						else // @TODO: check for other terminal's private escape sequences.
						{
							received_unknown_escape_sequence(payload);
						}
					}
					break;

					case Sequence_Type::OSC:
						process_osc_sequence(payload);
						break;

					case Sequence_Type::CSI:
						process_csi_sequence(payload);
						break;
	
					case Sequence_Type::ESC:
						process_esc_sequence(payload);
						break;
				}


				escape_sequence_payload.clear();

				sequence_type = Sequence_Type::None;
			}
			continue;
		}

		if (c == ESCAPE)
		{
			continue;
		}

		if (process_last_character == ESCAPE)
		{
			sequence_type = Sequence_Type::ESC;

			if (c == '[' || c == ']' || c == '(') // @TODO: handle x1b(B like sequences
			{
				switch (c)
				{
					case ']':
						sequence_type = Sequence_Type::OSC;
						break;

					case '[':
						sequence_type = Sequence_Type::CSI;
						break;

					case '(':
						sequence_type = Sequence_Type::ESC;
						break;
				}
			}

			if (sequence_type == Sequence_Type::ESC)
			{
				// Added, because unlike CSI and OSC 
				// sequences this character actually is useful
				escape_sequence_payload.add(c);
			}

			continue;
		}


		switch (c)
		{
			case BELL: // Bell
			{
				// @TODO: play sound
				continue;
			}
			break;
		}


		terminal.append_process_character(c);
	}
}


void Output_Processor::process_typer_sequence(String payload)
{
	ZoneScoped;

	auto send_number = [&](int number)
	{
		Scoped_Lock lock(terminal_io.data_to_write_mutex);

		terminal_io.write_string("\x1b[]");
		terminal_io.write_string(to_string(number, print_output_arena));
		terminal_io.write_string("\x07");
	};


	if (payload.starts_with("lock_terminal"))
	{
		terminal.characters_mutex.lock();
		Log(U"Locked terminal: %", frame_index);
	}
	else if (payload.starts_with("unlock_terminal"))
	{
		terminal.characters_mutex.unlock();
		Log(U"Unlocked terminal: %", frame_index);
	}
	else if (payload.starts_with("disable_auto_scroll"))
	{
		console_settings.auto_scroll = false;
	}
	else if (payload.starts_with("enable_auto_scroll"))
	{
		console_settings.auto_scroll = true;
	}

	else if (payload.starts_with("disable_scrollbar"))
	{
		console_settings.scrollbar_enabled = false;
	}
	else if (payload.starts_with("enable_scrollbar"))
	{
		console_settings.scrollbar_enabled = true;
	}


	else if (payload.starts_with("terminal_width_in_pixels"))
	{
		send_number(terminal.calculate_terminal_width_in_pixels());
	}
	else if (payload.starts_with("terminal_height_in_pixels"))
	{
		send_number(terminal.calculate_terminal_height_in_pixels());
	}

	else if (payload.starts_with("terminal_width_in_characters"))
	{
		send_number(terminal.get_terminal_width_in_characters());
	}
	else if (payload.starts_with("terminal_height_in_characters"))
	{
		send_number(terminal.get_terminal_height_in_characters());
	}
}

void Output_Processor::process_csi_sequence(String payload)
{
	ZoneScoped;

	constexpr bool use_relative_to_bottom_cursor_positioning = true;

	if (payload.ends_with("A"))
	{
		int move_delta = 1;
		
		if (parse_number(payload.sliced(0, payload.length - 1), &move_delta) || payload.length == 1)
		{
			terminal.move_cursor_up(move_delta);
		}
	}
	else if (payload.ends_with("B"))
	{
		int move_delta = 1;
		if (parse_number(payload.sliced(0, payload.length - 1), &move_delta) || payload.length == 1)
		{
			terminal.move_cursor_down(move_delta);
		}
	}
	else if (payload.ends_with("C"))
	{
		int move_delta = 1;
		if (parse_number(payload.sliced(0, payload.length - 1), &move_delta) || payload.length == 1)
		{
			terminal.move_cursor_right(move_delta);
		}
	}
	else if (payload.ends_with("D"))
	{
		int move_delta = 1;
		if (parse_number(payload.sliced(0, payload.length - 1), &move_delta) || payload.length == 1)
		{
			terminal.move_cursor_left(move_delta);
		}
	}
	else if (payload.ends_with("H") || payload.ends_with("f"))
	{
		payload.length -= 1; // Remove "H"
		auto arguments = split(payload, ';', print_output_arena);


		WRITE_DESC("Set cursor position\n");

		// terminal.set_cursor_position uses 0,0 based coordinate system,
		//   instead of 1,1 based

		if (arguments.count == 2)
		{
			arguments[0]->trim();
			arguments[1]->trim();

			int row    = -1;
			int column = -1;


			if (arguments[0]->length == 0)
				row = 1;
			else
				parse_number(*arguments[0], &row);


			if (arguments[1]->length == 0)
				column = 1;
			else
				parse_number(*arguments[1], &column);


			if (row == 23)
			{
				int k = 43;
				// __debugbreak();
			}

			if (row != -1 && column != -1)
				terminal.set_cursor_position(row - 1, column - 1, use_relative_to_bottom_cursor_positioning);
		}
		else if (arguments.count == 1)
		{
			int row = -1;


			if (arguments[0]->length == 0)
				row = 1;
			else
				parse_number(*arguments[0], &row);


			terminal.set_cursor_position(row - 1, 0, use_relative_to_bottom_cursor_positioning);
		}
	}
	else if (payload.ends_with("d"))
	{
		WRITE_DESC("Set cursor row\n");

		int row = 1;
		parse_number(payload.sliced(0, payload.length - 1), &row);

		terminal.set_cursor_row(row - 1, use_relative_to_bottom_cursor_positioning);
	}
	else if (payload.ends_with("G"))
	{
		WRITE_DESC("Set cursor column\n");

		int column = 1;
		parse_number(payload.sliced(0, payload.length - 1), &column);

		terminal.set_cursor_column(column - 1);
	}
	else if (payload.ends_with("J"))
	{
		WRITE_DESC("Clear screen\n");

		int clear_mode = 0;
		parse_number(payload.sliced(0, payload.length - 1), &clear_mode);

		terminal.clear_screen(clear_mode);
	}
	else if (payload.ends_with("K"))
	{
		WRITE_DESC("Erase line\n");

		int erase_mode = 0;
		parse_number(payload.sliced(0, payload.length - 1), &erase_mode);

		terminal.erase_line(erase_mode);
	}
	else if (payload.ends_with("L"))
	{
		WRITE_DESC("Insert line\n");

		int insert_count = 1;
		parse_number(payload.sliced(0, payload.length - 1), &insert_count);

		terminal.insert_line(insert_count);
	}
	else if (payload.ends_with("X"))
	{
		WRITE_DESC("Erase characters\n");

		int erase_count = 1;
		parse_number(payload.sliced(0, payload.length - 1), &erase_count);

		terminal.erase_characters_after_cursor(erase_count);
	}
	else if (payload.ends_with("r"))
	{
		WRITE_DESC("Set scrollable region\n");

		// @TODO: implement
	}
	else if (payload.ends_with("m")) // Select graphic rendition 
	{
		payload.length -= 1;

		process_sgr(payload);
	}
	else
	{
		received_unknown_escape_sequence(payload);
	}
}

void Output_Processor::process_esc_sequence(String payload)
{
	ZoneScoped;

	if (payload.length == 0) return;

	// @TODO: implement two character sequences from
	//  https://man7.org/linux/man-pages/man4/console_codes.4.html


	// "Start sequence defining G0 character set"
	// "Start sequence defining G1 character set"
	if (payload.starts_with("(") || payload.starts_with(")")) 
	{
		// @TODO: do we really want to ignore this??
		return;
	}

	if (payload == "=") // @TODO: implement
	{

	}
	else if (payload == ">") // @TODO: implement
	{

	}



	// @TODO: implement
}

void Output_Processor::process_osc_sequence(String payload)
{
	ZoneScoped;
	
	// @TODO: implement
}


void Output_Processor::process_sgr(String payload)
{
	auto arguments = split(payload, ';', print_output_arena);

	while (arguments.count)
	{
		String first_arg = *arguments[0];
		arguments.advance(1);

		if (first_arg.is_empty()) // Special case.
		{
			terminal.sgr_reset();
		}
		else
		{
			int number;
			if (parse_number(first_arg, &number))
			{
				//log(ctx.logger, U"Got sgr mode: %", number);

				switch (number)
				{
					case 0: // SGR reset
					{
						terminal.sgr_reset();
					}
					break;

					case 30: // Set foreground color
					case 31:
					case 32:
					case 33:
					case 34:
					case 35:
					case 36:
					case 37:
					{
						terminal.set_foreground_color(decode_8_bit_color(number - 30));
					}
					break;
					case 38: // Set foreground color
					{
						if (arguments.count == 0)
						{
							terminal.set_foreground_color(typer_ui.default_foreground_color.rgb_value);
						}
						else
						{
							rgb new_foreground_color;

							int arguments_advance = 0;
							if (parse_color_from_escape_sequence(arguments, &new_foreground_color, &arguments_advance))
							{
								terminal.set_foreground_color(new_foreground_color);
							}

							arguments.advance(arguments_advance);
						}
					}
					break;
					case 39:
					{
						terminal.set_foreground_color(typer_ui.default_foreground_color.rgb_value);
					}
					break;
					case 40: // Set background color
					case 41:
					case 42:
					case 43:
					case 44:
					case 45:
					case 46:
					case 47:
					{
						terminal.set_background_color(decode_8_bit_color(number - 40));
					}
					break;
					case 48: // Set background color
					{
						if (arguments.count == 0)
						{
							terminal.set_background_color(typer_ui.default_background_color.rgb_value);
						}
						else
						{
							rgb new_background_color;

							int arguments_advance = 0;
							if (parse_color_from_escape_sequence(arguments, &new_background_color, &arguments_advance))
							{
								terminal.set_background_color(new_background_color);
							}
							arguments.advance(arguments_advance);
						}
					}
					break;
					case 49:
					{
						terminal.set_background_color(typer_ui.default_background_color.rgb_value);
					}
					break;

					case 90: // Set bright foreground color
					case 91:
					case 92:
					case 93:
					case 94:
					case 95:
					case 96:
					case 97:
					{
						terminal.set_foreground_color(decode_8_bit_color(number - 90 + 8));
					}
					break;

					case 100: // Set bright background color
					case 101:
					case 102:
					case 103:
					case 104:
					case 105:
					case 106:
					case 107:
					{
						terminal.set_background_color(decode_8_bit_color(number - 100 + 8));
					}
					break;
					break;
				}
			}
			else
			{
				received_unknown_escape_sequence(payload);
				// @TODO: handle
			}
		}
	}
}