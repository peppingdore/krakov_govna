#include "Typer_UI.h"

#include "Renderer.h"
#include "Main.h"
#include "Input.h"
#include "Terminal.h"

#include "Ease_Functions.h"

void Typer_UI::init()
{
	renderer_lines = make_array<Renderer_Line>(48, c_allocator);
	found_entries = make_array<Found_Text>(32, c_allocator);


	terminal_focused = true;
}



void Typer_UI::recalculate_renderer_lines()
{
	ZoneScopedNC("recalculate_renderer_lines", 0xff0000);
	
	
	auto face = typer_font_face;



	if (first_invalid_buffer_char_index > terminal.characters.count) return;

	Scoped_Lock lock(terminal.characters_mutex	);



	int start_index = 0;

	bool is_line_wrap_continuation = false;

	{
		bool did_find_renderer_line = false;
		for (int i = 0; i < renderer_lines.count; i++)
		{
			Renderer_Line* renderer_line = renderer_lines[i];
			if (renderer_line->start >= first_invalid_buffer_char_index || (first_invalid_buffer_char_index >= renderer_line->start && first_invalid_buffer_char_index <= (renderer_line->start + renderer_line->length)))
			{
				if (i > 0)
				{
					Renderer_Line* previous_renderer_line = renderer_line - 1;
					
					start_index = renderer_line->start;
					renderer_lines.count = i;
					is_line_wrap_continuation = previous_renderer_line->is_wrapped;
				}
				else
				{
					start_index          = 0;
					renderer_lines.count = 0;
				}
				did_find_renderer_line = true;
				break;
			}
		}

		if (!did_find_renderer_line)
		{
			if (renderer_lines.count)
			{
				start_index = renderer_lines.last()->start;
				renderer_lines.count -= 1;
				is_line_wrap_continuation = (renderer_lines.last() - 1)->is_wrapped;
			}
		}
	}


	//log(ctx.logger, U"typer_font_face glyphs count: %", typer_font_face->glyphs.entries.count);



	int line_width = is_line_wrap_continuation ? wrapped_line_margin : 0;

	Glyph_Iterator glyph_iterator = iterate_glyphs<char32_t>(face);


	int terminal_width = terminal.calculate_terminal_width_in_pixels();

	for (int i = start_index; i < terminal.characters.count; i++)
	{
		Terminal_Character character = *terminal.characters[i];

		if (character.c == U'\n')
		{
			Renderer_Line line;
			line.start  = start_index;
			line.length = i - start_index + 1;
			line.left_margin = is_line_wrap_continuation ? wrapped_line_margin : 0;
			line.is_wrapped = false;

			line.has_new_line_at_the_end = true;

			start_index = i + 1;

			is_line_wrap_continuation = false;
			line_width = 0;

			renderer_lines.add(line);

			glyph_iterator.reset();

			continue;
		}

		int previous_x = glyph_iterator.x;
		glyph_iterator.next_char(character.c);
		int x_delta = glyph_iterator.x - previous_x;

		line_width += x_delta;

		if (line_width > terminal_width && i - start_index > 0)
		{
			Renderer_Line line;
			line.start  = start_index;
			line.length = i - start_index;
			line.left_margin = is_line_wrap_continuation ? wrapped_line_margin : 0;
			line.is_wrapped = true;

			line.has_new_line_at_the_end = false;

			start_index = i;

			i -= 1;
			

			is_line_wrap_continuation = true;
			line_width = wrapped_line_margin;

			renderer_lines.add(line);
			
			glyph_iterator.reset();

			continue;
		}
	}


	u64 remaining_length = terminal.characters.count - start_index;

	if (!renderer_lines.count || remaining_length)
	{
		Renderer_Line line;
		line.start  = start_index;
		line.length = terminal.characters.count - start_index;
		line.left_margin = is_line_wrap_continuation ? wrapped_line_margin : 0;
		line.is_wrapped = false;

		if (terminal.characters.count)
		{
			line.has_new_line_at_the_end = terminal.characters.data[terminal.characters.count - 1].c == U'\n';
		}
		else
		{
			line.has_new_line_at_the_end = false;
		}


		renderer_lines.add(line);
	}
	else
	{
		if (renderer_lines.count)
		{
			if (renderer_lines.last()->has_new_line_at_the_end)
			{
				Renderer_Line line;
				line.start  = terminal.characters.count;
				line.length = 0;
				line.left_margin = is_line_wrap_continuation ? wrapped_line_margin : 0;
				line.is_wrapped = false;

				line.has_new_line_at_the_end = false;

				renderer_lines.add(line);
			}
		}
	}


	first_invalid_buffer_char_index = s64_max;
}



void Typer_UI::set_typer_font_face()
{
	Font* font = font_storage.find_font(settings.text_font_face);

	bool did_fallback = false;
	if (!font)
	{
		assert(font_storage.fonts.count);
		font = font_storage.fonts[0];

		did_fallback = true;
	}

	Font::Face* face = font->get_face(renderer.scaled(settings.text_font_face_size));
	typer_font_face = face;

	if (did_fallback && (typer_font_face != face))
	{
		log(ctx.logger, U"Failed to find font called '%' from settings. Falling back to first font called '%'", settings.text_font_face, font->name);
	}
}

void Typer_UI::save_presursor_pixels_on_line(Renderer_Line line)
{
	Scoped_Lock lock(terminal.characters_mutex);

	int precursor_width = line.left_margin;

	auto iter = iterate_glyphs<char32_t>(typer_font_face);

	for (int i = line.start; i < user_cursor; i++)
	{
		iter.next_char(terminal.characters[i]->c);

		precursor_width += iter.x_delta;
	}

	desired_precursor_pixels = precursor_width;
}

void Typer_UI::set_cursor_on_line_based_on_the_saved_precursor_pixels_on_line(Renderer_Line line)
{
	Scoped_Lock lock(terminal.characters_mutex);

	assert(desired_precursor_pixels != -1);


	auto iter = iterate_glyphs<char32_t>(typer_font_face);


	iter.x += line.left_margin;
	int left_width = iter.x;


	for (int i = line.start; i < (line.start + line.length); i++)
	{
		if (iter.x >= desired_precursor_pixels)
		{
			// Find which character is closer. left or right.
			if (desired_precursor_pixels - left_width < iter.x - desired_precursor_pixels)
			{
				// Left is closer.
				user_cursor = max(0, i - 1);
				return;
			}

			user_cursor = i;
			return;
		}

		left_width = iter.x;

		iter.next_char(terminal.characters[i]->c);
	}

	user_cursor = line.start + line.length_without_new_line();
}

void Typer_UI::scroll_to_char(int char_index, bool scroll_even_if_on_screen)
{
	// log(ctx.logger, U"Scroll to");

	int overall_height = (renderer_lines.count) * typer_font_face->line_spacing;

	int view_height = get_terminal_view_height();

	int height_with_appendix_at_the_bottom = overall_height + get_scroll_bottom_appendix_height(view_height);

	int scroll_range_height = clamp(0, s32_max, height_with_appendix_at_the_bottom - view_height);


	int grip_height;

	float overall_height_to_view_height = float(height_with_appendix_at_the_bottom) / float(view_height);
	if (overall_height_to_view_height > 10.0f)
		grip_height = renderer.scaled(48);
	else
		grip_height = renderer.scaled(float(view_height) / (float(height_with_appendix_at_the_bottom) / float(view_height)));

	int scrollgrip_offset_max = view_height - grip_height;

	int scrollgrip_offset = lerp(0, scrollgrip_offset_max, float(scroll_top_pixels_offset) / float(scroll_range_height));

	

	need_to_redraw_next_frame(code_location());

	s64 cursor_line = find_to_which_renderer_line_position_belongs(char_index);

	if (cursor_line >= 0)
	{
		bool is_cursor_on_screen;
		{
			int cursor_top_pixels_offset = typer_font_face->line_spacing * cursor_line;
			is_cursor_on_screen = cursor_top_pixels_offset >= (scroll_top_pixels_offset + typer_font_face->line_spacing) && cursor_top_pixels_offset <= (scroll_top_pixels_offset + renderer.height - typer_font_face->line_spacing);
		}


		if (!is_cursor_on_screen || scroll_even_if_on_screen)
		{
			scroll_top_pixels_offset = clamp<int>(0, scroll_range_height, typer_font_face->line_spacing * (cursor_line + 1));

			scrollgrip_offset = lerp(0, scrollgrip_offset_max, float(scroll_top_pixels_offset) / float(scroll_range_height)); // :SameCode:scroll_top_pixels_offset

			was_screen_at_the_most_bottom_last_frame = false;
		}
	}
}

void Typer_UI::invalidate_after(s64 after) // @Cleanup: move this to cpp file.
{
	Scoped_Lock lock(terminal.characters_mutex);
	
	if (after < first_invalid_buffer_char_index)
	{
		first_invalid_buffer_char_index = after;
	}
}

int Typer_UI::get_scroll_bottom_appendix_height(int view_height)
{
	// @TODO: decide whether to leave appendix
#if 0
	return view_height / 2;
#else
#if 0
	return 0;
	// return (window_border_size); // @Hack should be 0, but this is a temporary workaround.
#else
	if (typer_font_face)
	{
		return typer_font_face->baseline_offset;
	}

	return 0;
#endif
#endif

}



s64 Typer_UI::find_to_which_renderer_line_position_belongs(u64 position)
{
	recalculate_renderer_lines();


	s64 result_line = -1;

	for (s64 i = 0; i < renderer_lines.count; i++)
	{
		Renderer_Line line = *renderer_lines[i];

		int line_end;
		if (line.is_wrapped)
		{
			line_end = line.start + line.length - 1;
		}
		else
		{
			line_end = line.start + line.length_without_new_line();
		}

		if (position >= line.start && position <= line_end)
		{
			assert(result_line == -1);
			result_line = i; // Not breaking here just for debugging purposes.
		}
	}

	return result_line;
}

s64 Typer_UI::find_closest_renderer_line_to_y_coordinate(int y)
{
	int current_line_y_bottom = renderer.height + scroll_top_pixels_offset - renderer.scaled(window_header_size);

	for (s64 i = 0; i < renderer_lines.count; i++)
	{
		Renderer_Line line = *renderer_lines[i];
		current_line_y_bottom -= typer_font_face->line_spacing;

		if (y > current_line_y_bottom)
		{
			return i;
		}
	}

	return renderer_lines.count - 1;
}

s64 Typer_UI::find_cursor_position_for_mouse()
{
	s64 line_at_cursor = find_closest_renderer_line_to_y_coordinate(clamp(0, renderer.height, input.mouse_y));

	s64 result = 0;

	if (line_at_cursor == -1)
	{
		result = 0;
	}
	else
	{
		Renderer_Line line = *renderer_lines[line_at_cursor];

		int line_y_top = renderer.height + scroll_top_pixels_offset - renderer.scaled(window_header_size) - typer_font_face->line_spacing * line_at_cursor;
		int line_y_bottom = line_y_top - typer_font_face->line_spacing;

		if (input.mouse_y > line_y_top)
		{
			result = line.start;
		}
		else if (input.mouse_y < line_y_bottom)
		{
			result = line.start + line.length_without_new_line();
		}
		else
		{
			Scoped_Lock lock(terminal.characters_mutex);

			String_Builder<char32_t> line_str = build_string<char32_t>(frame_allocator);

			for (int i = line.start; i < (line.start + line.length_without_new_line()); i++)
			{
				line_str.append(terminal.characters[i]->c);
			}


			int cursor_line_local = pick_appropriate_cursor_position(line_str.get_string(), typer_font_face, input.mouse_x - line.left_margin);

			result = line.start + cursor_line_local;
		}
	}

	result = clamp<s64>(0, terminal.get_characters_count(), result);

	return result;
};

s64 Typer_UI::find_exact_cursor_position_for_mouse()
{
	s64 line_at_cursor = find_closest_renderer_line_to_y_coordinate(input.mouse_y);

	s64 result = 0;

	if (line_at_cursor == -1)
	{
		result = -1;
	}
	else
	{
		Renderer_Line line = *renderer_lines[line_at_cursor];

		int line_y_top = renderer.height + scroll_top_pixels_offset - renderer.scaled(window_header_size) - typer_font_face->line_spacing * line_at_cursor;
		int line_y_bottom = line_y_top - typer_font_face->line_spacing;

		if (input.mouse_y > line_y_top)
		{
			result = -1;
		}
		else if (input.mouse_y < line_y_bottom)
		{
			result = -1;
		}
		else
		{
			String_Builder<char32_t> line_str = build_string<char32_t>(frame_allocator);

			for (int i = line.start; i < (line.start + line.length_without_new_line()); i++)
			{
				line_str.append(terminal.characters[i]->c);
			}


			int cursor_line_local = pick_appropriate_cursor_position(line_str.get_string(), typer_font_face, input.mouse_x - line.left_margin);

			{
				if (cursor_line_local == 0)
				{
					if (input.mouse_x < line.left_margin)
					{
						result = -1;
					}
					else
					{
						result = line.start + 0;
					}
				}
				else if (cursor_line_local == line_str.length)
				{
					int text_width = measure_text_width(line_str.get_string(), typer_font_face);
					if (input.mouse_x > text_width + line.left_margin)
					{
						result = -1;
					}
					else
					{
						result = line.start + line_str.length;
					}
				}
				else
				{
					result = line.start + cursor_line_local;
				}
			}
		}
	}

	if (result > terminal.get_characters_count())
	{
		result = -1;
	}

	return result;
}

Vector2i Typer_UI::find_pixel_position_for_character(u64 position)
{
	s64 renderer_line_index = find_to_which_renderer_line_position_belongs(position);
	Renderer_Line renderer_line = *renderer_lines[renderer_line_index];

	Unicode_String line_until_cursor = terminal.copy_region(renderer_line.start, position - renderer_line.start, frame_allocator);

	int width = measure_text_width(line_until_cursor, typer_font_face);

	return {
		.x = renderer_line.left_margin + width, 
		.y = (int) (y_top - typer_font_face->line_spacing + scroll_top_pixels_offset - renderer_line_index * typer_font_face->line_spacing),
	};
}




bool Typer_UI::is_header_ui_touching_cursor(int x, int y)
{
	Rect settings_toggle_button_rect = get_settings_toggle_button_rect();

	bool touching_settings_stuff = settings_screen.is_header_ui_touching_cursor(x, y);

	return
		x >= renderer.width - get_header_window_control_buttons_width() ||
		settings_toggle_button_rect.is_point_inside(x, y) ||
		touching_settings_stuff;
}

Rect Typer_UI::get_settings_toggle_button_rect()
{
	int x_right = renderer.width - get_header_window_control_buttons_width() - renderer.scaled(settings_toggle_button_margin);

	return Rect::make(x_right - renderer.scaled(settings_toggle_button_width), renderer.height - renderer.scaled(window_header_size), x_right, renderer.height);
}

int Typer_UI::get_header_window_control_buttons_width()
{
	return renderer.scaled(window_header_button_width) * 3;
}


bool Typer_UI::header_button(Rect rect, rgba color, float* darkness, UI_ID ui_id)
{
	// I didn't comment why you can't use ui.button (fuck)
	//  but as i remember there was a serious reason for that.
	//  @TODO: figure out why.

	ZoneScoped;

	UI_Button_State* state = (UI_Button_State*) ui.get_ui_item_data(ui_id);

	if (!state)
	{
		state = (UI_Button_State*) c_allocator.alloc(sizeof(*state), code_location());
		ui.ui_id_data_array.put(ui_id, state);
		
		state->darkness = 0.0;
	}


	bool result = false;
	bool do_need_to_redraw = false;

	bool hovering = rect.is_point_inside(input.mouse_x, input.mouse_y);

	if (hovering && input.is_key_down(Key::LMB))
	{
		state->darkness = 1.0;
		result = true;
	}

	int old_darkness = state->darkness;

	if ((!hovering && state->darkness) || (state->darkness > 0.3001))
	{
		state->darkness = clamp<double>(0, 1, state->darkness - frame_time * 5.0);
		do_need_to_redraw = true;
	}

	if (hovering)
	{
		if (state->darkness < 0.3)
		{
			state->darkness = 0.3;
			do_need_to_redraw = true;
		}
	}


	if (do_need_to_redraw)
	{
		need_to_redraw_next_frame(code_location());
	}


	rgba darken_color = color;
	{
		darken_color.rgb_value = darken_color.rgb_value * lerp(1.0f, 0.1f, state->darkness);
	}

	renderer.draw_rect(rect, darken_color);

	*darkness = state->darkness;

	return result;
}

void Typer_UI::draw_window_header()
{
	int header_height = renderer.scaled(window_header_size);
	int button_width  = renderer.scaled(window_header_button_width);

	Rect window_header_rect = Rect::make(0, renderer.height - header_height, renderer.width, renderer.height);

	y_top = window_header_rect.y_bottom;

	renderer.draw_rect(window_header_rect, window_border_color);

	scoped_set_and_revert(ui.parameters.text_font_name, Unicode_String(U"DancingScript-Regular"));
	scoped_set_and_revert(ui.parameters.text_font_face_size, 15);
	scoped_set_and_revert(ui.parameters.text_color, rgba(0, 0, 0, 255));
	scoped_set_and_revert(ui.parameters.center_text_vertically, true);
	scoped_set_and_revert(ui.parameters.text_alignment, Text_Alignment::Left);
	

	int  icon_left_margin = renderer.scaled(12);
	int  icon_width = renderer.scaled(70);
	int  icon_vertical_margin = renderer.scaled(5);
	rgba icon_color = rgba(255, 255, 255, 255);

	renderer.draw_rect_with_alpha_fade(Rect::make(0, renderer.height - header_height + icon_vertical_margin, icon_left_margin, renderer.height - icon_vertical_margin), icon_color, 0, 150);

	icon_vertical_margin = renderer.scaled(4);

	Rect window_title_background_rect = Rect::make(icon_left_margin, renderer.height - header_height + icon_vertical_margin, icon_left_margin + icon_width, renderer.height - icon_vertical_margin);

	renderer.draw_rect(window_title_background_rect, icon_color);

	ui.draw_text(icon_left_margin + renderer.scaled(12), renderer.height - header_height / 2, U"Terminal");




	rgba close_button_color    = rgba(255, 0,   0,   255);
	rgba maximize_button_color = rgba(125, 125, 125, 255);
	rgba minimize_button_color = rgba(175, 175, 175, 255);


	int x = renderer.width;

	{
		x -= button_width;

		Rect rect = Rect::make(x, renderer.height - header_height, x + button_width, renderer.height);
		
		float darkness;
		if (header_button(rect, close_button_color, &darkness, ui_id(0)))
		{
        #if OS_WINDOWS
			SendMessage(windows.hwnd, WM_CLOSE, 0, 0);
		#elif OS_LINUX
            typer.terminate();
            exit(0);
        #endif
        }

		rgba line_color = rgba(255, 255, 255, 255);
		int margin = renderer.scaled(4);

		int shrink_offset = lerp(0, renderer.scaled(12), darkness);

		renderer.draw_line(rect.x_left + margin + shrink_offset, rect.y_top - margin - shrink_offset, rect.x_right - margin - shrink_offset + 1, rect.y_bottom + margin + shrink_offset, line_color);
		renderer.draw_line(rect.x_left + margin + shrink_offset - 1, rect.y_bottom + margin + shrink_offset, rect.x_right - margin - shrink_offset, rect.y_top - margin - shrink_offset, line_color);
	}

	{
		x -= button_width;

		Rect rect = Rect::make(x, renderer.height - header_height, x + button_width, renderer.height);

		float darkness;
		if (header_button(rect, maximize_button_color, &darkness, ui_id(0)))
		{
        #if OS_WINDOWS
			ShowWindowAsync(windows.hwnd, (IsMaximized(windows.hwnd) ? SW_RESTORE : SW_MAXIMIZE));
		#elif OS_LINUX
			x11_toggle_window_maximization();
        #endif
        }

		rgba line_color = rgba(255, 255, 255, 255);
		int horizontal_margin = renderer.scaled(4);
		int vertical_margin = header_height / 4;

		int top_line_y;
		int bottom_line_y;

#if OS_WINDOWS
		if (!IsMaximized(windows.hwnd))
#elif OS_LINUX
        if (is_x11_window_likely_maximized)
#elif OS_DARWIN
		if (false) // @TODO: implement
#else
	static_assert(false);
#endif
        {
			int expand_offset = renderer.scaled(lerp(0, 10, darkness));

			top_line_y    = rect.y_top    - vertical_margin + expand_offset;
			bottom_line_y = rect.y_bottom + vertical_margin - expand_offset;
		}
		else
		{
			int collapse_offset = renderer.scaled(lerp(0, 10, darkness));

			int center_y = rect.center_y();
			
			top_line_y    = center_y + vertical_margin - collapse_offset;
			bottom_line_y = center_y - vertical_margin + collapse_offset;
		}

		renderer.draw_line(rect.x_left + horizontal_margin, top_line_y, rect.x_right - horizontal_margin, top_line_y, line_color);
		renderer.draw_line(rect.x_left + horizontal_margin, bottom_line_y, rect.x_right - horizontal_margin, bottom_line_y, line_color);
	}

	{
		x -= button_width;
	
		float darkness;
		Rect rect = Rect::make(x, renderer.height - header_height, x + button_width, renderer.height);

		if (header_button(rect, minimize_button_color, &darkness, ui_id(0)))
		{
        #if OS_WINDOWS
			ShowWindowAsync(windows.hwnd, SW_MINIMIZE);
		#elif OS_LINUX
			minimize_x11_window();
        #endif
        }

		rgba line_color = rgba(255, 255, 255, 255);
		int margin = renderer.scaled(lerp(4, 20, darkness));

		renderer.draw_line(rect.x_left + margin, rect.center_y(), rect.x_right - margin, rect.center_y(), line_color);
	}

	Rect window_control_rect = window_header_rect;
	window_control_rect.x_left = window_title_background_rect.x_right;
	window_control_rect.x_right = x;

#if OS_LINUX
	do_x11_window_controlling(window_control_rect);
#endif


	draw_settings_toggle_button();
}

void Typer_UI::draw_settings_toggle_button()
{
	Rect rect = get_settings_toggle_button_rect();


	float darkness;

	if (header_button(rect, settings_toggle_button_color, &darkness, ui_id(0)))
	{
		// Not a good place for this, but we have to reset it somewhere if settings_screen is not open.
		key_bindings.key_binding_recording_ui_id = invalid_ui_id;


		if (screen_page == Screen_Page::Terminal)
		{
			screen_page = Screen_Page::Settings;
			settings_screen.openness = 0.0;
		}
		else
		{
			screen_page = Screen_Page::Terminal;
			settings_screen.openness = 1.0;
		}
    }


    // Draw the lines
    {
    	int scaled_margin = renderer.scaled(settings_toggle_button_line_margin);

    	int height_quarter = rect.height() / 4;

    	// Top line
    	{
	    	Vector2i line_left_0  = { .x = rect.x_left  + scaled_margin, .y = rect.y_top - height_quarter };
	    	Vector2i line_right_0 = { .x = rect.x_right - scaled_margin, .y = rect.y_top - height_quarter };

	    	Vector2i line_left_1  = { .x = rect.x_left, .y = rect.y_bottom };
	    	Vector2i line_right_1 = { .x = rect.x_left, .y = rect.y_top };

	    	Vector2i line_left  = lerp(line_left_0,  line_left_1,  settings_screen.openness);
	    	Vector2i line_right = lerp(line_right_0, line_right_1, settings_screen.openness);

	    	renderer.draw_line(line_left.x, line_left.y, line_right.x, line_right.y, settings_toggle_button_line_color.scaled_alpha(1.0 - settings_screen.openness));
    	}

    	// Center line
    	{
    		int christ_offset = height_quarter - 2;

    		{
	    		Vector2i line_left_0  = { .x = rect.x_left  + scaled_margin, .y = rect.center_y() };
		    	Vector2i line_right_0 = { .x = rect.x_right - scaled_margin, .y = rect.center_y() };

		    	Vector2i line_left_1  = line_left_0;
		    		line_left_1.y += christ_offset;

		    	Vector2i line_right_1 = line_right_0;
		    		line_right_1.y -= christ_offset;

		    	Vector2i line_left  = lerp(line_left_0,  line_left_1,  settings_screen.openness_linear);
		    	Vector2i line_right = lerp(line_right_0, line_right_1, settings_screen.openness_linear);

		    	renderer.draw_line(line_left.x, line_left.y, line_right.x, line_right.y, settings_toggle_button_line_color);
	    	}

	    	{
	    		Vector2i line_left_0  = { .x = rect.x_left  + scaled_margin, .y = rect.center_y() };
		    	Vector2i line_right_0 = { .x = rect.x_right - scaled_margin, .y = rect.center_y() };

		    	Vector2i line_left_1  = line_left_0;
		    		line_left_1.y -= christ_offset;

		    	Vector2i line_right_1 = line_right_0;
		    		line_right_1.y += christ_offset;

		    	Vector2i line_left  = lerp(line_left_0,  line_left_1,  settings_screen.openness_linear);
		    	Vector2i line_right = lerp(line_right_0, line_right_1, settings_screen.openness_linear);

		    	renderer.draw_line(line_left.x, line_left.y, line_right.x, line_right.y, settings_toggle_button_line_color);
	    	}
    	}

    	// Bottom line
    	{
	    	Vector2i line_left_0  = { .x = rect.x_left  + scaled_margin, .y = rect.y_bottom + height_quarter };
	    	Vector2i line_right_0 = { .x = rect.x_right - scaled_margin, .y = rect.y_bottom + height_quarter };

	    	Vector2i line_left_1  = { .x = rect.x_right, .y = rect.y_bottom };
	    	Vector2i line_right_1 = { .x = rect.x_right, .y = rect.y_top };

	    	Vector2i line_left  = lerp(line_left_0,  line_left_1,  settings_screen.openness);
	    	Vector2i line_right = lerp(line_right_0, line_right_1, settings_screen.openness);

	    	renderer.draw_line(line_left.x, line_left.y, line_right.x, line_right.y, settings_toggle_button_line_color.scaled_alpha(1.0 - settings_screen.openness));
    	}
    }
}

void Typer_UI::do_frame()
{	
	// Keeping scroll state
	{
		if (need_to_keep_scroll_state_this_frame && typer_font_face) // need_to_keep_scroll_state_this_frame will be set during startup, we need typer_font_face to be set before this.
		{
			defer 
			{
				need_to_keep_scroll_state_this_frame = false;
			};

			int renderer_line_index = find_closest_renderer_line_to_y_coordinate(renderer.height);

			int character_that_we_care_about_index = renderer_lines[renderer_line_index]->start;


			int scroll_appendix = scroll_top_pixels_offset - (typer_font_face->line_spacing * renderer_line_index);

			int old_typer_font_face_line_spacing = typer_font_face->line_spacing;

				set_typer_font_face();


			typer_ui.invalidate_after(-1);
			typer_ui.recalculate_renderer_lines();

			renderer_line_index = find_to_which_renderer_line_position_belongs(character_that_we_care_about_index);

			
			scroll_appendix *= float(old_typer_font_face_line_spacing) / float(typer_font_face->line_spacing);

			scroll_top_pixels_offset = (typer_font_face->line_spacing * renderer_line_index) + scroll_appendix;
		}
		else
		{
				set_typer_font_face();
		}
	}




	draw_window_header();


	if (screen_page == Screen_Page::Terminal ||
		screen_page == Screen_Page::Settings && settings_screen.openness_linear < 1.0)
	{
		python_debugger.do_frame();
		typer.do_terminal_frame();
	}

	if (screen_page == Screen_Page::Settings || 
		screen_page == Screen_Page::Terminal && settings_screen.openness_linear > 0.0)
	{
		settings_screen.do_frame();
	}


	if (screen_page == Screen_Page::Terminal && settings_screen.openness_linear > 0.0)
	{
		settings_screen.openness_linear -= frame_time * screen_page_transition_speed;

		need_to_redraw_next_frame(code_location());

		if (settings_screen.openness_linear <= 0.0)
			settings_screen.openness_linear  = 0.0;
	}

	if (screen_page == Screen_Page::Settings && settings_screen.openness_linear < 1.0)
	{
		settings_screen.openness_linear += frame_time * screen_page_transition_speed;

		need_to_redraw_next_frame(code_location());

		if (settings_screen.openness_linear >= 1.0)
			settings_screen.openness_linear  = 1.0;
	}


	settings_screen.openness = ease(settings_screen.openness_linear);

	draw_fps();
}

void Typer_UI::draw_fps()
{
	if (settings.show_fps)
	{
		Unicode_String fps_str = format_unicode_string(frame_allocator, U"AVG FPS: %, FPS: %, ms: %",
			Unicode_String::from_ascii(to_string(fps, frame_allocator), frame_allocator),
			Unicode_String::from_ascii(to_string(1 / frame_time, frame_allocator, 0), frame_allocator),
			Unicode_String::from_ascii(to_string(frame_time * 1000.0, frame_allocator, 4), frame_allocator));

		int fps_str_width = measure_text_width(fps_str, typer_font_face);
		renderer.draw_text(typer_font_face, fps_str, renderer.width - fps_str_width, typer_font_face->baseline_offset);
	}
}


#if OS_LINUX
void Typer_UI::do_x11_window_controlling(Rect window_control_rect)
{
	// Manual window resize maximization stuff.
	

	if (input.is_key_down(Key::LMB) && window_control_rect.is_point_inside(input.mouse_x, input.mouse_y))
	{
		if (time_from_window_header_click != -1 && time_from_window_header_click <= double_click_threshold)
		{
			x11_toggle_window_maximization();
			time_from_window_header_click = -1;
		}
		else
		{
			time_from_window_header_click = 0.0;
		}

	}

	if (time_from_window_header_click != -1)
	{
		time_from_window_header_click += frame_time;
	}


	if (input.is_key_down(Key::LMB))
	{
		// This all is set even if no dragging/resizing will happen
		{
			x11_get_window_position(&drag_start_window_x, &drag_start_window_y);
			x11_get_mouse_position(&drag_start_mouse_x, &drag_start_mouse_y, DefaultRootWindow(x11.display));
			x11_get_window_size(&drag_start_window_width, &drag_start_window_height);
		}


		u32 mouse_dragging_edge = DRAGGING_EDGE_NONE;

		if (!is_x11_window_maximized())
		{
			if (input.mouse_x <= window_resize_border_size)
				mouse_dragging_edge |= DRAGGING_EDGE_LEFT;
			if (input.mouse_x >= renderer.width - window_resize_border_size)
				mouse_dragging_edge |= DRAGGING_EDGE_RIGHT;

			if (input.mouse_y <= window_resize_border_size)
				mouse_dragging_edge |= DRAGGING_EDGE_BOTTOM;
			if (input.mouse_y >= renderer.height - window_resize_border_size)
				mouse_dragging_edge |= DRAGGING_EDGE_TOP;
		}

		if (mouse_dragging_edge != DRAGGING_EDGE_NONE)
		{
			resizing_window = true;

			dragging_edge = mouse_dragging_edge;
		}
		else if (window_control_rect.is_point_inside(input.mouse_x, input.mouse_y))
		{
			dragging_window = true;	
		}

	}

	if (input.is_key_up(Key::LMB))
	{
		dragging_window = false;
		resizing_window = false;
	}


	if (dragging_window || resizing_window)
	{
		int mouse_x, mouse_y;
		x11_get_mouse_position(&mouse_x, &mouse_y, DefaultRootWindow(x11.display));

		int x_delta = mouse_x - drag_start_mouse_x;
		int y_delta = mouse_y - drag_start_mouse_y;

		if (x_delta || y_delta)
		{
			if (dragging_window)
			{
				XMoveWindow(x11.display, x11.window, x_delta + drag_start_window_x, y_delta + drag_start_window_y);
			}
			else if (resizing_window)
			{
				// int window_width, window_height;
				// x11_get_window_size(&window_width, &window_height);


				int expand_left_edge_by   = 0;
				int expand_right_edge_by  = 0;
				int expand_top_edge_by    = 0;
				int expand_bottom_edge_by = 0;

				if (dragging_edge & DRAGGING_EDGE_TOP)
				{
					expand_top_edge_by = y_delta;
					expand_top_edge_by = clamp(-INT_MAX, (drag_start_window_height - window_min_height), expand_top_edge_by);
				}

				if (dragging_edge & DRAGGING_EDGE_BOTTOM)
				{
					expand_bottom_edge_by = y_delta;
					expand_bottom_edge_by = clamp(-(drag_start_window_height - window_min_height), INT_MAX, expand_bottom_edge_by);
				}

				if (dragging_edge & DRAGGING_EDGE_LEFT)
				{
					expand_left_edge_by = x_delta;
					expand_left_edge_by = clamp(-INT_MAX, (drag_start_window_width - window_min_width), expand_left_edge_by);
				}

				if (dragging_edge & DRAGGING_EDGE_RIGHT)
				{
					expand_right_edge_by = x_delta;
					expand_right_edge_by = clamp(-(drag_start_window_width - window_min_width), INT_MAX, expand_right_edge_by);
				}


				XMoveResizeWindow(x11.display, x11.window,
					drag_start_window_x + expand_left_edge_by,
					drag_start_window_y + expand_top_edge_by,
					drag_start_window_width  + expand_right_edge_by  - expand_left_edge_by,
					drag_start_window_height + expand_bottom_edge_by - expand_top_edge_by);
			}
		}
	}
}
#endif