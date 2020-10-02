#include "UI.h"

#include "Main.h"
#include "Input.h"


Font* UI::find_font(Unicode_String name)
{
	return font_storage.find_font(name);
}

Font::Face* UI::get_font_face()
{
	Font* font = find_font(parameters.text_font_name);
	
	if (!font)
		font = find_font(U"NotoMono");

	assert(font);

	Font::Face* face = font->get_face(renderer.scaled(parameters.text_font_face_size));
	return face;
}


void UI::init()
{
	ZoneScoped;

	ui_id_data_array = Array_Map<UI_ID, void*>(32, c_allocator);
	active_mask_stack = make_array<Active_Mask>(32, c_allocator);
	active_mask_stack_states = make_array<Active_Mask_Stack_State>(32, c_allocator);

	create_arena_allocator(&first_arena_allocator,  c_allocator, 4096);
	create_arena_allocator(&second_arena_allocator, c_allocator, 4096);

	current_arena_allocator = first_arena_allocator;
}

void UI::pre_frame()
{
	ZoneScoped;

	current_layer = 0;

	current_hovering_layer               = 0;
	current_hovering_scroll_region_layer = 0;

	current_hovering               = invalid_ui_id;
	current_hovering_scroll_region = invalid_ui_id;
}

void UI::post_frame()
{
	ZoneScoped;

	assert(active_mask_stack_states.count == 0);

	active_mask_stack.clear();

	hover_scroll_region = current_hovering_scroll_region;
	hover_scroll_region_layer = current_hovering_scroll_region_layer;

	hover = current_hovering;
	hover_layer = current_hovering_layer;

	down = invalid_ui_id;
	up   = invalid_ui_id;

	if (input.is_key_down(Key::LMB))
	{
		if (current_hovering != invalid_ui_id)
		{
			down    = current_hovering;
			holding = current_hovering;

            capture_mouse();
		}
		else
		{
			down    = null_ui_id;
			holding = null_ui_id;
		}
	}
	
	if (input.is_key_up(Key::LMB))
	{
		up = holding;
		holding = invalid_ui_id;
		
        release_mouse();
	}



	if (current_arena_allocator == first_arena_allocator)
		current_arena_allocator = second_arena_allocator;
	else
		current_arena_allocator = first_arena_allocator;


	current_arena_allocator.reset();

	if (holding.next) // ui.holding can stay across frames, so we have to keep copying it to not corrupt it.
	{
		holding = copy_ui_id(holding, current_arena_allocator);
	}
}



UI_ID UI::copy_ui_id(UI_ID ui_id, Allocator allocator)
{
	UI_ID* current = &ui_id;
	while (current->next)
	{
		current->next = allocate_and_copy(current->next, allocator);
		current = current->next;		
	}

	return ui_id;
}

void UI::im_hovering(UI_ID ui_id)
{
	if (current_layer >= current_hovering_layer)
	{
		current_hovering       = copy_ui_id(ui_id, current_arena_allocator);
		current_hovering_layer = current_layer;
	}
}

void UI::this_scroll_region_is_hovering(UI_ID ui_id)
{
	if (current_layer >= current_hovering_scroll_region_layer)
	{
		current_hovering_scroll_region       = copy_ui_id(ui_id, current_arena_allocator);
		current_hovering_scroll_region_layer = current_layer;
	}
}






bool UI::is_point_inside_active_zone(int x, int y)
{
	for (Active_Mask mask: active_mask_stack)
	{
		if (mask.inversed)
		{
			if (mask.rect.is_point_inside(x, y)) return false;
		}
		else
		{
			if (!mask.rect.is_point_inside(x, y)) return false;
		}
	}

	return true;
}

void UI::set_active_masks_as_renderer_masks()
{
	ZoneScoped;

	if (active_mask_stack.count > 0)
	{
		// You're not allowed to combine ui mask stack and renderer mask stack

		renderer.imm_mask_stack.clear();

		for (Active_Mask mask : active_mask_stack)
		{
			renderer.imm_mask_stack.add({
				.rect = mask.rect,
				.inversed = mask.inversed,
			});
		}
	}
	else
	{
		renderer.imm_mask_stack.clear();
	}

	renderer.imm_recalculate_mask_buffer();
}

void UI::save_active_mask_stack_state()
{
	ZoneScoped;

	Active_Mask_Stack_State state;
	state.saved_mask_stack = active_mask_stack.copy_with(frame_allocator);

	active_mask_stack_states.add(state);
}

void UI::restore_active_mask_stack_state()
{
	ZoneScoped;

	assert(active_mask_stack_states.count);

	Active_Mask_Stack_State saved_state = active_mask_stack_states.pop_last();

	active_mask_stack.clear();
	saved_state.saved_mask_stack.copy_to(&active_mask_stack);
}




void UI::draw_text(int x, int y, Unicode_String text, Rect* cull_rect)
{
	ZoneScoped;


	Font::Face* face = get_font_face();

	int text_width = measure_text_width(text, face);

	int draw_x;

	switch (parameters.text_alignment)
	{
		case Text_Alignment::Left:
		{
			draw_x = x;
		}
		break;

		case Text_Alignment::Center:
		{
			draw_x = x - text_width / 2;
		}
		break;

		case Text_Alignment::Right:
		{
			draw_x = x - text_width;
		}
		break;

		default: assert(false);
	}



	int text_y = y;

	if (parameters.center_text_vertically)
	{
		text_y -= face->baseline_offset;
	}


	if (cull_rect)
	{
		renderer.imm_draw_text_culled(face, text, draw_x, text_y, *cull_rect, parameters.text_color);
	}
	else
	{
		renderer.imm_draw_text(face, text, draw_x, text_y, parameters.text_color);
	}
}


bool UI::button(Rect rect, rgba color, UI_ID ui_id)
{
	ZoneScoped;

	UI_Button_State* state;
	get_or_create_ui_item_data(ui_id, &state);


	bool result = false;


	if (down == ui_id)
	{
		state->darkness = 1.0;
		result = true;
	}

	int old_darkness = state->darkness;

	if ((hover != ui_id && state->darkness) || (state->darkness > 0.3001))
	{
		state->darkness = clamp<double>(0, 1, state->darkness - frame_time * 5.0);
	}

	if (hover == ui_id)
	{
		desired_cursor_type = Cursor_Type::Link;

		if (state->darkness < 0.3)
		{
			state->darkness = 0.3;
		}
	}


	if (is_point_inside_active_zone(input.mouse_x, input.mouse_y) && rect.is_point_inside(input.mouse_x, input.mouse_y) && current_layer >= current_hovering_layer)
	{
		im_hovering(ui_id);
	}


	rgba darken_color = color;
	{
		darken_color.rgb_value = darken_color.rgb_value * lerp(1.0f, 0.3f, state->darkness);
	}

	renderer.imm_draw_rect(rect, darken_color);

	return result;
}

bool UI::button(Rect rect, Unicode_String text, rgba color, UI_ID ui_id)
{
	ZoneScoped;

	bool result = button(rect, color, ui_id);

	// Let the caller set the text alignment
	// scoped_set_and_revert(parameters.text_alignment, Text_Alignment::Center);
	scoped_set_and_revert(parameters.center_text_vertically, true);

	int draw_text_x;

	switch (parameters.text_alignment)
	{
		case Text_Alignment::Left:
			draw_text_x = rect.x_left + renderer.scaled(parameters.button_text_margin);
			break;

		case Text_Alignment::Center:
			draw_text_x = rect.center_x();
		break;

		case Text_Alignment::Right:
			draw_text_x = rect.x_right - renderer.scaled(parameters.button_text_margin);
			break;
	}

	draw_text(draw_text_x, rect.center_y(), text);


	if (parameters.enable_button_text_fading)
	{
		// @Hack: idk if borrowing state is good
		UI_Button_State* state = (UI_Button_State*) get_ui_item_data(ui_id);

		int alpha_fade_width = renderer.scaled(8);

		rgba darkened_color = color;
		// @CopyPaste lerp is from ui.button() 
		darkened_color.rgb_value = darkened_color.rgb_value * lerp(1.0f, 0.3f, state->darkness);

		renderer.imm_draw_rect_with_alpha_fade(Rect::make(rect.x_left, rect.y_bottom, rect.x_left + alpha_fade_width, rect.y_top), darkened_color, 255, 0);
		renderer.imm_draw_rect_with_alpha_fade(Rect::make(rect.x_right - alpha_fade_width, rect.y_bottom, rect.x_right, rect.y_top), darkened_color, 0, 255);
	}

	return result;
}

bool UI::checkbox(Rect rect, bool value, UI_ID ui_id)
{
	ZoneScoped;

	UI_Checkbox_State* state;
	get_or_create_ui_item_data(ui_id, &state);

	bool result = false;

	if (down == ui_id)
	{
		result = true;
	}

	//state->darkness = clamp<double>(0, 1, state->darkness - frame_time * 5.0);


	if (is_point_inside_active_zone(input.mouse_x, input.mouse_y) && rect.is_point_inside(input.mouse_x, input.mouse_y))
	{
		im_hovering(ui_id);
	}


	if (value)
	{
		renderer.imm_draw_rect(rect, parameters.checkbox_background_color);
	}
	else
	{
		renderer.imm_draw_rect(rect, parameters.checkbox_frame_color); // @Performance: borders just should be calculated properly non filling the whole bunch of useless pixels

		Rect inner_rect = rect;
		int border_size = renderer.scaled(1);
		inner_rect.x_left   += border_size;
		inner_rect.y_bottom += border_size;
		inner_rect.x_right  -= border_size;
		inner_rect.y_top    -= border_size;

		renderer.imm_draw_rect(inner_rect, parameters.checkbox_background_color);
	}


	if (value)
	{
		auto face = get_font_face();
		Glyph tick_glyph = face->request_glyph(U'\x2713');

		renderer.imm_draw_glyph(&tick_glyph, rect.center_x() - tick_glyph.width / 2, rect.center_y() - tick_glyph.height / 2, parameters.checkbox_tick_color);
	}

	return result;
}



bool UI::text_editor(Rect rect, Unicode_String text, Unicode_String* out_result, Allocator string_allocator, UI_ID ui_id, bool multiline, bool report_when_modified, bool finish_on_enter, UI_Text_Editor_Finish_Cause* finish_cause, Unicode_String* hint_text)
{
	ZoneScoped;


	UI_Multiline_Text_Editor_State* state;
	get_or_create_ui_item_data(ui_id, &state);


	bool result = false;

	bool editing_finished_during_this_frame = false;

		
	auto finish_editing = [&](UI_Text_Editor_Finish_Cause cause)
	{
		editing_finished_during_this_frame = true;

		state->editing = false;

		state->editor.cursor = 0;
		state->editor.selection_length = 0;

		state->mouse_offscreen_scroll_target_y = 0.0;
		state->mouse_offscreen_scroll_target_x = 0.0;

		state->desired_precursor_pixels = -1;
		state->scroll_region_ui_id = null_ui_id;

		*out_result = state->builder.get_string().copy_with(string_allocator);

		state->builder.free();

		result = true;

		if (finish_cause)
		{
			*finish_cause = cause;
		}
	};

	if (down != ui_id && down != invalid_ui_id && state->scroll_region_ui_id != down) // Clicked on smth else
	{
		if (state->editing)
		{
			finish_editing(UI_TEXT_EDITOR_CLICKED_ON_SMTH_ELSE);
		}
	}



	bool modified = false;
	bool should_scroll_to_cursor = false;

	bool modified_because_of_enter = false;


	Font::Face* face = get_font_face();


	if (state->editing)
	{
		for (Input_Node node : input.nodes)
		{
			if (node.input_type == Input_Type::Key)
			{
				if (node.key_action == Key_Action::Down)
				{
					switch (node.key)
					{
						case Key::Backspace:
						{
							should_scroll_to_cursor = true;
							state->desired_precursor_pixels = -1;

							state->editor.delete_before_cursor();
							modified = true;
						}
						break;
						case Key::Delete:
						{
							should_scroll_to_cursor = true;
							state->desired_precursor_pixels = -1;

							state->editor.delete_after_cursor();
							modified = true;
						}
						break;

						case Key::Left_Arrow:
						{
							should_scroll_to_cursor = true;
							state->desired_precursor_pixels = -1;

							if (input.is_key_down_or_held(Key::Any_Control))
							{
								state->editor.next_word(-1, input.is_key_down_or_held(Key::Any_Shift));
							}
							else
							{
								if (input.is_key_down_or_held(Key::Any_Shift))
								{
									state->editor.advance_selection(-1);
								}
								else
								{
									state->editor.move_cursor(-1);
								}
							}
						}
						break;
						case Key::Right_Arrow:
						{
							should_scroll_to_cursor = true;
							state->desired_precursor_pixels = -1;

							if (input.is_key_down_or_held(Key::Any_Control))
							{
								state->editor.next_word(1, input.is_key_down_or_held(Key::Any_Shift));
							}
							else
							{
								if (input.is_key_down_or_held(Key::Any_Shift))
								{
									state->editor.advance_selection(1);
								}
								else
								{
									state->editor.move_cursor(1);
								}
							}
						}
						break;

						// UpArrow and DownArrow handling is done after we calculated lines,
						//   it makes code easier, but breaks input order.

						case Key::Enter:
						{
							if (multiline)
							{
								should_scroll_to_cursor = true;
								state->desired_precursor_pixels = -1;

								state->editor.append_at_cursor(U'\n');

								modified = true;
								modified_because_of_enter = true;
							}
							else
							{
								if (finish_on_enter)
								{
									finish_editing(UI_TEXT_EDITOR_PRESSED_ENTER);
									goto end_input_processing;
								}

								modified = true;
								modified_because_of_enter = true;
							}
						}
						break;
						case Key::Tab:
						{
							should_scroll_to_cursor = true;
							state->desired_precursor_pixels = -1;

							state->editor.append_at_cursor(U'\t');	
							modified = true;
						}
						break;
					}
				}
			}
			else if (node.input_type == Input_Type::Char)
			{
				should_scroll_to_cursor = true;
				state->editor.append_at_cursor(node.character);
				modified = true;
			}
		}

		if (input.is_key_combo_pressed(Key::Any_Control, Key::C))
		{
			Unicode_String selection = state->editor.get_selected_string();
			copy_to_os_clipboard(selection, frame_allocator);
		}
		else if (input.is_key_combo_pressed(Key::Any_Control, Key::X))
		{
			should_scroll_to_cursor = true;
			Unicode_String selection = state->editor.get_selected_string();
			copy_to_os_clipboard(selection, frame_allocator);
			state->editor.maybe_delete_selection();
			modified = true;
		}
		else if (input.is_key_combo_pressed(Key::Any_Control, Key::V))
		{
			should_scroll_to_cursor = true;
			Unicode_String clipboard = get_os_clipboard<char32_t>(frame_allocator);
			if (clipboard.length)
			{
				state->editor.maybe_delete_selection();

				Unicode_String append_string;

				if (multiline)
				{
					append_string = clipboard;
				}
				else
				{
					// Remove new line symbols.

					String_Builder<char32_t> builder = build_string<char32_t>(frame_allocator);

					for (char32_t c: clipboard)
					{
						if (c == U'\n') continue;
						builder.append(c);
					}

					append_string = builder.get_string();
				}

				state->builder.append(state->editor.cursor, append_string);
				state->editor.cursor += append_string.length;
				modified = true;
			}
		}
		else if (input.is_key_combo_pressed(Key::Any_Control, Key::A))
		{
			should_scroll_to_cursor = true;
			state->editor.cursor = 0;
			state->editor.selection_length = state->builder.length;
		}
	}


	end_input_processing:

	bool using_hint_text = false;
	Unicode_String lines_source;
	if (state->editing)
	{
		lines_source = state->editor.get_string();

		state->editor.cursor = clamp(0, lines_source.length, state->editor.cursor);
	}
	else
	{
		if (hint_text && text.length <= 0)
		{
			using_hint_text = true;
			lines_source = *hint_text;
		}
		else
		{
			lines_source = text;
		}
	}
	if (editing_finished_during_this_frame)
	{
		// out_result  was just set above.
		lines_source = *out_result;
	}


	struct Line
	{
		int start;
		int length;
		int length_without_new_line;
	};

	auto lines = make_array<Line>(32, frame_allocator);
	{
		if (multiline)
		{		
			int last_end = 0;
			for (int i = 0; i < lines_source.length; i++)
			{
				char32_t c = lines_source[i];

				if (c == U'\n')
				{
					Line new_line = {
						.start = last_end,
						.length = i - last_end + 1,
						.length_without_new_line = i - last_end,
					};

					last_end = i + 1;

					lines.add(new_line);
				}
			}

			if (!lines.count || (lines_source.length - last_end))
			{
				lines.add({
					.start = last_end,
					.length = lines_source.length - last_end,
					.length_without_new_line = lines_source.length - last_end,
				});
			}
			else if (lines.count)
			{
				if (lines.last()->length_without_new_line != lines.last()->length)
				{
					lines.add({
						.start  = lines_source.length,
						.length = 0,
						.length_without_new_line = 0
					});
				}
			}
		}
		else
		{
			lines.add({
				.start = 0,
				.length = lines_source.length,
				.length_without_new_line = lines_source.length
			});
		}


		assert(lines.count);
	}

	int max_line_width = 0;
	for (Line& line: lines)
	{
		Unicode_String line_str = lines_source.sliced(line.start, line.length);

		int line_width = measure_text_width(line_str, face);
		if (line_width > max_line_width)
		{
			max_line_width = line_width + renderer.scaled(24);
		}
	}

	auto pick_line_for_y_coord = [&](int y) -> Line*
	{
		assert(lines.count);


		assert(state->scroll_region_ui_id != null_ui_id);
		int y_top = rect.y_top + state->previous_scroll_region_result.scroll_from_top;

		if (y > y_top) return lines[0];

		for (Line& line : lines)
		{
			int y_bottom = y_top - face->line_spacing;

			if (y <= y_top && y > y_bottom)
			{
				return &line;
			}

			y_top = y_bottom;
		}

		return lines.last();
	};

	auto get_line_string = [&](Line* line) -> Unicode_String
	{
		return lines_source.sliced(line->start, line->length);
	};

	auto get_line_string_without_new_line = [&](Line* line) -> Unicode_String
	{
		return lines_source.sliced(line->start, line->length_without_new_line);
	};

	auto get_character_line = [&](int char_index) -> Line*
	{
		for (Line& line : lines)
		{
			if (char_index >= line.start && char_index < (line.start + line.length))
			{
				return &line;
			}
		}

		if (char_index == lines_source.length)
		{
			return lines.last();
		}

		assert(false);
	};


	int character_y_offset = face->baseline_offset;


	auto get_cursor_coordinates = [&](int* out_cursor_x_left, int* out_cursor_y_top, int* out_cursor_left_text_width)
	{
		Line* cursor_line = get_character_line(state->editor.cursor);

		int cursor_left_text_width = measure_text_width(get_line_string(cursor_line).sliced(0, state->editor.cursor - cursor_line->start), face);

		int cursor_x_left = cursor_left_text_width - state->previous_scroll_region_result.scroll_from_left + rect.x_left + renderer.scaled(parameters.text_field_margin);
		
		int cursor_y_top;

		if (multiline)
		{
			cursor_y_top = rect.y_top + state->previous_scroll_region_result.scroll_from_top - ((lines.fast_pointer_index(cursor_line)) * face->line_spacing) - face->baseline_offset;
		}
		else
		{
			cursor_y_top = rect.y_top - (rect.height() / 2) + face->line_spacing - face->baseline_offset - character_y_offset;
		}


		if (out_cursor_x_left)
			*out_cursor_x_left = cursor_x_left;

		if (out_cursor_y_top)
			*out_cursor_y_top = cursor_y_top;

		if (out_cursor_left_text_width)
			*out_cursor_left_text_width = cursor_left_text_width;
	};




	UI_ID scroll_region_ui_id = ui_id;
	scroll_region_ui_id.id += 1337;

	state->scroll_region_ui_id = scroll_region_ui_id;


	int scroll_region_height;
	if (multiline)
	{
		scroll_region_height = lines.count * face->line_spacing + face->baseline_offset;
	}
	else
	{
		scroll_region_height = face->size;
	}


	Scroll_Region_Result scroll_region_result;
	{
		scoped_set_and_revert(parameters.scrollbar_width, multiline ? parameters.scrollbar_width : 8)
		scoped_set_and_revert(parameters.scroll_region_background, parameters.text_field_background);

		scroll_region_result = ui.scroll_region(rect, scroll_region_height, max_line_width, true, scroll_region_ui_id);
	}
	state->previous_scroll_region_result = scroll_region_result;





	if (is_point_inside_active_zone(input.mouse_x, input.mouse_y) && rect.is_point_inside(input.mouse_x, input.mouse_y))
	{
		im_hovering(ui_id);
	}



	if (state->editing)
	{
		// For UpArrow and DownArrow we have to what lines are.
		{
			auto save_precursor_pixels_if_its_not_set = [&](Line* line)
			{
				if (state->desired_precursor_pixels == -1)
				{
					state->desired_precursor_pixels = measure_text_width(get_line_string_without_new_line(line).sliced(0, state->editor.cursor - line->start), face);
				}
			};

			auto set_cursor_based_on_precursor_pixels = [&](Line* line)
			{
				int old_cursor = state->editor.cursor;

				state->editor.cursor = line->start + pick_appropriate_cursor_position(get_line_string_without_new_line(line), face, state->desired_precursor_pixels);

				if (input.is_key_down_or_held(Key::Any_Shift))
				{
					state->editor.selection_length -= (state->editor.cursor - old_cursor);
				}
				else
				{
					state->editor.selection_length = 0;
				}
			};


			for (Input_Node node : input.nodes)
			{
				if (node.input_type == Input_Type::Key)
				{
					if (node.key_action == Key_Action::Down)
					{
						switch (node.key)
						{
							case Key::Up_Arrow:
							{
								should_scroll_to_cursor = true;

								if (!input.is_key_down_or_held(Key::Any_Shift) && state->editor.selection_length)
								{
									state->editor.move_cursor(-1);
								}

								Line* cursor_line = get_character_line(state->editor.cursor);
								save_precursor_pixels_if_its_not_set(cursor_line);
								
								if (lines.fast_pointer_index(cursor_line) > 0)
								{
									Line* target_line = cursor_line - 1;

									set_cursor_based_on_precursor_pixels(target_line);								
								}
							}
							break;

							case Key::Down_Arrow:
							{
								should_scroll_to_cursor = true;


								if (!input.is_key_down_or_held(Key::Any_Shift) && state->editor.selection_length)
								{
									state->editor.move_cursor(1);
								}

								Line* cursor_line = get_character_line(state->editor.cursor);
								save_precursor_pixels_if_its_not_set(cursor_line);
								
								if (lines.fast_pointer_index(cursor_line) < (lines.count - 1))
								{
									Line* target_line = cursor_line + 1;

									set_cursor_based_on_precursor_pixels(target_line);								
								}
							}
							break;
						}
					}
				}
			}
		}
	}


	// This is the same code that is responsible for drawing cursor,
	//   but in this case it doesn't draw, but scrolls to the cursor
	if (state->editing && state->scroll_region_ui_id != null_ui_id)
	{		
		int cursor_left_text_width;
		int cursor_x_left;		
		int cursor_y_top;

		get_cursor_coordinates(&cursor_x_left, &cursor_y_top, &cursor_left_text_width);


		Line* cursor_line = get_character_line(state->editor.cursor);


		if (should_scroll_to_cursor)
		{
			if (cursor_y_top > state->previous_scroll_region_result.view_rect.y_top)
			{
				state->previous_scroll_region_result.state->scroll_from_top = (lines.fast_pointer_index(cursor_line) * face->line_spacing) - face->baseline_offset / 2;
			}
			else if ((cursor_y_top - face->line_spacing) < state->previous_scroll_region_result.view_rect.y_bottom)
			{
				state->previous_scroll_region_result.state->scroll_from_top = (lines.fast_pointer_index(cursor_line) * face->line_spacing) + (face->baseline_offset / 2) - state->previous_scroll_region_result.view_rect.height() + face->line_spacing;
			}


			if (cursor_x_left < state->previous_scroll_region_result.view_rect.x_left)
			{
				state->previous_scroll_region_result.state->scroll_from_left = cursor_left_text_width;
			}
			else if ((cursor_x_left + renderer.scaled(parameters.cursor_width) * 10) > state->previous_scroll_region_result.view_rect.x_right)
			{
				state->previous_scroll_region_result.state->scroll_from_left = cursor_left_text_width - state->previous_scroll_region_result.view_rect.width() + renderer.scaled(parameters.cursor_width) * 10;
			}
		}
	}






	if (down == ui_id)
	{
		if (!state->editing)
		{
			state->builder = build_string<char32_t>(c_allocator); // @MemoryLeak
			state->editor  = create_text_editor(&state->builder);

			state->builder.append(text);

			Line* line = pick_line_for_y_coord(input.old_mouse_y);

			int chars_before_mouse = line->start + pick_appropriate_cursor_position(get_line_string_without_new_line(line), face, input.old_mouse_x - rect.x_left + scroll_region_result.scroll_from_left - renderer.scaled(parameters.text_field_margin));

			state->editor.cursor = chars_before_mouse;

			state->editing = true;
		}
		else
		{
			Line* line = pick_line_for_y_coord(input.old_mouse_y);

			int chars_before_mouse = line->start + pick_appropriate_cursor_position(get_line_string_without_new_line(line), face, input.old_mouse_x - rect.x_left + scroll_region_result.scroll_from_left - renderer.scaled(parameters.text_field_margin));

			if (input.is_key_down_or_held(Key::Any_Shift))
			{
				state->editor.selection_length -= chars_before_mouse - state->editor.cursor;
			}
			else
			{
				state->editor.selection_length = 0;
			}


			state->editor.cursor = chars_before_mouse;
		}
	}
	else if (holding == ui_id)
	{
		int mouse_clipped = clamp(rect.x_left + 1, rect.x_right - 1, input.mouse_x); // This +1 -1 is some hack to make scroll speed sane when mouse is outside of rect. I don't know how it works.

		Line* line = pick_line_for_y_coord(input.mouse_y);

		int chars_before_mouse = line->start + pick_appropriate_cursor_position(get_line_string_without_new_line(line), face, mouse_clipped - rect.x_left + scroll_region_result.scroll_from_left - renderer.scaled(parameters.text_field_margin));

		int old_cursor = state->editor.cursor;

		state->editor.cursor            = chars_before_mouse;
		state->editor.selection_length -= chars_before_mouse - old_cursor;



		{		
			float speed = 10.0;
			float delta = 0.0;

			float font_face_float = (float) face->line_spacing;

			if (input.mouse_y < scroll_region_result.view_rect.y_bottom)
			{
				speed *= float(scroll_region_result.view_rect.y_bottom - input.mouse_y) * 0.1;

				delta = font_face_float * speed * frame_time;
			}
			else if (input.mouse_y > scroll_region_result.view_rect.y_top)
			{
				speed *= float(input.mouse_y - scroll_region_result.view_rect.y_top) * 0.1;

				delta = font_face_float * speed * frame_time;
				delta *= -1.0;
			}


			state->mouse_offscreen_scroll_target_y += delta;


			if (abs(state->mouse_offscreen_scroll_target_y) > 1.0)
			{
				float move_delta = round(state->mouse_offscreen_scroll_target_y);

				state->mouse_offscreen_scroll_target_y -= move_delta;
				scroll_region_result.state->scroll_from_top += (int) move_delta;
			}
		}

		{		
			float speed = 10.0;
			float delta = 0.0;

			float font_face_float = (float) face->line_spacing;

			if (input.mouse_x < scroll_region_result.view_rect.x_left)
			{
				speed *= float(scroll_region_result.view_rect.x_left - input.mouse_x) * 0.1;

				delta = font_face_float * speed * frame_time;
			}
			else if (input.mouse_x > scroll_region_result.view_rect.x_right)
			{
				speed *= float(input.mouse_x - scroll_region_result.view_rect.x_right) * 0.1;

				delta = font_face_float * speed * frame_time;
				delta *= -1.0;
			}


			state->mouse_offscreen_scroll_target_x += delta;


			if (abs(state->mouse_offscreen_scroll_target_x) > 1.0)
			{
				float move_delta = round(state->mouse_offscreen_scroll_target_x);

				state->mouse_offscreen_scroll_target_x -= move_delta;
				scroll_region_result.state->scroll_from_left -= (int) move_delta;
			}
		}
	}




	{
		int selection_start = min(state->editor.cursor, state->editor.cursor + state->editor.selection_length);
		int selection_end   = max(state->editor.cursor, state->editor.cursor + state->editor.selection_length);

		int y;
		if (multiline)
		{
			y = rect.y_top + scroll_region_result.scroll_from_top;
		}
		else
		{
			y = rect.y_top - (rect.height() / 2) - face->baseline_offset + face->line_spacing;
		}


		rgba text_color = ui.parameters.text_color;
		if (using_hint_text)
		{
			text_color = text_color * 0.5;
		}

		for (Line& line: lines)
		{
			y -= face->line_spacing;

			String_Glyph_Iterator<char32_t> glyph_iterator = string_by_glyphs<char32_t>(get_line_string(&line), face);
			int glyph_iterator_previous_x = glyph_iterator.x;

			int x = rect.x_left - scroll_region_result.scroll_from_left + renderer.scaled(parameters.text_field_margin);


			int i = line.start;
			while (glyph_iterator.next())
			{
				defer{ i += 1; };
				int x_delta = glyph_iterator.x - glyph_iterator_previous_x;
				defer{ glyph_iterator_previous_x = glyph_iterator.x; };
				defer{ x += x_delta; };

				Rect char_background_rect = Rect::make(x, y - character_y_offset, x + x_delta, y - character_y_offset + face->line_spacing);

				if (i >= selection_start && i < selection_end)
				{
					renderer.imm_draw_rect(char_background_rect, ui.parameters.text_selection_background);
				}

				if (glyph_iterator.render_glyph)
				{
					Glyph glyph = glyph_iterator.current_glyph;
					// :GlyphLocalCoords:
					renderer.imm_draw_glyph(&glyph, x + glyph.left_offset, y - (glyph.height - glyph.top_offset), text_color);
				}
			}
		}


		if (state->editing)
		{		
			int cursor_left_text_width;
			int cursor_x_left;		
			int cursor_y_top;
			
			get_cursor_coordinates(&cursor_x_left, &cursor_y_top, &cursor_left_text_width);
			
			renderer.imm_draw_rect(Rect::make(cursor_x_left, cursor_y_top - face->line_spacing, cursor_x_left + renderer.scaled(parameters.cursor_width), cursor_y_top), parameters.cursor_color);
		}
	}

	ui.end_scroll_region();


	if (!result && report_when_modified && modified && state->editing)
	{
		*out_result = state->builder.get_string().copy_with(string_allocator);
		result = true;
		if (finish_cause)
		{
			if (modified_because_of_enter)
			{
				*finish_cause = UI_TEXT_EDITOR_PRESSED_ENTER;
			}
			else
			{
				*finish_cause = UI_TEXT_EDITOR_MODIFIED_TEXT;
			}
		}
	}


	return result;
}


bool UI::file_picker(Rect rect, Unicode_String starting_folder, Unicode_String* out_result, Allocator result_allocator, UI_ID ui_id)
{
	UI_File_Picker_State* state;
	get_or_create_ui_item_data(ui_id, &state);


	if (state->is_opened)
	{
		
	}
	else
	{

	}


	return false;
}




bool UI::dropdown(Rect rect, int selected, Dynamic_Array<Unicode_String> options, int* out_selected, UI_ID ui_id)
{
	ZoneScoped;


	assert(selected >= 0 && selected < options.count);

	UI_Dropdown_State* state;
	get_or_create_ui_item_data(ui_id, &state);


	// @TODO: figure out what to do with empty options array

	if (is_point_inside_active_zone(input.mouse_x, input.mouse_y) && rect.is_point_inside(input.mouse_x, input.mouse_y))
	{
		im_hovering(ui_id);
	}



	renderer.imm_draw_rect(rect, parameters.dropdown_background_color);

	scoped_set_and_revert(parameters.text_alignment, Text_Alignment::Left);
	scoped_set_and_revert(parameters.center_text_vertically, true);



	renderer.imm_push_mask({
		.rect = rect,
		.inversed = false
	});

	draw_text(rect.x_left + parameters.dropdown_text_margin_left, rect.center_y(), *options[selected]);

	renderer.imm_pop_mask();



	bool result = false;


	if (!state->is_selected)
	{
		if (down == ui_id)
		{
			state->is_selected = true;
		}

		int fadeout_x_left = rect.x_right - parameters.dropdown_arrow_margin_right * 4 - parameters.dropdown_arrow_size;
		int fadeout_x_right = rect.x_right - parameters.dropdown_arrow_margin_right - parameters.dropdown_arrow_size;

		renderer.imm_draw_rect_with_alpha_fade(Rect::make(fadeout_x_left,
			rect.y_bottom, fadeout_x_right, rect.y_top), parameters.dropdown_background_color, 0, 255);

		renderer.imm_draw_rect(Rect::make(fadeout_x_right,
			rect.y_bottom, rect.x_right, rect.y_top), parameters.dropdown_background_color);

		// Draw arrow
		renderer.imm_draw_rect(Rect::make(
			rect.x_right - parameters.dropdown_arrow_margin_right - parameters.dropdown_arrow_size,
			rect.center_y() - parameters.dropdown_arrow_size / 2,
			rect.x_right - parameters.dropdown_arrow_margin_right,
			rect.center_y() + parameters.dropdown_arrow_size / 2),

			parameters.dropdown_arrow_color);

		
	}
	else
	{
		if (down == ui_id)
		{
			if (rect.is_point_inside(input.old_mouse_x, input.old_mouse_y))
			{
				state->is_selected = false;
			}
		}

		int all_items_height = renderer.scaled(parameters.dropdown_item_height * options.count);


		int distance_to_bottom = rect.y_bottom;
		int distance_to_top    = renderer.height - rect.y_top;

		bool going_to_the_sky = false;

		if (all_items_height > distance_to_bottom && distance_to_top > distance_to_bottom)
		{
			going_to_the_sky = true;			
		}

		Rect items_rect;
		if (!going_to_the_sky)
		{
			items_rect = Rect::make(
				rect.x_left,
				rect.y_bottom - min(distance_to_bottom, all_items_height),
				rect.x_right,
				rect.y_bottom);
		}
		else
		{
			items_rect = Rect::make(
				rect.x_left,
				rect.y_top,
				rect.x_right,
				rect.y_top + min(distance_to_top, all_items_height));
		}


		UI_ID scroll_region_id = ui_id; 
		scroll_region_id.id += 1;

		scoped_set_and_revert(parameters.scroll_region_background, parameters.dropdown_items_list_background);


		Scroll_Region_Result scroll_region_result = scroll_region(items_rect, all_items_height, rect.width(), false, scroll_region_id);


		// @TODO: scroll to the bottom initially if we are going to the top of the screen.

		if (down != invalid_ui_id)
		{
			if (down != ui_id && down != scroll_region_id)
			{
				state->is_selected = false;
			}
		}

		{


			for (int i = 0; i < options.count; i++)
			{
				Unicode_String option = *options[i];
				
				Rect option_rect;
				if (!going_to_the_sky)
				{
					int option_y_top = rect.y_bottom - renderer.scaled(parameters.dropdown_item_height) * i + scroll_region_result.scroll_from_top;

					option_rect = Rect::make(rect.x_left, option_y_top - renderer.scaled(parameters.dropdown_item_height), rect.x_right, option_y_top);
				}
				else
				{
					int option_y_bottom = rect.y_top + renderer.scaled(parameters.dropdown_item_height) * i + scroll_region_result.scroll_from_top - max(0, all_items_height - distance_to_top);
					// - max(0, all_items_height - distance_to_top) is needed because we start to draw items 
					//   from the bottom to top.

					option_rect = Rect::make(rect.x_left, option_y_bottom, rect.x_right, option_y_bottom + renderer.scaled(parameters.dropdown_item_height));
				}

				option_rect.x_left  -= scroll_region_result.scroll_from_left;
				option_rect.x_right -= scroll_region_result.scroll_from_left;


				bool hovering = false;
				if (hover == ui_id)
				{
					if (!rect.is_point_inside(input.old_mouse_x, input.old_mouse_y) && option_rect.is_point_inside(input.old_mouse_x, input.old_mouse_y))
					{
						hovering = true;
						renderer.imm_draw_rect(option_rect, i == selected ? parameters.dropdown_item_hover_and_selected_background : parameters.dropdown_item_hover_background);
					}
				}

				if (!hovering && i == selected)
				{
					renderer.imm_draw_rect(option_rect, parameters.dropdown_item_selected_background);
				}

				if (down == ui_id)
				{
					if (hovering)
					{
						state->is_selected = false;
						*out_selected = i;
						result = true;
					}
				}

				draw_text(option_rect.x_left + renderer.scaled(parameters.dropdown_item_text_left_margin), option_rect.center_y(), option);

				if (is_point_inside_active_zone(input.mouse_x, input.mouse_y) && option_rect.is_point_inside(input.mouse_x, input.mouse_y))
				{
					im_hovering(ui_id);
				}
			}
		}

		end_scroll_region();


		active_mask_stack.add({
			.rect = items_rect,
			.inversed = true,
			});

		set_active_masks_as_renderer_masks();
	}

	return result;
}

Scroll_Region_Result UI::scroll_region(Rect rect, int content_height, int content_width, bool show_horizontal_scrollbar, UI_ID ui_id)
{
	ZoneScoped;

	UI_Scroll_Region_State* state;
	get_or_create_ui_item_data(ui_id, &state);


	renderer.imm_draw_rect(rect, parameters.scroll_region_background);
	
	bool do_show_vertical_scrollbar = content_height > rect.height();


	if ((down != invalid_ui_id && down != ui_id) || up == ui_id)
	{
		state->dragging_vertical_scrollgrip = false;
		state->dragging_horizontal_scrollgrip = false;
	}


	if (is_point_inside_active_zone(input.mouse_x, input.mouse_y) && rect.is_point_inside(input.mouse_x, input.mouse_y))
	{
		this_scroll_region_is_hovering(ui_id);
		im_hovering(ui_id);
	}


	// This rect is rect but without scrollbars
	Rect view_rect = rect;

	if (do_show_vertical_scrollbar)
	{
		view_rect.x_right -= renderer.scaled(parameters.scrollbar_width);
	}

	bool do_show_horizontal_scrollbar = show_horizontal_scrollbar && (content_width > view_rect.width());

	if (do_show_horizontal_scrollbar)
	{
		view_rect.y_bottom += renderer.scaled(parameters.scrollbar_width);
	}


	int amount_of_vertical_scroll_movement   = content_height - view_rect.height();
	int amount_of_horizontal_scroll_movement = content_width  - view_rect.width();
	if (amount_of_vertical_scroll_movement < 0)
		amount_of_vertical_scroll_movement = 0;
	if (amount_of_horizontal_scroll_movement < 0)
		amount_of_horizontal_scroll_movement = 0;


	state->scroll_from_top  = clamp(0, amount_of_vertical_scroll_movement,   state->scroll_from_top);
	state->scroll_from_left = clamp(0, amount_of_horizontal_scroll_movement, state->scroll_from_left);


	Rect vertical_scrollbar_rect;
	Rect vertical_scrollgrip_rect;

	Rect horizontal_scrollbar_rect;
	Rect horizontal_scrollgrip_rect;



	if (do_show_vertical_scrollbar)
	{
		vertical_scrollbar_rect = Rect::make(rect.x_right - renderer.scaled(parameters.scrollbar_width), rect.y_bottom, rect.x_right, rect.y_top);


		if (!state->dragging_vertical_scrollgrip && hover_scroll_region == ui_id)
		{
			state->scroll_from_top = clamp(0, amount_of_vertical_scroll_movement, state->scroll_from_top + input.mouse_wheel_delta * parameters.scroll_region_mouse_wheel_scroll_speed_pixels);
		}



		float visible_to_overall = float(view_rect.height()) / float(content_height);

		int scrollgrip_height = max((int) (visible_to_overall * rect.height()), renderer.scaled(parameters.min_scrollgrip_height));


		int amount_of_scrollgrip_movement = rect.height() - scrollgrip_height;

		int scrollgrip_offset = (int) lerp(0.0f, float(amount_of_scrollgrip_movement), float(state->scroll_from_top) / float(amount_of_vertical_scroll_movement));

		vertical_scrollgrip_rect = Rect::make(rect.x_right - renderer.scaled(parameters.scrollbar_width), rect.y_top - scrollgrip_offset - scrollgrip_height, rect.x_right, rect.y_top - scrollgrip_offset);


		if (down == ui_id)
		{
			if (vertical_scrollgrip_rect.is_point_inside(input.old_mouse_x, input.old_mouse_y))
			{
				state->dragging_vertical_scrollgrip = true;
				state->dragging_scrollgrip_offset_top = input.old_mouse_y - vertical_scrollgrip_rect.y_top;
			}
			else if (vertical_scrollbar_rect.is_point_inside(input.old_mouse_x, input.old_mouse_y))
			{
				int direction = input.old_mouse_y < vertical_scrollgrip_rect.center_y() ? 1 : -1;

				scrollgrip_offset = clamp(0, amount_of_scrollgrip_movement, scrollgrip_offset + direction * vertical_scrollgrip_rect.height() / 2);

				state->scroll_from_top = (int) lerp(0.0f, float(amount_of_vertical_scroll_movement), float(scrollgrip_offset) / float(amount_of_scrollgrip_movement));
			}
		}

		if (state->dragging_vertical_scrollgrip)
		{
			scrollgrip_offset = clamp(0, amount_of_scrollgrip_movement, (vertical_scrollbar_rect.y_top - input.mouse_y) + state->dragging_scrollgrip_offset_top);

			state->scroll_from_top = (int) lerp(0.0f, float(amount_of_vertical_scroll_movement), float(scrollgrip_offset) / float(amount_of_scrollgrip_movement));
		}
	}


	if (do_show_horizontal_scrollbar)
	{
		horizontal_scrollbar_rect = Rect::make(rect.x_left, rect.y_bottom, view_rect.x_right, rect.y_bottom + renderer.scaled(parameters.scrollbar_width));


		// The reason we use view_rect instead of rect (Using rect in vertical scrollbar calculations) is: 
		//  When we show vertical scrollbar we won't use whole rect's width for 
		//    horizontal scrollbar to not overlap with vertical scrollbar.
		//  Instead we use rect.width - (vertical scrollbar width), which equals to view_rect.width()

		float visible_to_overall = float(view_rect.width()) / float(content_width);

		int scrollgrip_width = max((int) (visible_to_overall * view_rect.width()), renderer.scaled(parameters.min_scrollgrip_height));

		int amount_of_scrollgrip_movement = view_rect.width() - scrollgrip_width;

		int scrollgrip_offset = (int) lerp(0.0f, float(amount_of_scrollgrip_movement), float(state->scroll_from_left) / float(amount_of_horizontal_scroll_movement));

		horizontal_scrollgrip_rect = Rect::make(rect.x_left + scrollgrip_offset, rect.y_bottom, rect.x_left + scrollgrip_offset + scrollgrip_width, rect.y_bottom + renderer.scaled(parameters.scrollbar_width));


		if (down == ui_id)
		{
			if (horizontal_scrollgrip_rect.is_point_inside(input.old_mouse_x, input.old_mouse_y))
			{
				state->dragging_horizontal_scrollgrip = true;
				state->dragging_scrollgrip_offset_left = input.old_mouse_x - horizontal_scrollgrip_rect.x_left;
			}
			else if (horizontal_scrollbar_rect.is_point_inside(input.old_mouse_x, input.old_mouse_y))
			{
				int direction = input.old_mouse_x < horizontal_scrollgrip_rect.center_x() ? -1 : 1;

				scrollgrip_offset = clamp(0, amount_of_scrollgrip_movement, scrollgrip_offset + direction * horizontal_scrollgrip_rect.height());

				state->scroll_from_left = (int) lerp(0.0f, float(amount_of_horizontal_scroll_movement), float(scrollgrip_offset) / float(amount_of_scrollgrip_movement));
			}
		}

		if (state->dragging_horizontal_scrollgrip)
		{
			scrollgrip_offset = clamp(0, amount_of_scrollgrip_movement, (input.mouse_x - horizontal_scrollbar_rect.x_left) - state->dragging_scrollgrip_offset_left);

			state->scroll_from_left = (int) lerp(0.0f, float(amount_of_horizontal_scroll_movement), float(scrollgrip_offset) / float(amount_of_scrollgrip_movement));
		}
	}



	state->scroll_from_top  = clamp(0, amount_of_vertical_scroll_movement,   state->scroll_from_top);
	state->scroll_from_left = clamp(0, amount_of_horizontal_scroll_movement, state->scroll_from_left);


	if (do_show_vertical_scrollbar)
	{
		rgba scrollgrip_color = parameters.scrollgrip_color;

		if (state->dragging_vertical_scrollgrip)
		{
			scrollgrip_color = parameters.scrollgrip_color_active;
		}
		else if (hover == ui_id && vertical_scrollgrip_rect.is_point_inside(input.old_mouse_x, input.old_mouse_y))
		{
			scrollgrip_color = parameters.scrollgrip_color_hover;
		}


		renderer.imm_draw_rect(vertical_scrollbar_rect, parameters.vertical_scrollbar_background_color);
		renderer.imm_draw_rect(vertical_scrollgrip_rect, scrollgrip_color);
	}

	if (do_show_horizontal_scrollbar)
	{
		rgba scrollgrip_color = parameters.scrollgrip_color;

		if (state->dragging_horizontal_scrollgrip)
		{
			scrollgrip_color = parameters.scrollgrip_color_active;
		}
		else if (hover == ui_id && horizontal_scrollgrip_rect.is_point_inside(input.old_mouse_x, input.old_mouse_y))
		{
			scrollgrip_color = parameters.scrollgrip_color_hover;
		}


		renderer.imm_draw_rect(horizontal_scrollbar_rect, parameters.horizontal_scrollbar_background_color);
		renderer.imm_draw_rect(horizontal_scrollgrip_rect, scrollgrip_color);
	}




	Scroll_Region_Result result;

	result.did_show_scrollbar            = do_show_vertical_scrollbar;
	result.did_show_horizontal_scrollbar = do_show_horizontal_scrollbar;

	result.scroll_from_top  = state->scroll_from_top;
	result.scroll_from_left = state->scroll_from_left;

	result.state = state;

	result.view_rect = view_rect;


	active_mask_stack.add({
		.rect = view_rect,
		.inversed = false,
	});

	set_active_masks_as_renderer_masks();

	return result;
}

void UI::end_scroll_region()
{
	ZoneScoped;

	assert(active_mask_stack.count);

	active_mask_stack.count -= 1;
	set_active_masks_as_renderer_masks();
}