#include "Tracy_Header.h"

#include "Python_Debugger.h"

#include "Main.h"
#include "UI.h"
#include "Input.h"

PyObject* typer_debugger_breakpoint_impl(PyObject* self, PyObject* args);
PyObject* typer_debugger_set_trace(PyObject* self, PyObject* args);

PyMethodDef typer_debugger_methods[] =
{
	{"breakpoint_impl", typer_debugger_breakpoint_impl, METH_VARARGS, ""},
	{"set_trace",       typer_debugger_set_trace,        METH_VARARGS, ""},
	{NULL} // Zero terminator
};

PyGetSetDef typer_debugger_getsetdefs[] =
{
	{
		.name = "__dict__",
		.get = PyObject_GenericGetDict,
		.set = PyObject_GenericSetDict,
		.doc = NULL,
		.closure = NULL,
	},
	{NULL}
};

struct typer_debugger
{
	PyObject_HEAD;
	PyObject* dict;
};

PyTypeObject debugger_type =
{
	PyVarObject_HEAD_INIT(0, 0)
	"embedded_debugger_extension_type",     /* tp_name */
	sizeof(typer_debugger),       /* tp_basicsize */
	0,                    /* tp_itemsize */
	0,                    /* tp_dealloc */
	0,                    /* tp_print */
	0,                    /* tp_getattr */
	0,                    /* tp_setattr */
	0,                    /* tp_reserved */
	0,                    /* tp_repr */
	0,                    /* tp_as_number */
	0,                    /* tp_as_sequence */
	0,                    /* tp_as_mapping */
	0,                    /* tp_hash  */
	0,                    /* tp_call */
	0,                    /* tp_str */
	PyObject_GenericGetAttr,                    /* tp_getattro */
	PyObject_GenericSetAttr,                    /* tp_setattro */
	0,                    /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,   /* tp_flags */
	"Typer's debugger implementation", /* tp_doc */
	0,                    /* tp_traverse */
	0,                    /* tp_clear */
	0,                    /* tp_richcompare */
	0,                    /* tp_weaklistoffset */
	0,                    /* tp_iter */
	0,                    /* tp_iternext */
	typer_debugger_methods,       /* tp_methods */
	0,                    /* tp_members */
	typer_debugger_getsetdefs,    /* tp_getset */
	0,                    /* tp_base */
	0,                    /* tp_dict */
	0,                    /* tp_descr_get */
	0,                    /* tp_descr_set */
	offsetof(typer_debugger, dict), /* tp_dictoffset */
	0,                    /* tp_init */
	0,                    /* tp_alloc */
	PyType_GenericNew,    /* tp_new */
};


Unicode_String code_to_string(PyCodeObject* code)
{
	PyObject* code_module = call_python(python_debugger.inspect_module, "getmodule", code);
	if (!code_module)
	{
		python.print_error();

		PyObject* source_code = call_python(python_debugger.inspect_module, "getsource", code);
		return Unicode_String::from_utf8(PyUnicode_AsUTF8(source_code), c_allocator);
	}
	else
	{
		PyObject* source_code = call_python(python_debugger.inspect_module, "getsource", code_module);

		if (!source_code)
		{
			python.print_error();
			source_code = call_python(python_debugger.inspect_module, "getsource", code);
		}

		if (source_code)
			return Unicode_String::from_utf8(PyUnicode_AsUTF8(source_code), c_allocator);
	}

	PyErr_Clear();
	return U"No source code";
};


Unicode_String python_to_string(PyObject* obj)
{
	if (PyErr_Occurred())
	{
		PyErr_Clear();
		PyErr_Print();
	}


	if (!obj)
		return U"python_to_string: NULL";

	return Unicode_String::from_utf8(PyUnicode_AsUTF8(PyObject_Repr(obj)), c_allocator);
}

PyObject* typer_debugger_breakpoint_impl(PyObject* self, PyObject* args)
{
	python_debugger.breakpoint();

	Py_RETURN_NONE;
};

PyObject* typer_debugger_set_trace(PyObject* self, PyObject* args)
{
	int python_debugger_trace_func(PyObject* obj, PyFrameObject* frame, int what, PyObject* arg);

	PyEval_SetTrace(python_debugger_trace_func, NULL);

	Py_RETURN_NONE;
};






int python_debugger_trace_func(PyObject* obj, PyFrameObject* frame, int what, PyObject* arg)
{
	Python_Debugger* debugger = &python_debugger;

	// Fast out, overhead is still substantial
	if (debugger->breakpoints.count == 0 && debugger->stepping_type == Stepping_Type::None)
		return 0;


	Py_Trace_Type trace_type = (Py_Trace_Type) what;


	Breakpoint* triggered_breakpoint = NULL;
	if (trace_type == Py_Trace_Type::Line)
	{
		triggered_breakpoint = debugger->evaluate_breakpoints_for_frame(frame);
	}

	if (triggered_breakpoint)
	{
		// @TODO: notify which breakpoint has triggered

		debugger->breakpoint();
	}
	else
	{
		switch (debugger->stepping_type)
		{
			case Stepping_Type::Step_Into:
			{
				if (trace_type == Py_Trace_Type::Line)
				{
					PyThreadState* current_ts = PyThreadState_Get();
					if (debugger->step_into_thread == current_ts)
					{
						debugger->breakpoint();
					}
				}
			}
			break;

			case Stepping_Type::Step_Down:
			{
				if (frame == debugger->stop_frame ||
					frame == debugger->stop_frame->f_back)
				{
					debugger->breakpoint();
				}
			}
			break;

			case Stepping_Type::Step_Out:
			{
				if (frame == debugger->stop_frame)
				{
					debugger->breakpoint();
				}
			}
			break;
		}
	}

	return 0;
};




void Python_Debugger::breakpoint()
{
	PyThreadState* saved_thread_state;

	// typer.append_semantic(typer.buffer.count, format_unicode_string(c_allocator, U"\nbreakpoint. thread.id = %\n", PyThreadState_Get()->id), SEMANTIC_ERROR_MESSAGE);

	{
		saved_thread_state = PyEval_SaveThread();
		Scoped_Lock debugger_lock(debugger_mutex);
		PyEval_RestoreThread(saved_thread_state);


		reset_stepping();

		ulong switch_interval = _PyEval_GetSwitchInterval();
		_PyEval_SetSwitchInterval(u32_max);
		defer{
			_PyEval_SetSwitchInterval(switch_interval);
		};


		PyThreadState* python_thread_state = PyInterpreterState_ThreadHead(PyInterpreterState_Main());
		PyThreadState* current_python_thread_state = PyThreadState_Get();

		is_at_breakpoint += 1;

		for (Thread_State& ts: thread_states)
		{
			ts.is_outdated = true;
		}

		while (python_thread_state)
		{
			defer{
				python_thread_state = PyThreadState_Next(python_thread_state);
			};

			if (python_thread_state->frame == NULL) continue; // Do not show frameless thread.


			Thread_State* thread_state = NULL;
			{
				for (auto& existing_thread_state: thread_states)
				{
					if (existing_thread_state.python_thread_state == python_thread_state)
					{
						thread_state = &existing_thread_state;
							
						thread_state->stack.clear();

						break;
					}
				}


				if (!thread_state)
				{
					thread_state = thread_states.add();

					thread_state->x_offset = 0;
					thread_state->y_offset = 0;

					thread_state->python_thread_state = python_thread_state;

					thread_state->stack = make_array<Stack_Entry>(32, c_allocator);
				}

				thread_state->is_outdated = false;
			}


			build_stack(python_thread_state->frame, thread_state);
			assert(thread_state->stack.count > 0);

			thread_state->viewing_stack_entry = 0;

			have_to_reevaluate_watches = true;

			if (current_python_thread_state == python_thread_state)
			{
				viewing_thread_state = thread_states.fast_pointer_index(thread_state);

				if (thread_state->stack.count > 0)
				{
					// set_source_code_from_stack_entry(thread_state->stack[0]);
				}
			}
		}

		for (int i = 0; i < thread_states.count; i++)
		{
			Thread_State* ts = thread_states[i];
			if (ts->is_outdated)
			{
				thread_states.fast_remove_pointer(ts);
				i -= 1;
			}
		}


		need_to_open_debugger = true;

		saved_thread_state = PyEval_SaveThread();
	}


	breakpoint_thread_lock.wait_and_decrement();

	PyEval_RestoreThread(saved_thread_state);

	// typer.append_semantic(typer.buffer.count, format_unicode_string(c_allocator, U"\ncontinue. thread.id = %\n", saved_thread_state->id), SEMANTIC_ERROR_MESSAGE);
}


void Python_Debugger::continue_execution()
{
	have_to_continue_execution = true;
}

void Python_Debugger::free_thread_states()
{
	for (Thread_State& thread_state: thread_states)
	{
		thread_state.free();
	}

	thread_states.clear();
}


Breakpoint* Python_Debugger::evaluate_breakpoints_for_frame(PyFrameObject* frame)
{
	for (Breakpoint& breakpoint: breakpoints)
	{
		if (breakpoint.line_number == PyFrame_GetLineNumber(frame))
		{
			if (breakpoint.is_same_file(frame->f_code->co_filename))
			{
				if (breakpoint.condition)
				{
					PyObject* eval_result = PyEval_EvalCode((PyObject*) breakpoint.condition, frame->f_globals, frame->f_locals);


					if (eval_result)
					{
						int is_true = PyObject_IsTrue(eval_result);
						if (is_true == -1)
						{
							// @TODO: smth went wrong, notify about it.
							return &breakpoint;
						}
						else if (is_true == 1)
						{
							return &breakpoint;
						}
					}
					else
					{ 
						// @TODO: condition didn't return a result, notify about it.
						return &breakpoint;
					}


					if (PyErr_Occurred())
					{
						PyErr_Clear();
					}
				}
				else
				{
					return &breakpoint;
				}
			}
		}
	}

	return NULL;
}


void Python_Debugger::step_out(Thread_State* thread_state, Stack_Entry* stack_entry)
{
	// If no backing frame, just continue the execution
	if (stack_entry->frame->f_back)
	{
		stepping_type = Stepping_Type::Step_Out;
		stop_frame    = stack_entry->frame->f_back;
	} 

	continue_execution();
}

void Python_Debugger::step_into(Thread_State* thread_state, Stack_Entry* stack_entry)
{
	stepping_type    = Stepping_Type::Step_Into;
	step_into_thread = thread_state->python_thread_state;

	continue_execution();
}

void Python_Debugger::step_down(Thread_State* thread_state, Stack_Entry* stack_entry)
{
	stepping_type = Stepping_Type::Step_Down;
	stop_frame    = stack_entry->frame;

	continue_execution();
}




void Python_Debugger::build_stack(PyFrameObject* frame, Thread_State* thread_state)
{
	assert(!python.interp.done_execution);

	while (frame)
	{
		Stack_Entry* stack_entry = thread_state->stack.add({
			.source_file_name = Unicode_String::from_utf8(PyUnicode_AsUTF8(frame->f_code->co_filename), c_allocator),

			.line_number = PyFrame_GetLineNumber(frame),

			.uuid = generate_uuid(),

			.frame = frame,
		});


		stack_entry->local_source_code_starting_line_number = -1;

		if (frame->f_code == (PyCodeObject*) python.interp.running_code)
		{
			stack_entry->local_source_code = python.interp.code_string.copy_with(c_allocator);
			stack_entry->local_source_code_starting_line_number = 1;
		}
		else
		{
			PyObject* source_code_and_starting_line = call_python(debugger_module, "find_source_code", frame);

			stack_entry->local_source_code = Unicode_String::from_utf8(PyUnicode_AsUTF8(PyTuple_GetItem(source_code_and_starting_line, 0)), c_allocator);
			stack_entry->local_source_code_starting_line_number = PyNumber_AsSsize_t(PyTuple_GetItem(source_code_and_starting_line, 1), NULL);

			Py_DECREF(source_code_and_starting_line);
		}


		stack_entry->local_source_code_lines = make_array<Source_Line>(32, c_allocator);
		recalculate_source_code_lines(stack_entry->local_source_code, &stack_entry->local_source_code_lines, stack_entry->local_source_code_starting_line_number);

		frame = frame->f_back;
	}
}

void Python_Debugger::init()
{
	expand_states_for_frames = decltype(expand_states_for_frames)(64, c_allocator);
	thread_states = make_array<Thread_State>(32, c_allocator);
	breakpoints   = make_array<Breakpoint>(32, c_allocator);
	watches       = make_array<Watch>(32, c_allocator);


	debugger_thread_state = PyThreadState_New(PyInterpreterState_Main());

	// Find inspect source code stuff.
	{
		inspect_module = PyImport_ImportModule("inspect");
		if (!inspect_module)
			abort_the_mission(U"Failed to find 'inspect' module");

		debugger_module = PyImport_ImportModule("typer_debugger");
		if (!debugger_module)
			abort_the_mission(U"Failed to find 'typer_debugger' module");


		#if 0
		inspect_getsourcelines = PyDict_GetItemString(PyModule_GetDict(inspect_module), "getsourcelines");
		if (!inspect_getsourcelines)
			abort_the_mission(U"Failed to find 'inspect.getsourcelines'");

		inspect_getsource = PyDict_GetItemString(PyModule_GetDict(inspect_module), "getsource");
		if (!inspect_getsource)
			abort_the_mission(U"Failed to find 'inspect.getsource'");
		#endif
	}

	// Build breakpoint and set_trace implemenation containing object.
	{
		if (PyType_Ready(&debugger_type) < 0)
		{
			assert(false);
		}

		PyObject* typer_debugger_object = debugger_type.tp_new(&debugger_type, 0, 0);
		Py_INCREF(typer_debugger_object);

		PySys_SetObject("breakpointhook", PyObject_GetAttrString(typer_debugger_object, "breakpoint_impl"));

		PyObject* function = PyDict_GetItemString(PyModule_GetDict(debugger_module), "__hook_thread_creation");
		assert(function);
		PyObject_CallFunctionObjArgs(function, PyObject_GetAttrString(typer_debugger_object, "set_trace"), NULL);
	}


	PyEval_SetTrace(python_debugger_trace_func, NULL);
}

void Python_Debugger::reset()
{
	free_thread_states();
}

Font::Face* Python_Debugger::get_source_code_face()
{
	scoped_set_and_revert(ui.parameters.text_font_face_size, scaled(9));

	return ui.get_font_face();
}


void Python_Debugger::recalculate_source_code_lines(Unicode_String source_code, Dynamic_Array<Source_Line>* lines, int lines_start)
{
	lines->clear();

	int source_code_width   = scaled(stack_entry_width - source_code_border_width);
	int wrapped_line_margin = scaled(8);

	bool is_line_wrap_continuation = false;

	Glyph_Iterator glyph_iterator = iterate_glyphs<char32_t>(get_source_code_face());
	
	int line_width = 0; 

	int line_start = 0;

	int line_index = lines_start;

	for (int i = 0; i < source_code.length; i++)
	{
		char32_t c = source_code[i];

		if (c == '\n')
		{
			lines->add({
				.index = line_index,

				.start = line_start,
				.length = i - line_start + 1,
				.length_without_new_line = i - line_start,
				
				.margin_left = is_line_wrap_continuation ? wrapped_line_margin : 0,
			});

			line_start = i + 1;

			line_width = 0;
			line_index += 1;

			is_line_wrap_continuation = false;
		}

		int previous_x = glyph_iterator.x;
		glyph_iterator.next_char(c);
		int x_delta = glyph_iterator.x - previous_x;

		line_width += x_delta;

		if (line_width > source_code_width)
		{
			lines->add({
				.index = line_index,

				.start = line_start,
				.length = i - line_start,
				.length_without_new_line = i - line_start,
				
				.margin_left = is_line_wrap_continuation ? wrapped_line_margin : 0,
			});

			i -= 1;

			line_start = i + 1;
			
			is_line_wrap_continuation = true;

			glyph_iterator.reset();

			line_width = 0;

			continue;
		}
	}

	if (line_start != source_code.length)
	{
		lines->add({
			.index = line_index,

			.start = line_start,
			.length = source_code.length - line_start,
			.length_without_new_line = (source_code.length - line_start) + (source_code[source_code.length - 1] == '\n'),

			.margin_left = is_line_wrap_continuation ? wrapped_line_margin : 0,
		});
	}
}

#if 0
void Python_Debugger::set_source_code_from_stack_entry(Stack_Entry* stack_entry)
{
	if (has_source_code_came_from_file)
	{
		c_allocator.free(source_code.data, code_location());
	}


	// Find the source code
	{
		has_source_code_came_from_file = false;

		PyCodeObject* code = stack_entry->frame->f_code;

		if ((PyObject*) code == python.interp.running_code)
		{
			source_code = python.interp.code_string;
		}
		else
		{
			auto code_module = call_python(python_debugger.inspect_module, "getmodule", code);
			auto code_source = call_python(python_debugger.inspect_module, "getsource", code_module);
			if (code_source)
			{
				source_code = Unicode_String::from_utf8(PyUnicode_AsUTF8(code_source), c_allocator);
				has_source_code_came_from_file = true;
			}
			else
			{
				source_code = U"NO SOURCE CODE AVAILABLE";
			}
		}
	}
	
	// source_code_ui_id = ui_id_uuid(stack_entry->uuid, 0);
	// source_code_ui_id = ui_id(0);
	source_code_ui_id = ui_id((u64) stack_entry->frame);



	source_code_stack_entry = stack_entry;

	recalculate_source_code_lines();
}
#endif



int Python_Debugger::get_width()
{
	return scale(renderer.width, width);
}

int Python_Debugger::get_x_right()
{
	return renderer.width;
}

int Python_Debugger::get_x_left()
{
	return get_x_right() - get_width(); 
}

void Python_Debugger::keep_debugger_size_sane()
{
	if (width < min_debugger_width)
	{
		width = min_debugger_width;
	}

	if (1.0 - width < min_terminal_width)
	{
		width = 1.0 - min_terminal_width;
	}
}

void Python_Debugger::do_frame()
{
	assert(threading.is_main_thread());

	scoped_set_and_revert(ui.parameters.text_font_face_size, 11);
	scoped_set_and_revert(ui.parameters.scrollgrip_color, rgba(10, 70, 100, 255));
	scoped_set_and_revert(ui.parameters.scrollgrip_color_active, rgba(10, 70 / 2, 150 / 2, 255));
	scoped_set_and_revert(ui.parameters.scrollgrip_color_hover,  rgba(10, 70    , 150    , 255));

	scoped_set_and_revert(ui.parameters.vertical_scrollbar_background_color, rgba(10, 10, 20, 255));
	scoped_set_and_revert(ui.parameters.horizontal_scrollbar_background_color, rgba(20, 10, 10, 255));



	Scoped_Lock debugger_lock(debugger_mutex);

#if 0
	Py_tracefunc saved_tracefunc = current_trace_func;
	remove_trace_func();
	defer { set_trace_func(saved_tracefunc); };
#endif


	bool previous_is_open = is_open;

	if (input.is_key_down(Key::F12))
	{
		is_open = !is_open;
	}

	if (need_to_open_debugger)
	{
		is_open = true;
		need_to_open_debugger = false;
	}



	if (is_open != previous_is_open)
	{
		typer_ui.invalidate_after(-1);
		typer_ui.need_to_keep_scroll_state_this_frame = true;
	}

	defer { have_to_recalculate_source_code_lines = false; };
	

	if (!is_open) return;


	int debugger_y_top = typer_ui.y_top;


	{
		UI_ID dragger_ui_id = ui_id(0);

		rgba dragger_color = rgba(30, 60, 50, 255);

		if (ui.hover == dragger_ui_id)
		{
			desired_cursor_type = Cursor_Type::Resize_Horizontal;

			need_to_redraw_next_frame(code_location());

			dragger_color = dragger_color * 1.4;
		}

		// Handle dragging before drawing
		if (ui.down == dragger_ui_id)
		{
			dragger_color = dragger_color * 0.8;
			dragger_x_offset = input.mouse_x - (renderer.width - get_width());
		}


		float old_width = width;

		if (ui.holding == dragger_ui_id)
		{
			need_to_redraw_next_frame(code_location());

			dragger_color = dragger_color * 0.8;
			int debugger_pixel_width = renderer.width - input.mouse_x + dragger_x_offset;

			width = float(debugger_pixel_width) / float(renderer.width);

			keep_debugger_size_sane();
		}


		if (old_width != width)
		{
			typer_ui.invalidate_after(-1);
			typer_ui.need_to_keep_scroll_state_this_frame = true;
		}



		int dragger_x_left = get_x_left();

		Rect debugger_width_dragger_rect = Rect::make(dragger_x_left, 0, dragger_x_left + renderer.scaled(debugger_width_dragger_width), debugger_y_top);

		renderer.draw_rect(debugger_width_dragger_rect, dragger_color);


		if (debugger_width_dragger_rect.is_point_inside(input.mouse_x, input.mouse_y))
		{
			ui.im_hovering(dragger_ui_id);
		}
	}

	int x_left  = get_x_left() + renderer.scaled(debugger_width_dragger_width);
	int x_right = get_x_right();

	int debugger_width = x_right - x_left;

	Rect debugger_rect = Rect::make(x_left, 0, x_right, debugger_y_top);

	if (debugger_rect.is_point_inside(input.mouse_x, input.mouse_y))
	{
		ui.im_hovering(debugger_ui_id);
	}

	renderer.draw_rect(debugger_rect, is_debugger_focused ? background_color : rgba(0, 0, 0, 255));


	if (ui.down != invalid_ui_id)
	{
		if (ui.down != debugger_ui_id)
		{
			if (is_debugger_focused)
			{
				is_debugger_going_to_lose_focus = true;
			}
		}
		else
		{
			is_debugger_focused = true;
		}
	}
	else
	{
		if (is_debugger_going_to_lose_focus)
		{
			is_debugger_focused = false;

			is_debugger_going_to_lose_focus = false;
		}

		if (is_debugger_going_to_get_focus)
		{
			is_debugger_focused = true;

			is_debugger_going_to_get_focus = false;
		}
	}


	if (is_at_breakpoint > 0)
	{
		auto gil_state = PyGILState_Ensure();
		defer{ PyGILState_Release(gil_state); };

		PyThreadState* this_ts = PyGILState_GetThisThreadState();
		assert(this_ts);
		PyThreadState* previous_ts = PyThreadState_Swap(this_ts);
		defer { PyThreadState_Swap(previous_ts); };

		Rect continue_button_rect = Rect::make(x_right - scaled(continue_button_width), debugger_y_top - scaled(thread_dropdown_height), x_right, debugger_y_top);

		// Continue button
		{
			if (ui.button(continue_button_rect, U">", continue_button_color, ui_id(0)))
			{
				have_to_continue_execution = true;
			}
		}


		Rect thread_dropdown_rect = Rect::make(x_left, get_active_height() - renderer.scaled(thread_dropdown_height), continue_button_rect.x_left, get_active_height());

		if (viewing_thread_state >= 0 && viewing_thread_state < thread_states.count)
		{
			Dynamic_Array<Unicode_String> options = make_array<Unicode_String>(16, frame_allocator);
			for (Thread_State& thread_state : thread_states)
			{
				// PyThreadState* previous_ts = PyThreadState_Swap(thread_state.python_thread_state);

				PyObject* thread_name = call_python(debugger_module, "get_thread_name", PyLong_FromLongLong(thread_state.python_thread_state->thread_id));
				const char* huy = PyUnicode_AsUTF8(thread_name);

				options.add(Unicode_String::from_utf8(huy, frame_allocator));
			
				Py_DECREF(thread_name);

				// PyThreadState_Swap(previous_ts);
			}


			ui.dropdown(thread_dropdown_rect, viewing_thread_state, options, &viewing_thread_state, ui_id(0));
		}



		int y = get_active_height() - thread_dropdown_rect.height();

		if (viewing_thread_state >= 0 && viewing_thread_state < thread_states.count)
		{
			Rect thread_state_rect = Rect::make(x_left, 0, x_right, y);

			Thread_State* thread_state = thread_states[viewing_thread_state];

			if (thread_state_rect.is_point_inside(input.mouse_x, input.mouse_y))
			{
				float old_scaling = scaling;
				scaling = clamp<float>(0.25, 1.5, scaling - float(input.mouse_wheel_delta) * 0.04 * scaling);

				if (old_scaling != scaling)
				{
					float mouse_x = input.mouse_x - (thread_state_rect.x_left + thread_state->x_offset);
					float mouse_y = input.mouse_y - (thread_state_rect.y_top + thread_state->y_offset);
				
					float new_x = mouse_x * (scaling / old_scaling);
					float new_y = mouse_y * (scaling / old_scaling);

					thread_state->x_offset += mouse_x - new_x;
					thread_state->y_offset += mouse_y - new_y;
				


					have_to_recalculate_source_code_lines = true;
					need_to_redraw_next_frame(code_location());
				}
			}


#if 0
			// Smooth scaling
			{
				if (scaling != target_scaling)
				{
					if (abs(target_scaling - scaling) <= frame_time * scaling_approach_speed)
					{
						scaling = target_scaling;

						thread_state->x_offset += scaling_movement_x;
						thread_state->y_offset += scaling_movement_y;

						scaling_movement_x = 0;
						scaling_movement_y = 0;

						need_to_redraw_next_frame(code_location());
					}
					else
					{
						float delta = sign(scaling - target_scaling) * scaling_approach_speed * frame_time;

						float frac_x = scaling_movement_x * scaling_approach_speed * frame_time * 100;
						float frac_y = scaling_movement_y * scaling_approach_speed * frame_time * 100;



						scaling -= delta;

						thread_state->x_offset += frac_x;
						thread_state->y_offset += frac_y;

						scaling_movement_x -= frac_x;
						scaling_movement_y -= frac_y;

						need_to_redraw_next_frame(code_location());

					}
				}
			}
#endif

			draw_thread_state(thread_state, thread_state_rect);
		}


		if (have_to_continue_execution)
		{
			for (int i = 0; i < is_at_breakpoint; i++)
			{
				breakpoint_thread_lock.increment();
			}

			is_at_breakpoint = 0;
			have_to_continue_execution = false;
		}
	}
}


void Python_Debugger::draw_thread_state(Thread_State* thread_state, Rect rect)
{
	ui.active_mask_stack.add({
		.rect = rect,
		.inversed = false
	});
	ui.set_active_masks_as_renderer_masks();
	defer {
		ui.active_mask_stack.count -= 1;
		ui.set_active_masks_as_renderer_masks();
	};


	UI_ID ui_id = ui_id(0);

	if (ui.is_point_inside_active_zone(input.mouse_x, input.mouse_y) && rect.is_point_inside(input.mouse_x, input.mouse_y))
	{
		ui.im_hovering(ui_id);
	}

	if (ui.down == ui_id)
	{
		focus_debugger();
	}

	if (ui.down == ui_id || ui.holding == ui_id)
	{
		thread_state->x_offset += input.mouse_x_delta;
		thread_state->y_offset += input.mouse_y_delta;

		focus_debugger();
	}




	int x_left = rect.x_left + thread_state->x_offset;
	int y_top  = rect.y_top  + thread_state->y_offset;


	int watches_x_left = x_left;
	int watches_y_top  = y_top;
	Stack_Entry* watches_stack_entry = NULL;




	scoped_set_and_revert(ui.parameters.text_font_face_size, scale(ui.parameters.text_font_face_size, scaling));



	for (int i = thread_state->stack.count - 1; i >= 0; i--)
	{
		Stack_Entry* stack_entry = thread_state->stack[i];

		bool is_stack_entry_focused = get_watching_stack_entry(thread_state) == stack_entry && is_debugger_focused;


		// :adding_empty_child
		Expand_State* frame_expand_state;
		{
			Expand_State** hash_map_result = expand_states_for_frames.get(stack_entry->frame);
			if (hash_map_result)
			{
				frame_expand_state = *hash_map_result;
			}
			else
			{
				hash_map_result = expand_states_for_frames.put(stack_entry->frame);

				frame_expand_state = (Expand_State*) c_allocator.alloc(sizeof(Expand_State), code_location());


				Field_State dummy_first_field_state = {
					// .py_object = (PyObject*) 0xffff'ffff'ffff'ffff,
					.child = NULL,
					.next = NULL,
					.ui_id = invalid_ui_id,
					.expanded = false,
				};

				frame_expand_state->first_field = allocate_and_copy<Field_State>(&dummy_first_field_state, c_allocator);
				frame_expand_state->are_arguments_expanded = false;

				*hash_map_result = frame_expand_state;
			}
		}




		Rect stack_header_rect = Rect::make(x_left, y_top - scaled(stack_entry_height), x_left + scaled(stack_entry_width), y_top);
		
		Rect expand_button_rect = stack_header_rect;
		expand_button_rect.x_right = expand_button_rect.x_left + scaled(stack_entry_expand_button_width);

		{
			scoped_set_and_revert(ui.parameters.button_text_margin, 0);
			scoped_set_and_revert(ui.parameters.text_alignment, Text_Alignment::Center);

			if (ui.button(expand_button_rect, frame_expand_state->are_arguments_expanded ? U"-" : U"+", stack_arguments_expand_color, ui_id(i)))
			{
				frame_expand_state->are_arguments_expanded = !frame_expand_state->are_arguments_expanded;
				
				focus_debugger();
			}
		}



		UI_ID stack_entry_ui_id = ui_id_sub_id((u64) stack_entry->frame, 0);

		{
			Unicode_String function_name = Unicode_String::from_utf8(PyUnicode_AsUTF8(stack_entry->frame->f_code->co_name), frame_allocator);


			PyObject* args;
			PyObject* varargs;
			PyObject* keywords;
			PyObject* locals;

			auto arg_values = call_python(inspect_module, "getargvalues", stack_entry->frame);
			if (!arg_values)
			{
				args = PyList_New(0);
				varargs = Py_None;
				keywords = Py_None;
				locals = PyDict_New();
			}
			else
			{
				args     = PyTuple_GetItem(arg_values, 0);
				varargs  = PyTuple_GetItem(arg_values, 1);
				keywords = PyTuple_GetItem(arg_values, 2);
				locals   = PyTuple_GetItem(arg_values, 3);
			}


			auto formatted_arg_values = call_python(inspect_module, "formatargvalues", args, varargs, keywords, locals);
			auto formatted_arg_values_str = PyObject_Str(formatted_arg_values);


			Unicode_String stack_function_name_with_arguments = format_unicode_string(frame_allocator, U"%: %", function_name,
				Unicode_String::from_utf8(PyUnicode_AsUTF8(formatted_arg_values_str), frame_allocator));

			scoped_set_and_revert(ui.parameters.text_alignment,         Text_Alignment::Left);
			scoped_set_and_revert(ui.parameters.center_text_vertically, true);
			scoped_set_and_revert(ui.parameters.text_font_face_size,    scale(stack_entry_text_height, scaling));


			Rect stack_button_rect = stack_header_rect;
			stack_button_rect.x_left = expand_button_rect.x_right;

			rgba stack_button_color = thread_state->viewing_stack_entry == i ? stack_entry_viewing_color : stack_entry_color;

			if (ui.button(stack_button_rect, stack_button_color, ui_id(i)))
			{
				thread_state->viewing_stack_entry = i;

				have_to_reevaluate_watches = true;

				focus_debugger();
			}

			{
				renderer.push_mask({
					.rect = stack_button_rect,
					.inversed = false
				});

				ui.draw_text(stack_button_rect.x_left + scaled(stack_entry_function_name_margin), stack_button_rect.center_y(), stack_function_name_with_arguments);

				Rect fade_rect = stack_button_rect;
				fade_rect.x_left = fade_rect.x_right - scaled(stack_entry_function_name_fade_width);

				renderer.draw_rect_with_alpha_fade(fade_rect, stack_button_color, 0, 255);

				renderer.pop_mask();
			}



			// Stepping buttons
			{
				scoped_set_and_revert(ui.parameters.text_alignment, Text_Alignment::Center);

				int x_right = stack_button_rect.x_right - scaled(8.0);
				int scaled_size = scaled(step_button_size);

				int step_button_width = scaled_size;
				if (is_stack_entry_focused)
				{
					step_button_width = scaled(step_button_focused_width);
				}

				const Unicode_String step_out_str  = U"^";
				const Unicode_String step_into_str = U"|";
				const Unicode_String step_down_str = U"-";

				const Unicode_String step_out_str_with_bind   = U"^ (F10)";
				const Unicode_String step_into_str_with_bind  = U"| (F11)";
				const Unicode_String step_down_str_with_bind  = U"- (F8)";




				Rect step_out_button_rect = Rect::make(x_right - step_button_width, stack_button_rect.center_y() - scaled_size / 2, x_right, stack_button_rect.center_y() + scaled_size / 2);


				if (ui.button(step_out_button_rect,
					is_stack_entry_focused ? step_out_str_with_bind : step_out_str, step_button_color, ui_id(i)) ||
					(is_stack_entry_focused && input.is_key_down(Key::F10)))
				{
					step_out(thread_state, stack_entry);

					focus_debugger();
				}

				Rect step_into_button_rect = step_out_button_rect.moved(-step_button_width -scaled(8), 0);
				if (ui.button(step_into_button_rect, is_stack_entry_focused ? step_into_str_with_bind : step_into_str, step_button_color, ui_id(i)) ||
					(is_stack_entry_focused && input.is_key_down(Key::F11)))
				{
					step_into(thread_state, stack_entry);

					focus_debugger();
				}

				Rect step_down_button_rect = step_into_button_rect.moved(-step_button_width - scaled(8), 0);
				if (ui.button(step_down_button_rect, is_stack_entry_focused ? step_down_str_with_bind : step_down_str, step_button_color, ui_id(i)) || 
					(is_stack_entry_focused && input.is_key_down(Key::F8)))
				{
					step_down(thread_state, stack_entry);

					focus_debugger();
				}
			}


			if (frame_expand_state->are_arguments_expanded)
			{
				{
					scoped_set_and_revert(ui.parameters.text_alignment,         Text_Alignment::Left);
					scoped_set_and_revert(ui.parameters.center_text_vertically, true);
					scoped_set_and_revert(ui.parameters.text_font_face_size,    scale(stack_entry_text_height, scaling));


					PyObject *iterator = PyObject_GetIter(args);
					PyObject *item;

					int variable_index = 0;
					while ((item = PyIter_Next(iterator)))
					{
						Unicode_String arg_name = Unicode_String::from_utf8(PyUnicode_AsUTF8(item), frame_allocator);


						PyObject* arg_value = PyDict_GetItem(locals, item);

						UI_ID variable_ui_id = ui_id_next((u64) arg_value, variable_index, &stack_entry_ui_id);

						Field_State* field_state = frame_expand_state->first_field->find_field_state(variable_ui_id);
						if (!field_state)
						{
							Field_State new_field_state = {
								// .py_object = arg_value,
								.child = NULL,
								.next = NULL,
								.ui_id = ui.copy_ui_id(variable_ui_id, c_allocator),
								.expanded = false,
							};

							field_state = allocate_and_copy(&new_field_state, c_allocator);
							
							frame_expand_state->first_field->add(field_state);
						}


						Variable_Parent variable_parent = {
							.dict = args,
							.key = item,
							.type = Variable_Parent_Type::Dict
						};

						stack_header_rect.y_bottom = draw_variable(arg_name, stack_header_rect.x_left, stack_header_rect.y_bottom, stack_header_rect.x_right, variable_parent, arg_value, field_state, variable_ui_id);


						Py_DECREF(item);

						variable_index += 1;
					}


					{
						Rect footer_rect = Rect::make(stack_header_rect.x_left, stack_header_rect.y_bottom - scaled(varargs_spacing), stack_header_rect.x_right, stack_header_rect.y_bottom);
	
						renderer.draw_rect(footer_rect, stack_arg_background_color);

						stack_header_rect.y_bottom = footer_rect.y_bottom;
					}

					Py_DECREF(iterator);
				}

				if (varargs != Py_None)
				{
					scoped_set_and_revert(ui.parameters.text_alignment,         Text_Alignment::Left);
					scoped_set_and_revert(ui.parameters.center_text_vertically, true);
					scoped_set_and_revert(ui.parameters.text_font_face_size,    scale(stack_entry_text_height, scaling));

					{
						Rect varargs_name_rect = Rect::make(stack_header_rect.x_left, stack_header_rect.y_bottom - scaled(24), stack_header_rect.x_right, stack_header_rect.y_bottom);
 
						stack_header_rect.y_bottom = varargs_name_rect.y_bottom;

						renderer.draw_rect(varargs_name_rect, stack_arg_background_color);

						PyObject* varargs_str = PyObject_Str(varargs);

						Unicode_String print_text = Unicode_String::from_utf8(PyUnicode_AsUTF8(varargs_str), frame_allocator);

						ui.draw_text(varargs_name_rect.x_left + scaled(stack_arg_margin), varargs_name_rect.center_y(), print_text);

						Py_DECREF(varargs_str);
					}

					PyObject* varargs_list = PyDict_GetItem(locals, varargs);

					PyObject *iterator = PyObject_GetIter(varargs_list);
					PyObject *item;

					int variable_index = 0;
					while ((item = PyIter_Next(iterator)))
					{
						Unicode_String arg_name = format_unicode_string(frame_allocator, U"[%]", variable_index);

						UI_ID variable_ui_id = ui_id_next((u64) item, variable_index, &stack_entry_ui_id);

						Field_State* field_state = frame_expand_state->first_field->find_field_state(variable_ui_id);
						if (!field_state)
						{
							Field_State new_field_state = {
								// .py_object = arg_value,
								.child = NULL,
								.next = NULL,
								.ui_id = ui.copy_ui_id(variable_ui_id, c_allocator),
								.expanded = false,
							};

							field_state = allocate_and_copy(&new_field_state, c_allocator);
							
							frame_expand_state->first_field->add(field_state);
						}


						Variable_Parent variable_parent = {
							.list  = varargs_list,
							.index = variable_index,
							.type  = Variable_Parent_Type::List
						};

						stack_header_rect.y_bottom = draw_variable(arg_name, stack_header_rect.x_left, stack_header_rect.y_bottom, stack_header_rect.x_right, variable_parent, item, field_state, variable_ui_id);

						Py_DECREF(item);

						variable_index += 1;
					}


					{
						Rect footer_rect = Rect::make(stack_header_rect.x_left, stack_header_rect.y_bottom - scaled(varargs_spacing), stack_header_rect.x_right, stack_header_rect.y_bottom);
	
						renderer.draw_rect(footer_rect, stack_arg_background_color);

						stack_header_rect.y_bottom = footer_rect.y_bottom;
					}

					Py_DECREF(iterator);
				}

				if (keywords != Py_None)
				{
					scoped_set_and_revert(ui.parameters.text_alignment,         Text_Alignment::Left);
					scoped_set_and_revert(ui.parameters.center_text_vertically, true);
					scoped_set_and_revert(ui.parameters.text_font_face_size,    scale(stack_entry_text_height, scaling));

					{
						Rect kwargs_name_rect = Rect::make(stack_header_rect.x_left, stack_header_rect.y_bottom - scaled(24), stack_header_rect.x_right, stack_header_rect.y_bottom);

						stack_header_rect.y_bottom = kwargs_name_rect.y_bottom;

						renderer.draw_rect(kwargs_name_rect, stack_arg_background_color);

						PyObject* kwargs_str = PyObject_Str(keywords);

						Unicode_String print_text = Unicode_String::from_utf8(PyUnicode_AsUTF8(kwargs_str), frame_allocator);

						ui.draw_text(kwargs_name_rect.x_left + scaled(stack_arg_margin), kwargs_name_rect.center_y(), print_text);
						
						Py_DECREF(kwargs_str);
					}



					PyObject* keywords_dict = PyDict_GetItem(locals, keywords);

					PyObject *iterator = PyObject_GetIter(keywords_dict);
					PyObject *item;
					
					int variable_index = 0;
					while ((item = PyIter_Next(iterator)))
					{
						Unicode_String arg_name = Unicode_String::from_utf8(PyUnicode_AsUTF8(item), frame_allocator);


						PyObject* arg_value = PyDict_GetItem(keywords_dict, item);

						UI_ID variable_ui_id = ui_id_next((u64) arg_value, variable_index, &stack_entry_ui_id);

						Field_State* field_state = frame_expand_state->first_field->find_field_state(variable_ui_id);
						if (!field_state)
						{
							Field_State new_field_state = {
								// .py_object = arg_value,
								.child = NULL,
								.next = NULL,
								.ui_id = ui.copy_ui_id(variable_ui_id, c_allocator),
								.expanded = false,
							};

							field_state = allocate_and_copy(&new_field_state, c_allocator);
							
							frame_expand_state->first_field->add(field_state);
						}


						Variable_Parent variable_parent = {
							.dict = keywords_dict,
							.key = item,
							.type = Variable_Parent_Type::Dict
						};

						stack_header_rect.y_bottom = draw_variable(arg_name, stack_header_rect.x_left, stack_header_rect.y_bottom, stack_header_rect.x_right, variable_parent, arg_value, field_state, variable_ui_id);


						Py_DECREF(item);

						variable_index += 1;
					}



					{
						Rect footer_rect = Rect::make(stack_header_rect.x_left, stack_header_rect.y_bottom - scaled(varargs_spacing), stack_header_rect.x_right, stack_header_rect.y_bottom);
	
						renderer.draw_rect(footer_rect, stack_arg_background_color);

						stack_header_rect.y_bottom = footer_rect.y_bottom;
					}

					Py_DECREF(iterator);
				}
			}

			if (formatted_arg_values)
				Py_DECREF(formatted_arg_values);
			if (formatted_arg_values_str)
				Py_DECREF(formatted_arg_values_str);
			if (arg_values)
				Py_DECREF(arg_values);
		}

		y_top = stack_header_rect.y_bottom;

		int source_code_height = get_source_code_face()->line_spacing * stack_entry->local_source_code_lines.count + scaled(ui.parameters.text_field_margin);
		Rect stack_source_code_rect = Rect::make(x_left, y_top - source_code_height, x_left + scaled(stack_entry_width), y_top);

		int line_number = PyFrame_GetLineNumber(stack_entry->frame);


		if (have_to_recalculate_source_code_lines)
		{
			recalculate_source_code_lines(stack_entry->local_source_code, &stack_entry->local_source_code_lines, stack_entry->local_source_code_starting_line_number);
		}
		
		
		Source_Code source_code = {
			.file_name = stack_entry->frame->f_code->co_filename,
			.lines_offset = stack_entry->local_source_code_starting_line_number,
		};	
		
		draw_source_code(
			stack_source_code_rect, 
			stack_entry->local_source_code,
			stack_entry->local_source_code_lines,
			line_number,

			source_code,

			ui_id_uuid(stack_entry->uuid, 0));



		if (thread_state->viewing_stack_entry == i)
		{
			watches_x_left = stack_header_rect.x_right;
			watches_y_top  = stack_header_rect.y_top;
			watches_stack_entry  = stack_entry;
		}

		x_left += scaled(stack_entry_inner_x_offset);
		y_top  -= (stack_source_code_rect.height() + scaled(stack_entry_inner_y_offset));
	}



	Rect eval_console_rect = Rect::make(watches_x_left + scaled(watches_margin_left), watches_y_top - scaled(eval_console_height), watches_x_left + scaled(watches_margin_left + watch_width), watches_y_top);

	draw_eval_console(eval_console_rect, thread_state, watches_stack_entry);

	draw_watches(watches_x_left + scaled(watches_margin_left), eval_console_rect.y_bottom - scaled(watches_margin_top), thread_state, watches_stack_entry);
}

void Python_Debugger::draw_eval_console(Rect rect, Thread_State* thread_state, Stack_Entry* stack_entry)
{
	// renderer.draw_rect(rect, rgba(255, 0, 0, 255));

	scoped_set_and_revert(ui.parameters.text_alignment, Text_Alignment::Left);

	Unicode_String hint_string = U"Type expression and press ENTER";
	Unicode_String eval_string;

	UI_Text_Editor_Finish_Cause text_editor_finish_cause;

	UI_ID text_editor_ui_id = ui_id(0);

	// @Hack
	UI_ID text_editor_scroll_region_ui_id = text_editor_ui_id;
	text_editor_scroll_region_ui_id.id += 1337;


	if (ui.text_editor(rect, Unicode_String::empty, &eval_string, frame_allocator, text_editor_ui_id, false, false, true, &text_editor_finish_cause, &hint_string))
	{
		if (text_editor_finish_cause == UI_TEXT_EDITOR_PRESSED_ENTER)
		{
			PyObject* expression_code = Py_CompileString(eval_string.to_utf8(frame_allocator, NULL), "eval expression", Py_single_input);
			if (!expression_code)
			{
				if (PyErr_Occurred())
				{
					PyErr_Print();		
				}
			}
			else
			{
				PyObject* eval_result = PyEval_EvalCode(expression_code, stack_entry->frame->f_globals, stack_entry->frame->f_locals);

				if (!eval_result)
				{
					if (PyErr_Occurred())
					{
						PyErr_Print();
					}
				}
				else
				{
					Py_DECREF(eval_result);
				}

				Py_DECREF(expression_code);
			}
		}
	}

	if (ui.down == text_editor_ui_id || ui.down == text_editor_scroll_region_ui_id)
	{
		focus_debugger();
	}

	renderer.draw_rect_outline(rect, watch_outline_color);
}

void Python_Debugger::draw_watches(int x_left, int y_top, Thread_State* thread_state, Stack_Entry* stack_entry)
{
	scoped_set_and_revert(ui.current_layer, ui.current_layer + 1);


	int watch_x_left  = x_left;
	int watch_x_right = watch_x_left + scaled(watch_width); // -1 to not draw over outline
	int watch_y_top   = y_top;

	int i = 0;
	for (Watch& watch: watches)
	{
		Rect watch_rect = Rect::make(watch_x_left, watch_y_top - scaled(watch_height), watch_x_right, watch_y_top);


		Rect watch_expression_rect = watch_rect;
		watch_expression_rect.x_right -= watch_rect.width() / 2;

		Unicode_String new_expression_string;
		UI_ID text_editor_ui_id = ui_id_uuid(watch.uuid, 0);

		// @Hack
		UI_ID text_editor_scroll_region_ui_id = text_editor_ui_id;
		text_editor_scroll_region_ui_id.id += 1337;

		if (ui.down == text_editor_ui_id || ui.down == text_editor_scroll_region_ui_id)
		{
			focus_debugger();
		}

		// @TODO: remove have_to_reevaluate_watches
		bool need_to_reeval_this_watch = true;
		//bool need_to_reeval_this_watch = have_to_reevaluate_watches;

		if (ui.text_editor(watch_expression_rect, watch.expression_string, &new_expression_string, c_allocator, text_editor_ui_id))
		{
			if (watch.expression_string.data)
			{
				c_allocator.free(watch.expression_string.data, code_location());
			}

			watch.expression_string = new_expression_string;

			PyObject* watch_expression = Py_CompileString(watch.expression_string.to_utf8(frame_allocator, NULL), "watch expression", Py_eval_input);

			if (!watch_expression)
			{
				if (PyErr_Occurred())
				{					
					PyObject* ptype, * pvalue, * ptraceback;
					PyErr_Fetch(&ptype, &pvalue, &ptraceback);

					if (pvalue)
					{
						PyObject* pstr = PyObject_Str(pvalue);

						const char* err_msg = PyUnicode_AsUTF8(pstr);
						if (pstr)
						{
							Unicode_String error_string = Unicode_String::from_utf8(err_msg, frame_allocator);

							watch.set_result_string(error_string);
						}
					}
				}
			}

			watch.expression = (PyCodeObject*) watch_expression;

			need_to_reeval_this_watch = true;
		}



		PyObject* eval_result = NULL;
		Unicode_String eval_result_string = Unicode_String::empty;

		if (watch.expression)
		{
			if (stack_entry && need_to_reeval_this_watch)
			{
				eval_result = PyEval_EvalCode((PyObject*) watch.expression, stack_entry->frame->f_globals, stack_entry->frame->f_locals);

				if (!eval_result)
				{
					if (PyErr_Occurred())
					{					
						PyObject* ptype, * pvalue, * ptraceback;
						PyErr_Fetch(&ptype, &pvalue, &ptraceback);

						if (pvalue)
						{
							PyObject* pstr = PyObject_Str(pvalue);

							Unicode_String error_string = Unicode_String::from_utf8(PyUnicode_AsUTF8(pstr), frame_allocator);
							
							watch.set_result_string(error_string);

							Py_DECREF(pstr);
						}

						Py_DECREF(ptype);
						Py_DECREF(pvalue);
						Py_DECREF(ptraceback);
					}
					else
					{
						watch.set_result_string(U"Watch didn't return a result");
					}
				}
				else
				{
					PyObject* pstr = PyObject_Str(eval_result);

					watch.set_result_string(Unicode_String::from_utf8(PyUnicode_AsUTF8(pstr), frame_allocator));

					Py_DECREF(pstr);
				}
			}
		}

		if (!stack_entry)
		{
			eval_result_string = U"Select stack entry to activate watches";
		}
		else
		{
			eval_result_string = watch.result_string;
		}



		Rect watch_result_rect = watch_rect;
		watch_result_rect.x_left = watch_expression_rect.x_right;
		
		if (!eval_result)
		{
			int text_width = measure_text_width(eval_result_string, ui.get_font_face());

			if (text_width > watch_result_rect.width())
			{
				watch_result_rect.x_right = watch_result_rect.x_left + min(text_width + scaled(default_fade_width), scaled(watch_result_max_width));

				watch_rect.x_right = watch_result_rect.x_right;
			}


			scoped_set_and_revert(ui.parameters.text_alignment, Text_Alignment::Left);

			renderer.draw_rect(watch_result_rect, watch_background_color);

			{
				scoped_renderer_mask(watch_result_rect, false);

				ui.draw_text(watch_result_rect.x_left + scaled(4), watch_result_rect.center_y(), eval_result_string, &watch_result_rect);

				{
					Rect fade_rect = watch_result_rect;
					fade_rect.x_left = fade_rect.x_right - scaled(default_fade_width);

					renderer.draw_rect_with_alpha_fade(fade_rect, watch_background_color, 0, 255);
				}
			}

			renderer.draw_rect_outline(watch_rect, watch_outline_color);
		}
		else
		{
			watch_result_rect.x_right = watch_result_rect.x_left + scaled(watch_variable_width);


			renderer.draw_rect_outline(watch_expression_rect, watch_outline_color, RECT_OUTLINE_LEFT_EDGE | RECT_OUTLINE_TOP_EDGE | RECT_OUTLINE_BOTTOM_EDGE);

			UI_ID variable_ui_id = ui_id_uuid(watch.uuid, 0);

			Field_State* field_state = (Field_State*) ui.get_ui_item_data(variable_ui_id);
			if (!field_state)
			{
				Field_State new_field_state = {
					// .py_object = arg_value,
					.child = NULL,
					.next = NULL,					
					.ui_id = variable_ui_id,
					.expanded = false,
				};

				field_state = allocate_and_copy(&new_field_state, c_allocator);
				
				ui.ui_id_data_array.put(variable_ui_id, field_state);
			}



			Variable_Parent variable_parent = {
				.type  = Variable_Parent_Type::None
			};


			renderer.draw_rect_outline(watch_result_rect, watch_outline_color, RECT_OUTLINE_TOP_EDGE | RECT_OUTLINE_RIGHT_EDGE | RECT_OUTLINE_BOTTOM_EDGE);

			int draw_variable_y_bottom = draw_variable(Unicode_String::empty, watch_result_rect.x_left, watch_result_rect.y_top, watch_result_rect.x_right, variable_parent, eval_result, field_state, variable_ui_id);

			if (draw_variable_y_bottom < watch_rect.y_bottom)
			{
				watch_rect.y_bottom = draw_variable_y_bottom;
			}
		}

		watch_y_top = watch_rect.y_bottom;
		i += 1;
	}


	{
		Rect add_new_watch_button_rect = Rect::make(watch_x_left, watch_y_top - scaled(add_new_watch_height), watch_x_right, watch_y_top);

		if (ui.button(add_new_watch_button_rect, U"Add new watch", add_new_watch_button_color, ui_id(0)))
		{
			Watch new_watch = {
				.expression = NULL,

				.expression_string = Unicode_String::empty,
				.result_string     = Unicode_String::empty,
			
				.uuid = generate_uuid(),
			};

			watches.add(new_watch);

			focus_debugger();
		}

		renderer.draw_rect_outline(add_new_watch_button_rect, watch_outline_color);
	}

	have_to_reevaluate_watches = false;
}


int Python_Debugger::draw_variable(Unicode_String variable_name, int x_left, int y_top, int x_right, Variable_Parent parent, PyObject* py_object, Field_State* field_state, UI_ID ui_id)
{
	scoped_set_and_revert(ui.parameters.text_alignment, Text_Alignment::Left);

	bool editable = parent.type != Variable_Parent_Type::None;

	// :adding_empty_child
	if (field_state->child == NULL)
	{
		Field_State dummy_first_field_state = {
			// .py_object = (PyObject*) 0xffff'ffff'ffff'ffff,
			.child = NULL,
			.next = NULL,
			.ui_id = invalid_ui_id,
			.expanded = false,
		};

		field_state->child = allocate_and_copy(&dummy_first_field_state, c_allocator);
	}


	auto change_hue = [](float hue)
	{
		float result = hue + 45.0;
		
		if (result > 360)
			return result - 360;

		return result;
	};
	scoped_set_and_revert(stack_arg_background_nested_color.h, change_hue(stack_arg_background_nested_color.h)); 



	Rect expand_button_rect = Rect::make(x_left, y_top - scaled(draw_variable_item_height), x_left, y_top); // By default width = 0

	auto draw_expand_button = [&]()
	{
		expand_button_rect.x_right = expand_button_rect.x_left + scaled(20.0);

		if (expand_button_rect.y_top >= 0 && expand_button_rect.y_bottom <= renderer.height)
		{
			scoped_set_and_revert(ui.parameters.button_text_margin, 0);
			scoped_set_and_revert(ui.parameters.text_alignment, Text_Alignment::Center);		 


			// Watches get redrawn every frame, so expression like 'dir(os)' is going to return a new object
			//   every frame, so we can't make UI_ID to rely on object pointer, cause it gets changed
			//   every time we evaluate a watch.
			//   My solution is to pass NULL instead of '(u64) py_object'
			//
			// UI_ID expand_button_ui_id = ui_id_next((u64) py_object, ui_id.sub_id, &ui_id);
			UI_ID expand_button_ui_id = ui_id_next(0, ui_id.sub_id, &ui_id);

			if (ui.button(expand_button_rect, field_state->expanded ? U"-" : U"+", rgba(40, 40, 80, 255), expand_button_ui_id))
			{
				field_state->expanded = !field_state->expanded;
			
				focus_debugger();
			}
		}
	};

	auto draw_object_repr = [&](Unicode_String additional_start_str)
	{
		Rect arg_rect = expand_button_rect;
		arg_rect.x_left = arg_rect.x_right;
		arg_rect.x_right = x_right;

		if (arg_rect.y_top >= 0 && arg_rect.y_bottom <= renderer.height)
		{
			renderer.draw_rect(arg_rect, stack_arg_background_color);

			PyObject* value_python_str = PyObject_Repr(py_object);
			
			Unicode_String value_str;
			if (value_python_str)
			{
				value_str = Unicode_String::from_utf8(PyUnicode_AsUTF8(value_python_str), frame_allocator);
				Py_DECREF(value_python_str);
			}
			else
			{
				value_str = format_unicode_string(frame_allocator, U"repr() returned NULL. Type: %", Py_TYPE(py_object)->tp_name);
			}


			Unicode_String print_text;
			if (variable_name.length)
			{
				print_text = format_unicode_string(frame_allocator, U"% % = %", additional_start_str, variable_name, value_str);
			}
			else
			{
				print_text = format_unicode_string(frame_allocator, U"% %", additional_start_str, value_str);
			}

		#define KILL_THE_PERFORMANCE 1
		#if KILL_THE_PERFORMANCE
			renderer.push_mask({
				.rect = arg_rect,
				.inversed = false
			});
		#endif
			ui.draw_text(arg_rect.x_left + scaled(stack_arg_margin), arg_rect.center_y(), print_text, &arg_rect);
			
			Rect fade_rect = arg_rect;
			fade_rect.x_left = fade_rect.x_right - scaled(stack_arg_background_fade_width);

			renderer.draw_rect_with_alpha_fade(fade_rect, stack_arg_background_color, 0, 255);
	
	
		#if KILL_THE_PERFORMANCE
			renderer.pop_mask();
		#endif
		#undef KILL_THE_PERFORMANCE
		}

		y_top = arg_rect.y_bottom;
	};


	if (PyDict_Check(py_object))
	{
		draw_expand_button();
		draw_object_repr(U"{}");

		if (field_state->expanded)
		{
			scoped_set_and_revert(stack_arg_background_color, stack_arg_background_nested_color.to_rgba());

			PyObject *key, *value;
			Py_ssize_t pos = 0;

			int variable_index = 0;	
			while (PyDict_Next(py_object, &pos, &key, &value))
			{
				UI_ID variable_ui_id = ui_id_next((u64) value, variable_index, &ui_id);

				Field_State* child_field_state = field_state->child->find_field_state(variable_ui_id);
				if (!child_field_state)
				{
					Field_State new_field_state = {
						// .py_object = value,
						.child = NULL,
						.next = NULL,						
						.ui_id = ui.copy_ui_id(variable_ui_id, c_allocator),
						.expanded = false,
					};

					child_field_state = allocate_and_copy(&new_field_state, c_allocator);
					
					field_state->child->add(child_field_state);
				}

				PyObject* key_py_str = PyObject_Repr(key);
				defer{ Py_DECREF(key_py_str); };

				Unicode_String arg_name = Unicode_String::from_utf8(PyUnicode_AsUTF8(key_py_str), frame_allocator);

				int offset_x = scaled(4.0);

				Variable_Parent variable_parent = {
					.dict = py_object,
					.key = key,
					.type = Variable_Parent_Type::Dict
				};

				y_top = draw_variable(arg_name, x_left + offset_x, y_top, x_right + offset_x, variable_parent, value, child_field_state, variable_ui_id);

				variable_index += 1;
			}
		}
	}
	else if (PyList_Check(py_object))
	{
		draw_expand_button();
		draw_object_repr(U"[]");


		if (field_state->expanded)
		{
			scoped_set_and_revert(stack_arg_background_color, stack_arg_background_nested_color.to_rgba());

			int list_count = PyList_Size(py_object);


			for (int variable_index = 0; variable_index < list_count; variable_index += 1)
			{
				PyObject* item = PyList_GetItem(py_object, variable_index);

				UI_ID variable_ui_id = ui_id_next((u64) item, variable_index, &ui_id);

				Field_State* child_field_state = field_state->child->find_field_state(variable_ui_id);
				if (!child_field_state)
				{
					Field_State new_field_state = {
						// .py_object = value,
						.child = NULL,
						.next  = NULL,						
						.ui_id = ui.copy_ui_id(variable_ui_id, c_allocator),
						.expanded = false,
					};

					child_field_state = allocate_and_copy(&new_field_state, c_allocator);
					
					field_state->child->add(child_field_state);
				}


				Unicode_String arg_name = format_unicode_string(frame_allocator, U"[%]", variable_index);

				int offset_x = scaled(4.0);

				Variable_Parent variable_parent = {
					.list = py_object,
					.index = variable_index,
					.type = Variable_Parent_Type::List
				};

				y_top = draw_variable(arg_name, x_left + offset_x, y_top, x_right + offset_x, variable_parent, item, child_field_state, variable_ui_id);
			}
		}
	}
	else if (PyNumber_Check(py_object))
	{
		draw_object_repr(U"");
	}
	else if (PyUnicode_Check(py_object))
	{
		draw_object_repr(U"");
	}
	else
	{
		PyObject* object_dict = PyObject_GenericGetDict(py_object, NULL);
		if (!object_dict)
			PyErr_Clear();

		if (object_dict)
			draw_expand_button();
		
		draw_object_repr(U"");

		
		if (object_dict && field_state->expanded)
		{
			scoped_set_and_revert(stack_arg_background_color, stack_arg_background_nested_color.to_rgba());

			PyObject *key, *value;
			Py_ssize_t pos = 0;

			int variable_index = 0;	
			while (PyDict_Next(object_dict, &pos, &key, &value))
			{
				UI_ID variable_ui_id = ui_id_next((u64) value, variable_index, &ui_id);

				Field_State* child_field_state = field_state->child->find_field_state(variable_ui_id);
				if (!child_field_state)
				{
					Field_State new_field_state = {
						// .py_object = value,
						.child = NULL,
						.next = NULL,						
						.ui_id = ui.copy_ui_id(variable_ui_id, c_allocator),
						.expanded = false,
					};

					child_field_state = allocate_and_copy(&new_field_state, c_allocator);
					
					field_state->child->add(child_field_state);
				}


				Unicode_String arg_name = Unicode_String::from_utf8(PyUnicode_AsUTF8(key), frame_allocator);

				int offset_x = scaled(4.0);

				Variable_Parent variable_parent = {
					.dict = object_dict,
					.key = key,
					.type = Variable_Parent_Type::Dict
				};

				y_top = draw_variable(arg_name, x_left + offset_x, y_top, x_right + offset_x, variable_parent, value, child_field_state, variable_ui_id);

				variable_index += 1;
			}
		}
	}

	return y_top;
}


void Python_Debugger::draw_source_code(Rect rect, Unicode_String source_code_string, Dynamic_Array<Source_Line> source_lines, int breakpoint_line, Source_Code source_code, UI_ID ui_id)
{
	assert(source_lines.count > 0);

	ZoneScoped;

	defer {
		renderer.draw_line(rect.x_left,  rect.y_top,    rect.x_right, rect.y_top,    source_code_outline_color);
		renderer.draw_line(rect.x_left,  rect.y_top,    rect.x_left,  rect.y_bottom,     source_code_outline_color);
		renderer.draw_line(rect.x_left,  rect.y_bottom, rect.x_right, rect.y_bottom, source_code_outline_color);
		renderer.draw_line(rect.x_right, rect.y_top,    rect.x_right, rect.y_bottom, source_code_outline_color);
	};

	struct State
	{
		bool focused;

		int cursor;
		int selection_length;
		int desired_precursor_pixels = -1;

		float mouse_offscreen_scroll_target_x;
		float mouse_offscreen_scroll_target_y;

		UI_ID scroll_region_ui_id;
		Scroll_Region_Result previous_scroll_region_result;
	};


	UI_ID text_editor_ui_id = ui_id_next(0, 0, &ui_id);
	UI_ID border_ui_id      = ui_id_next(1, 0, &ui_id);


	// UI_ID ui_id = source_code_ui_id;
	State* state = (decltype(state)) ui.get_ui_item_data(ui_id);

	if (!state)
	{
		state = (decltype(state)) c_allocator.alloc(sizeof(*state), code_location());
		ui.ui_id_data_array.put(ui_id, state);

		state->focused = false;
		state->cursor = 0;
		state->selection_length = 0;


		state->mouse_offscreen_scroll_target_y = 0.0;
		state->mouse_offscreen_scroll_target_x = 0.0;
		
		state->desired_precursor_pixels = -1;

		state->scroll_region_ui_id = null_ui_id;
	}


	if (state->cursor > source_code_string.length)
	{
		state->cursor = 0;
		state->selection_length = 0;
	}


	if (ui.down != invalid_ui_id && ui.down != text_editor_ui_id)
	{
		state->selection_length = 0;
		state->focused = false;
	}




	auto advance_selection = [&](int delta)
	{
		int old_cursor = state->cursor;
		state->cursor = clamp(0, source_code_string.length, state->cursor + delta);
		state->selection_length -= state->cursor - old_cursor;
	};

	auto next_word = [&](int direction, bool keep_selecting)
	{
		if (direction >= 0)
		{
			if (!keep_selecting)
			{
				state->cursor = min(state->cursor + state->selection_length, state->cursor);
				state->selection_length = 0;
			}


			int old_cursor = state->cursor;

			while (true)
			{
				if (state->cursor >= source_code_string.length) break;

				char32_t c = source_code_string[state->cursor];
				if (!is_whitespace(c))
				{
					break;
				}
				state->cursor += 1;
			}

			while (true)
			{
				if (state->cursor >= source_code_string.length) break;

				char32_t c = source_code_string[state->cursor + 1];

				state->cursor += 1;

				if (should_ctrl_arrow_stop_at_char(c))
				{
					break;
				}
			}

			if (keep_selecting)
			{
				state->selection_length -= (state->cursor - old_cursor);
			}
		}
		else
		{
			if (!keep_selecting)
			{
				state->cursor = max(state->cursor + state->selection_length, state->cursor);
				state->selection_length = 0;
			}


			int old_cursor = state->cursor;

			while (true)
			{
				if (state->cursor <= 0) break;

				char32_t c = source_code_string[state->cursor - 1];				
				if (!is_whitespace(c))
				{
					break;
				}
				state->cursor -= 1;
			}

			while (true)
			{
				if (state->cursor <= 0) break;

				char32_t c = source_code_string[state->cursor - 1];

				state->cursor -= 1;

				if (should_ctrl_arrow_stop_at_char(c))
				{
					break;
				}
			}

			if (keep_selecting)
			{
				state->selection_length -= (state->cursor - old_cursor);
			}
		}
	};

	auto move_cursor = [&](int delta)
	{
		if (state->selection_length)
		{
			int delta_sign = sign(delta);

			if (delta_sign == 0) return;


			int selection_side = state->cursor + state->selection_length;
			
			if (delta > 0)
				state->cursor = max(selection_side, state->cursor);  // Cursor is just another selection side
			else
				state->cursor = min(selection_side, state->cursor); 

			state->selection_length = 0;

			// Move the remaining delta. delta_sign is -1 or 1 always.
			delta -= delta_sign;
			state->cursor = clamp(0, source_code_string.length, state->cursor + delta);
		}
		else
		{
			state->cursor = clamp(0, source_code_string.length, state->cursor + delta);
		}
	};

	auto get_selected_string = [&]()
	{
		int selection_left  = min(state->cursor, state->cursor + state->selection_length);
		return source_code_string.sliced(selection_left, abs(state->selection_length));
	};





	bool modified = false;
	bool should_scroll_to_cursor = false;

	bool modified_because_of_enter = false;


	Font::Face* face = get_source_code_face();



	if (state->focused)
	{
		for (Input_Node node : input.nodes)
		{
			if (node.input_type == Input_Type::Key)
			{
				if (node.key_action == Key_Action::Down)
				{
					switch (node.key)
					{
						case Key::Left_Arrow:
						{
							should_scroll_to_cursor = true;
							state->desired_precursor_pixels = -1;

							if (input.is_key_down_or_held(Key::Any_Control))
							{
								next_word(-1, input.is_key_down_or_held(Key::Any_Shift));
							}
							else
							{
								if (input.is_key_down_or_held(Key::Any_Shift))
								{
									advance_selection(-1);
								}
								else
								{
									move_cursor(-1);
								}
							}
						}
						break;
						case Key::Right_Arrow:
						{
							should_scroll_to_cursor = true;
							state->desired_precursor_pixels = -1;

							if (input.is_key_down_or_held(Key::Any_Control))
							{
								next_word(1, input.is_key_down_or_held(Key::Any_Shift));
							}
							else
							{
								if (input.is_key_down_or_held(Key::Any_Shift))
								{
									advance_selection(1);
								}
								else
								{
									move_cursor(1);
								}
							}
						}
						break;

						// UpArrow and DownArrow handling is done after we calculated lines,
						//   it makes code easier, but breaks input order.
					}
				}
			}
		}

		if (input.is_key_combo_pressed(Key::Any_Control, Key::C))
		{
			Unicode_String selection = get_selected_string();
			copy_to_os_clipboard(selection, frame_allocator);
		}
		else if (input.is_key_combo_pressed(Key::Any_Control, Key::A))
		{
			should_scroll_to_cursor = true;
			state->cursor = 0;
			state->selection_length = source_code_string.length;
		}
	}



	auto y_top_of_the_line = [&](Source_Line* line) -> int
	{
		int index = source_lines.fast_pointer_index(line);

		return rect.y_top + state->previous_scroll_region_result.scroll_from_top - ((index + 1) * face->line_spacing) + face->line_spacing - face->baseline_offset;
	};


	auto pick_line_for_y_coord = [&](int y) -> Source_Line*
	{
		assert(source_lines.count);


		assert(state->scroll_region_ui_id != null_ui_id);
		int y_top = rect.y_top + state->previous_scroll_region_result.scroll_from_top;

		if (y > y_top) return source_lines[0];

		for (Source_Line& line : source_lines)
		{
			int y_bottom = y_top - face->line_spacing;

			if (y <= y_top && y > y_bottom)
			{
				return &line;
			}

			y_top = y_bottom;
		}

		return source_lines.last();
	};

	auto get_line_string = [&](Source_Line* line) -> Unicode_String
	{
		return source_code_string.sliced(line->start, line->length);
	};

	auto get_line_string_without_new_line = [&](Source_Line* line) -> Unicode_String
	{
		return source_code_string.sliced(line->start, line->length_without_new_line);
	};

	auto get_character_line = [&](int char_index) -> Source_Line*
	{
		for (Source_Line& line : source_lines)
		{
			if (char_index >= line.start && char_index < (line.start + line.length))
			{
				return &line;
			}
		}

		if (char_index == source_code_string.length)
		{
			return source_lines.last();
		}

		assert(false);
	};


	// This is the same code that is responsible for drawing cursor,
	//   but in this case it doesn't draw, but scrolls to the cursor
	if (state->scroll_region_ui_id != null_ui_id)
	{		
		Source_Line* cursor_line = get_character_line(state->cursor);

		int cursor_left_text_width = measure_text_width(get_line_string(cursor_line).sliced(0, state->cursor - cursor_line->start), face);

		int cursor_x_left = cursor_left_text_width - state->previous_scroll_region_result.state->scroll_from_left + rect.x_left + scaled(ui.parameters.text_field_margin);
		
		int cursor_y_top = y_top_of_the_line(cursor_line);

		if (should_scroll_to_cursor)
		{
			if (cursor_y_top > state->previous_scroll_region_result.view_rect.y_top)
			{
				state->previous_scroll_region_result.state->scroll_from_top = (source_lines.fast_pointer_index(cursor_line) * face->line_spacing) - face->baseline_offset / 2;
			}
			else if ((cursor_y_top - face->line_spacing) < state->previous_scroll_region_result.view_rect.y_bottom)
			{
				state->previous_scroll_region_result.state->scroll_from_top = (source_lines.fast_pointer_index(cursor_line) * face->line_spacing) + (face->baseline_offset / 2) - state->previous_scroll_region_result.view_rect.height() + face->line_spacing;
			}


			if (cursor_x_left < state->previous_scroll_region_result.view_rect.x_left)
			{
				state->previous_scroll_region_result.state->scroll_from_left = cursor_left_text_width;
			}
			else if ((cursor_x_left + ui.parameters.cursor_width * 10) > state->previous_scroll_region_result.view_rect.x_right)
			{
				state->previous_scroll_region_result.state->scroll_from_left = cursor_left_text_width - state->previous_scroll_region_result.view_rect.width() + ui.parameters.cursor_width * 10;
			}
		}
	}




	UI_ID scroll_region_ui_id = ui_id;
	scroll_region_ui_id.id += 1337;

	state->scroll_region_ui_id = scroll_region_ui_id;


	int scroll_region_height = source_lines.count * face->line_spacing + face->baseline_offset;


	Scroll_Region_Result scroll_region_result;
	{
		int previous_width = ui.parameters.scrollbar_width;

		scoped_set_and_revert(ui.parameters.scroll_region_background, source_code_background_color);


		scroll_region_result = ui.scroll_region(rect, scroll_region_height, 0, false, scroll_region_ui_id);

		ui.parameters.scrollbar_width = previous_width;
	}
	state->previous_scroll_region_result = scroll_region_result;





	auto is_mouse_in_text_editor_region = [&](int x)
	{
		return x > rect.x_left + scaled(source_code_border_width);
	};



	if (ui.is_point_inside_active_zone(input.mouse_x, input.mouse_y) && rect.is_point_inside(input.mouse_x, input.mouse_y))
	{
		// ui.im_hovering(ui_id);

		if (is_mouse_in_text_editor_region(input.mouse_x))
		{
			ui.im_hovering(text_editor_ui_id);
		}
		else
		{
			ui.im_hovering(border_ui_id);
		}
	}





	if (state->focused)
	{
		// For UpArrow and DownArrow we have to what lines are.
		{
			auto save_precursor_pixels_if_its_not_set = [&](Source_Line* line)
			{
				if (state->desired_precursor_pixels == -1)
				{
					state->desired_precursor_pixels = measure_text_width(get_line_string_without_new_line(line).sliced(0, state->cursor - line->start), face);
				}
			};

			auto set_cursor_based_on_precursor_pixels = [&](Source_Line* line)
			{
				int old_cursor = state->cursor;

				state->cursor = line->start + pick_appropriate_cursor_position(get_line_string_without_new_line(line), face, state->desired_precursor_pixels);

				if (input.is_key_down_or_held(Key::Any_Shift))
				{
					state->selection_length -= (state->cursor - old_cursor);
				}
				else
				{
					state->selection_length = 0;
				}
			};


			for (Input_Node node : input.nodes)
			{
				if (node.input_type == Input_Type::Key)
				{
					if (node.key_action == Key_Action::Down)
					{
						switch (node.key)
						{
							case Key::Up_Arrow:
							{
								should_scroll_to_cursor = true;

								if (!input.is_key_down_or_held(Key::Any_Shift) && state->selection_length)
								{
									move_cursor(-1);
								}

								Source_Line* cursor_line = get_character_line(state->cursor);
								save_precursor_pixels_if_its_not_set(cursor_line);
								
								if (source_lines.fast_pointer_index(cursor_line) > 0)
								{
									Source_Line* target_line = cursor_line - 1;

									set_cursor_based_on_precursor_pixels(target_line);								
								}
							}
							break;

							case Key::Down_Arrow:
							{
								should_scroll_to_cursor = true;


								if (!input.is_key_down_or_held(Key::Any_Shift) && state->selection_length)
								{
									move_cursor(1);
								}

								Source_Line* cursor_line = get_character_line(state->cursor);
								save_precursor_pixels_if_its_not_set(cursor_line);
								
								if (source_lines.fast_pointer_index(cursor_line) < (source_lines.count - 1))
								{
									Source_Line* target_line = cursor_line + 1;

									set_cursor_based_on_precursor_pixels(target_line);								
								}
							}
							break;
						}
					}
				}
			}
		}
	}


	int global_x_left_offset = scaled(source_code_border_width) + scaled(ui.parameters.text_field_margin);


	if (ui.down == text_editor_ui_id)
	{
		focus_debugger();

		Source_Line* line = pick_line_for_y_coord(input.old_mouse_y);

		int chars_before_mouse = line->start + pick_appropriate_cursor_position(get_line_string_without_new_line(line), face, input.old_mouse_x - rect.x_left + scroll_region_result.scroll_from_left - global_x_left_offset);

		if (input.is_key_down_or_held(Key::Any_Shift))
		{
			state->selection_length -= chars_before_mouse - state->cursor;
		}
		else
		{
			state->selection_length = 0;
		}

		state->cursor = chars_before_mouse;

		state->focused = true;
	}
	else if (ui.holding == text_editor_ui_id)
	{
		int mouse_clipped = clamp(rect.x_left + 1, rect.x_right - 1, input.mouse_x); // This +1 -1 is some hack to make scroll speed sane when mouse is outside of rect. I don't know how it works.

		Source_Line* line = pick_line_for_y_coord(input.mouse_y);

		int chars_before_mouse = line->start + pick_appropriate_cursor_position(get_line_string_without_new_line(line), face, mouse_clipped - rect.x_left + scroll_region_result.scroll_from_left - global_x_left_offset);

		int old_cursor = state->cursor;

		state->cursor            = chars_before_mouse;
		state->selection_length -= chars_before_mouse - old_cursor;



		{		
			float speed = 10.0;
			float delta = 0.0;

			float font_face_float = (float) face->line_spacing;

			if (input.mouse_y < scroll_region_result.view_rect.y_bottom)
			{
				speed *= float(scroll_region_result.view_rect.y_bottom - input.mouse_y) * 0.1;

				delta = font_face_float * speed * frame_time;
			}
			else if (input.mouse_y > scroll_region_result.view_rect.y_top)
			{
				speed *= float(input.mouse_y - scroll_region_result.view_rect.y_top) * 0.1;

				delta = font_face_float * speed * frame_time;
				delta *= -1.0;
			}


			state->mouse_offscreen_scroll_target_y += delta;


			if (abs(state->mouse_offscreen_scroll_target_y) > 1.0)
			{
				float move_delta = round(state->mouse_offscreen_scroll_target_y);

				state->mouse_offscreen_scroll_target_y -= move_delta;
				scroll_region_result.state->scroll_from_top += (int) move_delta;
			}
		}

		{
			float speed = 10.0;
			float delta = 0.0;

			float font_face_float = (float) face->line_spacing;

			if (input.mouse_x < scroll_region_result.view_rect.x_left)
			{
				speed *= float(scroll_region_result.view_rect.x_left - input.mouse_x) * 0.1;

				delta = font_face_float * speed * frame_time;
			}
			else if (input.mouse_x > scroll_region_result.view_rect.x_right)
			{
				speed *= float(input.mouse_x - scroll_region_result.view_rect.x_right) * 0.1;

				delta = font_face_float * speed * frame_time;
				delta *= -1.0;
			}


			state->mouse_offscreen_scroll_target_x += delta;


			if (abs(state->mouse_offscreen_scroll_target_x) > 1.0)
			{
				float move_delta = round(state->mouse_offscreen_scroll_target_x);

				state->mouse_offscreen_scroll_target_x -= move_delta;
				scroll_region_result.state->scroll_from_left -= (int) move_delta;
			}
		}
	}



	{

		int selection_start = min(state->cursor, state->cursor + state->selection_length);
		int selection_end   = max(state->cursor, state->cursor + state->selection_length);

		int y = rect.y_top + scroll_region_result.scroll_from_top - scroll_region_result.scroll_from_left;


		for (Source_Line& line: source_lines)
		{
			y -= face->line_spacing;

			if (false) // Draw line index for debugging purposes
			{
				scoped_set_and_revert(ui.parameters.text_alignment, Text_Alignment::Left);

				ui.draw_text(rect.x_left, y, to_string(line.index, frame_allocator).to_unicode_string(frame_allocator));
			}

			Unicode_String line_string = get_line_string(&line);

			if (line.index == breakpoint_line)
			{
				renderer.draw_rect(Rect::make(rect.x_left, y - face->baseline_offset, rect.x_right, y + face->line_spacing - face->baseline_offset), rgba(64, 10, 10, 255));
			}


			String_Glyph_Iterator<char32_t> glyph_iterator = string_by_glyphs<char32_t>(line_string, face);
			int glyph_iterator_previous_x = glyph_iterator.x;

			int x = rect.x_left + global_x_left_offset;


			int i = line.start;
			while (glyph_iterator.next())
			{
				defer{ i += 1; };
				int x_delta = glyph_iterator.x - glyph_iterator_previous_x;
				defer{ glyph_iterator_previous_x = glyph_iterator.x; };
				defer{ x += x_delta; };

				Rect char_background_rect = Rect::make(x, y - face->baseline_offset, x + x_delta, y - face->baseline_offset + face->line_spacing);

				if (i >= selection_start && i < selection_end)
				{
					renderer.draw_rect(char_background_rect, ui.parameters.text_selection_background);
				}

				if (glyph_iterator.render_glyph)
				{
					Glyph glyph = glyph_iterator.current_glyph;
					// :GlyphLocalCoords:
					renderer.draw_glyph(&glyph, x + glyph.left_offset, y - (glyph.height - glyph.top_offset), ui.parameters.text_color, do_need_to_gamma_correct(face));
				}
			}
		}


		if (state->focused)
		{		
			Source_Line* cursor_line = get_character_line(state->cursor);

			int cursor_left_text_width = measure_text_width(get_line_string(cursor_line).sliced(0, state->cursor - cursor_line->start), face);

			int cursor_x_left = rect.x_left + cursor_left_text_width + global_x_left_offset;
			
			int cursor_y_top = y_top_of_the_line(cursor_line);
			
			renderer.draw_rect(Rect::make(cursor_x_left, cursor_y_top - face->line_spacing, cursor_x_left + ui.parameters.cursor_width, cursor_y_top), rgba(255, 255, 255, 255));
		}
	}


	// Draw breakpoints
	{
		Rect border_rect = rect;
		border_rect.x_right = border_rect.x_left + scaled(source_code_border_width);
		

		rgba border_draw_color = source_code_border_color;
		if (ui.hover == border_ui_id)
		{
			border_draw_color = border_draw_color * 1.4;
		}
		renderer.draw_rect(border_rect, border_draw_color);

		if (ui.down == border_ui_id)
		{
			focus_debugger();
		}



		auto find_source_line = [&](int line_number) -> Source_Line*
		{
			for (int i = 0; i < source_lines.count; i++)
			{
				if (source_lines[i]->index == line_number)
				{
					return source_lines[i];
				}
			}

			return NULL;
		};


		Source_Line* clicked_line = NULL;
		if (ui.down == border_ui_id)
		{
			clicked_line = pick_line_for_y_coord(input.old_mouse_y);
		}

		bool was_there_a_breakpoint_on_clicked = false;

		assert(source_code.file_name);

		for (int i = 0; i < breakpoints.count; i++)
		{
			Breakpoint* breakpoint = breakpoints[i];

			if (!breakpoint->is_same_file(source_code.file_name)) continue;

			if (clicked_line)
			{
				if (breakpoint->line_number == clicked_line->index)
				{
					assert(!was_there_a_breakpoint_on_clicked);
					was_there_a_breakpoint_on_clicked = true;

					breakpoint->free();
					breakpoints.remove_at_index(i);
					i -= 1;

					continue;
				}
			}
			else
			{

			}

			Source_Line* breakpoint_line = find_source_line(breakpoint->line_number);

			if (breakpoint_line)
			{
				UI_ID breakpoint_ui_id = ui_id_uuid(breakpoint->uuid, 0);

				int y_top = y_top_of_the_line(breakpoint_line);
				int y_bottom = y_top - face->line_spacing;

				for (int i = source_lines.fast_pointer_index(breakpoint_line); i < source_lines.count; i++)
				{
					if (source_lines[i]->index != breakpoint_line->index) break;

					y_bottom = y_top_of_the_line(source_lines[i]) - face->line_spacing;
				}

				Rect breakpoint_rect = Rect::make(rect.x_left, y_bottom, rect.x_left + scaled(source_code_border_width), y_top);

				breakpoint_rect.shrink(breakpoint_margin, breakpoint_margin, breakpoint_margin, breakpoint_margin);

				renderer.draw_rect(breakpoint_rect, breakpoint_color);
			}
		}

		if (clicked_line && !was_there_a_breakpoint_on_clicked)
		{
		#if DEBUG
			for (Breakpoint& breakpoint: breakpoints)
			{
				if (breakpoint.is_same_file(source_code.file_name) && breakpoint.line_number == clicked_line->index)
				{
					assert(false && "Found existing breakpoint at the same line and file");
				}
			}
		#endif

			Breakpoint breakpoint = {
				.python_file_name = source_code.file_name,
				.line_number = clicked_line->index,
				
				.condition = NULL,
				.condition_string = Unicode_String::empty,

				.uuid = generate_uuid(),
			};

			Py_INCREF(source_code.file_name);

			breakpoints.add(breakpoint);
		}
	}


	ui.end_scroll_region();
}