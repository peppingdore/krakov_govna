#include "Settings.h"

#include "b_lib/Reflection.h"
#include "b_lib/File.h"
#include "b_lib/Tokenizer.h"
#include "b_lib/Collection.h"

#include "Main.h"
#include "UI.h"
#include "Input.h"
#include "Ease_Functions.h"


bool save_settings()
{
	ZoneScoped;

	assert(threading.is_main_thread());

	Unicode_String path = path_concat(frame_allocator, typer_directory, Unicode_String(U"settings.txt"));
	File file = open_file(frame_allocator, path, FILE_WRITE | FILE_CREATE_NEW);

	if (!file.succeeded_to_open())
	{
		log(ctx.logger, U"Failed to open settings file to save settings");
		return false;
	}

	defer { file.close(); };

	using namespace Reflection;

	{
		Struct_Type* settings_type_info = (Struct_Type*) Reflection::type_of<Settings>();
		for (auto iter = settings_type_info->iterate_members(frame_allocator); Struct_Member* member = iter.next();)
		{
			file.write(":");
			file.write(member->name);
			file.write(" ");
			
			file.write(write_thing(member->type, add_bytes_to_pointer(&settings, member->offset), frame_allocator));
		
			file.write("\n");
		}
	}

	// log(ctx.logger, U"Successfully saved settings");
	return true;
}

bool load_settings()
{
	ZoneScoped;

	assert(threading.is_main_thread());


	Unicode_String path = path_concat(frame_allocator, typer_directory, Unicode_String(U"settings.txt"));
	File file = open_file(frame_allocator, path, FILE_READ);

	if (!file.succeeded_to_open())
	{
		return false;
	}

	defer{
		file.close();
		save_settings();
	};

	using namespace Reflection;

	{
		Struct_Type* settings_type_info = (Struct_Type*) Reflection::type_of<Settings>();

		// @MemoryLeak: nothing is properly freed here.
		Tokenizer tokenizer = tokenize(&file, frame_allocator);
		tokenizer.key_characters.add(':');


		String token;
		while (true)
		{
			if (tokenizer.peek_token().is_empty()) break;

			if (!tokenizer.expect_token(":"))
			{
				log(ctx.logger, U"Expected ':' at line: %", tokenizer.line);
				return false;
			}

			token = tokenizer.peek_token();

			Struct_Member* found_member = NULL;
			for (auto iter = settings_type_info->iterate_members(frame_allocator); Struct_Member* member = iter.next();)
			{
				if (member->name == token)
				{
					found_member = member;
					break;
				}
			}

			if (!found_member)
			{
				log(ctx.logger, U"Field '%' wasn't found", token);
				return false;
			}

			if (!read_thing(&tokenizer, found_member->type, add_bytes_to_pointer(&settings, found_member->offset), frame_allocator, c_allocator))
			{
				log(ctx.logger, U"Failed to parse field '%'", found_member->name);
				return false;
			}
		}
	}

	// Validating loaded settings.
	{
		if (!font_storage.find_font(settings.text_font_face))
		{
			log(ctx.logger, U"Font with name '%' declared in settings.txt wasn't found", settings.text_font_face);

			settings.text_font_face = font_storage.fonts[0]->name;

			log(ctx.logger, U"Using first available font named '%'", settings.text_font_face);
		}

		if (settings.text_font_face_size < typer_ui.min_typer_font_face_size &&
			settings.text_font_face_size > typer_ui.max_typer_font_face_size)
		{
			settings.text_font_face_size = typer_ui.typer_default_font_face_size;
			log(ctx.logger, U"Expected font size in range [%, %] in settings.txt. Using default size: %", typer_ui.typer_default_font_face_size);
		}
	}

	return true;
}



void Settings_Screen::init()
{
	
}


Rect Settings_Screen::get_background_rect()
{
	return {
		.x_left = 0,
		.y_bottom = 0,
		.x_right = renderer.width,
		.y_top = typer_ui.y_top,
	};
}

void Settings_Screen::do_frame()
{
	ZoneScopedNC("do_settings_frame", 0xff0000);



	Color_Scaling_Context color_ctx = scale_and_save_colors(openness);	
	defer { 
		color_ctx.restore_colors();
	};
	

	Rect background_rect = get_background_rect();

	// Draw background
	{
		int alpha = lerp(0, 255, openness);
		renderer.draw_rect(background_rect, rgba(background_color, alpha));
	}



	// Draw version number
	defer
	{
		scoped_set_and_revert(ui.parameters.text_font_face_size, 12);
		scoped_set_and_revert(ui.parameters.text_alignment, Text_Alignment::Right);
		scoped_set_and_revert(ui.parameters.center_text_vertically, false);

		ui.draw_text(renderer.width - renderer.scaled(4), renderer.scaled(4), version_string);
	};



	auto get_page_function = [&](Settings_Page page)
	{
		switch (page)
		{
			case Settings_Page::Macros:
				return &Settings_Screen::draw_macros_page;

			case Settings_Page::UI:
				return &Settings_Screen::draw_ui_page;

			case Settings_Page::Bindings:
				return &Settings_Screen::draw_bindings_page;
		
			default:
				return (void (Settings_Screen::*)(float)) NULL;
		}
	};


	if (transitioning_from_page == Settings_Page::None)
	{
		auto page_func = get_page_function(current_page);

		if (page_func)
			(this->*page_func)(1.0);
	}
	else
	{
		auto page_func = get_page_function(current_page);
		auto previous_page_func = get_page_function(transitioning_from_page);

		float direction = transitioning_from_page > current_page ? 1 : -1;

		if (previous_page_func)
		{
			(this->*previous_page_func)((1.0 - transition_state) * direction);
		}

		if (page_func)
		{
			(this->*page_func)(transition_state * direction * -1);
		}

		
		transition_state_linear += frame_time * transition_speed;
		if (transition_state_linear > 1.0)
		{
			transition_state_linear = 1.0;
			transitioning_from_page = Settings_Page::None;
		}

		transition_state = ease(transition_state_linear);
	}


	draw_page_selector();
}


Dynamic_Array<Page> Settings_Screen::get_pages(Allocator allocator)
{
	Font::Face* face = ui.get_font_face();

	Dynamic_Array<Page> pages = make_array<Page>(8, allocator);

	{
		Reflection::Enum_Type* type = (Reflection::Enum_Type*) Reflection::type_of<Settings_Page>();

		for (Reflection::Enum_Value& enum_value: type->values)
		{
			if (Reflection::contains_tag(enum_value.tags, "Page"))
			{
				Page page = {
					.enum_value = enum_value,
					.unicode_name = enum_value.name.to_unicode_string(frame_allocator),
					.page_state = 0.0,
				};

				page.text_width = measure_text_width(page.unicode_name, ui.get_font_face());
				page.rect_width = page.text_width + renderer.scaled(page_selector_item_padding * 2);

				pages.add(page);
			}
		}
	}

	int total_width = 0;
	for (Page& page: pages)
	{
		total_width += page.rect_width;

		if (!pages.is_last(&page))
		{
			total_width += renderer.scaled(page_selector_item_margin);
		}
	}


	total_width *= openness;

	int x = renderer.width / 2 - total_width / 2;
	int y = typer_ui.y_top + face->line_spacing / 2;


	for (Page& page: pages)
	{
		page.rect_width *= openness;

		int height = face->line_spacing;
		int appendix = height * 0.5;

		if ((Settings_Page) page.enum_value.value.s32_value == transitioning_from_page)
		{
			page.page_state = (1.0 - transition_state);
		}
		else if ((Settings_Page) page.enum_value.value.s32_value == current_page)
		{
			page.page_state = transition_state;
		}


		height += page.page_state * appendix;


		page.rect = Rect::make(x, y - height, x + page.rect_width, y);

		x += page.rect_width + renderer.scaled(page_selector_item_margin);
	}

	return pages;
}


void Settings_Screen::transition_to_page(Settings_Page to_page)
{
	if (current_page == to_page) return;

	transition_state_linear = 0.0;
	transition_state = 0.0;

	transitioning_from_page = current_page;
	current_page = to_page;
}

void Settings_Screen::draw_page_selector()
{
	// @CopyPaste: PageSelectorUiSettings
	// Should be set before get_pages and if you're using pages to draw/calculate smth.
	scoped_set_and_revert(ui.parameters.text_font_face_size, renderer.scaled(12));
	scoped_set_and_revert(ui.parameters.text_alignment, Text_Alignment::Center);
	scoped_set_and_revert(ui.parameters.text_font_name, Unicode_String(U"NotoMono"));

	auto pages = get_pages(frame_allocator);

	for (auto& page: pages)
	{
		scoped_set_and_revert(ui.parameters.text_color, lerp(page_selector_item_font_color, page_selector_item_color, page.page_state));

		if (ui.button(page.rect, page.unicode_name.to_upper_case(frame_allocator), 
			
			lerp(page_selector_item_color, page_selector_item_font_color, page.page_state).scaled_alpha(openness),

			ui_id(pages.fast_pointer_index(&page))))
		{
			transition_to_page((Settings_Page) page.enum_value.value.s32_value);
		}

		renderer.draw_rect_outline(page.rect, lerp(page_selector_item_font_color, page_selector_item_color, page.page_state).scaled_alpha(openness));
	}


	auto find_page = [&](Settings_Page settings_page) -> Page*
	{
		for (auto& page: pages)
		{
			if ((Settings_Page) page.enum_value.value.s32_value == settings_page)
				return &page;
		}

		return NULL;
	};



	Page* from_page = find_page(transitioning_from_page);
	Page* to_page   = find_page(current_page);


	int y = to_page->rect.y_bottom + renderer.scaled(page_selector_selected_line_margin);

	if (from_page)
	{
		need_to_redraw_next_frame(code_location());
	}


#if 0
	if (!from_page)
	{
		renderer.draw_rect(to_page->rect, page_selector_selected_color);
	}
	else
	{
		assert(to_page);

		int x_left  = lerp(from_page->rect.x_left,  to_page->rect.x_left,  transition_state);
		int x_right = lerp(from_page->rect.x_right, to_page->rect.x_right, transition_state);

		// Assuming y values are the same across rects.

		Rect from_rect = Rect::make(
			clamp(from_page->rect.x_left, from_page->rect.x_right, x_left),
			from_page->rect.y_bottom, 
			clamp(from_page->rect.x_left, from_page->rect.x_right, x_right),
			from_page->rect.y_top);

		Rect to_rect = Rect::make(
			clamp(to_page->rect.x_left, to_page->rect.x_right, x_left),
			to_page->rect.y_bottom, 
			clamp(to_page->rect.x_left, to_page->rect.x_right, x_right),
			to_page->rect.y_top);

		renderer.draw_rect(from_rect, page_selector_selected_color);
		renderer.draw_rect(to_rect,   page_selector_selected_color);

		need_to_redraw_next_frame(code_location());
	}
#endif
}

bool Settings_Screen::is_header_ui_touching_cursor(int x, int y)
{
	// @CopyPaste: PageSelectorUiSettings
	scoped_set_and_revert(ui.parameters.text_color, page_selector_item_font_color);
	scoped_set_and_revert(ui.parameters.text_font_face_size, renderer.scaled(12));
	scoped_set_and_revert(ui.parameters.text_alignment, Text_Alignment::Center);
	scoped_set_and_revert(ui.parameters.text_font_name, Unicode_String(U"NotoMono"));

	auto pages = get_pages(frame_allocator);

	for (auto& page: pages)
	{
		if (page.rect.is_point_inside(x, y))
		{
			return true;
		}
	}

	return false;
}

void Settings_Screen::draw_macros_page(float state)
{
	Color_Scaling_Context color_ctx = scale_and_save_colors(get_alpha_for_page(state));	
	defer { 
		color_ctx.restore_colors();
	};


	auto& bound_actions = key_bindings.bound_actions;


	auto macro_bindings = make_array<Binding_And_Action*>(16, frame_allocator);

	auto filter_macro_bindings_from_key_bindings = [&](Dynamic_Array<Binding_And_Action*>* array)
	{
		array->clear();

		for (auto& item: bound_actions)
		{
			if (item.action.type == Action_Type::Run_A_Macro)
			{
				array->add(&item);
			}
		}
	};



	filter_macro_bindings_from_key_bindings(&macro_bindings);


	Rect page_rect = get_page_rect(state);

	int x = page_rect.x_left;
	int y = page_rect.y_top;


	int macros_scroll_region_height = page_rect.height();

	int macros_list_width = renderer.scaled(256);
	int macro_mailbox_margin = renderer.scaled(48);

	Rect macro_editing_rect = Rect::make(x + macros_list_width + macro_mailbox_margin, page_rect.y_bottom, page_rect.x_right, page_rect.y_top);


	int macro_height = renderer.scaled(distance_between_macros_in_the_list + macro_height_in_the_list);

	Rect macros_scroll_region_rect = Rect::make(x, y - macros_scroll_region_height, x + macros_list_width, y);

	Scroll_Region_Result scroll_region_result = ui.scroll_region(macros_scroll_region_rect, 
		macro_height * macro_bindings.count + renderer.scaled(new_macro_button_height + new_macro_button_offset + distance_between_macros_in_the_list * 2 + macro_margin_in_the_list),
		 0, false, ui_id(0));

	Rect edited_macro_rect;

	int scaled_macro_margin_in_the_list = renderer.scaled(macro_margin_in_the_list);

	Binding_And_Action* macro_binding_that_we_should_delete = NULL;

	// Macros
	{
		for (int i = 0; i < macro_bindings.count; i++)
		{
			Binding_And_Action* macro_binding = *macro_bindings[i];
			Key_Binding* binding = &macro_binding->binding;
			Action*      action  = &macro_binding->action;


			UI_ID macro_ui_id = ui_id(i);



			Macro_Rect_State* macro_rect_state = (Macro_Rect_State*) ui.get_ui_item_data(macro_ui_id);
			if (!macro_rect_state)
			{
				macro_rect_state = (Macro_Rect_State*) c_allocator.alloc(sizeof(*macro_rect_state), code_location());
				memset(macro_rect_state, sizeof(*macro_rect_state), 0);

				ui.put_ui_item_data(macro_ui_id, macro_rect_state);
			}				



			Rect macro_rect = Rect::make(
				x + scaled_macro_margin_in_the_list,
				y - renderer.scaled(macro_height_in_the_list) + scroll_region_result.scroll_from_top - scaled_macro_margin_in_the_list,
				x + scroll_region_result.view_rect.width() - scaled_macro_margin_in_the_list,
				y + scroll_region_result.scroll_from_top - scaled_macro_margin_in_the_list);


			bool is_being_edited = macro_currently_being_edited == i;

			if (is_being_edited)
			{
				edited_macro_rect = macro_rect;
			}

			bool can_delete = false;

			scoped_set_and_revert(ui.parameters.text_color, is_being_edited ? rgba(0, 0, 0, 255) : ui.parameters.text_color);


			UI_ID macro_button_ui_id = ui_id(i);


#if 0
			Log(U"%, %", 
				ui.ui_id_data_array.keys.index_of(macro_button_ui_id),
				to_string<u64, 16>((u64) (ui.get_ui_item_data(macro_button_ui_id)), frame_allocator));
#endif


			if (ui.button(macro_rect, is_being_edited ? macro_rect_editing_color : macro_rect_color, macro_button_ui_id))
			{
				macro_currently_being_edited = i;

				is_being_edited = true;
			}

			ui.active_mask_stack.add({
				.rect = macro_rect,
				.inversed = false,
			});
			ui.set_active_masks_as_renderer_masks();

			{
				scoped_set_and_revert(ui.parameters.text_font_face_size, 11);
				scoped_set_and_revert(ui.parameters.text_alignment, Text_Alignment::Left);
				scoped_set_and_revert(ui.parameters.center_text_vertically, true);


				bool draw_binding = binding->keys.count;

				ui.draw_text(macro_rect.x_left + renderer.scaled(macro_rect_text_margin), 
					draw_binding ? 
						macro_rect.y_top - ui.get_font_face()->line_spacing :
						macro_rect.center_y(),
					action->macro_string, &macro_rect);


				if (draw_binding)
				{
					// renderer.draw_rect(Rect::make(macro_rect.x_right - macro_key_rect_width, macro_rect.y_bottom, macro_rect.x_right, macro_rect.y_top), macro_bind_background_color);
					// renderer.draw_rect_with_alpha_fade(Rect::make(macro_rect.x_right - macro_key_rect_width - renderer.scaled(32), macro_rect.y_bottom, macro_rect.x_right - macro_key_rect_width, macro_rect.y_top), macro_bind_background_color, 0, 128);
					

					scoped_set_and_revert(ui.parameters.text_color, rgba(255, 255, 255, 255).scaled_alpha(openness));
					scoped_set_and_revert(ui.parameters.text_font_face_size, 11);
					scoped_set_and_revert(ui.parameters.text_alignment, Text_Alignment::Right);
					scoped_set_and_revert(ui.parameters.center_text_vertically, false);

					Unicode_String binding_string = key_bindings.key_binding_to_string(*binding, frame_allocator);

					int vertical_background_offset = 3;

					Rect binding_background_rect = Rect::make(
						macro_rect.x_right - measure_text_width(binding_string, ui.get_font_face()) - renderer.scaled(14),
						macro_rect.y_bottom + renderer.scaled(vertical_background_offset),
						macro_rect.x_right - 1,
						macro_rect.y_bottom + ui.get_font_face()->line_spacing + renderer.scaled(vertical_background_offset));

					renderer.draw_rect(binding_background_rect, macro_bind_background_color * 1.5);
					renderer.draw_rect_outline(binding_background_rect, rgba(0, 0, 0, 255));


					ui.draw_text(macro_rect.x_right - renderer.scaled(8), macro_rect.y_bottom + renderer.scaled(6), binding_string);
				}
			}

			ui.active_mask_stack.count -= 1;
			ui.set_active_masks_as_renderer_masks();



			rgba delete_accept_button_color = rgba(255, 255, 255, 0);

			// Delete button
			{
				UI_ID delete_button_ui_id = ui_id(i);

				Rect delete_button_rect = macro_rect;
				delete_button_rect.x_left = delete_button_rect.x_right - renderer.scaled(macro_delete_rect_width);


				int macro_delete_line_size_scaled = renderer.scaled(macro_delete_line_size);


				Vector2i tick_0_0 = { 0, 0 };
				Vector2i tick_0_1 = { 0 - macro_delete_line_size_scaled, 0 + macro_delete_line_size_scaled };

				Vector2i tick_1_0 = { 0, 0 };
				Vector2i tick_1_1 = { 0 + macro_delete_line_size_scaled * 2, 0 + macro_delete_line_size_scaled * 2 };

				Vector2i tick_offset = macro_delete_tick_offset * renderer.scaling;
				tick_0_0 += tick_offset;
				tick_0_1 += tick_offset;
				tick_1_0 += tick_offset;
				tick_1_1 += tick_offset;

				if (ui.holding == delete_button_ui_id)
				{
					hsva hsva_color = hsva(sin(uptime) * 360, 0.5, 0.5, 1.0);

					delete_accept_button_color = hsva_color.to_rgba();

					Rect delete_accept_rect = macro_rect;
					delete_accept_rect.x_right = delete_accept_rect.x_left + renderer.scaled(macro_delete_rect_width);

					renderer.draw_rect(delete_accept_rect, delete_accept_button_color);


					Vector2i tick_offset = { delete_accept_rect.center_x(), delete_accept_rect.center_y() };

					Vector2i line_0_0 = tick_0_0 + tick_offset;
					Vector2i line_0_1 = tick_0_1 + tick_offset;
					
					Vector2i line_1_0 = tick_1_0 + tick_offset;
					Vector2i line_1_1 = tick_1_1 + tick_offset;


					renderer.draw_line(
						line_0_0.x, line_0_0.y,
						line_0_1.x, line_0_1.y,
							macro_delete_line_color);

					renderer.draw_line(
						line_1_0.x, line_1_0.y,
						line_1_1.x, line_1_1.y,
							macro_delete_line_color);


					need_to_redraw_next_frame(code_location());
				}



				float old_delete_button_brightness = macro_rect_state->delete_button_brightness;

				if (
					ui.holding == invalid_ui_id && (
						ui.hover == macro_ui_id ||
						ui.hover == macro_button_ui_id ||
						ui.hover == delete_button_ui_id )
					||
					ui.holding == delete_button_ui_id ||
					ui.up      == delete_button_ui_id)
				{
					if (ui.down == delete_button_ui_id)
					{
						macro_rect_state->delete_drag_start = { input.mouse_x, input.mouse_y };
						macro_rect_state->delete_button_brightness = 1.0;
					}

					macro_rect_state->delete_button_brightness = clamp<double>(0.0, 1.0, macro_rect_state->delete_button_brightness + frame_time * macro_delete_brightness_change_speed);

				}
				else
				{
					macro_rect_state->delete_button_brightness = clamp<double>(0, 1, macro_rect_state->delete_button_brightness - frame_time * macro_delete_brightness_change_speed);
				}

				if (old_delete_button_brightness != macro_rect_state->delete_button_brightness)
				{
					need_to_redraw_next_frame(code_location());
				}

				if (macro_rect_state->delete_button_brightness > 0.0)
				{
					float delete_button_state = 0.0;

					if (ui.holding == delete_button_ui_id ||
						ui.up == delete_button_ui_id)
					{
						int max_appendix = macro_rect.width() - delete_button_rect.width();

						int appendix = clamp(-max_appendix, 0, input.mouse_x - macro_rect_state->delete_drag_start.x);

						delete_button_state = inverse_lerp(0, -max_appendix, appendix);

						delete_button_rect.move(appendix, 0);
					}



					rgba delete_button_color = lerp(macro_delete_rect_color, delete_accept_button_color, delete_button_state);

					delete_button_color.scale_alpha(macro_rect_state->delete_button_brightness);

					can_delete = delete_button_state > 0.9999;

					if (can_delete)
					{
						delete_button_color.darken(127);
					}

					// Handling click above with ui.down, because ui.holding will happen in the same frame and we will have junk in macro_rect_state->delete_drag_start.
					ui.button(delete_button_rect, delete_button_color, delete_button_ui_id);

					Rect delete_button_right_rect = delete_button_rect;
					delete_button_right_rect.x_left = delete_button_right_rect.x_right;
					delete_button_right_rect.x_right = macro_rect.x_right;

					renderer.draw_rect(delete_button_right_rect, delete_button_color);


					// Draw cross -> tick
					{
						auto& rect = delete_button_rect;
						
						int center_x = rect.center_x();
						int center_y = rect.center_y();

						Vector2i tick_offset = { center_x, center_y };

						Vector2i cross_0_0 = { center_x - macro_delete_line_size_scaled, center_y - macro_delete_line_size_scaled };
						Vector2i cross_0_1 = { center_x + macro_delete_line_size_scaled, center_y + macro_delete_line_size_scaled };

						Vector2i cross_1_0 = { center_x - macro_delete_line_size_scaled, center_y + macro_delete_line_size_scaled };
						Vector2i cross_1_1 = { center_x + macro_delete_line_size_scaled, center_y - macro_delete_line_size_scaled };


						Vector2i line_0_0 = lerp(cross_0_0, tick_0_0 + tick_offset, delete_button_state);
						Vector2i line_0_1 = lerp(cross_0_1, tick_0_1 + tick_offset, delete_button_state);

						Vector2i line_1_0 = lerp(cross_1_0, tick_1_0 + tick_offset, delete_button_state);
						Vector2i line_1_1 = lerp(cross_1_1, tick_1_1 + tick_offset, delete_button_state);

						renderer.draw_line(
							line_0_0.x, line_0_0.y,
							line_0_1.x, line_0_1.y,
								macro_delete_line_color.scaled_alpha(macro_rect_state->delete_button_brightness));

						renderer.draw_line(
							line_1_0.x, line_1_0.y,
							line_1_1.x, line_1_1.y,
								macro_delete_line_color.scaled_alpha(macro_rect_state->delete_button_brightness));
					}
				}


				if (ui.up == delete_button_ui_id)
				{
					if (can_delete)
					{
						macro_binding_that_we_should_delete = macro_binding;
					}
				}
			}


			renderer.draw_rect_outline(macro_rect, 
				can_delete ? 
					delete_accept_button_color :
					macro_rect_outline_color.scaled_alpha(openness * get_alpha_for_page(state)));


			y -= macro_rect.height() + renderer.scaled(distance_between_macros_in_the_list);
		}
	}

	if (macro_binding_that_we_should_delete)
	{
		if (macro_binding_that_we_should_delete->action.macro_string.data)
		{
			c_allocator.free(macro_binding_that_we_should_delete->action.macro_string.data, code_location());
		}

		key_bindings.bound_actions.fast_remove_pointer(macro_binding_that_we_should_delete);
		key_bindings.save_bindings();

		// key bindings modified so we have to rebuild macro bindings list
		filter_macro_bindings_from_key_bindings(&macro_bindings);

		need_to_redraw_next_frame(code_location());
	}


	// New macro button
	{
		scoped_set_and_revert(ui.parameters.text_font_face_size, 14);


		Rect new_macro_button_rect = Rect::make(
				x + scaled_macro_margin_in_the_list,
				y - renderer.scaled(new_macro_button_height) + scroll_region_result.scroll_from_top - scaled_macro_margin_in_the_list - renderer.scaled(distance_between_macros_in_the_list) -
					renderer.scaled(new_macro_button_offset),
				x + scroll_region_result.view_rect.width() - scaled_macro_margin_in_the_list,
				y + scroll_region_result.scroll_from_top - scaled_macro_margin_in_the_list - renderer.scaled(new_macro_button_offset)); // This is a little too big u know...



		if (ui.button(new_macro_button_rect, U"+", new_macro_color, ui_id(0)))
		{
			Binding_And_Action bound_action = 
			{
				.binding = {
					.keys = Dynamic_Array<Key>::empty()
				},
				.action = {
					.type = Action_Type::Run_A_Macro,
					.macro_string = Unicode_String(U"Sample macro text").copy_with(c_allocator)
				}
			};

			key_bindings.bound_actions.add(bound_action);

			// key_bindings.bound_actions.add may invalidate pointer to macro bindings, so we have to find them again.
			filter_macro_bindings_from_key_bindings(&macro_bindings);

			key_bindings.save_bindings();
		}

		renderer.draw_rect_outline(new_macro_button_rect, macro_rect_outline_color);
	}

	ui.end_scroll_region();



	static int previous_frame_edited_macro = -1;
	if (macro_currently_being_edited != -1)
	{
		defer{
			previous_frame_edited_macro = macro_currently_being_edited;
		};

		if (macro_currently_being_edited < 0 || macro_currently_being_edited >= macro_bindings.count)
		{
			macro_currently_being_edited = -1;
		}
		else
		{

			// renderer.draw_line(macro_editing_rect.x_left, macro_editing_rect.center_y(), edited_macro_rect.x_right, clamp<int>(macros_scroll_region_rect.y_bottom, macros_scroll_region_rect.y_top, edited_macro_rect.center_y()), macro_umbilical_noose_color);

			Binding_And_Action* macro_binding = *macro_bindings[macro_currently_being_edited];

			int macro_key_binding_height = renderer.scaled(56);				

			
			{
				UI_ID key_binding_recorder_ui_id = ui_id(0);

				{
					scoped_set_and_revert(ui.parameters.text_font_face_size, 14);
					scoped_set_and_revert(ui.parameters.enable_button_text_fading, true);


					Unicode_String draw_string;

					if (macro_binding->binding.keys.count)
					{
						draw_string = key_bindings.key_binding_to_string(macro_binding->binding, frame_allocator);
					}
					else
					{
						draw_string = U"Bind";
					}

					Rect key_binding_button_rect = Rect::make(
						macro_editing_rect.x_left,
						macro_editing_rect.y_top - macro_key_binding_height,
						macro_editing_rect.x_right,
						macro_editing_rect.y_top);


					if (ui.button(key_binding_button_rect, draw_string, macro_bind_background_color, ui_id(0)))
					{
						key_bindings.begin_key_binding_recording(key_binding_recorder_ui_id);
					}
				}

				if (key_bindings.recorded_key_binding_ui_id == key_binding_recorder_ui_id)
				{
					if (macro_binding->binding.keys.data)
					{
						c_allocator.free(macro_binding->binding.keys.data, code_location());
					}

					macro_binding->binding.keys = key_bindings.recorded_binding.keys.copy_with(c_allocator);

					key_bindings.save_bindings();
				}

				{
					scoped_set_and_revert(ui.parameters.text_font_face_size, 12);

					// ui.draw_text(macro_mailbox_rect.x_left, macro_mailbox_rect.y_bottom - ui.get_font_face()->size / 2, key_binding_title);
				}

				int macro_delete_button_and_binding_dropdown_width = renderer.scaled(192);
				int macro_delete_button_and_binding_dropdown_actual_width = min(macro_editing_rect.width() / 2 - renderer.scaled(4), macro_delete_button_and_binding_dropdown_width);

				#if 0
				{
					
					renderer.mask_stack.add({
						.rect = delete_button_rect,
						.inversed = false
					});
					renderer.recalculate_mask_buffer();
					defer{
						renderer.mask_stack.count -= 1;
						renderer.recalculate_mask_buffer();
					};

					scoped_set_and_revert(ui.parameters.text_font_face_size, 14);


					if (ui.button(delete_button_rect, U"Delete this macro", delete_macro_button_color, ui_id(macro_currently_being_edited)))
					{
						settings.macros.remove_at_index(macro_currently_being_edited);
						save_settings();
						macro_currently_being_edited = -1;
					}
				}
				#endif
			}


			scoped_set_and_revert(ui.parameters.text_font_face_size, 14);

			Unicode_String& macro_string_ref = macro_binding->action.macro_string;

			Unicode_String macro_text_result;
			if (ui.text_editor(Rect::make(
				macro_editing_rect.x_left,
				macro_editing_rect.y_bottom,
				macro_editing_rect.x_right,
				macro_editing_rect.y_top - macro_key_binding_height - renderer.scaled(12)),

				macro_string_ref, &macro_text_result, frame_allocator, ui_id(macro_currently_being_edited), true, true))
			{
				if (macro_string_ref != macro_text_result)
				{
					if (macro_string_ref.data)
					{
						c_allocator.free(macro_string_ref.data, code_location());
					}
					macro_string_ref = macro_text_result.copy_with(c_allocator);
					
					key_bindings.save_bindings();
				}
			}
		}
	}
}

void Settings_Screen::draw_ui_page(float state)
{
	// @TODO: wrap all of this in scroll region


	Color_Scaling_Context color_ctx = scale_and_save_colors(get_alpha_for_page(state));	
	defer { 
		color_ctx.restore_colors();
	};
	

	Rect page_rect = get_page_rect(state);

	int x = page_rect.x_left;
	int y = page_rect.y_top;

	// Font
	{
		Dynamic_Array<Unicode_String> dropdown_options = make_array<Unicode_String>(32, frame_allocator);


		int selected_font_index = -1;
		for (auto iter = font_storage.fonts.iterate(); Font* font = iter.next();)
		{
			dropdown_options.add(font->name);
			if (font->name == settings.text_font_face)
			{
				selected_font_index = iter.index;
			}
		}


		int selected;
		if (ui.dropdown(Rect::make(x, y - renderer.scaled(48), x + renderer.scaled(384), y), selected_font_index, dropdown_options, &selected, ui_id(0)))
		{
			typer_ui.invalidate_after(-1);
			settings.text_font_face = *dropdown_options[selected];
			save_settings();
		}

	}
	
	// Font size
	{
		scoped_set_and_revert(y, y + renderer.scaled(reverse_openness_scaled(56)));

		x += renderer.scaled(384 + 48);

		Unicode_String new_font_size_string;
		if (ui.text_editor(Rect::make(x, y - renderer.scaled(48), x + renderer.scaled(128), y), to_string(settings.text_font_face_size, frame_allocator).to_unicode_string(frame_allocator), &new_font_size_string, frame_allocator, ui_id(0), false, false))
		{
			new_font_size_string.trim();

			int new_font_size;
			if (parse_number(new_font_size_string.to_utf8_but_ascii(frame_allocator), &new_font_size))
			{
				if (new_font_size >= 4 && new_font_size <= 120)
				{
					settings.text_font_face_size = new_font_size;
					typer_ui.need_to_keep_scroll_state_this_frame = true;
					save_settings();
				}
				else
				{
					log(ctx.logger, U"Font size % must be (>= 4 && <= 120)", new_font_size);
				}
			}
			else
			{
				log(ctx.logger, U"Wrong font size: %", new_font_size);
			}

			typer_ui.invalidate_after(-1);
		}
	}
}

void Settings_Screen::draw_bindings_page(float state)
{
	// @TODO: wrap all of this in scroll region

	Color_Scaling_Context color_ctx = scale_and_save_colors(get_alpha_for_page(state));	
	defer { 
		color_ctx.restore_colors();
	};

	Rect full_page_rect = get_full_page_rect(state);
	Rect page_rect = get_page_rect(state);

	
	struct Action_Info
	{
		Action_Type action_type;

		Dynamic_Array<Key_Binding> bindings;

		Rect rect;
	};


	Dynamic_Array<Action_Info> action_infos = make_array<Action_Info>(32, frame_allocator);
	{
		Reflection::Enum_Type* type = (decltype(type)) Reflection::type_of<Action_Type>();

		for (Reflection::Enum_Value& enum_value: type->values)
		{
			if (enum_value.value.s32_value == (s32) Action_Type::Run_A_Macro) continue;

			if (Reflection::contains_tag(enum_value.tags, ACTION_TYPE_NAME_TAG))
			{
				action_infos.add({
					.action_type = (Action_Type) enum_value.value.s32_value,
					.bindings = make_array<Key_Binding>(4, frame_allocator),
				});
			}		
		}
	}




	for (Binding_And_Action& binding: key_bindings.bound_actions)
	{
		if (binding.action.type == Action_Type::Run_A_Macro) continue;

		Action_Info* action_info = find(action_infos, [&](auto item) {
			return item.action_type == binding.action.type;
		});

		assert(action_info);

		action_info->bindings.add(binding.binding);
	}


	int total_height = 0;


	int x_left  = page_rect.x_left;
	int x_right = page_rect.x_right;

	int y_top = full_page_rect.y_top;


	total_height += renderer.scaled(bindings_list_top_margin);

	for (auto& action_info: action_infos)
	{
		int binding_height = 0;
		binding_height += renderer.scaled(binding_action_type_height);

		if (action_info.bindings.count > 0)
		{
			binding_height += renderer.scaled(binding_action_type_additional_binding_height) * (action_info.bindings.count - 1);
		}

		action_info.rect = Rect::make(x_left, y_top - binding_height, x_right, y_top);
		action_info.rect.move(0, -total_height);

		total_height += binding_height;

		total_height += renderer.scaled(distance_between_bindings);	
	}


	total_height += renderer.scaled(bindings_list_bottom_margin);


	scoped_set_and_revert(ui.parameters.scroll_region_background, rgba(0, 0, 0, 0));

	Scroll_Region_Result scroll_region_result = ui.scroll_region(get_full_page_rect(state), total_height, 0, false, ui_id(0));
	defer { ui.end_scroll_region(); };

	int right_shrink_delta = full_page_rect.x_right - scroll_region_result.view_rect.x_right;
	for (auto& action_info: action_infos)
	{
		action_info.rect.move(0, scroll_region_result.scroll_from_top);
		action_info.rect.shrink(0, 0, right_shrink_delta, 0);
	}


	scoped_set_and_revert(ui.parameters.text_font_face_size, 12);
	scoped_set_and_revert(ui.parameters.text_alignment, Text_Alignment::Left);
	scoped_set_and_revert(ui.parameters.center_text_vertically, true);

	for (auto& action_info: action_infos)
	{
		renderer.draw_rect(action_info.rect, rgba(0, 100, 0, 255).scaled_alpha(openness));

		String action_name = "[Unknown action]";

		Reflection::Enum_Value enum_value;
		if (Reflection::get_enum_value_from_value(action_info.action_type, &enum_value))
		{
			auto tag = Reflection::contains_tag(enum_value.tags, ACTION_TYPE_NAME_TAG);

			if (tag && tag->value)
			{
				action_name = *((String*) tag->value);
			}
		}


		ui.draw_text(
			action_info.rect.x_left + renderer.scaled(14),
			action_info.rect.y_top - renderer.scaled(binding_action_type_height) / 2,
			action_name.to_unicode_string(frame_allocator));


		int binding_y_center = action_info.rect.y_top - renderer.scaled(binding_action_type_height) / 2;

		scoped_set_and_revert(ui.parameters.text_font_face_size, 10);

		for (auto& binding: action_info.bindings)
		{
			ui.draw_text(action_info.rect.center_x(), binding_y_center, key_bindings.key_binding_to_string(binding, frame_allocator));

			binding_y_center -= renderer.scaled(binding_action_type_additional_binding_height);
		}
	}
}

Color_Scaling_Context scale_and_save_colors(float scale)
{
	// Battle testing Reflection.h
	Dynamic_Array<Old_Color_Value> old_color_values = make_array<Old_Color_Value>(32, frame_allocator);

	auto remember_and_scale_color = [&](rgba& color)
	{
		old_color_values.add({
			.ptr = &color,
			.value = color
		});

		color.a = color.a * scale;
	};

	auto save_and_scale_colors_of_struct = [&](auto _struct)
	{
		static_assert(std::is_pointer_v<decltype(_struct)>);

		auto type = (Reflection::Struct_Type*) Reflection::type_of<
			std::remove_reference_t<
				decltype(*_struct)>
				>();

		for (auto it = type->iterate_members(frame_allocator); auto member = it.next();)
		{
			if (member->type != Reflection::type_of<rgba>()) continue;

			if (Reflection::contains_tag(member->tags, "SettingsScalableColor"))
			{
				rgba* ptr = (rgba*) add_bytes_to_pointer(_struct, member->offset);

				remember_and_scale_color(*ptr);
			}
		}
	};

	save_and_scale_colors_of_struct(&settings_screen);
	save_and_scale_colors_of_struct(&ui.parameters);

	return {
		.old_color_values = old_color_values,
	};
}