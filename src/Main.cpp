#include "Main.h"

#include "b_lib/Dynamic_Array.h"
#include "b_lib/File.h"
#include "b_lib/Threading.h"
#include "b_lib/Font.h"
#include "b_lib/Tokenizer.h"


#include "UI.h"

#include "Input.h"
#include "Output_Processor.h"
#include "Input_Processor.h"
#include "Key_Bindings.h"
#include "Terminal_IO.h"
#include "Terminal.h"



#include "ASCII_Sequence_Stuff.h"


#include <locale.h>
#include <fcntl.h>

#include <cctype>




#if OS_WINDOWS
#include <io.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Shell32.lib")



#include <Dbghelp.h>
#pragma comment(lib, "Dbghelp.lib")

#include <dwmapi.h>
#pragma comment(lib, "Dwmapi.lib")

#endif



#if IS_POSIX
#include <sys/time.h>
#include <sys/ioctl.h>
#include <cpuid.h>
#endif

#include <immintrin.h>



#include "Python_Interp.h"
#include "Python_Debugger.h"


#include "Renderer_Vulkan.h"

#include "Ease_Functions.h"



void Typer::init()
{
	ZoneScoped;

	time_measurer = create_time_measurer();

	running_macro = build_string<char32_t>(c_allocator);

	defer{ frame_allocator.reset(); };



	typer_directory = get_executable_directory(c_allocator);


	// Load assets
	{

	 	// @TODO: add fonts for other OSs
		#if !OS_WINDOWS
		static_assert(false);
		#endif
		font_storage.init(c_allocator, make_array<Unicode_String>(c_allocator, {
			U"C:/Windows/Fonts",
			path_concat(c_allocator, typer_directory, Unicode_String(U"fonts"))
		}));



		auto textures_folder = path_concat(frame_allocator, typer_directory, Unicode_String(U"assets/textures"));

		auto iter = iterate_files(textures_folder, frame_allocator);

		if (iter.succeeded_to_open())
		{
			while (iter.next().is_not_empty())
			{
				ZoneScopedN("load texture");

				if (iter.current.ends_with(U".png"))
				{
					Unicode_String file_path = path_concat(frame_allocator, textures_folder, iter.current);
					bool result = renderer.load_texture(file_path);
					if (!result)
					{
						Log(U"Failed to load texture at path: %", file_path);
					}
				}
			}
		}		
	}



	typer_ui.init();


	load_settings();

	settings_screen.init();

#if OS_WINDOWS
	{
		ZoneScopedN("alloc_console_finished_semaphore.wait_and_decrement()");
		alloc_console_finished_semaphore.wait_and_decrement();
	}
#endif

#if OS_WINDOWS
	{
		ZoneScopedN("cmd /c chcp 65001");

		// SetConsoleOutputCP(65001);
		// SetConsoleCP(65001);

		PROCESS_INFORMATION pi = {};

		STARTUPINFOW si = {};
		si.cb = sizeof(STARTUPINFOW); 

		wchar_t cmd[MAX_PATH];
		size_t nSize = _countof(cmd);
		_wgetenv_s(&nSize, cmd, L"COMSPEC");

		CreateProcessW(cmd, L" /c chcp 65001", NULL, NULL, false, 0, NULL, NULL, &si, &pi);
		

		// CreateProcessW(L"", L"cmd /c chcp 65001", NULL, NULL, false, CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi);
	}
#endif

	key_bindings.init();

	terminal.init();

	output_processor.init();

	terminal_io.init();


	user_cursor = terminal.characters.count;
	
	prepare_for_running_command();

	python.init();
	python.import_user_main();
}



bool is_character_at_index_is_user_input(u64 index)
{
	assert(terminal.characters_mutex.is_being_locked());

	Terminal_Character* character = terminal.characters.get_or_null(index);
	if (character && character->character_type == TERMINAL_CHARACTER_TYPE_USER_INPUT)
	{
		return true;
	}
	else
	{
		Terminal_Character* character_before_cursor = terminal.characters.get_or_null(index - 1);

		if (character_before_cursor && character_before_cursor->character_type == TERMINAL_CHARACTER_TYPE_USER_INPUT)
		{
			return true;
		}
	}

	if (index == terminal.get_characters_count()) // At the end of user input.
	{
		return true;
	}

	return false;
}




bool process_user_input()
{
	assert(terminal.characters_mutex.is_being_locked());


	Unicode_String user_input = terminal.copy_user_input(frame_allocator);

	// Mark user input as old
	{
		assert(user_input.length == terminal.get_user_input_length());

		for (s64 i = terminal.get_characters_count() - user_input.length; i < terminal.get_characters_count(); i++)
		{
			assert(terminal.characters[i]->character_type == TERMINAL_CHARACTER_TYPE_USER_INPUT);

			terminal.characters[i]->character_type = TERMINAL_CHARACTER_TYPE_OLD_USER_INPUT;
		}
	}

	user_input.trim();

	typer_ui.invalidate_after(terminal.get_characters_count());


	terminal.append_non_process(terminal.get_characters_count(), U"\n", TERMINAL_CHARACTER_TYPE_NONE);


	prepare_for_running_command();

	if (!python.run(user_input))
	{
		cleanup_after_command();
	}

	return true;
}

void Typer::tell_python_to_run_prompt()
{
	if (!python.is_running)
	{
		prepare_for_running_command();
	}

	python.run_only_prompt();
}

bool is_typing_command()
{
	if (python.is_running_in_non_limited_context()) return false;

	if (selection_length) return false;

	Scoped_Lock lock(terminal.characters_mutex);


	int user_input_length = terminal.get_user_input_length();

	int characters_count = terminal.get_characters_count();

	return user_cursor >= (characters_count - user_input_length) &&
		  user_cursor <=  characters_count;
}

void Typer::terminate()
{
#if LOG_PROCESS_OUTPUT_TO_HUYE_TXT
	huye_file.flush();
#endif

	// @TODO: finish terminal_io
}

void Typer::do_frame()
{
	defer { FrameMark; };

	defer{ frame_index += 1; };

	defer {
		if (frame_index == 0)
		{
			log(ctx.logger, U"Time from start to first frame done: % ms", time_to_from_start_to_first_frame_done.ms_elapsed_double());
		}
	};


	// Do frame time measurement
	{
		frame_allocator.reset();

		frame_time_us = time_measurer.us_elapsed();
		frame_time_ms = frame_time_us / 1000.0;
		frame_time    = frame_time_us / 1000'000.0;
		time_measurer.reset();

		uptime += frame_time;
	}

	// Do fps calculation
	{
		if (frame_index % fps_update_frames_count == 0)
		{
			frame_time_accum = 0;
			frame_time_accum_count = 0;
		}


		frame_time_accum += frame_time;
		frame_time_accum_count += 1;

		fps = 1 / (frame_time_accum / frame_time_accum_count);
	}


	desired_cursor_type = Cursor_Type::Normal;


 	input.pre_frame();
	defer { input.post_frame(); };
	
 	key_bindings.do_frame();


	// Maybe we want to redraw
	{
		redraw_current_frame = (next_redraw_frame == frame_index);

		if (non_main_thread_told_to_redraw)
		{
			redraw_current_frame = true;
			atomic_set<b32>(&non_main_thread_told_to_redraw, false);
		}
	}	


	if (key_bindings.is_action_type_triggered(Action_Type::Exit))
	{
    #if OS_WINDOWS
		PostQuitMessage(0);
    #elif OS_LINUX
        terminate();
        exit(0);
    #endif
		return;
	}


	if (python.is_running)
	{
		python.supervise_running_interp();
	}
	

	if (!python.is_running)
	{
		if (running_macro.length)
		{
			Unicode_String str = take_until(running_macro.get_string(), U'\n').copy_with(frame_allocator);
			running_macro.remove(0, str.length);

			terminal.set_user_input(str);

			if (running_macro.length) // This means that there are \n left.
			{
				running_macro.remove(0, 1);
				process_user_input();
			}
		}
	}



#if DEBUG
	if (input.is_key_down(Key::F10))
	{
		enable_slow_output_processing = !enable_slow_output_processing;
	}

#endif

#if DEBUG
	if (input.is_key_down(Key::F12))
	{
		vulkan_memory_allocator.dump_allocations();
	}
#endif



	if (window_height == 0 || window_width == 0)
	{
		return;
	}




#if OS_WINDOWS
	windows.window_dpi = GetDpiForWindow(windows.hwnd);
	windows.window_scaling = float(windows.window_dpi) / 96.0;

    auto old_renderer_scaling = renderer.scaling;

	if (key_bindings.is_action_type_triggered(Action_Type::Toggle_Usage_Of_DPI))
	{
		renderer.use_dpi_scaling = !renderer.use_dpi_scaling;
	}

	renderer.scaling = renderer.use_dpi_scaling ? windows.window_scaling : 1.0;
	// @Hack ??? if we don't do this here scrollbar code will recalculate_renderer_lines,
	//    and that will ruin result of saving scroll state.
	typer_ui.current_scrollbar_width = renderer.scaled(typer_ui.able_to_scroll ? typer_ui.scrollbar_width : 0);

	if (old_renderer_scaling != renderer.scaling)
	{
		window_size_changed = true;
		typer_ui.need_to_keep_scroll_state_this_frame = true;

		// Can't call SetWindowPos directly, cause it will lead to infinite loop of toggling DPI usage.
		have_to_inform_window_about_changes = true;
	}

#elif OS_LINUX
    // @TODO: implement
#endif



	if (window_size_changed || (renderer.use_vulkan && vk.swapchain_is_dead))
	{
		if (window_width != renderer.width || window_height != renderer.height)
		{
			typer_ui.invalidate_after(-1);
		}

		renderer.resize(window_width, window_height);
		window_size_changed = false;
		redraw_current_frame = true;
	}

	ui.pre_frame();
	defer { ui.post_frame(); };


	if ((ui.holding != invalid_ui_id) ||
		(input.mouse_x_delta || input.mouse_y_delta) ||
		input.mouse_wheel_delta ||
		input.nodes.count ||
		input.pressed_keys.count)
	{
		redraw_current_frame = true;
	}

#if OS_DARWIN
	// @TODO: remove this, used only for debugging.
	redraw_current_frame = true;
#endif

	// Setting redraw_current_frame after this point makes no sense.


	renderer.frame_begin();
	defer { renderer.frame_end(); } ;


	terminal.update_size();

	typer_ui.do_frame();
}

void Typer::do_terminal_frame()
{
	ZoneScopedNC("do_terminal_frame", 0xff0000);
	
	Scoped_Lock buffer_lock_xxx(terminal.characters_mutex);
	Scoped_Lock terminal_lock(terminal_mutex); // Locking terminal must occur after buffer is locked to prevent deadlock.


	defer {
		buffer_changed = false;
	};

	input_processor.process_input();


	scoped_ui_active_mask(Rect::make(0, 0, renderer.width, typer_ui.y_top), false);

	if (is_mouse_over_terminal())
	{
		ui.im_hovering(typer_ui.terminal_ui_id);
	}

	if (ui.down != invalid_ui_id)
	{
		typer_ui.terminal_focused = (ui.down == typer_ui.terminal_ui_id);
	}


	// Handle zooming
	{
		if (is_mouse_over_terminal())
		{
			int zoom_in  = key_bindings.get_action_type_trigger_count(Action_Type::Zoom_In);
			int zoom_out = key_bindings.get_action_type_trigger_count(Action_Type::Zoom_Out);

			int font_face_size_delta = zoom_in - zoom_out;

			if (font_face_size_delta)
			{
				int old_size = settings.text_font_face_size;

				settings.text_font_face_size = clamp(typer_ui.min_typer_font_face_size, typer_ui.max_typer_font_face_size, settings.text_font_face_size + font_face_size_delta);

				if (old_size != settings.text_font_face_size)
				{
					typer_ui.invalidate_after(-1);

					typer_ui.need_to_keep_scroll_state_this_frame = true;

					save_settings();
				}
			}
		}
	}

	typer_ui.recalculate_renderer_lines();



	if (typer_ui.drag == Drag::Text_Selection)
	{
		s64 new_user_cursor = typer_ui.find_cursor_position_for_mouse();

		selection_length -= new_user_cursor - user_cursor;
		user_cursor = new_user_cursor;

		float speed = 10.0;
		float delta = 0.0;


		float font_face_float = (float) typer_ui.typer_font_face->line_spacing;

		int top_border = typer_ui.y_top - renderer.scaled(out_of_border_scrolling_threshold);
		int bottom_border = renderer.scaled(out_of_border_scrolling_threshold);

		if (input.mouse_y < bottom_border)
		{
			speed *= float(abs(bottom_border - input.mouse_y)) * 0.1;

			delta = font_face_float * speed * frame_time;
		}
		else if (input.mouse_y > top_border)
		{
			speed *= float(input.mouse_y - top_border) * 0.1;

			delta = font_face_float * speed * frame_time;
			delta *= -1.0;
		}


		typer_ui.mouse_offscreen_scroll_target += delta;

		if (abs(typer_ui.mouse_offscreen_scroll_target) > 1.0)
		{
			float move_delta = round(typer_ui.mouse_offscreen_scroll_target);

			if (move_delta < 0)
			{
				typer_ui.was_screen_at_the_most_bottom_last_frame = false;
			}

			typer_ui.mouse_offscreen_scroll_target -= move_delta;
			typer_ui.scroll_top_pixels_offset += (int) move_delta;
		}
	}



	if (typer_ui.drag != Drag::Scrollbar && typer_ui.terminal_focused)
	{
		for (Input_Node node : input.nodes)
		{
			if (node.input_type == Input_Type::Key && node.key_action == Key_Action::Down)
			{
				// typer_ui.scroll_top_pixels_offset is going to be clamped after this in do_terminal_scrollbar

				if (node.key == Key::Page_Down)
				{
					typer_ui.scroll_top_pixels_offset += renderer.height;
				}
				else if (node.key == Key::Page_Up)
				{
					typer_ui.scroll_top_pixels_offset -= renderer.height;
					typer_ui.was_screen_at_the_most_bottom_last_frame = false;
				}
			}
		}
	}



	{
		ZoneScopedNC("do_terminal_scrollbar", 0xff0000);

		int overall_height = (typer_ui.renderer_lines.count) * typer_ui.typer_font_face->line_spacing;

		int view_height = typer_ui.get_terminal_view_height();

		int height_with_appendix_at_the_bottom = overall_height + typer_ui.get_scroll_bottom_appendix_height(view_height);
		int scroll_range_height = clamp(0, s32_max, height_with_appendix_at_the_bottom - view_height);



		typer_ui.able_to_scroll = overall_height > view_height - typer_ui.get_scroll_bottom_appendix_height(view_height);

		if (python.is_running)
		{
			if (!console_settings.scrollbar_enabled)
			{
				typer_ui.able_to_scroll = false;
			}
		}

		if (!typer_ui.able_to_scroll)
		{
			if (typer_ui.drag == Drag::Scrollbar)
			{
				typer_ui.drag = Drag::None;
			}

			typer_ui.scroll_top_pixels_offset = 0;
			scroll_range_height = 0;
		}


		int old_scrollbar_width = typer_ui.current_scrollbar_width;
		typer_ui.current_scrollbar_width = renderer.scaled(typer_ui.able_to_scroll ? typer_ui.scrollbar_width : 0);
		if (typer_ui.current_scrollbar_width != old_scrollbar_width)
		{
			typer_ui.invalidate_after(-1);

			terminal.update_size();
		}

		int grip_height;

		float overall_height_to_view_height = float(height_with_appendix_at_the_bottom) / float(view_height);
		if (overall_height_to_view_height > 10.0f)
			grip_height = renderer.scaled(48);
		else
			grip_height = float(view_height) / (float(height_with_appendix_at_the_bottom) / float(view_height));

		int max_grip_y = view_height - grip_height / 2;
		int min_grip_y = grip_height / 2;

		int scrollgrip_offset_max = view_height - grip_height;

		int scrollgrip_offset = lerp(0, scrollgrip_offset_max, float(typer_ui.scroll_top_pixels_offset) / float(scroll_range_height));

		int scrollbar_left_x = 0 + get_active_width() - typer_ui.current_scrollbar_width;


		Rect scrollbar_rect = Rect::make(scrollbar_left_x, typer_ui.current_bottom_bar_height, scrollbar_left_x + typer_ui.current_scrollbar_width, typer_ui.y_top);

		Rect scrollgrip_rect;
		auto update_scrollgrip_rect = [&]()
		{
			scrollgrip_rect = Rect::make(scrollbar_left_x, scrollbar_rect.y_top - grip_height - scrollgrip_offset, scrollbar_left_x + typer_ui.current_scrollbar_width, scrollbar_rect.y_top - scrollgrip_offset);
		};

		update_scrollgrip_rect();



		typer_ui.scroll_top_pixels_offset = clamp(0, scroll_range_height, typer_ui.scroll_top_pixels_offset); // :SameCode:typer_ui.scroll_top_pixels_offset
		scrollgrip_offset = lerp(0, scrollgrip_offset_max, float(typer_ui.scroll_top_pixels_offset) / float(scroll_range_height));
		update_scrollgrip_rect();


		if (typer_ui.drag == Drag::Scrollbar)
		{
			int y_delta = input.mouse_y - typer_ui.drag_start_y;

			if (input.mouse_x < get_active_width() - typer_ui.current_scrollbar_width)
			{
				double ma = double(scrollgrip_offset) / double(scrollgrip_offset_max) * double(scroll_range_height);

				int view_rect_width = (get_active_width() - typer_ui.current_scrollbar_width);

				auto ease = [](double x) -> double
				{
					 return x == 0 ? 0 : pow(2, 10 * x - 10);
				};

				ma = double(clamp(0, scrollgrip_offset_max, typer_ui.scrollgrip_offset_drag_start - y_delta)) / double(scrollgrip_offset_max) * double(scroll_range_height);


				double scale      = clamp<double>(0, 1, inverse_lerp(view_rect_width - view_rect_width / 4,     0, view_rect_width - input.mouse_x));
				double smol_scale = clamp<double>(0, 1, inverse_lerp(view_rect_width / 2, 0, view_rect_width - input.mouse_x));


				double mb = lerp(1.0, double(scroll_range_height) / double(renderer.height), 
					ease(scale)) * -input.mouse_y_delta;

				// ma = typer_ui.scroll_top_pixels_offset;

				//typer_ui.scroll_top_pixels_offset = clamp<int>(0, scroll_range_height, ma + mb);

				typer_ui.scroll_top_pixels_offset = clamp<int>(0, scroll_range_height, typer_ui.scroll_top_pixels_offset + mb);
				double pixels_buffer = lerp(0.0, double(scroll_range_height), double(1 - smol_scale));
				typer_ui.scroll_top_pixels_offset = clamp<int>(ma - pixels_buffer, ma + pixels_buffer, typer_ui.scroll_top_pixels_offset);

				scrollgrip_offset = lerp(0.0, double(scrollgrip_offset_max), double(typer_ui.scroll_top_pixels_offset) / double(scroll_range_height)); // :SameCode:typer_ui.scroll_top_pixels_offset
			}
			else
			{

				scrollgrip_offset = clamp(0, scrollgrip_offset_max, typer_ui.scrollgrip_offset_drag_start - y_delta);

				if (input.mouse_y_delta)
				{
					typer_ui.scroll_top_pixels_offset = int(double(scrollgrip_offset) / double(scrollgrip_offset_max) * double(scroll_range_height));
				}
			}

			

			update_scrollgrip_rect();

			need_to_redraw_next_frame(code_location());
		}
		else
		{
			if (is_mouse_over_terminal() && typer_ui.able_to_scroll && input.mouse_wheel_delta && !input.is_key_down_or_held(Key::Any_Control))
			{
				const int scroll_speed = 2;

				typer_ui.scroll_top_pixels_offset = clamp(0, scroll_range_height, typer_ui.scroll_top_pixels_offset + (typer_ui.typer_font_face->line_spacing * input.mouse_wheel_delta * scroll_speed));

				scrollgrip_offset = lerp(0.0, double(scrollgrip_offset_max), double(typer_ui.scroll_top_pixels_offset) / double(scroll_range_height)); // :SameCode:typer_ui.scroll_top_pixels_offset

				update_scrollgrip_rect();

				need_to_redraw_next_frame(code_location());
			}

			if (input.is_key_down(Key::LMB))
			{
				#if OS_LINUX
				if (is_x11_window_maximized || input.mouse_x <= get_active_width() - window_resize_border_size)
				{
				#endif
				if (scrollgrip_rect.is_point_inside(input.mouse_x, input.mouse_y))
				{
					typer_ui.drag = Drag::Scrollbar;
					typer_ui.drag_start_x = input.mouse_x;
					typer_ui.drag_start_y = input.mouse_y;
					typer_ui.scrollgrip_offset_drag_start = scrollgrip_offset;

					need_to_redraw_next_frame(code_location());
				}
				else if (scrollbar_rect.is_point_inside(input.mouse_x, input.mouse_y))
				{
					int direction = input.mouse_y > scrollgrip_rect.center_y() ? -1 : 1;

					scrollgrip_offset = clamp(0, scrollgrip_offset_max, scrollgrip_offset + direction * scrollgrip_rect.height() / 2);

					typer_ui.scroll_top_pixels_offset = int(float(scrollgrip_offset) / float(scrollgrip_offset_max) * float(scroll_range_height));

					update_scrollgrip_rect();

					need_to_redraw_next_frame(code_location());

					if (typer_ui.scroll_top_pixels_offset != scroll_range_height)
					{
						typer_ui.was_screen_at_the_most_bottom_last_frame = false;
					}
				}
				#if OS_LINUX
				}
				#endif
			}
		}

		if (typer_ui.drag != Drag::Scrollbar && !input.mouse_wheel_delta && typer_ui.able_to_scroll)
		{
			if (typer_ui.was_screen_at_the_most_bottom_last_frame && settings.keep_scrolling_to_the_bottom_if_already_there && (!python.is_running || console_settings.auto_scroll))
			{
				int old_scroll_top_pixels_offset = typer_ui.scroll_top_pixels_offset;

				typer_ui.scroll_top_pixels_offset = scroll_range_height;
				scrollgrip_offset = scrollgrip_offset_max;

				if (old_scroll_top_pixels_offset != typer_ui.scroll_top_pixels_offset)
				{
					update_scrollgrip_rect();

					need_to_redraw_next_frame(code_location());
				}
			}
		}

		// Scroll to the cursor if user did any input and the cursor is not in the screen boundaries.
		{
			bool input_during_this_frame_wants_us_to_scroll_to_cursor = false;

			for (auto input_node : input.nodes)
			{
				if (input_node.input_type == Input_Type::Char)
				{
					input_during_this_frame_wants_us_to_scroll_to_cursor = true;
					break;
				}

				if (input_node.input_type == Input_Type::Key)
				{
					switch (input_node.key)
					{
						case Key::Up_Arrow:
						case Key::Down_Arrow:
						case Key::Left_Arrow:
						case Key::Right_Arrow:
						{
							input_during_this_frame_wants_us_to_scroll_to_cursor = true;
						}
						break;

						case Key::Backspace:
						case Key::Delete:
						{
							if (python.is_running)
							{
								if (user_cursor >= terminal.get_characters_count() - terminal.get_user_input_length())
								{
									input_during_this_frame_wants_us_to_scroll_to_cursor = true;
								}
							}
						}
						break;
					}

					if (input_during_this_frame_wants_us_to_scroll_to_cursor) break;
				}
			}

			if (input_during_this_frame_wants_us_to_scroll_to_cursor && !typer_ui.is_search_bar_focused)
			{
				// @TODO: decide
				if (!python.is_running)
				{
					typer_ui.scroll_to_char(user_cursor);
				}
			}
		}


		typer_ui.was_screen_at_the_most_bottom_last_frame = typer_ui.scroll_top_pixels_offset == scroll_range_height;


		// Draw scrollbar.
		if (typer_ui.current_scrollbar_width)
		{
			rgba scrollgrip_color = settings_screen.terminal_scrollbar_color;
			if (typer_ui.drag == Drag::Scrollbar)
			{
				scrollgrip_color.darken(127);
			}
			else if (scrollgrip_rect.is_point_inside(input.mouse_x, input.mouse_y))
			{
				scrollgrip_color.darken(192);
			}

			renderer.draw_rect(scrollbar_rect, settings_screen.terminal_scrollbar_background_color);
			renderer.draw_rect(scrollgrip_rect, scrollgrip_color);

			// renderer.draw_rect_with_alpha_fade(Rect::make(scrollbar_rect.x_left - renderer.scaled(8), scrollbar_rect.y_bottom, scrollbar_rect.x_left, scrollbar_rect.y_top), settings_screen.terminal_scrollbar_background_color, 0, 255);

			renderer.draw_rect_outline(scrollgrip_rect.shrinked_uniform(2 * renderer.scaling), rgba(255, 255, 255, 255));

			// renderer.draw_line(scrollgrip_rect.center_x(), scrollgrip_rect.y_top, scrollgrip_rect.x_left, scrollgrip_rect.y_top - (scrollgrip_rect.height() / 4), settings_screen.terminal_scrollbar_background_color);
			// renderer.draw_line(scrollgrip_rect.x_right, scrollgrip_rect.y_bottom, scrollgrip_rect.x_left + scrollgrip_rect.width() / 4, scrollgrip_rect.y_top - scrollgrip_rect.height() / 8, settings_screen.terminal_scrollbar_background_color);
		}
	}

	if (input.is_key_down(Key::LMB) && input.mouse_y < typer_ui.y_top)
	{
		if (typer_ui.drag == Drag::None && input.mouse_x < get_active_width() - typer_ui.current_scrollbar_width && input.mouse_y > typer_ui.current_bottom_bar_height)
		{
			typer_ui.desired_precursor_pixels = -1;
			
			typer_ui.drag = Drag::Text_Selection;

			s64 cursor_position = typer_ui.find_cursor_position_for_mouse();


			if (input.is_key_down_or_held(Key::Any_Shift))
			{
				selection_length -= cursor_position - user_cursor;
				user_cursor = cursor_position;
			}
			else
			{
			#if 0
				s64 exact_cursor_position = find_exact_cursor_position_for_mouse();

				if (exact_cursor_position == cursor_position && !input.is_key_down_or_held(Key::Any_Alt))
				{
					Terminal_Character sc = *typerbuffer[exact_cursor_position];

					if (sc.flags & SEMANTIC_CHAR_INTERACTIVE)
					{
						// This lock guarantees write_string won't be performed by other threads.
						Scoped_Lock lock(terminal_io.data_to_write_mutex);

						terminal_io.write_string("\x1b[]");
						terminal_io.write_string(to_string(sc.interactive_id, frame_allocator));
						terminal_io.write_string("\x07");

						Log(U"Send interactive id : %", sc.interactive_id);

						typer_ui.drag = Drag::None; // You can't click interactive character and start selecting text simultaniously

					}
				}
			#endif


				selection_length = 0;
				user_cursor = cursor_position;
			}
		}
	}


	if (input.is_key_up(Key::LMB))
	{
		typer_ui.drag = Drag::None;
	}



	// Lock for drawing and performing search
	Scoped_Lock characters_lock(terminal.characters_mutex);



	// This exists for optimization reasons.
	//    We don't want to look for each found_entry for every character.
	//    So we look for the next entry only.
	//    If we drew all the characters in that entry we move to the next entry. 
	int next_found_entry_to_look_for = 0;




	// @TODO: make properly moved user_cursor
	{
		user_cursor      = clamp<s64>(0, terminal.get_characters_count(), user_cursor);
		selection_length = clamp<s64>(0, terminal.get_characters_count(), user_cursor + selection_length) - user_cursor;
	}


	s64 user_cursor_line = typer_ui.find_to_which_renderer_line_position_belongs(user_cursor);

	if (user_cursor_line == -1)
	{
		assert(false);
	}


	s64 process_caret_line = -1; // Will prevent process caret drawing if -1
	if (python.is_running)
	{
		process_caret_line = typer_ui.find_to_which_renderer_line_position_belongs(terminal.process_caret);
	}


	u64 selection_start = min(user_cursor, user_cursor + selection_length);
	u64 selection_end   = max(user_cursor, user_cursor + selection_length);


	bool did_set_user_cursor_rect = false;
	bool did_set_process_caret_rect = false;

	Rect user_cursor_rect;
	Rect process_caret_rect;

	auto set_user_cursor_rect = [&](Rect rect)
	{
		user_cursor_rect = rect;
		did_set_user_cursor_rect = true;
	};

	auto set_process_caret_rect = [&](Rect rect)
	{
		process_caret_rect = rect;
		did_set_process_caret_rect = true;
	};


	scoped_set_and_revert(typer_ui.user_cursor_color,   typer_ui.user_cursor_color  .darkened(typer_ui.terminal_focused ? 255 : 125));
	scoped_set_and_revert(typer_ui.process_caret_color, typer_ui.process_caret_color.darkened(typer_ui.terminal_focused ? 255 : 125));

	struct Rect_And_Color
	{
		Rect rect;
		rgba color;
	};


	if (redraw_current_frame)
	{
		ZoneScopedN("Redraw buffer text");

		int y = typer_ui.y_top - typer_ui.typer_font_face->line_spacing + typer_ui.scroll_top_pixels_offset;

		// @TODO:
		// @Performance: do not iterate through all the lines,
		//  start where you need to draw instead.
		u32 line_index = 0;
		for (Renderer_Line renderer_line : typer_ui.renderer_lines)
		{
			defer { line_index += 1; };
			defer { y -= typer_ui.typer_font_face->line_spacing; };

			if (y < (-typer_ui.typer_font_face->line_spacing) || y > (renderer.height + 10)) continue;

			// ZoneScopedNC("draw_renderer_line", 0xff0000);


			int y_center = y + typer_ui.typer_font_face->baseline_offset;

			int x = renderer_line.left_margin;

			Glyph_Iterator<char32_t> glyph_iterator = iterate_glyphs<char32_t>(typer_ui.typer_font_face);

			int glyph_iterator_previous_x = glyph_iterator.x;

			for (u64 i = renderer_line.start; i < (renderer_line.start + renderer_line.length_without_new_line()); i++)
			{
				// @TODO: this is stolen from glyph iterator in Font.h
				Terminal_Character terminal_character = *terminal.characters[i];


				// ZoneScopedNC("draw_renderer_line_character", 0xff0000);



				glyph_iterator.next_char(terminal_character.c);
				int x_delta = glyph_iterator.x - glyph_iterator_previous_x;
				defer{ glyph_iterator_previous_x = glyph_iterator.x; };
				defer{ x += x_delta; };

				Rect char_background_rect = Rect::make(x, y_center - typer_ui.typer_font_face->line_spacing / 2, x + x_delta, y_center + typer_ui.typer_font_face->line_spacing / 2);

				bool is_in_selection = false;
				if (selection_length)
				{
					is_in_selection = i >= selection_start && i < selection_end;
				}

				bool is_being_searched = false;
				bool is_current_found_entry = false;
				if (typer_ui.found_entries.count)
				{
					if (next_found_entry_to_look_for < typer_ui.found_entries.count)
					{
						for (int entry_index = next_found_entry_to_look_for; entry_index < typer_ui.found_entries.count; entry_index += 1)
						{
							Found_Text entry = *typer_ui.found_entries[entry_index];

							if (i < entry.start)
							{
								break;
							}

							if (i >= (entry.start + entry.length))
							{
								next_found_entry_to_look_for += 1;
								continue;
							}

							if (i >= entry.start && i < (entry.start + entry.length))
							{
								is_being_searched = true;
								break;
							}
						}

						if (typer_ui.current_found_entry != -1)
						{
							Found_Text entry = *typer_ui.found_entries[typer_ui.current_found_entry];
							is_current_found_entry = (i >= entry.start) && (i < (entry.start + entry.length));
						}
					}
				}

				if (is_being_searched)
				{
					if (is_current_found_entry)
					{
						renderer.draw_rect(char_background_rect, rgba(255, 69, 0, 255));
					}
					else
					{
						renderer.draw_rect(char_background_rect, rgba(255, 255, 255, 255));
					}
				}


				if (is_in_selection)
				{
					renderer.draw_rect(char_background_rect, typer_ui.selection_color);
				}

				rgba color = rgba(255, 255, 255, 255);

				if (is_in_selection || is_being_searched)
				{
					color = rgba(0, 0, 0, 255);					
				}
				else if (terminal_character.flags & TERMINAL_CHARACTER_FLAG_COLORED)
				{
					color = terminal_character.color;
					if (!is_in_selection && !is_being_searched)
					{
						renderer.draw_rect(char_background_rect, terminal_character.background_color);
					}
				}
				else
				{
					switch (terminal_character.character_type)
					{	
						case TERMINAL_CHARACTER_TYPE_PROCESS_OUTPUT:
							color = rgba(240, 240, 240, 255);
							break;
						case TERMINAL_CHARACTER_TYPE_USER_INPUT:
							color = rgba(0, 255, 255, 255);
							break;

						case TERMINAL_CHARACTER_TYPE_OLD_USER_INPUT:
							color = rgba(80, 200, 200, 255);
							break;

						case TERMINAL_CHARACTER_TYPE_SENT_USER_INPUT:
							color = rgba(150, 60, 60, 255);
							break;
						case TERMINAL_CHARACTER_TYPE_CHAR_THAT_PROCESS_DID_READ:
							color = rgba(150, 60, 150, 255);
							break;


						case TERMINAL_CHARACTER_TYPE_ERROR_MESSAGE:
							color = rgba(255, 10, 10, 255);
							break;
					}
				}

				if (user_cursor_line == line_index)
				{
					if (i == user_cursor)
					{
						set_user_cursor_rect(Rect::make(x, y_center - typer_ui.typer_font_face->line_spacing / 2, x + typer_ui.cursor_width, y_center + typer_ui.typer_font_face->line_spacing / 2));
					}
				}

				if (process_caret_line == line_index)
				{
					if (i == terminal.process_caret)
					{
						set_process_caret_rect(Rect::make(x, y_center - typer_ui.typer_font_face->line_spacing / 2, x + typer_ui.cursor_width, y_center + typer_ui.typer_font_face->line_spacing / 2));
					}
				}

				if (glyph_iterator.render_glyph)
				{
					Glyph glyph = glyph_iterator.current_glyph;
					// :GlyphLocalCoords:
					renderer.draw_glyph(&glyph, x + glyph.left_offset, y - (glyph.height - glyph.top_offset), color, do_need_to_gamma_correct(typer_ui.typer_font_face));
				}
			}


			u64 last_character_index = renderer_line.start + renderer_line.length_without_new_line();

			if (selection_length && renderer_line.has_new_line_at_the_end)
			{
				if (last_character_index >= selection_start && last_character_index < selection_end)
				{
					// Draw rect below \n symbol width is half size of the font.
					renderer.draw_rect(Rect::make(x, y_center - typer_ui.typer_font_face->line_spacing / 2, x + typer_ui.typer_font_face->line_spacing / 2, y_center + typer_ui.typer_font_face->line_spacing / 2), typer_ui.selection_color);
				}
			}

			if (!did_set_user_cursor_rect && user_cursor_line == line_index)
			{
				if (user_cursor == last_character_index)
				{
					set_user_cursor_rect(Rect::make(x, y_center - typer_ui.typer_font_face->line_spacing / 2, x + typer_ui.cursor_width, y_center + typer_ui.typer_font_face->line_spacing / 2));
				}
			}

			if (!did_set_process_caret_rect && process_caret_line == line_index)
			{
				if (terminal.process_caret == last_character_index)
				{
					set_process_caret_rect(Rect::make(x, y_center - typer_ui.typer_font_face->line_spacing / 2, x + typer_ui.cursor_width, y_center + typer_ui.typer_font_face->line_spacing / 2));
				}
			}
		}
	}

	// Draw autocomplete entries
	{
		assert(terminal.characters_mutex.is_being_locked());

		Unicode_String user_input = terminal.copy_user_input(frame_allocator);


		Scoped_Lock lock(python.autocomplete_result_mutex);

		if (is_typing_command() && python.autocompleted_string == user_input)
		{
			Vector2i character_pos = typer_ui.find_pixel_position_for_character(terminal.get_characters_count());


			int y_delta = python.current_autocomplete_result_index * typer_ui.typer_font_face->line_spacing;


			if (typer_ui.autocomplete_suggestion_move_target)
			{
				const float speed = 10.0;
				typer_ui.autocomplete_suggestion_move_state += speed * typer_ui.autocomplete_suggestion_move_target * frame_time;

				float state = ease(abs(typer_ui.autocomplete_suggestion_move_state)) * typer_ui.autocomplete_suggestion_move_target;

				state = sign(state) * (abs(state)) - sign(state);

				y_delta += state * typer_ui.typer_font_face->line_spacing;

				need_to_redraw_next_frame(code_location());

				if (abs(typer_ui.autocomplete_suggestion_move_state) > 1.0)
				{
					typer_ui.autocomplete_suggestion_move_state  = 0;
					typer_ui.autocomplete_suggestion_move_target = 0;
				}
			}


			for (int i = 0; i < python.autocomplete_result.count; i++)
			{
				Unicode_String str = *python.autocomplete_result[i];
				
				rgba entry_color = rgba(80, 80, 80, 255);
				if (i == python.current_autocomplete_result_index)
				{
					entry_color = entry_color * 1.5;
				}

				int text_width = measure_text_width(str, typer_ui.typer_font_face);


				int y_bottom = character_pos.y - typer_ui.typer_font_face->line_spacing * i + y_delta;

				Rect suggestion_background_rect = {
					.x_left = character_pos.x,
					.y_bottom = y_bottom,
					.x_right = character_pos.x + text_width + renderer.scaled(8),
					.y_top = y_bottom + typer_ui.typer_font_face->line_spacing 
				};

				suggestion_background_rect.move(0, -typer_ui.typer_font_face->baseline_offset);

				int index_from_center = abs(i - python.current_autocomplete_result_index);
				
				float alpha_scaling = 1;
				int distance_from_center_to_border = 1;

				if (i < python.current_autocomplete_result_index)
				{
					distance_from_center_to_border = python.current_autocomplete_result_index + 1;
				}
				else if (i > python.current_autocomplete_result_index)
				{
					distance_from_center_to_border = python.autocomplete_result.count - python.current_autocomplete_result_index;
				}

				distance_from_center_to_border = abs(distance_from_center_to_border);
				distance_from_center_to_border = clamp(20, 40, distance_from_center_to_border);

				alpha_scaling = float(index_from_center) / float(distance_from_center_to_border);
				
				if (alpha_scaling > 1.0)
					alpha_scaling = 1.0;

				alpha_scaling = 1.0 - alpha_scaling;



				if (alpha_scaling != 0)
				{
					renderer.draw_rect(suggestion_background_rect, rgba(0, 0, 0, 255));
					renderer.draw_text(typer_ui.typer_font_face, str, character_pos.x, y_bottom, entry_color.scaled_alpha(alpha_scaling));
				}

				// renderer.draw_text(typer_ui.typer_font_face, format_unicode_string(frame_allocator, U"%, %", index_from_center, distance_from_center_to_border), character_pos.x, y_bottom);
			}
		}
	}

	// Carets need to be drawn after autocomplete, to not be overlapped.
	{
		if (did_set_user_cursor_rect)
			renderer.draw_rect(user_cursor_rect,   typer_ui.user_cursor_color);

		if (did_set_process_caret_rect)
			renderer.draw_rect(process_caret_rect, typer_ui.process_caret_color);
	}

	

	typer_ui.current_bottom_bar_height = 0;

	bool manually_focus_search_bar_this_frame = false;

	if (typer_ui.terminal_focused && key_bindings.is_action_type_triggered(Action_Type::Open_Search_Bar))
	{
		typer_ui.terminal_focused = false;
		typer_ui.is_search_bar_open = true;
		manually_focus_search_bar_this_frame = typer_ui.is_search_bar_open;
	}

	if (input.is_key_down(Key::Escape) && typer_ui.is_search_bar_open && (typer_ui.is_search_bar_focused || typer_ui.terminal_focused))
	{
		typer_ui.terminal_focused = true;
		typer_ui.is_search_bar_open = false;	
	}

	typer_ui.current_bottom_bar_height = renderer.scaled(typer_ui.is_search_bar_open ? typer_ui.process_bar_height : 0);

	if (typer_ui.is_search_bar_open)
	{
		renderer.draw_rect(Rect::make(0, 0, get_active_width(), typer_ui.current_bottom_bar_height), rgba(75, 75, 50, 125));

		Unicode_String new_search_bar_text;

		int search_move_direction = 0;
		bool perform_search = false;
		UI_Text_Editor_Finish_Cause finish_cause;


		{
			scoped_set_and_revert(ui.parameters.text_font_face_size, 14); 
			scoped_set_and_revert(ui.parameters.text_field_background, rgba(0, 0, 0, 255));

			int search_bar_margin = renderer.scaled(12);


			if (ui.text_editor(Rect::make(renderer.scaled(24), search_bar_margin, get_active_width() - renderer.scaled(24), typer_ui.current_bottom_bar_height - search_bar_margin), typer_ui.search_bar_text, &new_search_bar_text, c_allocator, typer_ui.search_bar_ui_id, false, true, false, &finish_cause))
			{
				if (typer_ui.search_bar_text.data)
				{
					c_allocator.free(typer_ui.search_bar_text.data, code_location());
				}

				typer_ui.search_bar_text = new_search_bar_text;

				if (finish_cause == UI_TEXT_EDITOR_PRESSED_ENTER || finish_cause == UI_TEXT_EDITOR_MODIFIED_TEXT)
				{
					if (finish_cause == UI_TEXT_EDITOR_PRESSED_ENTER)
					{
						if (input.is_key_down_or_held(Key::Any_Shift))
							search_move_direction = -1;
						else
							search_move_direction = 1;
					}

					perform_search = true;
				}
			}
		}

		auto search_text_editor_state = ((UI_Multiline_Text_Editor_State*) ui.get_ui_item_data(typer_ui.search_bar_ui_id));

		if (manually_focus_search_bar_this_frame)
		{
			// @Hack: stolen from UI::text_editor clicking code.
			search_text_editor_state->builder = build_string<char32_t>(c_allocator); // @MemoryLeak
			search_text_editor_state->editor  = create_text_editor(&search_text_editor_state->builder);

			search_text_editor_state->builder.append(typer_ui.search_bar_text);

			search_text_editor_state->editor.cursor = typer_ui.search_bar_text.length;
			search_text_editor_state->editor.selection_length = -typer_ui.search_bar_text.length; // Pre select the text in search bar

			search_text_editor_state->editing = true;
		}

		typer_ui.is_search_bar_focused = search_text_editor_state->editing;

		if (!typer_ui.is_search_bar_focused)
		{
			#if 0
			typer_ui.found_entries.clear();
		
			
			if (typer_ui.found_text.data)
				c_allocator.free(typer_ui.found_text.data, code_location());
			typer_ui.found_text = Unicode_String::empty;
			#endif
		}


		auto search = [&](Unicode_String text)
		{
			typer_ui.found_entries.clear();
			Unicode_String search_text = text.to_lower_case(frame_allocator);

			if (typer_ui.found_text.data)
				c_allocator.free(typer_ui.found_text.data, code_location());
			typer_ui.found_text = search_text.copy_with(c_allocator);


			int matched_length = 0;
			int characters_count = terminal.get_characters_count();
			for (int i = 0; i < characters_count; i++)
			{
				Terminal_Character* sc = terminal.characters[i];

				char32_t lower_c = u_tolower(sc->c);

				if (search_text[matched_length] == lower_c)
				{
					matched_length += 1;
					
					if (matched_length == search_text.length)
					{
						typer_ui.found_entries.add({.start = i - matched_length + 1, .length = matched_length});
						matched_length = 0;
						continue;
					}
				}
				else
				{
					matched_length = 0;
				}
			}
			
		};

		if (typer_ui.is_search_bar_focused && input.is_key_down(Key::Enter))
		{
			perform_search = true;

			if (input.is_key_down_or_held(Key::Any_Shift))
			{
				search_move_direction = -1;
			}
			else
			{
				search_move_direction =  1;				
			}
		}

		if (typer_ui.search_bar_text.length == 0)
		{
			typer_ui.found_text = Unicode_String::empty;
			typer_ui.found_entries.clear();
			perform_search = false;
		}

		if (buffer_changed)
		{
			perform_search = true;
		}

		if (perform_search)
		{
			if (typer_ui.search_bar_text.to_lower_case(frame_allocator) != typer_ui.found_text || buffer_changed)
			{
				search(typer_ui.search_bar_text);

				if (typer_ui.found_entries.count)
				{
					if (buffer_changed)
					{
						if (!(typer_ui.current_found_entry >= 0) || !(typer_ui.current_found_entry < typer_ui.found_entries.count))
						{
							typer_ui.current_found_entry = clamp(0, typer_ui.found_entries.count - 1, typer_ui.current_found_entry);
						}
					}
					else
					{
						// Scroll to the closest entry
						{
							Renderer_Line* closest_line_to_me = typer_ui.renderer_lines[typer_ui.find_closest_renderer_line_to_y_coordinate(renderer.height / 2)];

							int closest_entry_index    = -1;
							int closest_entry_distance = s32_max;
							for (auto& entry: typer_ui.found_entries)
							{
								int distance = min(abs(entry.start - closest_line_to_me->start), abs((entry.start + entry.length) -(closest_line_to_me->start + closest_line_to_me->length)));
								if (distance < closest_entry_distance)
								{
									closest_entry_distance = distance;
									closest_entry_index = typer_ui.found_entries.fast_pointer_index(&entry);
								}
							}

							typer_ui.current_found_entry = closest_entry_index;						
						}
					}

					typer_ui.scroll_to_char(typer_ui.found_entries[typer_ui.current_found_entry]->start, true);
				}
				else
				{
					typer_ui.current_found_entry = -1;
				}
			}

			if (search_move_direction)
			{
				if (typer_ui.found_entries.count)
				{
					if (search_move_direction == -1)
					{
						typer_ui.current_found_entry -= 1;

						if (typer_ui.current_found_entry < 0)
							typer_ui.current_found_entry = typer_ui.found_entries.count - 1;
					}
					else if (search_move_direction == 1)
					{
						typer_ui.current_found_entry += 1;

						if (typer_ui.current_found_entry >= typer_ui.found_entries.count)
							typer_ui.current_found_entry = 0;
					}

					typer_ui.scroll_to_char(typer_ui.found_entries[typer_ui.current_found_entry]->start, true);
				}
			}
		}
	}
	else
	{
		typer_ui.is_search_bar_focused = false;


		typer_ui.found_entries.clear();
		
		if (typer_ui.found_text.data)
			c_allocator.free(typer_ui.found_text.data, code_location());
		typer_ui.found_text = Unicode_String::empty;
	}
}

void prepare_for_running_command(bool limited_context)
{
	command_running_time.reset();

	python.is_running = true;

	if (!limited_context)
	{
		console_settings.reset();

		user_cursor = terminal.characters.count;

		terminal.prepare_for_process();

		output_processor.clean();

		terminal_io.start_io();
	}
}

void cleanup_after_command(bool limited_context)
{
	log(ctx.logger, U"cleanup_after_command. frame_index = %", frame_index);
	log(ctx.logger, U"Running command took: % seconds", command_running_time.seconds_elapsed_double());

	need_to_redraw_next_frame(code_location());

	python_debugger.reset();

	python.is_running = false;


	if (!limited_context)
	{
		terminal_io.finish_io();

		// Important to lock below terminal_io.finish_io(),
		//    otherwise if output is left in terminal_io.reader_thread gets locked by main thread,
		//    but main thread waits for it, so we get DEADLOCK.
		Scoped_Lock lock(terminal.characters_mutex);

		// Clean everything after process caret.
		{
			terminal.remove_non_process(terminal.process_caret, terminal.characters.count - terminal.process_caret);
			user_cursor = terminal.process_caret;
			selection_length = 0;
		}
	}
}





void Typer::terminate_running_process()
{
	if (!python.is_running) return;

	if (python.is_running) return;

	python.run_only_prompt();
}

#if OS_WINDOWS
LONG WINAPI exception_handler(_EXCEPTION_POINTERS* exc_info)
{
	typedef BOOL(WINAPI* MINIDUMPWRITEDUMP)(HANDLE hProcess, DWORD dwPid, HANDLE hFile, MINIDUMP_TYPE DumpType, CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam, CONST PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam, CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam);


	HMODULE mhLib = ::LoadLibraryW(L"dbghelp.dll");
	MINIDUMPWRITEDUMP pDump = (MINIDUMPWRITEDUMP)::GetProcAddress(mhLib, "MiniDumpWriteDump");

	SYSTEMTIME system_time;
	GetLocalTime(&system_time);

	Unicode_String crash_dumps_folder = path_concat(c_allocator, typer_directory, Unicode_String(U"crash_dumps"));

	create_directory_recursively(crash_dumps_folder, c_allocator);

	Unicode_String str = format_unicode_string(c_allocator, path_concat(c_allocator, crash_dumps_folder, Unicode_String(U"dump%.%.%-%.%.%.dmp")), system_time.wDay, system_time.wMonth, system_time.wYear, system_time.wHour, system_time.wMinute, system_time.wSecond);

	wchar_t* wstr = str.to_wide_string(c_allocator);


	HANDLE  hFile = ::CreateFileW(wstr, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);


	_MINIDUMP_EXCEPTION_INFORMATION ExInfo;
	ExInfo.ThreadId = ::GetCurrentThreadId();
	ExInfo.ExceptionPointers = exc_info;
	ExInfo.ClientPointers = FALSE;

	MINIDUMP_TYPE dump_type = MiniDumpNormal;
	if (settings.full_crash_dump)
	{
		dump_type = (MINIDUMP_TYPE) (dump_type | MiniDumpWithDataSegs | MiniDumpWithFullMemory | MiniDumpWithHandleData | MiniDumpWithCodeSegs);
	}

	pDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, dump_type, &ExInfo, NULL, NULL);
	::CloseHandle(hFile);

	return EXCEPTION_EXECUTE_HANDLER;
}
#endif

// Entry point for OS X is located in Main_Cocoa.mm
#if OS_DARWIN
int typer_main_for_osx()
#else
int main()
#endif
{
	// setsid(); // To be able to set controlling terminal

	mission_name = U"Typer";

	// Check whether CPU supports AVX.
	// But what if compiler uses AVX to check for AVX....????
	{
		// Stolen from: https://gist.github.com/hi2p-perim/7855506
    #if OS_WINDOWS
		int cpu_info[4];
		__cpuid(cpu_info, 1);
    #elif IS_POSIX
		u32 cpu_info[4];
        __get_cpuid(1, &cpu_info[0], &cpu_info[1], &cpu_info[2], &cpu_info[3]);
    #endif

		bool avxSupportted = cpu_info[2] & (1 << 28);
		bool osxsaveSupported = cpu_info[2] & (1 << 27);
		if (osxsaveSupported && avxSupportted)
		{

			auto xgetbv = [&](int num)
			{
			#if OS_WINDOWS

				return _xgetbv(num);
			#else
				uint32_t a, d;
				__asm("xgetbv" : "=a"(a),"=d"(d) : "c"(num) : );

	   			return a | (uint64_t(d) << 32);
	   		#endif
			};

   			u64 xcrFeatureMask = xgetbv(0);
			avxSupportted = (xcrFeatureMask & 0x6) == 0x6;
		}

		if (!avxSupportted)
		{
			abort_the_mission(U"Your CPU doesn't support AVX instruction set, which is required to run %", mission_name);
			return 1;
		}
	}

#if OS_WINDOWS
	// Set dump writer
	{
		SetUnhandledExceptionFilter(exception_handler);
		// AddVectoredExceptionHandler(1, exception_handler);
	}
#endif

	// Disanble stdio buffering
	{
		setvbuf(stdout, NULL, _IONBF, 0);
		setvbuf(stderr, NULL, _IONBF, 0);
	}

	// Check for admin rights
	{
    #if OS_WINDOWS
		HANDLE hToken = NULL;
		if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
		{
			TOKEN_ELEVATION Elevation;
			DWORD cbSize = sizeof(TOKEN_ELEVATION);
			if (GetTokenInformation(hToken, TokenElevation, &Elevation, sizeof(Elevation), &cbSize))
			{
				is_process_elevated = Elevation.TokenIsElevated;
			}
		}
		if (hToken)
		{
			CloseHandle(hToken);
		}
    #elif OS_LINUX
        if (getuid() == 0)
        {
            is_process_elevated = true;
        }
    #endif
	}


#if !DEBUG
	Unicode_String window_title = is_process_elevated ?
		Unicode_String(U"Typerminal (admin rights)") :
		Unicode_String(U"Typerminal");
#else
	Unicode_String window_title = is_process_elevated ?
		Unicode_String(U"Typerminal (DEBUG) (admin rights)") :
		Unicode_String(U"Typerminal (DEBUG)");
#endif





#if OS_WINDOWS
	{
		ZoneScopedN("SetThreadDpiAwarenessContext");
		SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	}
#endif


	typer_initial_stdout = stdout;


#if OS_WINDOWS
	auto alloc_console_thread_proc = [](void* dummy_ptr)
	{
		if (USE_HIDDEN_CONSOLE)
		{
			ZoneScopedN("Allocate console");

			{
				ZoneScopedN("AllocConsole()");
				AllocConsole();
			}
			{
				ZoneScopedN("AttachConsole()");
				AttachConsole(GetCurrentProcessId());
			}

			// HANDLE conout = CreateFileA("CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, NULL, NULL);
			// HANDLE conin  = CreateFileA("CONIN$", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, NULL, NULL);
			// SetStdHandle(STD_OUTPUT_HANDLE, conout);
			// SetStdHandle(STD_ERROR_HANDLE,  conout);
			// SetStdHandle(STD_INPUT_HANDLE,  conin);

#if 0
			int m_nCRTIn = _open_osfhandle(
				(long)GetStdHandle(STD_INPUT_HANDLE),
				_O_TEXT);

			FILE* m_fpCRTIn = _fdopen(m_nCRTIn, "r");

			FILE m_fOldStdIn = *stdin;
			*stdin = *m_fpCRTIn;

			int m_nCRTOut = _open_osfhandle(
				(long)GetStdHandle(STD_OUTPUT_HANDLE),
				_O_TEXT);

			FILE* m_fpCRTOut = _fdopen(m_nCRTOut, "w");

			freopen_s(&m_fpCRTOut, "CONOUT$", "w", stdout);
#endif
		}

		if (USE_HIDDEN_CONSOLE)
		{
			if (!IsDebuggerPresent())
			{
				ZoneScopedN("Hide console");
				ShowWindow(GetConsoleWindow(), SW_HIDE);
			}
		}



		alloc_console_finished_semaphore.increment();
	};
	create_thread(c_allocator, alloc_console_thread_proc, NULL);
#endif


	{
		ZoneScopedN("Open log file");


		create_directory_recursively(U"logs", c_allocator);

#if OS_WINDOWS
		SYSTEMTIME system_time;
		GetLocalTime(&system_time);
		Unicode_String str = format_unicode_string(c_allocator, U"logs/typer_last_log_%.%.%-%.%.%.txt", system_time.wDay, system_time.wMonth, system_time.wYear, system_time.wHour, system_time.wMinute, system_time.wSecond);
#elif IS_POSIX
        time_t t = time(NULL);
        struct tm* local_time = localtime(&t);

		Unicode_String str = format_unicode_string(c_allocator, U"logs/typer_last_log_%.%.%-%.%.%.txt", local_time->tm_mday, local_time->tm_mon, local_time->tm_year, local_time->tm_hour, local_time->tm_min, local_time->tm_sec);
#endif

		defer{ c_allocator.free(str.data, code_location()); };

		log_file = open_file(c_allocator, str, File_Open_Mode(FILE_WRITE | FILE_CREATE_NEW));
		if (!log_file.succeeded_to_open())
		{
			log(ctx.logger, U"Failed to open log file with name: %", str);
		}

		ctx.logger.concat_allocator = c_allocator;
		ctx.logger.log_proc = [](Unicode_String str)
		{
			ZoneScoped;

			Scoped_Lock logger_lock(typer_logger_mutex);

			int utf8_length;
			char* utf8_str = str.to_utf8(c_allocator, &utf8_length);
			fwrite(utf8_str, 1, utf8_length, stdout);
			fwrite("\n", 1, 1, stdout);

			if (stdout != typer_initial_stdout)
			{
				fwrite(utf8_str, 1, utf8_length, typer_initial_stdout);
				fwrite("\n", 1, 1, typer_initial_stdout);
			}

			if (log_file.succeeded_to_open())
			{
				log_file.write((u8*)utf8_str, utf8_length);
				log_file.write((u8*)"\n", 1);
				
			#if OS_WINDOWS
				log_file.flush();
			#endif
			}

			c_allocator.free(utf8_str, code_location());
		};

		typer_logger = ctx.logger;
	}

#if LOG_PROCESS_OUTPUT_TO_HUYE_TXT
	huye_file = open_file(c_allocator, U"HUY.txt", FILE_WRITE | FILE_CREATE_NEW);
	assert(huye_file.succeeded_to_open());
#endif


    // Code below starts using frame_allocator
	{
		ZoneScopedN("Allocate frame_allocator");

		create_arena_allocator(&frame_allocator, c_allocator, 12 * 1024 * 1024);
#ifdef ALLOCATOR_NAMES
		frame_allocator.name = "frame_allocator";
#endif

#if DEBUG
		frame_allocator.owning_thread = threading.current_thread_id();
#endif
	}


	TracyCZoneN(create_window_zone, "Create window", true);


#if OS_WINDOWS
	HINSTANCE hInstance = GetModuleHandle(NULL);

	const wchar_t CLASS_NAME[] = L"Typer";

	WNDCLASS wc = { };

	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;

	wc.lpfnWndProc = [](HWND h, UINT m, WPARAM wParam, LPARAM lParam) -> LRESULT
	{
		LRESULT dwm_proc_result = 0;
		// auto fucking_dwm_proc_adsadsasd = DwmDefWindowProc(h, m, wParam, lParam, &dwm_proc_result);

#if 1
		if (m == WM_NCHITTEST)
		{
			if (true)
			{
				int LEFTEXTENDWIDTH = window_border_size;
				int RIGHTEXTENDWIDTH = LEFTEXTENDWIDTH;
				int BOTTOMEXTENDWIDTH = LEFTEXTENDWIDTH;
				int TOPEXTENDWIDTH = renderer.scaled(window_header_size);

				if (IsMaximized(h))
				{
					TOPEXTENDWIDTH += 8;
				}


				// Get the point coordinates for the hit test.
				POINT ptMouse = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

				POINT ptMouseWindowLocal = ptMouse;

				ScreenToClient(windows.hwnd, &ptMouseWindowLocal);

				// Get the window rectangle.
				RECT rcWindow;
				GetWindowRect(h, &rcWindow);

				// Get the frame rectangle, adjusted for the style without a caption.
				// RECT rcFrame = { 0 };
				// AdjustWindowRectEx(&rcFrame, WS_OVERLAPPEDWINDOW & ~WS_CAPTION, FALSE, NULL);

				// Determine if the hit test is for resizing. Default middle (1,1).
				USHORT uRow = 1;
				USHORT uCol = 1;
				bool fOnResizeBorder = false;

				// Determine if the point is at the top or bottom of the window.
				if (ptMouse.y >= rcWindow.top && ptMouse.y < rcWindow.top + TOPEXTENDWIDTH)
				{
					fOnResizeBorder = (ptMouse.y < (rcWindow.top + window_top_border_drag_width));
					uRow = 0;
				}
				else if (ptMouse.y < rcWindow.bottom && ptMouse.y >= rcWindow.bottom - BOTTOMEXTENDWIDTH)
				{
					uRow = 2;
				}

				// Determine if the point is at the left or right of the window.
				if (ptMouse.x >= rcWindow.left && ptMouse.x < rcWindow.left + LEFTEXTENDWIDTH)
				{
					uCol = 0; // left side
				}
				else if (ptMouse.x < rcWindow.right && ptMouse.x >= rcWindow.right - RIGHTEXTENDWIDTH)
				{
					uCol = 2; // right side
				}

				if (uRow == 0 && uCol == 1)
				{
					if (typer_ui.is_header_ui_touching_cursor(ptMouseWindowLocal.x, renderer.height - ptMouseWindowLocal.y))
					{
						return HTCLIENT;
					}
				}

				if (IsMaximized(h))
				{
					if (uRow > 0)
					{
						return HTCLIENT;
					}

					if (uCol != 1)
					{
						return HTCLIENT;
					}

					return HTCAPTION;
				}

				// Hit test (HTTOPLEFT, ... HTBOTTOMRIGHT)
				LRESULT hitTests[3][3] =
				{
					{ HTTOPLEFT,    fOnResizeBorder ? HTTOP : HTCAPTION,    HTTOPRIGHT },
					{ HTLEFT,       HTCLIENT,     HTRIGHT },
					{ HTBOTTOMLEFT, HTBOTTOM, HTBOTTOMRIGHT },
				};

				return hitTests[uRow][uCol];
			}
		}
#endif

		switch (m)
		{
		case WM_DPICHANGED:
		{
			window_size_changed = true;
		}
		break;

		case WM_CREATE:
		{
			is_inside_window_creation = true;

			// Inform application of the frame change.
			SetWindowPos(h,
				NULL,
				0, 0,
				0, 0,
				SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);
		}
		break;


		case WM_DESTROY:
		case WM_CLOSE:
		{
			PostQuitMessage(0);
		}
		break;


		case WM_WINDOWPOSCHANGED:
		{
			WINDOWPOS* window_pos = (WINDOWPOS*)lParam;

			RECT new_client_rect;
			GetClientRect(h, &new_client_rect);

			int new_width  = new_client_rect.right  - new_client_rect.left;
			int new_height = new_client_rect.bottom - new_client_rect.top;

			handle_os_resize_event(new_width, new_height);
		}
		break;

#if 1
		case WM_NCCALCSIZE:
		{
#if 1
			if (IsMaximized(h))
			{
				NCCALCSIZE_PARAMS* pncsp = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);

				union {
					LPARAM lparam;
					RECT* rect;
				} params = { .lparam = lParam };

				RECT nonclient = *params.rect;
				DefWindowProcW(h, WM_NCCALCSIZE, wParam, lParam);
				RECT client = *params.rect;

				pncsp->rgrc[0] = nonclient;

				pncsp->rgrc[0].top += 8;
				pncsp->rgrc[0].bottom -= 8;
				pncsp->rgrc[0].left += 8;
				pncsp->rgrc[0].right -= 8;

			}
			else
			{

				int m_ncTop = 0;
				int m_ncBottom = -window_border_size;
				int m_ncLeft = -window_border_size;
				int m_ncRight = -window_border_size;

				if (wParam == TRUE)
				{
					NCCALCSIZE_PARAMS* params = (NCCALCSIZE_PARAMS*)lParam;
					params->rgrc[0].top -= m_ncTop;
					params->rgrc[0].bottom += m_ncBottom;
					params->rgrc[0].left -= m_ncLeft;
					params->rgrc[0].right += m_ncRight;

					return 0;
				}
			}
#endif

			return 0;
		}
		break;
#endif

#if 0
#define WM_NCUAHDRAWCAPTION (0x00AE)
#define WM_NCUAHDRAWFRAME (0x00AF)
		case WM_NCUAHDRAWCAPTION:
		case WM_NCUAHDRAWFRAME:
			/* These undocumented messages are sent to draw themed window borders.
			   Block them to prevent drawing borders over the client area. */
			return 0;
#endif


		case WM_GETMINMAXINFO:
		{
			LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;


			float scaling = float(GetDpiForWindow(windows.hwnd)) / 96.0;

			lpMMI->ptMinTrackSize.x = scale(window_min_width, scaling);
			lpMMI->ptMinTrackSize.y = scale(window_min_height, scaling);
		}
		break;

		case WM_CHAR:
		{
			if (wParam < 32) return true;

			switch (wParam)
			{
			case VK_BACK:
			case VK_TAB:
			case VK_RETURN:
			case VK_ESCAPE:
			case 127: // Ctrl + backspace
			{
				return true;
			}
			}

			static WPARAM high_surrogate = 0;
			if (IS_HIGH_SURROGATE(wParam))
			{
				high_surrogate = wParam;
				return 0;
			}

			u64 c = (u64)wParam;

			char32_t utf32_c;
			{
				if (high_surrogate && c >= 0xDC00)
				{
					u16 lower = (u16)(c);
					u16 higher = (u16)(high_surrogate);

					utf32_c = (higher - 0xD800) << 10;

					utf32_c |= ((char32_t)lower) - 0xDC00;

					utf32_c += 0x10000;
				}
				else
				{
					utf32_c = (char32_t)c;
				}
			}

			Input_Node node;
			node.input_type = Input_Type::Char;
			node.character = utf32_c;

			input.nodes.add(node);

			high_surrogate = 0;
		}
		break;

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		{
			Input_Node node;
			node.input_type = Input_Type::Key;
			node.key_action = Key_Action::Down;
			node.key = map_windows_key(wParam);
			node.key_code = wParam;

			input.nodes.add(node);
		}
		break;

		case WM_KEYUP:
		case WM_SYSKEYUP:
		{
			Input_Node node;
			node.input_type = Input_Type::Key;
			node.key_action = Key_Action::Up;
			node.key = map_windows_key(wParam);
			node.key_code = wParam;

			input.nodes.add(node);
		}
		break;

		case WM_MOUSEWHEEL:
		{
			input.mouse_wheel_delta = -GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
		}
		break;



		case WM_ACTIVATE:
		{
			MARGINS margins = { 0, 0, 0, 0 };
			DwmExtendFrameIntoClientArea(h, &margins);

			DWORD huy = DWMNCRP_ENABLED;
			DwmSetWindowAttribute(h, DWMWA_NCRENDERING_POLICY, &huy, sizeof(DWORD));

			has_window_focus = LOWORD(wParam) ? true : false;
		}
		break;

		case WM_MOUSEMOVE:
		{
			input.mouse_x = GET_X_LPARAM(lParam);
			input.mouse_y = renderer.height - GET_Y_LPARAM(lParam);
		}
		break;

		case WM_SETCURSOR:
		{
			if (LOWORD(lParam) != HTCLIENT)
			{
				return DefWindowProc(h, m, wParam, lParam);
			}


			handle_wm_setcursor(desired_cursor_type);
		}
		break;

		case WM_LBUTTONDOWN:
		{
			Input_Node node;
			node.input_type = Input_Type::Key;
			node.key_action = Key_Action::Down;
			node.key = Key::LMB;
			node.key_code = VK_LBUTTON;

			input.nodes.add(node);
		}
		break;

		case WM_RBUTTONDOWN:
		{
			Input_Node node;
			node.input_type = Input_Type::Key;
			node.key_action = Key_Action::Down;
			node.key = Key::RMB;
			node.key_code = VK_RBUTTON;

			input.nodes.add(node);
		}
		break;
		case WM_MBUTTONDOWN:
		{
			Input_Node node;
			node.input_type = Input_Type::Key;
			node.key_action = Key_Action::Down;
			node.key = Key::MMB;
			node.key_code = VK_MBUTTON;

			input.nodes.add(node);
		}
		break;

		case WM_NCLBUTTONUP:
		case WM_LBUTTONUP:
		{
			Input_Node node;
			node.input_type = Input_Type::Key;
			node.key_action = Key_Action::Up;
			node.key = Key::LMB;
			node.key_code = VK_LBUTTON;

			input.nodes.add(node);
		}
		break;

		case WM_NCRBUTTONUP:
		case WM_RBUTTONUP:
		{
			Input_Node node;
			node.input_type = Input_Type::Key;
			node.key_action = Key_Action::Up;
			node.key = Key::RMB;
			node.key_code = VK_RBUTTON;

			input.nodes.add(node);
		}
		break;

		case WM_NCMBUTTONUP:
		case WM_MBUTTONUP:
		{
			Input_Node node;
			node.input_type = Input_Type::Key;
			node.key_action = Key_Action::Up;
			node.key = Key::MMB;
			node.key_code = VK_MBUTTON;

			input.nodes.add(node);
		}
		break;

		default:
		{
			return DefWindowProc(h, m, wParam, lParam);
		}
		}

		return false;
	};

	RegisterClass(&wc);


	int window_x = GetSystemMetrics(SM_CXSCREEN) / 2 - window_width / 2;
	int window_y = GetSystemMetrics(SM_CYSCREEN) / 2 - window_height / 2;

	RECT window_size = {
		window_x,
		window_y,
		window_x + window_width,
		window_y + window_height
	};


	DWORD window_style = WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;

	AdjustWindowRect(&window_size, window_style, false);

	wchar_t* utf16_window_title = window_title.to_wide_string(frame_allocator);

	HWND hwnd = CreateWindowEx(
		WS_EX_APPWINDOW,                              // Optional windowCreateWistyles.
		CLASS_NAME,                     // Window class
		utf16_window_title,                       // Window text
		window_style,            // Window style

		// Size and position
		window_size.left,
		window_size.top,
		window_size.right - window_size.left,
		window_size.bottom - window_size.top,

		NULL,       // Parent window    
		NULL,       // Menu
		hInstance,  // Instance handle
		NULL        // Additional application data
	);

	if (hwnd == NULL)
	{
		return 0;
	}

	// SetLayeredWindowAttributes(hwnd, RGB(255, 0, 255), 0, LWA_COLORKEY);

	if (false)
	{
		DWM_BLURBEHIND bb = { 0 };

		// Specify blur-behind and blur region.
		bb.dwFlags = DWM_BB_ENABLE;
		bb.fEnable = true;
		bb.hRgnBlur = NULL;

		// Enable blur-behind.
		// DwmEnableBlurBehindWindow(hwnd, &bb);


		typedef enum _WINDOWCOMPOSITIONATTRIB
		{
			WCA_UNDEFINED = 0,
			WCA_NCRENDERING_ENABLED = 1,
			WCA_NCRENDERING_POLICY = 2,
			WCA_TRANSITIONS_FORCEDISABLED = 3,
			WCA_ALLOW_NCPAINT = 4,
			WCA_CAPTION_BUTTON_BOUNDS = 5,
			WCA_NONCLIENT_RTL_LAYOUT = 6,
			WCA_FORCE_ICONIC_REPRESENTATION = 7,
			WCA_EXTENDED_FRAME_BOUNDS = 8,
			WCA_HAS_ICONIC_BITMAP = 9,
			WCA_THEME_ATTRIBUTES = 10,
			WCA_NCRENDERING_EXILED = 11,
			WCA_NCADORNMENTINFO = 12,
			WCA_EXCLUDED_FROM_LIVEPREVIEW = 13,
			WCA_VIDEO_OVERLAY_ACTIVE = 14,
			WCA_FORCE_ACTIVEWINDOW_APPEARANCE = 15,
			WCA_DISALLOW_PEEK = 16,
			WCA_CLOAK = 17,
			WCA_CLOAKED = 18,
			WCA_ACCENT_POLICY = 19,
			WCA_FREEZE_REPRESENTATION = 20,
			WCA_EVER_UNCLOAKED = 21,
			WCA_VISUAL_OWNER = 22,
			WCA_HOLOGRAPHIC = 23,
			WCA_EXCLUDED_FROM_DDA = 24,
			WCA_PASSIVEUPDATEMODE = 25,
			WCA_USEDARKMODECOLORS = 26,
			WCA_LAST = 27
		} WINDOWCOMPOSITIONATTRIB;

		typedef struct _WINDOWCOMPOSITIONATTRIBDATA
		{
			WINDOWCOMPOSITIONATTRIB Attrib;
			PVOID pvData;
			SIZE_T cbData;
		} WINDOWCOMPOSITIONATTRIBDATA;

		typedef enum _ACCENT_STATE
		{
			ACCENT_DISABLED = 0,
			ACCENT_ENABLE_GRADIENT = 1,
			ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
			ACCENT_ENABLE_BLURBEHIND = 3,
			ACCENT_ENABLE_ACRYLICBLURBEHIND = 4, // RS4 1803
			ACCENT_ENABLE_HOSTBACKDROP = 5, // RS5 1809
			ACCENT_INVALID_STATE = 6
		} ACCENT_STATE;

		typedef struct _ACCENT_POLICY
		{
			ACCENT_STATE AccentState;
			DWORD AccentFlags;
			DWORD GradientColor;
			DWORD AnimationId;
		} ACCENT_POLICY;

		typedef BOOL(WINAPI* pfnGetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);
		typedef BOOL(WINAPI* pfnSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);


		HMODULE user32 = GetModuleHandleW(L"user32.dll");
		pfnSetWindowCompositionAttribute setWindowCompositionAttribute = (pfnSetWindowCompositionAttribute)GetProcAddress(user32, "SetWindowCompositionAttribute");
		if (setWindowCompositionAttribute)
		{
			// @TODO: As said in ACCENT_STATE definition ACCENT_ENABLE_ACRYLICBLURBEHIND is only available in Windows version >= 1803. Typerminal requires 1607 so detect at runtime.
			ACCENT_POLICY accent = { ACCENT_ENABLE_BLURBEHIND , 0, 0, 0 };
			WINDOWCOMPOSITIONATTRIBDATA data;
			data.Attrib = WCA_ACCENT_POLICY;
			data.pvData = &accent;
			data.cbData = sizeof(accent);
			setWindowCompositionAttribute(hwnd, &data);
		}
	}


	windows.dc = GetDC(hwnd);
	windows.hwnd = hwnd;

	windows.hinstance = hInstance;

	windows.window_dpi = GetDpiForWindow(hwnd);
	windows.window_scaling = float(windows.window_dpi) / 96.0;

	renderer.scaling = windows.window_scaling;

	log(ctx.logger, U"Initial window pixel scaling factor: %", windows.window_scaling);

    #elif OS_LINUX

    {
        ZoneScoped("X11 create_window");

        x11.display = XOpenDisplay((char *)0);
        x11.screen  = DefaultScreen(x11.display);



        auto black = BlackPixel(x11.display, x11.screen);
        auto white = WhitePixel(x11.display, x11.screen);


        {
        	long visualMask = VisualScreenMask;
			int numberOfVisuals;
			XVisualInfo vInfoTemplate = {};
			vInfoTemplate.screen = x11.screen;

        	XVisualInfo *visualInfo = XGetVisualInfo(x11.display, visualMask, &vInfoTemplate, &numberOfVisuals);

			Colormap colormap = XCreateColormap(x11.display, RootWindow(x11.display, vInfoTemplate.screen), visualInfo->visual, AllocNone);

			XSetWindowAttributes windowAttributes = {};
			windowAttributes.colormap = colormap;
			windowAttributes.background_pixel = 0xFFFFFFFF;
			windowAttributes.border_pixel = 0;
			windowAttributes.event_mask = KeyPressMask | KeyReleaseMask | StructureNotifyMask | ExposureMask;
      

			//x11.window = XCreateSimpleWindow(x11.display, DefaultRootWindow(x11.display),100,200,	window_width, window_height, 5, white, black);

			unsigned long valuemask = CWBorderPixel | CWColormap | CWEventMask;

	        x11.window = XCreateWindow(x11.display, RootWindow(x11.display, visualInfo->screen), 100,200,	window_width, window_height, 0, visualInfo->depth, InputOutput, visualInfo->visual, valuemask, &windowAttributes);

        }
      

        {
        	//associate PID
			// make PID known to X11
			{
				const long pid = getpid();

				Atom net_wm_pid = XInternAtom(x11.display, "_NET_WM_PID", False);
				XChangeProperty(x11.display, x11.window, net_wm_pid, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&pid, 1);
			}
        }

        XSetStandardProperties(x11.display, x11.window, window_title.to_utf8(frame_allocator, NULL), "", 0, NULL, 0, NULL);

        XSelectInput(x11.display, x11.window,
			ExposureMask |
			
			ButtonPressMask | ButtonReleaseMask |
			KeyPressMask | KeyReleaseMask |

			StructureNotifyMask | // Resizing
			
			SubstructureNotifyMask | SubstructureRedirectMask | // Maximizing
			
			FocusChangeMask);


        x11.gc = XCreateGC(x11.display, x11.window, 0, 0);  

        // Remove window titlebar
		if (1)
        {
        	struct Hints
			{
			    unsigned long   flags;
			    unsigned long   functions;
			    unsigned long   decorations;
			    long            inputMode;
			    unsigned long   status;
			};

			Hints hints;
			Atom property;
			hints.flags = 2;
			hints.decorations = 0;
			property = GET_X11_ATOM(_MOTIF_WM_HINTS);
			XChangeProperty(x11.display, x11.window, property, property, 32, PropModeReplace, (unsigned char *)&hints, 5);


		#if 0
            Atom window_type = GET_X11_ATOM(_NET_WM_WINDOW_TYPE);
            long value       = GET_X11_ATOM(_NET_WM_WINDOW_TYPE_UTILITY);
            XEvent e;
            XChangeProperty(x11.display, x11.window, window_type, XA_ATOM, 32, PropModeReplace, (unsigned char *) &value, 1);


            // Utility window appears unfocused, it needs to be forcefuly focused.
			XEvent xev = {};
			Atom net_active_window = GET_X11_ATOM(_NET_ACTIVE_WINDOW);

			xev.type = ClientMessage;
			xev.xclient.window = x11.window;
			xev.xclient.message_type = net_active_window;
			xev.xclient.format = 32;
			xev.xclient.data.l[0] = 1;
			xev.xclient.data.l[1] = CurrentTime;

			XSendEvent(x11.display, DefaultRootWindow(x11.display), False, SubstructureRedirectMask | SubstructureNotifyMask, &xev);
		#endif
        }
	
		XMapWindow(x11.display, x11.window);

		XSync(x11.display, False);


        {
        	XSetWindowBorderWidth(x11.display, x11.window, 10);
        }
    }

    #endif



	TracyCZoneEnd(create_window_zone);


	{
		Time_Measurer tm = create_time_measurer();
	
		Reflection::allow_runtime_type_generation = true;
		Reflection::init();
		log(ctx.logger, U"Reflection::init() took % ms", tm.ms_elapsed_double());
	}


#define INIT_RENDERER_ASYNCHRONOUSLY !OS_DARWIN

#if INIT_RENDERER_ASYNCHRONOUSLY
	auto renderer_init_thread = [](void* dummy_ptr)
	{
		ctx.logger = typer_logger;

#if OS_WINDOWS
		RECT client_rect;
		GetClientRect(windows.hwnd, &client_rect);

		renderer.init(client_rect.right - client_rect.left, client_rect.bottom - client_rect.top);
#elif OS_LINUX
		renderer.init(window_width, window_height);
#elif OS_DARWIN
		int osx_width, osx_height;
		osx_get_window_size(&osx_width, &osx_height);

		renderer.init(osx_width, osx_height);
#endif

		renderer_init_finished_semaphore.increment();
	};
	create_thread(c_allocator, renderer_init_thread, NULL);
#endif

	input.init();
	typer.init();


	ui.init();


#if INIT_RENDERER_ASYNCHRONOUSLY
	{
		ZoneScopedN("renderer_init_finished_semaphore.wait_and_decrement()");
		renderer_init_finished_semaphore.wait_and_decrement();
	}
#else
#if OS_WINDOWS
		RECT client_rect;
		GetClientRect(windows.hwnd, &client_rect);

		renderer.init(client_rect.right - client_rect.left, client_rect.bottom - client_rect.top);
#elif OS_LINUX
		renderer.init(window_width, window_height);
#elif OS_DARWIN
		int osx_width, osx_height;
		osx_get_window_size(&osx_width, &osx_height);

		renderer.init(osx_width, osx_height);
#endif
#endif

#if OS_WINDOWS
	{
		ZoneScopedN("ShowWindow");
		// Windows will call calbacks and crash the program cause nothing is initialized if we call this earlier.
		ShowWindow(hwnd, SW_SHOWDEFAULT);
		SetForegroundWindow(hwnd);
	}
#endif

#if OS_WINDOWS
	MSG msg;
	while (1)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			ZoneScopedN("Windows message");

			{
				ZoneScopedN("TranslateMessage");
				TranslateMessage(&msg);
			}

			{
#ifdef TRACY_ENABLE
				ZoneScopedN("DispatchMessage");
				String str = to_string(msg.message, c_allocator);
				
				ZoneText(str.data, str.length);

				c_allocator.free(str.data, code_location());
#endif

				DispatchMessage(&msg);
			}

			switch (msg.message)
			{
				case WM_QUIT:
				{
                    typer.terminate();
                    
					fflush(stdout);
					fclose(stdout);

					return 0;
				}
			}
		}
		else
		{
			if (is_inside_window_creation)
				is_inside_window_creation = false;

			if (have_to_inform_window_about_changes)
			{
				SetWindowPos(windows.hwnd, 
                     NULL, 
                     0, 0,
                     0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);


				have_to_inform_window_about_changes = false;
			}

			if (renderer.use_vulkan)
			{
				if (!redraw_current_frame && frame_index != 0)
				{
					ZoneScopedNC("DwmFlush", 0x00ff00);

					DwmFlush();
				}
			}
			else
			{
				if (settings.SACRIFICE_ONE_CPU_CORE_FOR_TRUE_PC_GAMING_EXPIRIENCE_FPS)
				{
					
				}
				else
				{
					ZoneScopedNC("DwmFlush", 0x00ff00);
					
					DwmFlush();
				}
			}

			typer.do_frame();
		}
	}
#elif OS_LINUX

    {
        XEvent event;

        while (true)
        {
            if (XCheckWindowEvent(x11.display, x11.window, LONG_MAX, &event))
            {
				X11EventType e_type = (X11EventType) event.type;
				// Log(U"New XEvent. Type: %. Frame: %", e_type, frame_index);

                switch (event.type)
                {
                    case KeyPress:
                    {
					Input_Node node;
					node.input_type = Input_Type::Key;
					node.key_action = Key_Action::Down;
					node.key = map_x11_key(XKeycodeToKeysym(x11.display, event.xkey.keycode, 0));
					node.key_code = event.xkey.keycode;

					input.nodes.add(node);

					// Generate char input from key press.
					{
						XIM x11_input_method = XOpenIM(x11.display, NULL, NULL, NULL);
						XIC x11_input_context = XCreateIC(x11_input_method, 
							XNInputStyle,   XIMPreeditNothing | XIMStatusNothing,
							XNClientWindow, x11.window,
							XNFocusWindow,  x11.window,
						NULL);

						defer {
							XDestroyIC(x11_input_context);
							XCloseIM(x11_input_method);
						};

						wchar_t buffer[48];
						int chars_count = XwcLookupString(x11_input_context, &event.xkey, buffer, sizeof(buffer), NULL, NULL);

						for (int i = 0; i < chars_count; i++)
						{
							if (u_iscntrl(buffer[i])) continue;

							Input_Node char_node = {
								.input_type = Input_Type::Char,
								.character = buffer[i],
							};

							input.nodes.add(char_node);
						}
					}
                    }
                    break;

                    case KeyRelease:
                    {
					Input_Node node;
					node.input_type = Input_Type::Key;
					node.key_action = Key_Action::Up;
					node.key = map_x11_key(XKeycodeToKeysym(x11.display, event.xkey.keycode, 0));
					node.key_code = event.xkey.keycode;

					input.nodes.add(node);
                    }
                    break;

					case ButtonPress:
					case ButtonRelease:
					{
						if (event.xbutton.button >= 1 && event.xbutton.button <= 3)
						{
							Input_Node node;
							node.input_type = Input_Type::Key;
							node.key_action = event.type == ButtonPress ? Key_Action::Down : Key_Action::Up;

							switch (event.xbutton.button)
							{
								case 1:
									node.key = Key::LMB;
									node.key_code = XK_Pointer_Button1;
									break;		
							
								case 2:
									node.key = Key::MMB;
									node.key_code = XK_Pointer_Button2;
									break;

								case 3:
									node.key = Key::RMB;
									node.key_code = XK_Pointer_Button3;
									break;
							}

							input.nodes.add(node);

							// XSetInputFocus(x11.display, x11.window, 0 /* RevertToNone */, CurrentTime);
						}
						else if (event.xbutton.button == Button4 || event.xbutton.button == Button5)
						{
							input.mouse_wheel_delta += (event.xbutton.button == Button4 ? -1 : 1);
						}
					}
					break;

					case ConfigureNotify:
					{
						XConfigureEvent xce = event.xconfigure;
						
						handle_os_resize_event(xce.width, xce.height);
					}
					break;

                    case FocusIn:
                    {
                        has_window_focus = true;
                    }
                    break;

                    case FocusOut:
                    {
                        has_window_focus = false;
                    }
                    break;
                }
            }
            else
            {
                typer.do_frame();
            }
        }
    }
#elif OS_DARWIN
	// Loop is implemented in ObjectiveC
#else
	assert(false); // This means that application main loop is not implemented
#endif
}

#if OS_DARWIN
void typer_do_frame_osx()
{
	typer.do_frame();	
}
#endif

inline void handle_os_resize_event(int new_width, int new_height)
{
	if (window_width != new_width ||
		window_height != new_height)
	{
		window_width = new_width;
		window_height = new_height;

	#if 0
		if (IsMaximized(h))
		{
			// window_width  -= 8;
			window_height -= 8;
		}
	#endif
		window_size_changed = true;
	}

	next_redraw_frame = frame_index;
	typer_ui.need_to_keep_scroll_state_this_frame = true;

#if OS_WINDOWS
	if (!is_inside_window_creation)
	{
		// If buffer is big recalculate_renderer_lines will be slow and will prevent smooth resizing.
		if (typer_ui.screen_page != Screen_Page::Terminal || terminal.characters.count < 100000)
		{
			typer.do_frame();
		}
	}
#endif
}


bool is_mouse_over_terminal()
{
#if OS_WINDOWS

	int window_border = renderer.scaled(window_border_size);
	return  input.mouse_x <= get_active_width() &&
			input.mouse_y >= 0 &&
			input.mouse_y < window_border + get_active_height();

#elif OS_LINUX
	
	int window_resize_border = is_x11_window_likely_maximized ? 0 : window_resize_border_size;

	return  input.mouse_x <= get_active_width() - window_resize_border &&
			input.mouse_y >= window_resize_border &&
			input.mouse_y <  get_active_height();

#endif
}




void copy_buffer_region_to_os_clipboard(int start, int length)
{
	Scoped_Lock characters_lock(terminal.characters_mutex);

	if (length == 0) return;

	Allocator allocator = frame_allocator;
	if (length > 4096) // Arena_Allocator is not good for big memory allocation
	{
		allocator = c_allocator;
	}

	Unicode_String buffer_string = terminal.copy_region(start, length, allocator);
	copy_to_os_clipboard(buffer_string, allocator);

	allocator.free(buffer_string.data, code_location());
};



void copy_selection_to_clipboard()
{
	Scoped_Lock characters_lock(terminal.characters_mutex);

	if (selection_length)
	{
		copy_buffer_region_to_os_clipboard(min(user_cursor, user_cursor + selection_length), abs(selection_length));
	}
}

void paste_from_clipboard_to_user_cursor()
{
	Scoped_Lock characters_lock(terminal.characters_mutex);

	bool can_insert = true;
	
	u64 selection_start = min(user_cursor, user_cursor + selection_length);
	u64 selection_end   = max(user_cursor, user_cursor + selection_length);

	for (u64 i = selection_start; i <= selection_end; i++)
	{
		if (!is_character_at_index_is_user_input(i))
		{
			can_insert = false;				
		}
	}

	if (can_insert)
	{
		Unicode_String str = get_os_clipboard<char32_t>(c_allocator);

		String_Builder<char32_t> b = build_string<char32_t>(c_allocator, str.length); 
		for (int i = 0; i < str.length; i++)
		{
			char32_t c = str[i];
			if (c != '\r')
				b.append(c);
		}

		c_allocator.free(str.data, code_location());
		defer { b.free(); };
		str = b.get_string();


		typer_ui.invalidate_after(selection_start);

		terminal.remove_non_process(selection_start, abs(selection_length));

		user_cursor = selection_start + str.length;

		terminal.append_non_process(selection_start, str, TERMINAL_CHARACTER_TYPE_USER_INPUT);


		selection_length = 0;
	}

	need_to_redraw_next_frame(code_location());
}

void cut_from_clipboard_to_user_cursor()
{
	Scoped_Lock characters_lock(terminal.characters_mutex);

	bool can_insert = true;

	u64 selection_start = min(user_cursor, user_cursor + selection_length);
	u64 selection_end = max(user_cursor, user_cursor + selection_length);

	for (u64 i = selection_start; i <= selection_end; i++)
	{
		if (!is_character_at_index_is_user_input(i))
		{
			can_insert = false;
		}
	}

	if (can_insert)
	{
		copy_buffer_region_to_os_clipboard(selection_start, abs(selection_length));

		
		typer_ui.invalidate_after(selection_start - 1);

		terminal.remove_non_process(selection_start, abs(selection_length));

		user_cursor = selection_start;
		selection_length = 0;
	}
}
