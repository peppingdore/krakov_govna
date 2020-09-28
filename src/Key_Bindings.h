#pragma once

#include "Input.h"

#include "b_lib/Hash_Map.h"
#include "b_lib/Reflection.h"

#include "UI.h"

struct Macro;



#define ACTION_TYPE_NAME_TAG "Action_Type_Name"

enum class Action_Type: s32
{
	None = 0,

	Run_A_Macro,

	Copy_Text,
	Paste_Text,
	Cut_Text,

	Select_All,

	Zoom_In,
	Zoom_Out,

	Exit,

	Toggle_Usage_Of_DPI,

	Open_Search_Bar,

	Interrupt,

	Autocomplete,
};
REFLECT(Action_Type)

	ENUM_VALUE(None);

	ENUM_VALUE(Run_A_Macro);
		TAG(ACTION_TYPE_NAME_TAG);
			TAG_VALUE_TYPED(String, "Run a macro");

	ENUM_VALUE(Copy_Text);
		TAG(ACTION_TYPE_NAME_TAG);
			TAG_VALUE_TYPED(String, "Copy text");

	ENUM_VALUE(Paste_Text);
		TAG(ACTION_TYPE_NAME_TAG);
			TAG_VALUE_TYPED(String, "Paste text");
	
	ENUM_VALUE(Cut_Text);
		TAG(ACTION_TYPE_NAME_TAG);
			TAG_VALUE_TYPED(String, "Cut text");

	ENUM_VALUE(Select_All);
		TAG(ACTION_TYPE_NAME_TAG);
			TAG_VALUE_TYPED(String, "Select all");


	ENUM_VALUE(Zoom_In);
		TAG(ACTION_TYPE_NAME_TAG);
			TAG_VALUE_TYPED(String, "Zoom in");

	ENUM_VALUE(Zoom_Out);
		TAG(ACTION_TYPE_NAME_TAG);
			TAG_VALUE_TYPED(String, "Zoom out");


	ENUM_VALUE(Exit);
		TAG(ACTION_TYPE_NAME_TAG);
			TAG_VALUE_TYPED(String, "Exit ");

	ENUM_VALUE(Toggle_Usage_Of_DPI);
		TAG(ACTION_TYPE_NAME_TAG);
			TAG_VALUE_TYPED(String, "Toggle DPI usage");


	ENUM_VALUE(Open_Search_Bar);
		TAG(ACTION_TYPE_NAME_TAG);
			TAG_VALUE_TYPED(String, "Open search bar");

	ENUM_VALUE(Interrupt);
		TAG(ACTION_TYPE_NAME_TAG);
			TAG_VALUE_TYPED(String, "Interrupt Python");

	ENUM_VALUE(Autocomplete);
		TAG(ACTION_TYPE_NAME_TAG);
			TAG_VALUE_TYPED(String, "Autocomplete");



REFLECT_END();

static_assert(std::is_same_v<Reflection::relaxed_underlying_type<Action_Type>::type, s32>, "Code that uses Action_Type assumes it's s32");



struct Action
{
	Action_Type type;

	Unicode_String macro_string = Unicode_String::empty;
};
REFLECT(Action)
	MEMBER(type);
	MEMBER(macro_string);
REFLECT_END();

struct Key_Binding
{
	Dynamic_Array<Key> keys;
};
REFLECT(Key_Binding)
	MEMBER(keys);
REFLECT_END();

inline bool operator==(const Key_Binding a, const Key_Binding b)
{
	return a.keys == b.keys;
}


struct Binding_And_Action
{
	Key_Binding binding;
	Action action;
};
REFLECT(Binding_And_Action)
	MEMBER(binding);
	MEMBER(action);
REFLECT_END();

struct Triggered_Action
{
	Key_Binding binding;
	Action action;
	int trigger_count;
};


struct Key_Bindings
{
	Vector2i binding_picker_size = { 400, 200 };
	int      binding_picker_button_text_size = 11;


	// Hash map is not suitable here, because same Key_Binding can have multiple Actions
	// and handling that in hash map will suck i think. 
	Dynamic_Array<Binding_And_Action> bound_actions;

	Dynamic_Array<Triggered_Action> triggered_actions;

	Dynamic_Array<Key> keys_used_by_triggered_actions;


	Unicode_String bindings_file_name = U"key_bindings.txt";



	inline bool is_key_used_by_triggered_action(Key key)
	{
		return keys_used_by_triggered_actions.contains(key);
	}

	int get_binding_trigger_count(Key_Binding binding);


	int get_action_type_trigger_count(Action_Type action_type);
	bool is_action_type_triggered(Action_Type action_type);


	bool load_bindings();
	void save_bindings();

	void use_default_bindings();

	Unicode_String key_binding_to_string(Key_Binding binding, Allocator allocator);


	UI_ID key_binding_recording_ui_id = invalid_ui_id;
	UI_ID recorded_key_binding_ui_id  = invalid_ui_id;

	Key_Binding recorded_binding;

	void begin_key_binding_recording(UI_ID ui_id);

	void process_key_binding_recording();

	void init();

	void do_frame();
};
inline Key_Bindings key_bindings;