#include "Input.h"

#include "Tracy_Header.h"

#include "Main.h"

#if OS_WINDOWS
Key map_windows_key(WPARAM wParam)
{
	#define binding(from, to) case from: return Key:: to;

	switch (wParam)
	{
		binding(VK_UP,    Up_Arrow);
		binding(VK_DOWN,  Down_Arrow);
		binding(VK_LEFT,  Left_Arrow);
		binding(VK_RIGHT, Right_Arrow);

		binding(VK_SHIFT,    Any_Shift);
		binding(VK_CONTROL,  Any_Control);
		binding(VK_MENU,     Any_Alt);

		binding(VK_BACK,   Backspace);
		binding(VK_DELETE, Delete);
		binding(VK_RETURN, Enter);
		binding(VK_ESCAPE, Escape);
		binding(VK_TAB,    Tab);

		binding(VK_LBUTTON, LMB);
		binding(VK_RBUTTON, RMB);
		binding(VK_MBUTTON, MMB);

		binding(VK_F1,  F1);
		binding(VK_F2,  F2);
		binding(VK_F3,  F3);
		binding(VK_F4,  F4);
		binding(VK_F5,  F5);
		binding(VK_F6,  F6);
		binding(VK_F7,  F7);
		binding(VK_F8,  F8);
		binding(VK_F9,  F9);
		binding(VK_F10, F10);
		binding(VK_F11, F11);
		binding(VK_F12, F12);
		
		binding(VK_PRIOR, Page_Up);
		binding(VK_NEXT,  Page_Down);

		binding(VK_ADD,      Plus);
		binding(VK_SUBTRACT, Minus);

		binding(VK_OEM_PLUS,  Plus);
		binding(VK_OEM_MINUS, Minus);

		// Character keys will be bound here
		default: 
			return (Key) wParam;
	}

	#undef binding
}
#elif OS_LINUX

Key map_x11_key(KeySym keysym)
{
	#define binding(from, to) case from: return Key:: to;

	switch (keysym)
	{
		binding(XK_Up,    Up_Arrow);
		binding(XK_Down,  Down_Arrow);
		binding(XK_Left,  Left_Arrow);
		binding(XK_Right, Right_Arrow);

		binding(XK_Shift_L,  Any_Shift);
		binding(XK_Shift_R,  Any_Shift);

		binding(XK_Control_L, Any_Control);
		binding(XK_Control_R, Any_Control);

		binding(XK_Alt_L, Any_Alt);
		binding(XK_Alt_R, Any_Alt);

		binding(XK_BackSpace,   Backspace);
		binding(XK_Delete, Delete);
		binding(XK_KP_Delete, Delete);

		binding(XK_Return, Enter);
		binding(XK_KP_Enter, Enter);

		binding(XK_Escape, Escape);
		binding(XK_Tab,    Tab);

		binding(XK_Pointer_Button1, LMB);
		binding(XK_Pointer_Button3, RMB);
		binding(XK_Pointer_Button2, MMB);

		binding(XK_F1,  F1);
		binding(XK_F2,  F2);
		binding(XK_F3,  F3);
		binding(XK_F4,  F4);
		binding(XK_F5,  F5);
		binding(XK_F6,  F6);
		binding(XK_F7,  F7);
		binding(XK_F8,  F8);
		binding(XK_F9,  F9);
		binding(XK_F10, F10);
		binding(XK_F11, F11);
		binding(XK_F12, F12);
		
		binding(XK_Page_Up, Page_Up);
		binding(XK_KP_Page_Up, Page_Up);
		
		binding(XK_Page_Down,  Page_Down);
		binding(XK_KP_Page_Down,  Page_Down);

		// Character keys will be bound here
		default:
			if (isalpha(keysym))
				return (Key) toupper(keysym);


			return (Key) keysym;
	}

	#undef binding
}
#endif




bool Input::is_key_down(Key key)
{
	for (Input_Key_State& pressed_key: pressed_keys)
	{
		if (pressed_key.key_code == key)
		{
			return pressed_key.action == Key_Action::Down;
			break;
		}
	}

	return false;
}

bool Input::is_key_down_or_held(Key key)
{
	for (Input_Key_State& pressed_key: pressed_keys)
	{
		if (pressed_key.key_code == key)
		{
			return (pressed_key.action == Key_Action::Down) || (pressed_key.action == Key_Action::Hold);
			break;
		}
	}

	return false;
}

bool Input::is_key_held(Key key)
{
	for (Input_Key_State& pressed_key: pressed_keys)
	{
		if (pressed_key.key_code == key)
		{
			return pressed_key.action == Key_Action::Hold;
			break;
		}
	}

	return false;
}

bool Input::is_key_up(Key key)
{
	for (Input_Key_State& pressed_key: pressed_keys)
	{
		if (pressed_key.key_code == key)
		{
			return pressed_key.action == Key_Action::Up;
			break;
		}
	}

	return false;
}

Input_Key_State* Input::find_key_state(Key key)
{
	for (Input_Key_State& pressed_key: pressed_keys)
	{
		if (pressed_key.key_code == key) 
			return &pressed_key;
	}

	return NULL;
}

bool Input::is_key_combo_held(Dynamic_Array<Key> keys)
{
	assert(keys.count);

	for (Key requested_key: keys)
	{
		Input_Key_State* key_state = find_key_state(requested_key);

		if (!key_state) return false;
	
		if (key_state->action == Key_Action::Down || key_state->action == Key_Action::Hold)
		{
			continue;
		}

		return false;
	}

	return true;
}

bool Input::is_key_combo_pressed(Dynamic_Array<Key> keys)
{	
	assert(keys.count);

	bool any_key_down = false;

	for (Key requested_key: keys)
	{
		Input_Key_State* key_state = find_key_state(requested_key);

		if (!key_state) return false;
	
		if (key_state->action == Key_Action::Down)
		{
			any_key_down = true;
			continue;
		}

		if (key_state->action != Key_Action::Hold)
		{
			return false;
		}
	}

	return any_key_down;
}
bool Input::is_key_combo_pressed(Key key_1, Key key_2)
{
	Key keys[2];
	keys[0] = key_1;
	keys[1] = key_2;

	return is_key_combo_pressed(Dynamic_Array<Key>::from_static_array(keys));
}



void Input::init()
{
	ZoneScoped;

	nodes = Dynamic_Array<Input_Node>(32, c_allocator);
	pressed_keys = Dynamic_Array<Input_Key_State>(32, c_allocator);
}


void Input::update_key_states()
{
	ZoneScoped;

	for (Input_Key_State& pressed_key: pressed_keys)
	{
		pressed_key.press_time += frame_time;

		if (pressed_key.action == Key_Action::Down)
		{
			pressed_key.action = Key_Action::Hold;
		}
	}

	for (int i = 0; i < pressed_keys.count; i++)
	{
		if (pressed_keys[i]->action == Key_Action::Up)
		{
			pressed_keys.remove_at_index(i);
			i--;
		}
	}


	if (!has_window_focus)
    {
		for (Input_Key_State& pressed_key: pressed_keys)
		{
			pressed_key.action = Key_Action::Up;
		}

		return;
	}


	for (Input_Node& node : input.nodes)
	{
		if (node.input_type != Input_Type::Key) continue;

		if (node.key_action == Key_Action::Down)
		{
			bool key_already_exists = false;

			for (Input_Key_State& pressed_key: pressed_keys)
			{
				if (pressed_key.key_code == node.key)
				{
					key_already_exists = true;
					break;
				}
			}

			if (key_already_exists) continue;


			pressed_keys.add({
				.key_code = node.key,

				.press_time = 0.0,
				.action = Key_Action::Down,
			});
		}
		else if (node.key_action == Key_Action::Up)
		{
			for (Input_Key_State& pressed_key: pressed_keys)
			{
				if (pressed_key.key_code == node.key)
				{
					pressed_key.action = Key_Action::Up;
					break;
				}
			}
		}
	}


	if (input.is_key_down(Key::LMB))
	{
        capture_mouse();
	}
	else if (input.is_key_up(Key::LMB))
	{
        release_mouse();
	}
}

void Input::pre_frame()
{
	 // Get mouse position
	{
    #if OS_WINDOWS

		POINT cursor_pos;
		GetCursorPos(&cursor_pos);

		ScreenToClient(windows.hwnd, &cursor_pos);

		mouse_x = cursor_pos.x;
		mouse_y = renderer.height - cursor_pos.y;

    #elif OS_LINUX

		x11_get_mouse_position(&mouse_x, &mouse_y, x11.window);
		mouse_y = renderer.height - mouse_y;

    #endif

		mouse_x -= renderer.framebuffer_margin.x_left;
		mouse_y += renderer.framebuffer_margin.y_bottom;
	}

	mouse_x_delta = mouse_x - old_mouse_x;
	mouse_y_delta = mouse_y - old_mouse_y;

	old_mouse_x = mouse_x;
	old_mouse_y = mouse_y;

	update_key_states();
}

void Input::post_frame()
{
	mouse_wheel_delta = 0;
	nodes.clear();
}