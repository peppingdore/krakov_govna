#include "Tracy_Header.h"


#include "Key_Bindings.h"

#include "b_lib/Tokenizer.h"

#include "Main.h"



void Key_Bindings::init()
{
	bound_actions = make_array<Binding_And_Action>(32, c_allocator);

	triggered_actions = make_array<Triggered_Action>(32, c_allocator);

	recorded_binding.keys = make_array<Key>(4, c_allocator);

	keys_used_by_triggered_actions = make_array<Key>(8, c_allocator);


	bool load_success = load_bindings();

	if (!load_success)
	{
		use_default_bindings();

		// @TODO @XXX: REMOVE
		// Do not save. Save only if user have changed the bindings
	}
}


int Key_Bindings::get_binding_trigger_count(Key_Binding binding)
{
	if (!binding.keys.count) return 0;

	int trigger_count = 1;

	if (binding.keys.contains(Key::Mouse_Wheel_Up))
	{
		// @CopyPaste: MouseWheelBindingHandling
		if (input.mouse_wheel_delta >= 0) return 0;
	
		auto keys_copy = binding.keys.copy_with(frame_allocator);
		keys_copy.remove_all(Key::Mouse_Wheel_Up);

		if (!keys_copy.count || input.is_key_combo_held(keys_copy))
		{
			return -input.mouse_wheel_delta;
		}
		else
		{
			return false;
		}
	}

	if (binding.keys.contains(Key::Mouse_Wheel_Down))
	{
		// @CopyPaste: MouseWheelBindingHandling
		if (input.mouse_wheel_delta < 0) return 0;

		auto keys_copy = binding.keys.copy_with(frame_allocator);
		keys_copy.remove_all(Key::Mouse_Wheel_Down);

		if (!keys_copy.count || input.is_key_combo_held(keys_copy))
		{
			return input.mouse_wheel_delta;
		}
		else
		{
			return false;
		}
	}

	if (!input.is_key_combo_pressed(binding.keys)) return 0;

	return trigger_count;
}

int Key_Bindings::get_action_type_trigger_count(Action_Type action_type)
{
	int trigger_count = 0;

	for (auto& triggered_action: triggered_actions)
	{
		if (triggered_action.action.type == action_type)
		{
			trigger_count += triggered_action.trigger_count;
		}
	}

	return trigger_count;
}

bool Key_Bindings::is_action_type_triggered(Action_Type action_type)
{
	return get_action_type_trigger_count(action_type) > 0;
}


#if 0
void Key_Bindings::process_key_binding_recording()
{
	// @TODO: show actions with current binding.

	// Refresh ui_id
	key_binding_recording_ui_id = ui.copy_ui_id(key_binding_recording_ui_id, ui.current_arena_allocator);


	scoped_set_and_revert(ui.current_layer, ui.current_layer + 10);

	UI_ID ui_id = ui_id(0);

	Rect rect = Rect::make_from_center_and_size(renderer.width / 2, typer_ui.y_top / 2, renderer.scaled(binding_picker_size.x), renderer.scaled(binding_picker_size.y));
		


	renderer.draw_rect(rect, rgba(10, 10, 10, 255));


	// Cover whole screen to not allow any input
	ui.im_hovering(ui_id);

	if (rect.is_point_inside(input.mouse_x, input.mouse_y))
	{
		ui.im_hovering(ui_id);
	}


	{
		scoped_set_and_revert(ui.parameters.text_alignment, Text_Alignment::Center);
		scoped_set_and_revert(ui.parameters.text_font_face_size, 12);


		Unicode_String draw_text;
		if (recorded_binding.keys.count)
		{
			draw_text = key_binding_to_string(recorded_binding, frame_allocator);
		}
		else
		{
			draw_text = U"No binding";
		}

		ui.draw_text(rect.center_x(), rect.center_y(), draw_text);
	}

	{
		scoped_set_and_revert(ui.parameters.text_font_face_size, renderer.scaled(binding_picker_button_text_size));

		Rect accept_button_rect = rect;

		accept_button_rect.y_top = accept_button_rect.y_bottom + renderer.scaled(36);
		accept_button_rect.x_right = rect.center_x();

		Rect cancel_button_rect = accept_button_rect;
		cancel_button_rect.x_left = accept_button_rect.x_right;
		cancel_button_rect.x_right = rect.x_right;

		// @Hack: manual mouse detection to preventing LMB overrding bind on the exit
		if (ui.button(accept_button_rect, U"OK", rgba(30, 190, 30, 255), ui_id(0)) ||
			(input.is_key_down(Key::LMB) && accept_button_rect.is_point_inside(input.mouse_x, input.mouse_y)))
		{
			recorded_key_binding_ui_id = key_binding_recording_ui_id;
			key_binding_recording_ui_id = invalid_ui_id;
			return;
		}

		if (ui.button(cancel_button_rect, U"Cancel", rgba(190, 30, 30, 255), ui_id(0)))
		{
			recorded_key_binding_ui_id  = invalid_ui_id;
			key_binding_recording_ui_id = invalid_ui_id;
			return;
		}
	}

	renderer.draw_rect_outline(rect.shrinked(1, 1, 1, 1), rgba(200, 200, 200, 255));



	bool ctrl  = input.is_key_down_or_held(Key::Any_Control); 
	bool alt   = input.is_key_down_or_held(Key::Any_Alt); 
	bool shift = input.is_key_down_or_held(Key::Any_Shift); 

	Dynamic_Array<Key> other_keys = make_array<Key>(4, frame_allocator);


	bool any_down_key = false;
	
	if (input.is_key_down(Key::Any_Control) ||
		input.is_key_down(Key::Any_Alt) ||
		input.is_key_down(Key::Any_Shift))
	{
		any_down_key = true;
	}

	for (auto& pressed_key: input.pressed_keys)
	{
		if (pressed_key.action == Key_Action::Up) continue;

		if (pressed_key.key_code == Key::Any_Control || 
			pressed_key.key_code == Key::Any_Alt ||
			pressed_key.key_code == Key::Any_Shift) continue;

		other_keys.add(pressed_key.key_code);

		if (pressed_key.action == Key_Action::Down)
		{
			any_down_key = true;
		}
	}

	if (input.mouse_wheel_delta)
	{
		any_down_key = true;

		other_keys.add(sign(input.mouse_wheel_delta) > 0 ?
			Key::Mouse_Wheel_Down :
			Key::Mouse_Wheel_Up);
	}

	if (any_down_key)
	{
		recorded_binding.keys.clear();

		if (ctrl)
			recorded_binding.keys.add(Key::Any_Control);
		if (alt)
			recorded_binding.keys.add(Key::Any_Alt);
		if (shift)
			recorded_binding.keys.add(Key::Any_Shift);

		recorded_binding.keys.add(other_keys);
	}

#if 1
	ui.active_mask_stack.add({
		.rect = rect,
		.inversed = true,
	});
	ui.set_active_masks_as_renderer_masks();
#endif
}


void Key_Bindings::begin_key_binding_recording(UI_ID ui_id)
{
	assert(key_binding_recording_ui_id == invalid_ui_id);

	key_binding_recording_ui_id = ui.copy_ui_id(ui_id, ui.current_arena_allocator);

	recor.keys.clear();
}
#endif

void Key_Bindings::do_frame()
{
	keys_used_by_triggered_actions.clear();
	triggered_actions.clear();

// @TODO: enable key binding recording.
#if 0
	if (key_binding_recording_ui_id != invalid_ui_id)
	{
		process_key_binding_recording();
	}
	else
	{
#endif
		recorded_key_binding_ui_id = invalid_ui_id;

		for (auto& item: bound_actions)
		{
			int trigger_count = get_binding_trigger_count(item.binding);

			if (trigger_count)
			{
				triggered_actions.add({
					.binding = item.binding,
					.action  = item.action,
					.trigger_count = trigger_count
				});
			}
		}
#if 0
	}
#endif


	for (auto& triggered_action: triggered_actions)
	{
		keys_used_by_triggered_actions.add_range(triggered_action.binding.keys.data, triggered_action.binding.keys.count);
	}
}




bool Key_Bindings::load_bindings()
{
	Unicode_String path = path_concat(frame_allocator, executable_directory, bindings_file_name);
	File file = open_file(frame_allocator, path, FILE_READ);

	if (!file.succeeded_to_open())
	{
		Log(U"Failed to open %. Using default bindings", bindings_file_name);
		return false;
	}

	defer {
		file.close();
	};

	using namespace Reflection;
	
	// @MemoryLeak: nothing is properly freed here.
	Tokenizer tokenizer = tokenize(&file, frame_allocator);

	Dynamic_Array<Binding_And_Action> loaded_bindings;

	if (!read_thing(&tokenizer, &loaded_bindings, frame_allocator, c_allocator))
	{
		Log(U"Failed to read bindings from %, Using default bindings.", bindings_file_name);
		return false;
	}



	bound_actions.free();
	bound_actions = loaded_bindings;

	return true;
}

void Key_Bindings::save_bindings()
{
	ZoneScoped;

	assert(threading.is_main_thread());

	Unicode_String path = path_concat(frame_allocator, executable_directory, bindings_file_name);
	File file = open_file(frame_allocator, path, FILE_WRITE | FILE_CREATE_NEW);

	if (!file.succeeded_to_open())
	{
		Log(U"Failed to open % to bindings", bindings_file_name);
		return;
	}

	defer { file.close(); };


	file.write(write_thing(bound_actions, frame_allocator));
}

void Key_Bindings::use_default_bindings()
{
	auto build_simple_action = [&](Action_Type action_type) -> Action
	{
		return {
			.type = action_type,
		};
	};

	auto put_binding = [&](std::initializer_list<Key> keys, Action action)
	{
		Key_Binding key_binding = {
			.keys = make_array(c_allocator, keys),
		};

		bound_actions.add({
			.binding = key_binding,
			.action  = action,
		});
	};



	put_binding({ Key::Any_Control, Key::C }, build_simple_action(Action_Type::Copy_Text));
	put_binding({ Key::Any_Control, Key::V }, build_simple_action(Action_Type::Paste_Text));
	put_binding({ Key::Any_Control, Key::X }, build_simple_action(Action_Type::Cut_Text));
	put_binding({ Key::Any_Control, Key::A }, build_simple_action(Action_Type::Select_All));

	put_binding({ Key::Any_Control, Key::Plus }  , build_simple_action(Action_Type::Zoom_In));
	put_binding({ Key::Any_Control, Key::Minus }, build_simple_action(Action_Type::Zoom_Out));

	put_binding({ Key::Any_Control, Key::Mouse_Wheel_Up }  , build_simple_action(Action_Type::Zoom_In));
	put_binding({ Key::Any_Control, Key::Mouse_Wheel_Down }, build_simple_action(Action_Type::Zoom_Out));

	put_binding({ Key::Any_Alt, Key::F4 }, build_simple_action(Action_Type::Exit));
	
	put_binding({ Key::F6 }, build_simple_action(Action_Type::Toggle_Usage_Of_DPI));

	put_binding({ Key::Any_Control, Key::F }, build_simple_action(Action_Type::Open_Search_Bar));

	put_binding({ Key::Any_Control, Key::C }, build_simple_action(Action_Type::Interrupt));

	put_binding({ Key::Tab }, build_simple_action(Action_Type::Autocomplete));
}

Unicode_String Key_Bindings::key_binding_to_string(Key_Binding binding, Allocator allocator)
{
	if (!binding.keys.count) return Unicode_String::empty;

	String_Builder<char32_t> builder = build_string<char32_t>(allocator);

	for (auto& key: binding.keys)
	{
		int index = binding.keys.fast_pointer_index(&key);

		String key_string = "[Unknown key]";

		Reflection::Enum_Value enum_value;
		if (Reflection::get_enum_value_from_value(key, &enum_value))
		{
			if (enum_value.tags.count > 0)
			{
				key_string = enum_value.tags[0]->name; 
			}
			else
			{
				key_string = enum_value.name;
			}
		}


		if (index > 0)
		{
			builder.append(U" + ");
		}

		builder.append(key_string);
	}

	return builder.get_string(); 
}