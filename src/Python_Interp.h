#pragma once

#include "b_lib/Threading.h"
#include "b_lib/String_Builder.h"
#include "b_lib/String.h"
#include "b_lib/Arena_Allocator.h"

// SIZEOF_WCHAR_T Non defined for some reason on OS X
#if OS_DARWIN
static_assert(sizeof(wchar_t) == 4);
#define SIZEOF_WCHAR_T 4
#endif


#define PY_SSIZE_T_CLEAN
#ifdef _DEBUG
#undef _DEBUG
#include "python_headers/Python.h"
#define _DEBUG
#else
#include "python_headers/Python.h"
#endif

#include "python_headers/frameobject.h"


#include "Tracy_Header.h"


// :LimitedExecutionContext
// Basiclly this means that Python doesn't get IO handles.
// But definition may change in future.



const u32 INTERP_STAGE_RUN_CODE                           = 1;
const u32 INTERP_STAGE_RUN_EXECUTION_BEGIN_FUNCTIONS      = 1 << 1;
const u32 INTERP_STAGE_RUN_PROMPT                         = 1 << 2;
const u32 INTERP_STAGE_IMPORT_USER_MAIN                   = 1 << 3;
const u32 INTERP_STAGE_RUN_AUTOCOMPLETE                   = 1 << 4;


struct Interp_Thread_Data
{
	Unicode_String code_string;

	bool done_execution;

	Thread thread;
	Semaphore begin_execution_semaphore = create_semaphore();
	Semaphore python_initialization_done_semaphore = create_semaphore();

	bool limited_context = false;
	u32 execute_stages = 0;

	Unicode_String string_to_autocomplete;

	PyObject* running_code = NULL;
};

const Unicode_String user_input_source_code_location_name = U"Terminal input";

struct Python
{
	PyObject* globals;


	Interp_Thread_Data interp;
	bool is_running = false;

	inline bool is_running_in_non_limited_context()
	{
		return is_running && !interp.limited_context;
	}


	PyObject* typer_io_internal_setup_module;

#if OS_WINDOWS
	PyObject* typer_windows_file_descriptors;
	PyObject* typer_close_file_descriptors;
	PyObject* typer_windows_handle_to_crt_descriptor;
#endif

	int stdout_fd = -1; // sys.stdout.fileno() will throw exception if this is -1
	int stdin_fd  = -1; // sys.stdin. fileno() will throw exception if this is -1

	PyObject* typer_module;
	PyObject* typer_execution_number;
	PyObject* number_1;

	Arena_Allocator arena;


	Mutex                         autocomplete_result_mutex;
	int                           current_autocomplete_result_index = 0;
	Unicode_String                autocompleted_string = Unicode_String(NULL, -1); // -1 to make even empty string trigger autocomplete.
	Dynamic_Array<Unicode_String> autocomplete_result = Dynamic_Array<Unicode_String>::empty();


	void init();


	void run_autocomplete_if_its_not_running();


	bool run(Unicode_String str);
	void run_only_prompt();
	void import_user_main();
	void run_autocomplete(Unicode_String str);

	void keyboard_interrupt(Code_Location code_location);

	void supervise_running_interp();

	void thread_proc();

	void print_error();
	void dump_error();


	void report_limited_context_error(Unicode_String str);
};
inline Python python;



// @TODO: remove to speedup
// UPD: maybe leave, i don't think overhead is actually big.
template <typename... Types>
inline PyObject* call_python(PyObject* module, const char* function_name, Types... args)
{
	PyObject* function = PyDict_GetItemString(PyModule_GetDict(module), function_name);
	assert(function);

	return PyObject_CallFunctionObjArgs(function, args..., NULL);
}


