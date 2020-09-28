#pragma once

#include "b_lib/Dynamic_Array.h"
#include "b_lib/Reflection.h"


#if OS_WINDOWS
#include <Windows.h>
#include <windowsx.h>
#endif

#if OS_LINUX
#include "X11_Header.h"
#endif




// Keys should be serialized by names.
enum class Key: u32
{
	Unknown = 0,


 	// Used by key bindings system.
	Mouse_Wheel_Up,
	Mouse_Wheel_Down,	


	Up_Arrow,
	Down_Arrow,
	Left_Arrow,
	Right_Arrow,

	Any_Shift,
	Any_Control,
	Any_Alt,

	Backspace,
	Delete,
	Enter,
	Escape,
	Tab,

	LMB,
	RMB,
	MMB,


	F1,
	F2,
	F3,
	F4,
	F5,
	F6,
	F7,
	F8,
	F9,
	F10,
	F11,
	F12,


	Page_Up,
	Page_Down,


	C = 'C',
	V = 'V',
	X = 'X',
	A = 'A',
	F = 'F',

	Plus,
	Minus,
};
REFLECT(Key)
	ENUM_FLAGS(false);


	ENUM_VALUE(Mouse_Wheel_Up);
		TAG("Mouse Wheel Up");
	ENUM_VALUE(Mouse_Wheel_Down);	
		TAG("Mouse Wheel Down");



	ENUM_VALUE(Up_Arrow);
	ENUM_VALUE(Down_Arrow);
	ENUM_VALUE(Left_Arrow);
	ENUM_VALUE(Right_Arrow);
	
	ENUM_VALUE(Any_Shift);
		TAG("Shift");
	ENUM_VALUE(Any_Control);
		TAG("Ctrl");
	ENUM_VALUE(Any_Alt);
		TAG("Alt");

	ENUM_VALUE(Backspace);
	ENUM_VALUE(Delete);
	ENUM_VALUE(Enter);
	ENUM_VALUE(Escape);

	ENUM_VALUE(LMB);
	ENUM_VALUE(RMB);
	ENUM_VALUE(MMB);

	ENUM_VALUE(F1);
	ENUM_VALUE(F2);
	ENUM_VALUE(F3);
	ENUM_VALUE(F4);
	ENUM_VALUE(F5);
	ENUM_VALUE(F6);
	ENUM_VALUE(F7);
	ENUM_VALUE(F8);
	ENUM_VALUE(F9);
	ENUM_VALUE(F10);
	ENUM_VALUE(F11);
	ENUM_VALUE(F12);


	ENUM_VALUE(C);
	ENUM_VALUE(V);
	ENUM_VALUE(X);
	ENUM_VALUE(A);
	ENUM_VALUE(F);

	ENUM_VALUE(Plus);
		TAG("+");
	ENUM_VALUE(Minus);
		TAG("-");


REFLECT_END();

#if OS_WINDOWS
Key map_windows_key(WPARAM wParam);
#elif OS_LINUX
Key map_x11_key(KeySym key_sym);
#endif




#if OS_WINDOWS

using OS_Key_Code = WPARAM;

#elif OS_LINUX

using OS_Key_Code = KeySym;

#elif OS_DARWIN

// @TODO: int is just a dummy
using OS_Key_Code = int;

#endif


enum class Input_Type
{
	Key,
	Char,
};
enum class Key_Action
{
	Down,
	Up,
	Hold,
};
REFLECT(Key_Action)
	ENUM_VALUE(Down);
	ENUM_VALUE(Up);
	ENUM_VALUE(Hold);
REFLECT_END();

struct Input_Node
{
	Input_Type input_type;

	union
	{
		struct
		{
			OS_Key_Code key_code;
			Key key;
			Key_Action key_action;
		};
		char32_t character;
	};
};


struct Input_Key_State
{
	Key key_code;
	float press_time;

	Key_Action action;
};
REFLECT(Input_Key_State)
	MEMBER(key_code);
	MEMBER(press_time);
	MEMBER(action);
REFLECT_END();

struct Input
{
	Dynamic_Array<Input_Node> nodes;

	Dynamic_Array<Input_Key_State> pressed_keys;


	int old_mouse_x = 0;
	int old_mouse_y = 0;

	int mouse_x = 0;
	int mouse_y = 0;

	int mouse_x_delta = 0;
	int mouse_y_delta = 0;

	int mouse_wheel_delta = 0;



	void init();

	void update_key_states();

	void pre_frame();
	void post_frame();

	bool is_key_down(Key key);
	bool is_key_down_or_held(Key key);
	bool is_key_held(Key key);
	bool is_key_up(Key key);


	Input_Key_State* find_key_state(Key key);

	bool is_key_combo_held(Dynamic_Array<Key> keys);

	bool is_key_combo_pressed(Dynamic_Array<Key> keys);
	bool is_key_combo_pressed(Key key_1, Key key_2);
};
inline Input input;

