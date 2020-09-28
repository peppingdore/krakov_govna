#pragma once

#include "Tracy_Header.h"

#include "Renderer.h"

#include "b_lib/Text_Editor.h"
#include "b_lib/UUID.h"
#include "b_lib/Arena_Allocator.h"



// Ideas in UI code are from Seeque's UI system. 

enum class UI_ID_Type: u8
{
	Id,
	Uuid,
};
REFLECT(UI_ID_Type)
	ENUM_VALUE(Id);
	ENUM_VALUE(Uuid);
REFLECT_END();

struct UI_ID
{
	const char* call_site_file_name;
	u32 call_site_line;

	UI_ID_Type type;
	union
	{
		u64 id;
		UUID uuid;
	};
	u32 sub_id;

	UI_ID* next;


	UI_ID() {};

	constexpr UI_ID(const char* call_site_file_name, u32 call_site_line, u64 id, u32 sub_id = 0, UI_ID* next = NULL):
		call_site_file_name(call_site_file_name),
		call_site_line(call_site_line),
		type(UI_ID_Type::Id),
		id(id),
		sub_id(sub_id),
		next(next)
	{
	}

	constexpr UI_ID(const char* call_site_file_name, u32 call_site_line, UUID uuid, u32 sub_id, UI_ID* next = NULL):
		call_site_file_name(call_site_file_name),
		call_site_line(call_site_line),
		type(UI_ID_Type::Uuid),
		uuid(uuid),
		sub_id(sub_id),
		next(next)
	{
	}


	inline bool operator==(const UI_ID other) const
	{
		// assert(!call_site_file_name || !use_uuid);

		bool pre_test = other.call_site_file_name == call_site_file_name
			&& other.call_site_line == call_site_line
			&& other.type == type
			&& (type == UI_ID_Type::Uuid ? ((other.uuid == uuid) && (other.sub_id == sub_id)) :
										   ((other.id == id)     && (other.sub_id == sub_id)));
		if (!pre_test) return false;
		if ((next == NULL) && (other.next == NULL)) return true;
		if ((next == NULL) || (other.next == NULL)) return false;

		return (*next == *other.next);
 	}

	inline bool operator!=(const UI_ID other) const
	{
		return !(other == *this);
	}
};
REFLECT(UI_ID)
	MEMBER(call_site_line);
	MEMBER(type);
	MEMBER(id);
	MEMBER(sub_id);
	MEMBER(next);
REFLECT_END();


#define ui_id( id ) UI_ID(__FILE__, __LINE__, id )
#define ui_id_next( id, sub_id, next ) UI_ID(__FILE__, __LINE__, id, sub_id, next )
#define ui_id_sub_id( id, sub_id ) UI_ID(__FILE__, __LINE__, id, sub_id )
#define ui_id_uuid( uuid , sub_id ) UI_ID(__FILE__, __LINE__, uuid, sub_id )

constexpr UI_ID invalid_ui_id = UI_ID(NULL, 0, UUID {}, 0);
constexpr UI_ID null_ui_id = UI_ID("Bruh", u32_max, UUID{}, 0);



struct UI_Button_State
{
	float darkness = 0;
};

struct UI_Checkbox_State
{
	int dummy = 0;
};

enum UI_Text_Editor_Finish_Cause: u32
{
	UI_TEXT_EDITOR_MODIFIED_TEXT,
	UI_TEXT_EDITOR_PRESSED_ENTER,
	UI_TEXT_EDITOR_CLICKED_ON_SMTH_ELSE,
};
REFLECT(UI_Text_Editor_Finish_Cause)
	ENUM_VALUE(UI_TEXT_EDITOR_MODIFIED_TEXT);
	ENUM_VALUE(UI_TEXT_EDITOR_PRESSED_ENTER);
	ENUM_VALUE(UI_TEXT_EDITOR_CLICKED_ON_SMTH_ELSE);
REFLECT_END();





struct UI_Scroll_Region_State;


struct Scroll_Region_Result
{
	bool did_show_scrollbar;
	bool did_show_horizontal_scrollbar;

	int scroll_from_top;
	int scroll_from_left;

	int desired_precursor_pixels;

	UI_Scroll_Region_State* state;

	Rect view_rect;
};


struct UI_Multiline_Text_Editor_State
{
	String_Builder<char32_t> builder;
	Text_Editor<char32_t>    editor;
	bool editing = false;

	int desired_precursor_pixels = -1;

	float mouse_offscreen_scroll_target_x = 0;
	float mouse_offscreen_scroll_target_y = 0;

	UI_ID scroll_region_ui_id = null_ui_id;
	Scroll_Region_Result previous_scroll_region_result;
};

struct UI_Dropdown_State
{
	bool is_selected = false;
};

struct UI_Scroll_Region_State
{
	int scroll_from_top  = 0;
	int scroll_from_left = 0;

	bool dragging_vertical_scrollgrip   = false;
	bool dragging_horizontal_scrollgrip = false;

	int dragging_scrollgrip_offset_top  = 0;
	int dragging_scrollgrip_offset_left = 0;
};


struct UI_File_Picker_State
{
	bool is_opened = false;
};


enum class Text_Alignment
{
	Left,
	Center,
	Right,
};
REFLECT(Text_Alignment)
	ENUM_VALUE(Left);
	ENUM_VALUE(Center);
	ENUM_VALUE(Right);
REFLECT_END();



struct Active_Mask
{
	Rect rect;
	bool inversed;
};

struct Active_Mask_Stack_State
{
	Dynamic_Array<Active_Mask> saved_mask_stack;
};



struct UI_Parameters
{	
	const rgba default_background_color = rgba(36, 36, 36, 255);


	Unicode_String text_font_name = U"NotoMono";
	int text_font_face_size       = 24;


	rgba text_color = rgba(255, 255, 255, 255);
	Text_Alignment text_alignment = Text_Alignment::Center;
	bool center_text_vertically = true;


	int button_text_margin = 14;
	bool enable_button_text_fading = true;

	
	rgba text_field_background = default_background_color;
	rgba text_selection_background = rgba(0, 120, 120, 255);
	int  text_field_background_fade_width = 12;
	int  cursor_width = 4;
	rgba cursor_color = rgba(0, 255, 0, 100);
	int   text_field_margin = 8;
	float text_field_mouse_scroll_delay_per_character = 0.025f;

	int  scrollbar_width = 24;
	rgba scrollgrip_color        = rgba(60, 60, 60, 255);
	rgba scrollgrip_color_active = rgba(90, 90, 90, 255);
	rgba scrollgrip_color_hover  = rgba(80, 80, 80, 255);

	rgba scroll_region_background = rgba(30, 30, 30, 255);
	int min_scrollgrip_height = 24;

	rgba vertical_scrollbar_background_color = rgba(40, 40, 50, 255);
	rgba horizontal_scrollbar_background_color = rgba(50, 40, 40, 255);

	int scroll_region_mouse_wheel_scroll_speed_pixels = 36;


	rgba checkbox_frame_color = rgba(255, 255, 255, 255);
	rgba checkbox_background_color = default_background_color;
	rgba checkbox_tick_color = rgba(255, 255, 255, 255);


	rgba dropdown_background_color = default_background_color;
	int  dropdown_text_margin_left = 8;
	int  dropdown_arrow_margin_right = 8;
	int  dropdown_arrow_size = 4;
	rgba dropdown_arrow_color = rgba(255, 255, 255, 255);
	int  dropdown_item_height = 48;
	int  dropdown_item_text_left_margin = 16;
	rgba dropdown_items_list_background = rgba(20, 20, 20, 255);
	rgba dropdown_item_hover_background = rgba(40, 40, 40, 255);
	rgba dropdown_item_selected_background = rgba(0, 60, 0, 255);
	rgba dropdown_item_hover_and_selected_background = rgba(0, 10, 0, 255);

};

REFLECT(UI_Parameters)
	MEMBER(text_font_name);
	MEMBER(text_font_face_size);


	MEMBER(text_color);
		TAG("SettingsScalableColor");

	MEMBER(text_alignment);
	MEMBER(center_text_vertically);


	MEMBER(button_text_margin);
	MEMBER(enable_button_text_fading);

	
	MEMBER(text_field_background);
		TAG("SettingsScalableColor");
	MEMBER(text_selection_background);
		TAG("SettingsScalableColor");

	MEMBER(text_field_background_fade_width);
	MEMBER(cursor_width);
	MEMBER(cursor_color);
		TAG("SettingsScalableColor");
	MEMBER(text_field_margin);
	MEMBER(text_field_mouse_scroll_delay_per_character);

	MEMBER(scrollbar_width);

	MEMBER(scrollgrip_color);
		TAG("SettingsScalableColor");
	MEMBER(scrollgrip_color_active);
		TAG("SettingsScalableColor");
	MEMBER(scrollgrip_color_hover);
		TAG("SettingsScalableColor");

	MEMBER(scroll_region_background);
		TAG("SettingsScalableColor");
	

	MEMBER(min_scrollgrip_height);


	MEMBER(vertical_scrollbar_background_color);
		TAG("SettingsScalableColor");
	MEMBER(horizontal_scrollbar_background_color);
		TAG("SettingsScalableColor");


	MEMBER(scroll_region_mouse_wheel_scroll_speed_pixels);


	MEMBER(checkbox_frame_color);
		TAG("SettingsScalableColor");
	MEMBER(checkbox_background_color);
		TAG("SettingsScalableColor");
	MEMBER(checkbox_tick_color);
		TAG("SettingsScalableColor");


	MEMBER(dropdown_background_color);
		TAG("SettingsScalableColor");

	MEMBER(dropdown_text_margin_left);
	MEMBER(dropdown_arrow_margin_right);
	MEMBER(dropdown_arrow_size);
	MEMBER(dropdown_arrow_color);
		TAG("SettingsScalableColor");

	MEMBER(dropdown_item_height);
	MEMBER(dropdown_item_text_left_margin);
	MEMBER(dropdown_items_list_background);
		TAG("SettingsScalableColor");
	MEMBER(dropdown_item_hover_background);
		TAG("SettingsScalableColor");
	MEMBER(dropdown_item_selected_background);
		TAG("SettingsScalableColor");
	MEMBER(dropdown_item_hover_and_selected_background);
		TAG("SettingsScalableColor");

REFLECT_END();

struct UI
{
	UI_Parameters parameters;



	Array_Map<UI_ID, void*> ui_id_data_array;

	Dynamic_Array<Active_Mask> active_mask_stack;
	Dynamic_Array<Active_Mask_Stack_State> active_mask_stack_states;
	// You can use this if you want to temprorarily ignore active mask stack.



	UI_ID down    = invalid_ui_id; // This is only for current frame
	UI_ID holding = invalid_ui_id;
	UI_ID up      = invalid_ui_id;

	UI_ID hover;
	int   hover_layer;


	UI_ID hover_scroll_region;
	int   hover_scroll_region_layer;


	int current_layer = 0;

	UI_ID current_hovering_scroll_region = invalid_ui_id;
	int   current_hovering_scroll_region_layer = 0;


	UI_ID current_hovering = invalid_ui_id;
	int   current_hovering_layer = 0;


	// Used to keep chained UI_ID between frames.
	Arena_Allocator first_arena_allocator;
	Arena_Allocator second_arena_allocator;

	Arena_Allocator current_arena_allocator;

	UI_ID copy_ui_id(UI_ID ui_id, Allocator allocator);


	inline void* get_ui_item_data(UI_ID ui_id)
	{
		void** ptr_to_ptr = (void**) ui_id_data_array.get(ui_id);
		if (ptr_to_ptr) return *ptr_to_ptr;

		return NULL;
	}
	inline void put_ui_item_data(UI_ID ui_id, void* data)
	{
		if (ui_id.next)
		{
			ui_id = copy_ui_id(ui_id, c_allocator);
		}

		ui_id_data_array.put(ui_id, data);
	}


	template <typename T>
	inline void get_or_create_ui_item_data(UI_ID ui_id, T** out_state_pointer)
	{
		T* state = (T*) get_ui_item_data(ui_id);
		if (!state)
		{
			state = (T*) c_allocator.alloc(sizeof(T), code_location());
			put_ui_item_data(ui_id, state);

			*state = T();
		}

		*out_state_pointer = state;
	}

	template <typename T>
	inline void get_or_create_ui_item_data(UI_ID ui_id, T** out_state_pointer, std::function<void(T* item)> initializer)
	{
		T* state = (T*) get_ui_item_data(ui_id);
		if (!state)
		{
			state = (T*) c_allocator.alloc(sizeof(T), code_location());
			put_ui_item_data(ui_id, state);

			*state = T();
			initializer(item);
		}

		*out_state_pointer = state;
	}




	Font* find_font(Unicode_String name);
	Font::Face* get_font_face();



	void init();
	
	void pre_frame();
	void post_frame();

	void im_hovering(UI_ID ui_id);
	void this_scroll_region_is_hovering(UI_ID ui_id);

	bool is_point_inside_active_zone(int x, int y);
	void set_active_masks_as_renderer_masks();


	void save_active_mask_stack_state();
	void restore_active_mask_stack_state();



	void draw_text(int x, int y, Unicode_String text, Rect* cull_rect = NULL);


	bool button(Rect rect, Unicode_String text, rgba color, UI_ID ui_id);
	bool button(Rect rect, rgba color, UI_ID ui_id);

	// Returns whether you should negate boolean.
	bool checkbox(Rect rect, bool value, UI_ID ui_id);

	bool text_editor(Rect rect, Unicode_String text, Unicode_String* out_result, Allocator string_allocator, UI_ID ui_id, bool multiline = false, bool report_when_modified = false, bool finish_on_enter = true, UI_Text_Editor_Finish_Cause* finish_cause = NULL, Unicode_String* hint_text = NULL);

	bool file_picker(Rect rect, Unicode_String starting_folder, Unicode_String* out_result, Allocator result_allocator, UI_ID ui_id);

	// Uses 2 sub ids
	bool dropdown(Rect rect, int selected, Dynamic_Array<Unicode_String> options, int* out_selected, UI_ID ui_id);

	Scroll_Region_Result scroll_region(Rect rect, int content_height, int content_width, bool show_horizontal_scrollbar, UI_ID ui_id);
	void end_scroll_region();
};
inline UI ui;

struct Scoped_Active_Mask
{
	Scoped_Active_Mask(Active_Mask mask)
	{
		ui.active_mask_stack.add(mask);
		ui.set_active_masks_as_renderer_masks();
	}

	~Scoped_Active_Mask()
	{
		ui.active_mask_stack.count -= 1;
		ui.set_active_masks_as_renderer_masks();
	}
};

#define scoped_ui_active_mask( __rect, __inversed ) Scoped_Active_Mask CONCAT(set_active_masks_as_renderer_masks_scoped, __LINE__) ( { .rect = __rect, .inversed = __inversed} );\
