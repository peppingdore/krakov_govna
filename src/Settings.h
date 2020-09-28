#pragma once

#include "Tracy_Header.h"


#include "b_lib/Reflection.h"


#include "Renderer.h"
#include "Key_Bindings.h"
#include "Typer_UI.h"


enum class Key: u32;


struct Macro_Rect_State
{
	float delete_button_brightness;
	Vector2i delete_drag_start;
};


struct Settings
{
	Unicode_String text_font_face = U"NotoMono";
	int text_font_face_size = 14;
	
	int saved_command_history_entries_count = 48;

	bool keep_scrolling_to_the_bottom_if_already_there = true;

	bool SACRIFICE_ONE_CPU_CORE_FOR_TRUE_PC_GAMING_EXPIRIENCE_FPS = false;

	bool full_crash_dump = false;
	bool show_fps = false;
};
REFLECT(Settings)
	MEMBER(text_font_face);
	MEMBER(text_font_face_size);
	MEMBER(saved_command_history_entries_count);
	MEMBER(keep_scrolling_to_the_bottom_if_already_there);
	MEMBER(SACRIFICE_ONE_CPU_CORE_FOR_TRUE_PC_GAMING_EXPIRIENCE_FPS);
	MEMBER(full_crash_dump);
	MEMBER(show_fps);
REFLECT_END();

inline Settings settings;


bool save_settings();
bool load_settings();





enum class Settings_Page: s32
{
	None = -2,

	Left_Bound = -1,

	Macros = 0,
	UI = 1,
	Bindings = 2,

	Right_Bound,
};

static_assert(std::is_same_v<Reflection::relaxed_underlying_type<Settings_Page>::type, s32>, "Code that uses Settings_Page assumes it's s32");

REFLECT(Settings_Page)
	ENUM_VALUE(None);
	ENUM_VALUE(Left_Bound);

	ENUM_VALUE(Macros);
		TAG("Page");

	ENUM_VALUE(UI);
		TAG("Page");

	ENUM_VALUE(Bindings);
		TAG("Page");

	ENUM_VALUE(Right_Bound);
REFLECT_END();


struct Page
{
	Reflection::Enum_Value enum_value;

	Unicode_String unicode_name;
	int text_width;
	int rect_width;

	float page_state;

	Rect rect;
};

struct Settings_Screen
{
	Settings_Page current_page = Settings_Page::Macros;

	float openness = 0.0;
	float openness_linear = 0.0;


	Settings_Page transitioning_from_page = Settings_Page::None;

	float transition_speed = 3.0;
	
	float transition_state = 1.0;
	float transition_state_linear = 0.0;


	int page_selector_item_padding = 24;
	int page_selector_item_margin = 16;
	int page_selector_inset = 8;

	rgba page_selector_item_color = rgba(250, 250, 250, 255);
	rgba page_selector_item_font_color = rgba(10, 10, 10, 255);

	int  page_selector_selected_line_margin = 8;


	int page_content_padding = 24;


	rgb background_color = rgb(15, 15, 15);

	rgba macro_mailbox_background_color = rgba(15, 60, 15, 255);
	rgba macro_mailbox_hover_background_color = rgba(30, 160, 30, 255);

	
	rgba macro_rect_color = rgba(85, 85, 85, 255);
	rgba macro_rect_editing_color = rgba(200, 200, 200, 255);
	rgba macro_rect_outline_color = rgba(100, 100, 100, 255);

	int  macro_delete_rect_width = 40;
	rgba macro_delete_rect_color = rgba(200, 0, 0, 255);

	rgba macro_delete_line_color = rgba(255, 255, 255, 255);
	int  macro_delete_line_size = 6;
	Vector2i  macro_delete_tick_offset = { -4, -4 };
	float  macro_delete_brightness_change_speed = 10.0;




	int macro_margin_in_the_list = 8;

	int macro_height_in_the_list = 42;
	int distance_between_macros_in_the_list = 8;

	rgba macro_bind_background_color = rgba(15, 140, 15, 255);

	int  new_macro_button_offset = 24;
	int  new_macro_button_height = 24;
	rgba new_macro_color = rgba(15, 100, 15, 255);

	int macro_rect_text_margin = 8;




	int bindings_list_top_margin = 48;
	int bindings_list_bottom_margin = 24;
	int binding_action_type_height = 48;
	int binding_action_type_additional_binding_height = 36;
	int distance_between_bindings = 12;



	// @TODO: move this boys to Settings struct
	rgba terminal_scrollbar_color = rgba(10, 50, 10, 255);
	rgba terminal_scrollbar_background_color = rgba(30, 120, 30, 255);



	int macro_currently_being_edited = -1;


	template <typename T>
	inline T openness_scaled(T value)
	{
		return value * openness;
	}

	template <typename T>
	inline T reverse_openness_scaled(T value)
	{
		return value * (1.0 - openness);
	}

	Rect get_background_rect();

	void init();

	void do_frame();


	inline float get_alpha_for_page(float state)
	{
		return abs(state);
	}


	inline Rect get_full_page_rect(float state)
	{
		int x_offset = sign(state) * (1.0 - abs(state)) * renderer.width;
		int y_offset = renderer.scaled(reverse_openness_scaled(100));

		return {
			.x_left   = 0  + x_offset,
			.y_bottom = 0  + y_offset,
			.x_right  = renderer.width  + x_offset,
			.y_top    = typer_ui.y_top  + y_offset
		};
	}

	inline Rect get_page_rect(float state)
	{
		Rect rect = get_full_page_rect(state);

		int padding = renderer.scaled(page_content_padding);

		rect.shrink(padding, padding, padding, padding);

		return rect;
	}

	bool is_header_ui_touching_cursor(int x, int y);

	Dynamic_Array<Page> get_pages(Allocator allocator);


	void transition_to_page(Settings_Page to_page);

	void draw_page_selector();


	void draw_macros_page(float state);
	void draw_ui_page(float state);
	void draw_bindings_page(float state);
};

inline Settings_Screen settings_screen;


REFLECT(Settings_Screen)
	MEMBER(current_page);

	MEMBER(openness);
	MEMBER(openness_linear);


	MEMBER(transitioning_from_page);

	MEMBER(transition_speed);

	MEMBER(transition_state);
	MEMBER(transition_state_linear);



	MEMBER(page_selector_item_padding);
	MEMBER(page_selector_item_margin);
	MEMBER(page_selector_inset);

	MEMBER(background_color);
	
	MEMBER(page_selector_item_color);
		TAG("SettingsScalableColor");
	MEMBER(page_selector_item_font_color);
		TAG("SettingsScalableColor");
	MEMBER(page_selector_selected_line_margin);



	MEMBER(macro_mailbox_background_color);
		TAG("SettingsScalableColor");

	MEMBER(macro_mailbox_hover_background_color);
		TAG("SettingsScalableColor");

	MEMBER(macro_rect_color);
		TAG("SettingsScalableColor");
	MEMBER(macro_rect_editing_color);
		TAG("SettingsScalableColor");
	MEMBER(macro_rect_outline_color);
		TAG("SettingsScalableColor")

	MEMBER(macro_delete_rect_width);
	MEMBER(macro_delete_rect_color);
		TAG("SettingsScalableColor")
	MEMBER(macro_delete_line_color);
		TAG("SettingsScalableColor")
	MEMBER(macro_delete_line_size);
	MEMBER(macro_delete_tick_offset);
	MEMBER(macro_delete_brightness_change_speed)


	MEMBER(macro_bind_background_color);
		TAG("SettingsScalableColor");

	MEMBER(new_macro_button_offset);
	MEMBER(new_macro_button_height);
	MEMBER(new_macro_color)
		TAG("SettingsScalableColor");

	MEMBER(terminal_scrollbar_color);
		TAG("SettingsScalableColor");

	MEMBER(terminal_scrollbar_background_color);
		TAG("SettingsScalableColor");

REFLECT_END();




struct Old_Color_Value
{
	rgba* ptr;
	rgba value;
};

struct Color_Scaling_Context
{
	Dynamic_Array<Old_Color_Value> old_color_values;

	inline void restore_colors()
	{
		for (auto old_color_value: old_color_values)
		{
			*old_color_value.ptr = old_color_value.value; 
		}
	}
};

Color_Scaling_Context scale_and_save_colors(float scale);
