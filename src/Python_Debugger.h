#pragma once

#include "Renderer.h"
#include "Python_Interp.h"
#include "UI.h"

#include "b_lib/UUID.h"
#include "b_lib/Hash_Map.h"


enum class Py_Trace_Type: int
{
	Call        = PyTrace_CALL,
	Exception   = PyTrace_EXCEPTION,
	Line        = PyTrace_LINE,
	Return      = PyTrace_RETURN,
	C_Call      = PyTrace_C_CALL,
	C_Exception = PyTrace_C_EXCEPTION,
	C_Return    = PyTrace_C_RETURN,
	Opcode      = PyTrace_OPCODE,
};
REFLECT(Py_Trace_Type)
	ENUM_VALUE(Call);
	ENUM_VALUE(Exception);
	ENUM_VALUE(Line);
	ENUM_VALUE(Return);
	ENUM_VALUE(C_Call);
	ENUM_VALUE(C_Exception);
	ENUM_VALUE(C_Return);
	ENUM_VALUE(Opcode);
REFLECT_END();



struct Source_Line
{
	int index;

	int start;
	int length;
	int length_without_new_line;
	
	int margin_left;
};

struct Stack_Entry
{
	Unicode_String source_file_name;
	int line_number;
	UUID uuid;

	Unicode_String              local_source_code;
	Dynamic_Array<Source_Line>  local_source_code_lines;
	int                         local_source_code_starting_line_number;

	PyFrameObject* frame;

	void free()
	{
		c_allocator.free(source_file_name .data, code_location());
		c_allocator.free(local_source_code.data, code_location());

		local_source_code_lines.free();
	}
};

struct Thread_State
{
	Dynamic_Array<Stack_Entry> stack;
	int viewing_stack_entry = -1;

	PyThreadState* python_thread_state;

	int x_offset = 0;
	int y_offset = 0;

	bool is_outdated = false;


	inline void free()
	{
		for (auto& stack_entry: stack)
		{
			stack_entry.free();
		}
		stack.free();
		// @TODO: implement
	}
};

struct Breakpoint
{
	PyObject* python_file_name;
	int       line_number;

	PyCodeObject*  condition;
	Unicode_String condition_string;

	UUID uuid;

	bool is_same_file(PyObject* file_name)
	{
		defer { 
			assert(!PyErr_Occurred());
		};

		return PyObject_RichCompareBool(python_file_name, file_name, Py_EQ) == 1;
	}

	void free()
	{
		Py_DECREF(python_file_name);
		Py_XDECREF(condition);

		c_allocator.free(condition_string.data, code_location());
	}
};


struct Watch
{
	PyCodeObject* expression;

	Unicode_String expression_string;

	Unicode_String result_string;

	UUID uuid;


	void set_result_string(Unicode_String str)
	{
		if (result_string.data)
			c_allocator.free(result_string.data, code_location());

		result_string = str.copy_with(c_allocator);
	}
};

struct Field_State
{
	// PyObject* py_object;

	Field_State* child;
	Field_State* next;

	UI_ID ui_id;

	b32 expanded;


	
	inline Field_State* find_field_state(UI_ID ui_id)
	{
		Field_State* state = (Field_State*) this;
		while (state)
		{
			if (state->ui_id == ui_id)
			{
				return state;
			}

			state = state->next;
		}

		return NULL;
	}

	inline void add(Field_State* new_state)
	{
		Field_State* state = (Field_State*) this;

		while (state->next)
		{
			state = state->next;
		}

		state->next = new_state;
	}
};

struct Expand_State
{
	Field_State* first_field;

	bool are_arguments_expanded;
};


#undef None // X11 header defines this
enum class Variable_Parent_Type
{
	None,

	List,
	Dict,
};

struct Variable_Parent
{
	union
	{
		struct 
		{
			PyObject* dict;
			PyObject* key;
		};

		struct
		{
			PyObject* list;
			int index;
		};
	};

	Variable_Parent_Type type;
};


enum class Stepping_Type
{
	None = 0,

	Step_Out,
	Step_Into,
	Step_Down,
};

struct Python_Debugger
{
	PyThreadState* debugger_thread_state = NULL;

	bool need_to_open_debugger = false;
	bool is_open;


	// bool                       has_source_code_came_from_file = false;
	// Unicode_String             source_code;
	// Dynamic_Array<Source_Line> source_lines;
	// UI_ID                      source_code_ui_id = ui_id(0);
	// Stack_Entry*               source_code_stack_entry = NULL;

	Hash_Map<PyFrameObject*, Expand_State*> expand_states_for_frames; 


	Dynamic_Array<Thread_State> thread_states;
	int                         viewing_thread_state = -1;

	Dynamic_Array<Breakpoint>   breakpoints;


	Dynamic_Array<Watch>        watches;
	bool have_to_reevaluate_watches = false;

	bool have_to_recalculate_source_code_lines = false;


	int is_at_breakpoint = 0;
	bool have_to_continue_execution = false;


	PyObject* inspect_module;
	PyObject* debugger_module;


	UI_ID debugger_ui_id = ui_id(0);

	bool is_debugger_focused = false;
	bool is_debugger_going_to_lose_focus = false;
	bool is_debugger_going_to_get_focus = false;

	inline void focus_debugger()
	{
		is_debugger_going_to_lose_focus = false;

		if (is_debugger_focused) return;

		is_debugger_going_to_get_focus = true;
	}




	Mutex debugger_mutex;

	PyFrameObject* stop_frame = NULL;
	PyThreadState* step_into_thread = NULL;
	Stepping_Type  stepping_type = Stepping_Type::None;


	Semaphore breakpoint_thread_lock = create_semaphore();

	rgba background_color = rgba(0, 15, 15, 255);

	rgba source_code_background_color = rgba(0, 0, 0, 255);
	rgba source_code_outline_color = rgba(20, 100, 100, 255);

	int debugger_width_dragger_width = 18;
	int thread_dropdown_height = 24;
	int stack_entry_text_height = 12;

	int  continue_button_width = 24;
	rgba continue_button_color = rgba(30, 200, 30, 255);



	int stack_entry_width  = 400;
	int stack_entry_height = 48;

	int  source_code_border_width  = 24;
	rgba source_code_border_color = rgba(20, 30, 20, 255);
	
	int  breakpoint_margin = 2;
	rgba breakpoint_color = rgba(100, 30, 20, 255);


	rgba stack_entry_color         = rgba(40, 90, 120, 255) * 0.5;
	rgba stack_entry_viewing_color = rgba(5, 80, 80, 255);
	rgba stack_arguments_expand_color = rgba(80, 20, 120, 255) * 0.5;
	rgba stack_arg_background_color   = rgba(5, 10, 30, 255);
	

	int  stack_entry_expand_button_width = 32;
	int  stack_entry_function_name_margin = 16;
	int  stack_entry_function_name_fade_width = default_fade_width;
	int  stack_entry_inner_x_offset = 54;
	int  stack_entry_inner_y_offset = 10;

	int  stack_arg_margin     = 4;
	int  stack_varargs_margin = 16;
	int  varargs_spacing = 16;

	hsva stack_arg_background_nested_color = hsva(60.0, 0.5, 0.5, 1.0);
	int  stack_arg_background_fade_width = default_fade_width;

	rgba add_new_watch_button_color = rgba(40, 120, 150, 255) * 0.5;

	int  step_button_size = 24;
	int  step_button_focused_width = 64;

	rgba step_button_color = rgba(10, 120, 150, 255);

	float width              = 0.4;
	float min_debugger_width = 0.2;
	float min_terminal_width = 0.2;

	int draw_variable_item_height = 24;

	int eval_console_height = 48;

	int watches_margin_top  = 8;
	int watches_margin_left = 8;
	
	int watch_height = draw_variable_item_height;
	int add_new_watch_height = 30;
	int watch_width = 400;
	int watch_result_max_width = 600;
	int watch_variable_width = 400;
	rgba watch_outline_color = rgba(20, 100, 100, 255);
	rgba watch_background_color = rgba(0, 0, 0, 255);


	int dragger_x_offset = 0;



	float scaling = 1.0;

//	float scaling_movement_x = 0;
//	float scaling_movement_y = 0;
//	float target_scaling = 1.0;
//	float scaling_approach_speed = 0.8;

	template <typename T>
	inline int scaled(T number)
	{
		return renderer.scaled(number * scaling);
	}



	void reset_stepping()
	{
		stop_frame = NULL;
		step_into_thread = NULL;
		stepping_type = Stepping_Type::None;
	}


	// void set_source_code_from_stack_entry(Stack_Entry* stack_entry);

	int  get_width();
	int  get_x_right();
	int  get_x_left();


	void keep_debugger_size_sane();


	void init();
	void do_frame();

	void reset();

	void draw_thread_state(Thread_State* thread_state, Rect rect);
	void draw_eval_console(Rect rect, Thread_State* thread_state, Stack_Entry* stack_entry);
	void draw_watches(int x_left, int y_top, Thread_State* thread_state, Stack_Entry* stack_entry);

	struct Source_Code
	{
		PyObject* file_name;
		int       lines_offset;

	};

	void draw_source_code(Rect rect, Unicode_String source_code_string, Dynamic_Array<Source_Line> source_lines, int breakpoint_line, Source_Code source_code, UI_ID ui_id);

	// Returns y_bottom
	int draw_variable(Unicode_String variable_name, int x_left, int y_top, int x_right,  Variable_Parent parent, PyObject* py_object, Field_State* field_state, UI_ID ui_id);

	inline Stack_Entry* get_watching_stack_entry(Thread_State* thread_state)
	{
		if (thread_state->viewing_stack_entry >= 0 && thread_state->viewing_stack_entry < thread_state->stack.count)
		{
			return thread_state->stack[thread_state->viewing_stack_entry];
		}

		return NULL;
	}


	Font::Face* get_source_code_face();

	void recalculate_source_code_lines(Unicode_String source_code, Dynamic_Array<Source_Line>* lines, int lines_start);


	void build_stack(PyFrameObject* frame, Thread_State* thread_state);
	void breakpoint();

 
	void continue_execution();
	void free_thread_states();

	Breakpoint* evaluate_breakpoints_for_frame(PyFrameObject* frame);

	void step_out (Thread_State* thread_state, Stack_Entry* stack_entry);
	void step_into(Thread_State* thread_state, Stack_Entry* stack_entry);
	void step_down(Thread_State* thread_state, Stack_Entry* stack_entry);
};

inline Python_Debugger python_debugger; 



Unicode_String code_to_string(PyCodeObject* code);