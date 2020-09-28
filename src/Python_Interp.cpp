#include "Tracy_Header.h"

#include "Python_Interp.h"

#include "Main.h"
#include "Terminal_IO.h"

#include "Python_IO_Module.h"



#if OS_WINDOWS

static PyObject* win_create_process_with_pty_impl(PyObject* self, PyObject* args)
{

#if 1
	char* cmd_line; 
	PyArg_ParseTuple(args, "s", &cmd_line);
#else
	const char* cmd_line = PyUnicode_AsUTF8(args);
#endif

	Unicode_String utf32_cmd_line = Unicode_String::from_utf8(cmd_line, c_allocator);
	wchar_t*       wide_cmd_line = (wchar_t*) utf32_cmd_line.to_utf16(c_allocator); 


	STARTUPINFOEX si;
    ZeroMemory(&si, sizeof(si));
    si.StartupInfo.cb = sizeof(STARTUPINFOEX);

	DWORD creation_flags = CREATE_NEW_PROCESS_GROUP;

#if USE_PTY
	creation_flags |= EXTENDED_STARTUPINFO_PRESENT;

    size_t bytesRequired;
    InitializeProcThreadAttributeList(NULL, 1, 0, &bytesRequired);
    
    terminal_io.pty_attr_list = (PPROC_THREAD_ATTRIBUTE_LIST) c_allocator.alloc(bytesRequired, code_location());

    // Initialize the list memory location
    if (!InitializeProcThreadAttributeList(terminal_io.pty_attr_list, 1, 0, &bytesRequired))
    {
    	abort_the_mission(U"InitializeProcThreadAttributeList have failed");
    }

    // Set the pseudoconsole information into the list
    if (!UpdateProcThreadAttribute(terminal_io.pty_attr_list,
                                   0,
                                   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   terminal_io.pty_handle,
                                   sizeof(terminal_io.pty_handle),
                                   NULL,
                                   NULL))
    {
    	abort_the_mission(U"UpdateProcThreadAttribute have failed");
    }

    si.lpAttributeList = terminal_io.pty_attr_list;
#else


    si.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput  = terminal_io.stdin_read_pipe;
    si.StartupInfo.hStdOutput = terminal_io.stdout_write_pipe;
    si.StartupInfo.hStdError  = terminal_io.stdout_write_pipe;
#endif


	PROCESS_INFORMATION pi;
	ZeroMemory(&pi, sizeof(pi));


    if (!CreateProcessW(NULL,
                        wide_cmd_line,
                        NULL,
                        NULL,
                        TRUE,
						creation_flags,
                        NULL,
                        NULL,
                        &si.StartupInfo,
                        &pi))
    {
    	PyErr_SetFromWindowsErr(0);

    	return NULL;
    }

    return Py_BuildValue("K, I", pi.hProcess, pi.dwProcessId);
};

#endif


void Python::thread_proc()
{
	create_arena_allocator(&arena, c_allocator, 4096);

	{
		ZoneScopedN("Python initialization");


		{
			/*
			PyModuleDef typer_internal_def =
			{
				PyModuleDef_HEAD_INIT,
				"typer_internal", NULL, -1, 0,
			};
			*/


		}



		Unicode_String python38_zip = path_concat(arena, typer_directory, Unicode_String(U"python38.zip"));



		Py_SetPath((wchar_t*) python38_zip.to_wide_string(arena));

		{
			ZoneScopedN("Py_Initialize()");
			Py_Initialize();
		}

		emb::PyInit_emb();

		Py_SetProgramName((wchar_t*) Unicode_String(U"Typerminal").to_wide_string(arena));

		python.globals = PyDict_New();
		PyDict_SetItemString(python.globals, "__builtins__", PyEval_GetBuiltins());


const char* preload = R"PRELOAD(
import os
import sys
import importlib

#__typer_dir__ will be set below on C++ side.

if sys.platform == 'win32':
	sys.path.append(os.path.normpath(os.path.join(__typer_dir__, "python/python_lib_windows")))
elif sys.platform == 'linux':
	sys.path.append(os.path.normpath(os.path.join(__typer_dir__, "python/python_lib_linux")))
elif sys.platform == 'darwin':
	sys.path.append(os.path.normpath(os.path.join(__typer_dir__, "python/python_lib_osx")))


sys.path.append(os.path.normpath(os.path.join(__typer_dir__, "python")))

import typer
import typer_io_internal_setup

from typer_commands import *

import typer_debugger


sys.path.append(os.path.normpath(os.path.join(__typer_dir__, "user_scripts")))

)PRELOAD";


		{
			ZoneScopedN("Compile PyUnicode_FromStringAndSize eval preload");

			char* typer_dir_utf8 = typer_directory.to_utf8(arena, NULL); 

			PyObject* typer_dir_py = PyUnicode_FromString(typer_dir_utf8);
			defer { Py_DECREF(typer_dir_py); };

			PyDict_SetItemString(python.globals, "__typer_dir__", typer_dir_py);

			PyObject* code = Py_CompileString(preload, "Typerminal.exe/preload", Py_file_input);
			if (!code)
			{
				Unicode_String error_str = Unicode_String::empty;
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
								error_str = Unicode_String::from_utf8(err_msg, arena);
							}
						}
					}
				}

				abort_the_mission(U"Failed to compile python/preload.py. Error:\n%", error_str);
				return;
			}

			PyObject* result = PyEval_EvalCode(code, python.globals, python.globals);
			Py_DECREF(code);

			if (!result)
			{
				python.dump_error();
				python.print_error();
			}
			else
			{
				Py_DECREF(result);
			}
		}

		{
		#if OS_WINDOWS
			ZoneScopedN("Call typer_io_internal_setup");

			python.typer_io_internal_setup_module = PyDict_GetItemString(python.globals, "typer_io_internal_setup");

			if (python.typer_io_internal_setup_module)
			{
				PyObject* typer_io_setup_module_dict = PyModule_GetDict(python.typer_io_internal_setup_module);

				python.typer_windows_file_descriptors = PyDict_GetItemString(typer_io_setup_module_dict, "typer_windows_file_descriptors");
				python.typer_close_file_descriptors = PyDict_GetItemString(typer_io_setup_module_dict, "typer_windows_close_file_descriptors");
				python.typer_windows_handle_to_crt_descriptor = PyDict_GetItemString(typer_io_setup_module_dict, "typer_windows_handle_to_crt_descriptor");

				if (!python.typer_windows_file_descriptors)
				{
					python.dump_error();
					abort_the_mission(U"Failed to find typer_windows_file_descriptors in preload.py");
				}
				if (!python.typer_close_file_descriptors)
				{
					python.dump_error();
					abort_the_mission(U"Failed to find typer_windows_close_file_descriptors in preload.py");
				}
				if (!python.typer_windows_handle_to_crt_descriptor)
				{
					python.dump_error();
					abort_the_mission(U"Failed to find typer_windows_handle_to_crt_descriptor in preload.py");
				}
			}
			else
			{
				abort_the_mission(U"Failed to find typer_io_internal_setup module in preload.py");
			}
		#endif
		}

		{
			python.typer_module = PyDict_GetItemString(python.globals, "typer");
			if (!python.typer_module)
			{
				abort_the_mission(U"Failed to find 'typer' module in preload.py");
			}

			python.typer_execution_number = PyDict_GetItemString(PyModule_GetDict(python.typer_module), "execution_index");
		
			if (!python.typer_execution_number)
			{
				abort_the_mission(U"Failed to find execution_index in module 'typer' in preload.py");
			}
		}

	#if OS_WINDOWS
		{
			
			static PyMethodDef win_create_process_with_pty = {
				"win_create_process_with_pty",
				win_create_process_with_pty_impl,
				METH_VARARGS,
				"typer.win_create_process_with_pty"
			};

			PyObject *func = PyCFunction_New(&win_create_process_with_pty, NULL);

			PyObject_SetAttrString(python.typer_module, win_create_process_with_pty.ml_name, func);

			python.print_error();
		}
	#endif

		python.number_1 = PyLong_FromLong(1);

		PyObject_SetAttrString(python.typer_module, "typer_directory", PyUnicode_FromString(typer_directory.to_utf8(arena)));

		// Important to call after preload.py
		python_debugger.init();
	}

	arena.reset();

	python.interp.python_initialization_done_semaphore.increment();

	while (true)
	{
		defer { arena.reset(); };

		python.interp.begin_execution_semaphore.wait_and_decrement();

		ZoneScopedN("Python interp execute");


		if (!python.interp.limited_context)
		{
	#if OS_WINDOWS
			PyObject* stdout_fd_result = PyObject_CallFunction(python.typer_windows_handle_to_crt_descriptor, "K", (u64) terminal_io.stdout_write_pipe);
			PyObject* stdin_fd_result = PyObject_CallFunction(python.typer_windows_handle_to_crt_descriptor, "K", (u64) terminal_io.stdin_read_pipe);

			if (!stdout_fd_result)
			{
				abort_the_mission(U"typer_windows_handle_to_crt_descriptor have failed on stdout_write_pipe");
			}

			if (!stdin_fd_result)
			{
				abort_the_mission(U"typer_windows_handle_to_crt_descriptor have failed on stdin_read_pipe");
			}

			python.stdout_fd = PyLong_AsLong(stdout_fd_result);
			python.stdin_fd  = PyLong_AsLong(stdin_fd_result);

	#else
			python.stdin_fd  = terminal_io.stdin_read_pipe;
			python.stdout_fd = terminal_io.stdout_write_pipe;
	#endif
		}
		
		if (python.interp.execute_stages & INTERP_STAGE_IMPORT_USER_MAIN)
		{
			assert(!python.interp.limited_context);
			ZoneScopedN("import user_main.py");

			// Execute user_main.py
			const char* code_string = "if os.path.exists(os.path.join(__typer_dir__, \"user_scripts\", \"user_main.py\")):\n\tfrom user_main import *";
			PyObject* code = Py_CompileString(code_string, "Load user_main.py", Py_file_input);

			PyObject* result = PyEval_EvalCode(code, python.globals, nullptr);

			if (!result)
			{
				python.print_error();
			}
			else
			{
				Py_DECREF(result);
			}

			PyDict_DelItemString(python.globals, "__typer_dir__");

			Py_DECREF(code);
		}

		if (python.interp.execute_stages & INTERP_STAGE_RUN_EXECUTION_BEGIN_FUNCTIONS)
		{
			assert(!python.interp.limited_context);
			ZoneScopedN("execute execution begin functions");

			// Increment execution number
			{
				PyObject* result = PyNumber_InPlaceAdd(python.typer_execution_number, python.number_1);
				Py_DECREF(python.typer_execution_number);
				python.typer_execution_number = result;

				if (PyDict_SetItemString(PyModule_GetDict(python.typer_module), "execution_index", result) == -1)
				{
					abort_the_mission(U"Failed to PyDict_SetItemString when tried to increment typer.execution_index");
				}
			}

			// Execute execution_begin_functions
			{
				PyObject* func = PyDict_GetItemString(PyModule_GetDict(python.typer_module), "execute_execution_begin_functions");
				if (!func)
				{
					abort_the_mission(U"Failed to find execute_execution_begin_functions in typer.py");
				}

				PyObject_CallFunction(func, "");

				if (PyErr_Occurred())
				{
					python.print_error();
				}
			}
		}


		if (python.interp.execute_stages & INTERP_STAGE_RUN_CODE)
		{
			assert(!python.interp.limited_context);
			ZoneScopedN("execute code string");

			defer { c_allocator.free(python.interp.code_string.data, code_location()); };

			int utf8_length;
			char* utf8 = python.interp.code_string.to_utf8(arena, &utf8_length);
			
			PyObject* py_string = PyUnicode_FromStringAndSize(utf8, utf8_length);
			
			call_python(python.typer_module, "input_procedure", py_string, python.globals);

			Py_DECREF(py_string);
	
			if (PyErr_Occurred())
			{
				PyErr_Print();
				PyErr_Clear();
			}
		}

		if (python.interp.execute_stages & INTERP_STAGE_RUN_PROMPT)
		{
			assert(!python.interp.limited_context);
			ZoneScopedN("execute_prompt");

			call_python(python.typer_module, "prompt");

			if (PyErr_Occurred())
			{
				PyErr_Print();
				PyErr_Clear();
			}
		}

		if (python.interp.execute_stages & INTERP_STAGE_RUN_AUTOCOMPLETE)
		{
			assert(python.interp.limited_context);
			ZoneScoped("run_autocomplete_proc");


			int utf8_length;
			char* utf8 = python.interp.string_to_autocomplete.to_utf8(arena, &utf8_length);
			PyObject* py_string = PyUnicode_FromStringAndSize(utf8, utf8_length);


			PyObject* result = call_python(python.typer_module, "autocomplete_procedure", py_string, python.globals);

			if (PyErr_Occurred())
			{
				python.dump_error();
				PyErr_Clear();
			}


			Scoped_Lock lock(python.autocomplete_result_mutex);

			// Free old results
			{

				current_autocomplete_result_index = 0;
				typer_ui.autocomplete_suggestion_move_state  = 0;
				typer_ui.autocomplete_suggestion_move_target = 0;

				if (python.autocompleted_string.data)
				{
					c_allocator.free(python.autocompleted_string.data, code_location());
				}


				python.autocompleted_string = python.interp.string_to_autocomplete; // String should be allocated by c_allocator
				python.interp.string_to_autocomplete = Unicode_String(NULL, -1);

				// Free old results, or allocate memory for new results.
				{
					if (python.autocomplete_result.data)
					{
						for (Unicode_String str : python.autocomplete_result)
						{
							c_allocator.free(str.data, code_location());
						}

						python.autocomplete_result.clear();
					}
					else
					{
						python.autocomplete_result = make_array<Unicode_String>(16, c_allocator);
					}
				}
			}


			if (result)
			{	
				auto add_to_autocomplete_result = [&](PyObject* object)
				{
					const char* utf8_str = PyUnicode_AsUTF8(object);
						
					python.autocomplete_result.add(Unicode_String::from_utf8(utf8_str, c_allocator));
				};

				if (PyUnicode_Check(result))
				{
					add_to_autocomplete_result(result);
				}
				else if (PyList_Check(result))
				{
					auto list_size = PyList_Size(result);
					
					for (decltype(list_size) i = 0; i < list_size; i++)
					{
						PyObject* list_item = PyList_GetItem(result, i);
						
						if (PyUnicode_Check(list_item))
						{
							add_to_autocomplete_result(list_item);
						}
					}
				}
			}

			if (PyErr_Occurred())
			{
				python.dump_error();
				PyErr_Clear();
			}
		}


		if (!python.interp.limited_context)
		{
	#if OS_WINDOWS
			PyObject_CallFunction(python.typer_close_file_descriptors, "");
	#endif

			python.stdin_fd = -1;
			python.stdout_fd = -1;
		}

		python.interp.done_execution = true;
	}
}



void Python::init()
{
	ZoneScopedN("Python::init");

	auto python_thread_proc = [](void* dummy_ptr)
	{
		TRACY_THREAD_NAME("Python interp thread");

		ctx.logger = typer_logger;

		python.thread_proc();
	};

	interp.thread = create_thread(c_allocator, python_thread_proc, NULL);

	{
		ZoneScopedN("wait for python initialization");
		interp.python_initialization_done_semaphore.wait_and_decrement();
	}

	need_to_redraw_next_frame(code_location());
}

bool Python::run(Unicode_String str) // str lifetime is function-local.
{
	ZoneScoped;


	interp.code_string = str.copy_with(c_allocator);

	is_running = true;

	interp.execute_stages = INTERP_STAGE_RUN_CODE | INTERP_STAGE_RUN_EXECUTION_BEGIN_FUNCTIONS  | INTERP_STAGE_RUN_PROMPT;

	interp.begin_execution_semaphore.increment();

	return true;
}

void Python::run_only_prompt()
{
	ZoneScoped;
	is_running = true;

	interp.execute_stages = INTERP_STAGE_RUN_PROMPT;

	interp.begin_execution_semaphore.increment();
}

void Python::import_user_main()
{
	ZoneScoped;

	is_running = true;

	interp.execute_stages = INTERP_STAGE_RUN_PROMPT | INTERP_STAGE_IMPORT_USER_MAIN;

	interp.begin_execution_semaphore.increment();
}

void Python::run_autocomplete(Unicode_String str)
{
	ZoneScoped;

	is_running = true;

	interp.limited_context = true;

	interp.execute_stages = INTERP_STAGE_RUN_AUTOCOMPLETE;

	interp.string_to_autocomplete = str.copy_with(c_allocator);

	interp.begin_execution_semaphore.increment();
}


void Python::keyboard_interrupt(Code_Location code_location)
{
	assert(threading.is_main_thread());

	auto gil_state = PyGILState_Ensure();
	// PyThreadState* this_ts = PyGILState_GetThisThreadState();
	// assert(this_ts);
	// PyThreadState* previous_ts = PyThreadState_Swap(this_ts);
	// defer { PyThreadState_Swap(previous_ts); };
	
	PyErr_SetInterrupt();
	// PyErr_CheckSignals();

	PyGILState_Release(gil_state);
}

void Python::supervise_running_interp()
{
	ZoneScoped;
	assert(is_running);
	assert(threading.is_main_thread());

	if (interp.done_execution)
	{
		interp.done_execution = false;

		is_running = false;

		cleanup_after_command(interp.limited_context);

		interp.limited_context = false;
		interp.execute_stages = 0;

		python_debugger.free_thread_states();
	}
}

void Python::run_autocomplete_if_its_not_running()
{
	if (is_running) return;

	prepare_for_running_command(true);

	run_autocomplete(terminal.copy_user_input(frame_allocator));
}


void Python::print_error()
{
	ZoneScoped;

	if (PyErr_Occurred())
	{
		PyErr_Print();
	}
}


void Python::dump_error()
{
	ZoneScoped;

	if (PyErr_Occurred())
	{
		Arena_Allocator arena;
		create_arena_allocator(&arena, c_allocator, 4096);
		defer { arena.free(); };

		auto b = build_string<char32_t>(arena);


		PyObject* ptype, * pvalue, * ptraceback;
		PyErr_Fetch(&ptype, &pvalue, &ptraceback);
		PyErr_NormalizeException(&ptype, &pvalue, &ptraceback);


		if (ptraceback)
		{

			PyTracebackObject* trace = (PyTracebackObject*)ptraceback;
			while (trace)
			{
				defer{
					trace = trace->tb_next;
				};

				PyFrameObject* frame = trace->tb_frame;

				PyCodeObject* code = frame->f_code;

				if (code->co_filename)
				{
					Unicode_String file_name = Unicode_String::from_utf8(PyUnicode_AsUTF8(code->co_filename), arena);
					Unicode_String code_name = Unicode_String::from_utf8(PyUnicode_AsUTF8(code->co_name), arena);

					b.append(format_unicode_string(arena, U"%:%    %", file_name, PyFrame_GetLineNumber(frame), code_name));
				}
				else
				{
					b.append(U"\"unknown source code location\"");
				}
				b.append('\n');
			}
		}


		if (pvalue)
		{
			{
				if (ptype)
				{
					PyObject* pstr = PyObject_Str(ptype);

					if (pstr)
					{
						const char* utf8_str = PyUnicode_AsUTF8(pstr);

						Unicode_String str = Unicode_String::from_utf8(utf8_str, arena);
						b.append(str);
						b.append(U"  ");
					}
				}
			}

			PyObject* pstr = PyObject_Str(pvalue);

			if (pstr)
			{
				const char* utf8_str = PyUnicode_AsUTF8(pstr);

				Unicode_String str = Unicode_String::from_utf8(utf8_str, arena);
				b.append('\n');
				b.append(str);

				if (python.interp.limited_context)
				{
					// To keep main thread's horses.
					Scoped_Lock lock_1(terminal.characters_mutex);

					report_limited_context_error(b.get_string());
				}
				else
				{
					abort_the_mission(b.get_string());
				}
			}
		}
	}
}

void Python::report_limited_context_error(Unicode_String str)
{
#if OS_WINDOWS

	char16_t* utf16_str = str.to_utf16(c_allocator);
	defer { c_allocator.free(utf16_str, code_location()); };

	MessageBoxW(NULL, (wchar_t*) utf16_str, L"Limited context error", 0);
#else
	static_assert(false); // @TODO: implement
#endif
}