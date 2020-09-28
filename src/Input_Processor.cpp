#include "Input_Processor.h"

#include "Main.h"
#include "Terminal.h"
#include "Terminal_IO.h"

#include <ctype.h>

void Input_Processor::check_if_macro_bindings_triggered()
{
	assert(!python.is_running);

	if (running_macro.length) return;

	// @TODO: implement
}

void Input_Processor::check_copy_paste_and_interrupt_bindings()
{
	if (!python.is_running)
	{
		if (key_bindings.is_action_type_triggered(Action_Type::Select_All))
		{
			int input_length = terminal.get_user_input_length();

			if (user_cursor >= (terminal.get_characters_count() - input_length) && input_length != 0)
			{
				user_cursor = terminal.get_characters_count();
				selection_length = -input_length;
			}
			else
			{
				user_cursor = terminal.get_characters_count();
				selection_length = -terminal.get_characters_count(); // @TODO: what happens during running process.
			}
		}
	}


	// Special handling happens here, because usually Interrupt and Copy_Text will be binded
	//  to the same binding.
	if (key_bindings.is_action_type_triggered(Action_Type::Copy_Text) && selection_length)
	{
		copy_selection_to_clipboard();
	}
	else if (key_bindings.is_action_type_triggered(Action_Type::Interrupt))
	{
		python.keyboard_interrupt(code_location());
	}


	if (!python.is_running)
	{
		if (key_bindings.is_action_type_triggered(Action_Type::Paste_Text))
		{
			paste_from_clipboard_to_user_cursor();
		}

		if (key_bindings.is_action_type_triggered(Action_Type::Cut_Text))
		{
			cut_from_clipboard_to_user_cursor();
		}
	}
}

void Input_Processor::process_input_for_terminal_outside_process()
{
	check_copy_paste_and_interrupt_bindings();

	check_if_macro_bindings_triggered();


	// Do autocomplete suggestion.
	defer {
		
		Unicode_String user_input = terminal.copy_user_input(frame_allocator);


		if (is_typing_command() && python.autocompleted_string == user_input && python.autocomplete_result.count > 0)
		{
			Scoped_Lock lock(python.autocomplete_result_mutex);


			bool do_complete = key_bindings.is_action_type_triggered(Action_Type::Autocomplete);

			if (do_complete)
			{
				typer_ui.invalidate_after(terminal.characters.count);
				terminal.append_non_process(terminal.characters.count, *python.autocomplete_result[python.current_autocomplete_result_index], TERMINAL_CHARACTER_TYPE_USER_INPUT);

				selection_length = 0;
				user_cursor = terminal.characters.count;
			}
		}
		else if (python.autocompleted_string != user_input)
		{
			python.run_autocomplete_if_its_not_running();
		}
	};


	auto append_user_character = [&](char32_t c)
	{
		bool can_append;

		if (selection_length)
		{
			u64 selection_start = min(user_cursor, user_cursor + selection_length);
			u64 selection_end   = max(user_cursor, user_cursor + selection_length);

			can_append = true;

			for (u64 i = selection_start; i <= selection_end; i++)
			{
				if (!is_character_at_index_is_user_input(i))
				{
					can_append = false;				
				}
			}

			if (can_append)
			{
				typer_ui.invalidate_after(selection_start);

				terminal.remove_non_process(selection_start, abs(selection_length));
				user_cursor = selection_start;
				selection_length = 0;

				buffer_changed = true;
			}
		}
		else
		{
			can_append = is_character_at_index_is_user_input(user_cursor);
		}

		if (can_append)
		{
			typer_ui.invalidate_after(user_cursor);
			

			Unicode_String to_append;
			to_append.data = &c;
			to_append.length = 1;

			terminal.append_non_process(user_cursor, to_append, TERMINAL_CHARACTER_TYPE_USER_INPUT);
				
			user_cursor += 1;

		}
	};	
	
	
	for (Input_Node& node : input.nodes)
	{	
		switch (node.input_type)
		{
			case Input_Type::Char:
			{
				append_user_character(node.character);
			}
			break;

			case Input_Type::Key:
			{
				if (node.key_action == Key_Action::Down)
				{
					if (key_bindings.is_key_used_by_triggered_action(node.key)) continue;


					switch (node.key)
					{
						case Key::Up_Arrow:
						{
							typer_ui.recalculate_renderer_lines();

							bool is_shift_held = input.is_key_down_or_held(Key::Any_Shift);

							if (is_typing_command() && !is_shift_held)
							{
								Scoped_Lock lock(python.autocomplete_result_mutex);
								if (python.current_autocomplete_result_index >= 1)
								{
									python.current_autocomplete_result_index -= 1;
									typer_ui.autocomplete_suggestion_move_target = -1;
									typer_ui.autocomplete_suggestion_move_state  = 0;
								}
							}
							else
							{
								if (selection_length && !is_shift_held)
								{
									user_cursor = min(user_cursor, user_cursor + selection_length);
									selection_length = 0;
								}

								s64 cursor_line = typer_ui.find_to_which_renderer_line_position_belongs(user_cursor);

								if (cursor_line > 0)
								{
									Renderer_Line line = *typer_ui.renderer_lines[cursor_line];
									
									if (typer_ui.desired_precursor_pixels == -1)
									{
										typer_ui.save_presursor_pixels_on_line(line);
									}

									Renderer_Line previous_line = *typer_ui.renderer_lines[cursor_line - 1];

									u64 old_cursor = user_cursor;

									typer_ui.set_cursor_on_line_based_on_the_saved_precursor_pixels_on_line(previous_line);

									if (is_shift_held)
									{
										selection_length += (old_cursor - user_cursor);
									}
								}
							}
						}
						break;

						case Key::Down_Arrow:
						{
							typer_ui.recalculate_renderer_lines();

							bool is_shift_held = input.is_key_down_or_held(Key::Any_Shift);

							if (is_typing_command() && !is_shift_held)
							{
								Scoped_Lock lock(python.autocomplete_result_mutex);
								if (python.current_autocomplete_result_index < python.autocomplete_result.count - 1)
								{
									python.current_autocomplete_result_index += 1;
									typer_ui.autocomplete_suggestion_move_target = 1;
									typer_ui.autocomplete_suggestion_move_state  = 0;
								}
							}
							else
							{
								if (selection_length && !is_shift_held)
								{
									user_cursor = max(user_cursor, user_cursor + selection_length);
									selection_length = 0;
								}

								s64 cursor_line = typer_ui.find_to_which_renderer_line_position_belongs(user_cursor);
								if (cursor_line < typer_ui.renderer_lines.count - 1)
								{
									Renderer_Line line = *typer_ui.renderer_lines[cursor_line];
									
									if (typer_ui.desired_precursor_pixels == -1) // This was set earlier if != -1
									{
										typer_ui.save_presursor_pixels_on_line(line);
									}

									Renderer_Line next_line = *typer_ui.renderer_lines[cursor_line + 1];

									u64 old_cursor = user_cursor;

									typer_ui.set_cursor_on_line_based_on_the_saved_precursor_pixels_on_line(next_line);

									if (is_shift_held)
									{
										selection_length += (old_cursor - user_cursor);
									}
								}
							}
						}
						break;


						case Key::Left_Arrow:
						{
							typer_ui.desired_precursor_pixels = -1;

							if (input.is_key_down_or_held(Key::Any_Control))
							{
								if (!input.is_key_down_or_held(Key::Any_Shift))
								{
									user_cursor = min(user_cursor, user_cursor + selection_length);
									selection_length = 0;
								}

								// Skip spaces
								u64 old_user_cursor = user_cursor;

								while (true)
								{
									if (user_cursor <= 0) break;

									char32_t c = terminal.characters.data[user_cursor - 1].c;
									if (!is_whitespace(c))
									{
										break;
									}
									user_cursor -= 1;
								}

								if (user_cursor > 1)
								{
									user_cursor -= 1;
								}

								while (true)
								{
									if (user_cursor <= 0) break;

									char32_t c = terminal.characters.data[user_cursor - 1].c;

									if (should_ctrl_arrow_stop_at_char(c))
									{
										break;
									}

									user_cursor -= 1;
								}

								if (input.is_key_down_or_held(Key::Any_Shift))
								{
									selection_length -= (user_cursor - old_user_cursor);
								}
							}
							else
							{
								if (input.is_key_down_or_held(Key::Any_Shift))
								{
									if (user_cursor > 0)
									{
										selection_length += 1;
										user_cursor -= 1;
									}
								}
								else
								{
									if (selection_length)
									{
										user_cursor = min(user_cursor, user_cursor + selection_length);
										selection_length = 0;
									}
									else
									{
										if (user_cursor > 0)
										{
											user_cursor -= 1;
										}
									}
								}
							}
						}
						break;

						case Key::Right_Arrow:
						{
							typer_ui.desired_precursor_pixels = -1;

							if (input.is_key_down_or_held(Key::Any_Control))
							{

								if (!input.is_key_down_or_held(Key::Any_Shift))
								{
									user_cursor = min(user_cursor, user_cursor + selection_length);
									selection_length = 0;
								}

								// Skip spaces
								u64 old_user_cursor = user_cursor;

								while (true)
								{
									if (user_cursor >= terminal.get_characters_count()) break;

									char32_t c = terminal.characters.data[user_cursor + 1].c;
									if (!is_whitespace(c))
									{
										break;
									}
									user_cursor += 1;
								}

								while (true)
								{
									if (user_cursor >= terminal.get_characters_count()) break;

									char32_t c = terminal.characters.data[user_cursor + 1].c;

									user_cursor += 1;

									if (should_ctrl_arrow_stop_at_char(c))
									{
										break;
									}
								}

								if (input.is_key_down_or_held(Key::Any_Shift))
								{
									selection_length -= (user_cursor - old_user_cursor);
								}
							}
							else
							{
								if (input.is_key_down_or_held(Key::Any_Shift))
								{
									if (user_cursor < terminal.get_characters_count())
									{
										selection_length -= 1;
										user_cursor += 1;
									}
								}
								else
								{
									if (selection_length)
									{
										user_cursor = max(user_cursor, user_cursor + selection_length);
										selection_length = 0;
									}
									else
									{
										if (user_cursor < terminal.get_characters_count())
										{
											user_cursor += 1;
										}
									}
								}
							}
						}
						break;

						case Key::Backspace:
						case Key::Delete:
						{
							if (selection_length)
							{
								u64 selection_start = min(user_cursor, user_cursor + selection_length);
								u64 selection_end   = max(user_cursor, user_cursor + selection_length);

								bool can_delete = true;

								for (u64 i = selection_start; i <= selection_end; i++)
								{
									if (!is_character_at_index_is_user_input(i))
									{
										can_delete = false;				
									}
								}

								if (can_delete)
								{
									typer_ui.invalidate_after(selection_start);

									terminal.remove_non_process(selection_start, abs(selection_length));
									user_cursor = selection_start;
									selection_length = 0;

								}
							}
							else
							{
								if (node.key == Key::Backspace)
								{
									if (is_character_at_index_is_user_input(user_cursor - 1))
									{
										terminal.remove_non_process(user_cursor - 1, 1);
										user_cursor -= 1;
								
										typer_ui.invalidate_after(user_cursor);

									}
								}
								else if (node.key == Key::Delete)
								{
									if (user_cursor < terminal.characters.count && is_character_at_index_is_user_input(user_cursor))
									{
										terminal.remove_non_process(user_cursor, 1);
										typer_ui.invalidate_after(user_cursor);
									}
								}
							}
						}
						break;

						case Key::Enter:
						{
							if (!input.is_key_down_or_held(Key::Any_Shift))
							{
								typer_ui.desired_precursor_pixels = -1;
								selection_length = 0;

								if (is_typing_command() && process_user_input())
								{
									
								}
							}
						}
						break;

						case Key::Tab:
						{
							append_user_character('\t');
						}
						break;
					}
				}
			}
		}
	}
}

void Input_Processor::process_input_for_terminal_inside_process()
{
	check_copy_paste_and_interrupt_bindings();
	


	#define SS3_LITERAL "\x1bO"
	#define ESC_LITERAL "\x1b"
	#define CSI_LITERAL "\x1b["

	for (Input_Node& node: input.nodes)
	{
		switch (node.input_type)
		{
			case Input_Type::Char:
				terminal_io.write_char(node.character);
				break;

			case Input_Type::Key:
			{
				if (node.key_action != Key_Action::Down) break;

				switch (node.key)
				{
					case Key::Up_Arrow:
						terminal_io.write_string(SS3_LITERAL "A");
						break;

					case Key::Down_Arrow:
						terminal_io.write_string(SS3_LITERAL "B");
						break;

					case Key::Left_Arrow:
						terminal_io.write_string(ESC_LITERAL "D");
						break;

					case Key::Right_Arrow:
						terminal_io.write_string(ESC_LITERAL "C");
						break;

					case Key::Escape:
						terminal_io.write_string("\x1b");
						break;

					case Key::Backspace:
						terminal_io.write_string("\x08"); // Backspace
						break;

					case Key::Delete:
						terminal_io.write_string("\x1b[3~");
						break;

					case Key::Enter:
						terminal_io.write_char('\n');
						break;

					case Key::Tab:
						terminal_io.write_char('\t');
						break;

					// Ignore this keys
					case Key::Any_Shift:
					case Key::Any_Control:
					case Key::Any_Alt:

					case Key::LMB:
					case Key::MMB:
					case Key::RMB:
						break;


					case Key::F1:
			        	terminal_io.write_string(CSI_LITERAL "11~");
						// terminal_io.write_string(ESC_LITERAL "0P");
						break;
			        case Key::F2:
			        	terminal_io.write_string(CSI_LITERAL "12~");
			        	// terminal_io.write_string(ESC_LITERAL "0Q");
			        	break;
			        case Key::F3:
			        	terminal_io.write_string(CSI_LITERAL "13~");
			        	// terminal_io.write_string(ESC_LITERAL "0R");
			        	break;
			        case Key::F4:
			        	terminal_io.write_string(CSI_LITERAL "14~");
			        	// terminal_io.write_string(ESC_LITERAL "0S");
			        	break;
			        case Key::F5:
			        	terminal_io.write_string(CSI_LITERAL "15~");
			        	break;
			        case Key::F6:
			        	terminal_io.write_string(CSI_LITERAL "17~");
			        	break;
			        case Key::F7:
			        	terminal_io.write_string(CSI_LITERAL "18~");
			        	break;
			        case Key::F8:
			        	terminal_io.write_string(CSI_LITERAL "19~");
			        	break;
			        case Key::F9:
			        	terminal_io.write_string(CSI_LITERAL "20~");
			        	break;
			        case Key::F10:
			        	terminal_io.write_string(CSI_LITERAL "21~");
			        	break;
			        case Key::F11:
			        	terminal_io.write_string(CSI_LITERAL "23~");
			        	break;
			        case Key::F12:
			        	terminal_io.write_string(CSI_LITERAL "24~");
			        	break;


					// @TODO:
					// What if this gets triggered at the same time as some Key_Binding.
					default:

						// @TODO: this is bullshit, cause Key enum doesn't guarantee that enum value
						// matches key's keycode.

						if (!isprint((int) node.key))
						{
							String str = format_string(frame_allocator, "\x1b[%~", (u32) node.key_code);
							terminal_io.write_string(str);
						}
						break;

				}

			}
			break;
		}
	}
}


void Input_Processor::process_input()
{
	if (typer_ui.terminal_focused)
	{
		if (python.is_running)
		{
			process_input_for_terminal_inside_process();
		}
		else
		{
			process_input_for_terminal_outside_process();
		}
	}
}