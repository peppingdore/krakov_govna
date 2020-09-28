#include "Main.h"

#include "b_lib/Dynamic_Array.h"
#include "b_lib/File.h"
#include "b_lib/Threading.h"
#include "b_lib/Font.h"
#include "b_lib/Tokenizer.h"


#include "UI.h"

#include "Input.h"
#include "Key_Bindings.h"



#if OS_WINDOWS
#include <io.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Shell32.lib")



#include <Dbghelp.h>
#pragma comment(lib, "Dbghelp.lib")

#include <dwmapi.h>
#pragma comment(lib, "Dwmapi.lib")

#endif



#if IS_POSIX
#include <sys/time.h>
#include <sys/ioctl.h>
#include <cpuid.h>
#endif

#include <immintrin.h>



#include "Renderer_Vulkan.h"

#include "Ease_Functions.h"



void do_frame()
{
	defer { FrameMark; };

	defer{ frame_index += 1; };


	// Do frame time measurement
	{
		frame_allocator.reset();

		frame_time_us = time_measurer.us_elapsed();
		frame_time_ms = frame_time_us / 1000.0;
		frame_time    = frame_time_us / 1000'000.0;
		time_measurer.reset();

		uptime += frame_time;
	}

	// Do fps calculation
	{
		if (frame_index % fps_update_frames_count == 0)
		{
			frame_time_accum = 0;
			frame_time_accum_count = 0;
		}


		frame_time_accum += frame_time;
		frame_time_accum_count += 1;

		fps = 1 / (frame_time_accum / frame_time_accum_count);
	}


	desired_cursor_type = Cursor_Type::Normal;


 	input.pre_frame();
	defer { input.post_frame(); };
	
 	key_bindings.do_frame();


	if (key_bindings.is_action_type_triggered(Action_Type::Exit))
	{
    #if OS_WINDOWS
		PostQuitMessage(0);
    #elif OS_LINUX
        terminate();
        exit(0);
    #endif
		return;
	}


#if DEBUG
	if (input.is_key_down(Key::F12))
	{
		vulkan_memory_allocator.dump_allocations();
	}
#endif



	if (window_height == 0 || window_width == 0)
	{
		return;
	}




#if OS_WINDOWS
	windows.window_dpi = GetDpiForWindow(windows.hwnd);
	windows.window_scaling = float(windows.window_dpi) / 96.0;

    auto old_renderer_scaling = renderer.scaling;

	if (key_bindings.is_action_type_triggered(Action_Type::Toggle_Usage_Of_DPI))
	{
		renderer.use_dpi_scaling = !renderer.use_dpi_scaling;
	}

	renderer.scaling = renderer.use_dpi_scaling ? windows.window_scaling : 1.0;

	if (old_renderer_scaling != renderer.scaling)
	{
		window_size_changed = true;

		// Can't call SetWindowPos directly, cause it will lead to infinite loop of toggling DPI usage.
		have_to_inform_window_about_changes = true;
	}

#endif



	if (window_size_changed || renderer.should_resize())
	{
		renderer.resize(window_width, window_height);
		window_size_changed = false;
	}

	ui.pre_frame();
	defer { ui.post_frame(); };


	renderer.frame_begin();
	defer { renderer.frame_end(); } ;
}

void terminate()
{

}




#if OS_WINDOWS
LONG WINAPI exception_handler(_EXCEPTION_POINTERS* exc_info)
{
	typedef BOOL(WINAPI* MINIDUMPWRITEDUMP)(HANDLE hProcess, DWORD dwPid, HANDLE hFile, MINIDUMP_TYPE DumpType, CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam, CONST PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam, CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam);


	HMODULE mhLib = ::LoadLibraryW(L"dbghelp.dll");
	MINIDUMPWRITEDUMP pDump = (MINIDUMPWRITEDUMP)::GetProcAddress(mhLib, "MiniDumpWriteDump");

	SYSTEMTIME system_time;
	GetLocalTime(&system_time);

	Unicode_String crash_dumps_folder = path_concat(c_allocator, executable_directory, Unicode_String(U"crash_dumps"));

	create_directory_recursively(crash_dumps_folder, c_allocator);

	Unicode_String str = format_unicode_string(c_allocator, path_concat(c_allocator, crash_dumps_folder, Unicode_String(U"dump%.%.%-%.%.%.dmp")), system_time.wDay, system_time.wMonth, system_time.wYear, system_time.wHour, system_time.wMinute, system_time.wSecond);

	wchar_t* wstr = str.to_wide_string(c_allocator);


	HANDLE  hFile = ::CreateFileW(wstr, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);


	_MINIDUMP_EXCEPTION_INFORMATION ExInfo;
	ExInfo.ThreadId = ::GetCurrentThreadId();
	ExInfo.ExceptionPointers = exc_info;
	ExInfo.ClientPointers = FALSE;

	MINIDUMP_TYPE dump_type = MiniDumpNormal;
	if (settings.full_crash_dump)
	{
		dump_type = (MINIDUMP_TYPE) (dump_type | MiniDumpWithDataSegs | MiniDumpWithFullMemory | MiniDumpWithHandleData | MiniDumpWithCodeSegs);
	}

	pDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, dump_type, &ExInfo, NULL, NULL);
	::CloseHandle(hFile);

	return EXCEPTION_EXECUTE_HANDLER;
}
#endif


int main()
{
	// setsid(); // To be able to set controlling terminal

	mission_name = U"Zazhopinsk";

	// Check whether CPU supports AVX.
	// But what if compiler uses AVX to check for AVX....????
	{
		// Stolen from: https://gist.github.com/hi2p-perim/7855506
    #if OS_WINDOWS
		int cpu_info[4];
		__cpuid(cpu_info, 1);
    #elif IS_POSIX
		u32 cpu_info[4];
        __get_cpuid(1, &cpu_info[0], &cpu_info[1], &cpu_info[2], &cpu_info[3]);
    #endif

		bool avxSupportted = cpu_info[2] & (1 << 28);
		bool osxsaveSupported = cpu_info[2] & (1 << 27);
		if (osxsaveSupported && avxSupportted)
		{

			auto xgetbv = [&](int num)
			{
			#if OS_WINDOWS

				return _xgetbv(num);
			#else
				uint32_t a, d;
				__asm("xgetbv" : "=a"(a),"=d"(d) : "c"(num) : );

	   			return a | (uint64_t(d) << 32);
	   		#endif
			};

   			u64 xcrFeatureMask = xgetbv(0);
			avxSupportted = (xcrFeatureMask & 0x6) == 0x6;
		}

		if (!avxSupportted)
		{
			abort_the_mission(U"Your CPU doesn't support AVX instruction set, which is required to run %", mission_name);
			return 1;
		}
	}

#if OS_WINDOWS
	// Set dump writer
	{
		SetUnhandledExceptionFilter(exception_handler);
		// AddVectoredExceptionHandler(1, exception_handler);
	}
#endif

	// Disanble stdio buffering
	{
		setvbuf(stdout, NULL, _IONBF, 0);
		setvbuf(stderr, NULL, _IONBF, 0);
	}


#if !DEBUG
	Unicode_String window_title = U"Zazhopinsk";
#else
	Unicode_String window_title = U"Zazhopinsk (DEBUG)";
#endif





#if OS_WINDOWS
	{
		ZoneScopedN("SetThreadDpiAwarenessContext");
		SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	}
#endif


	{
		ZoneScopedN("Open log file");


		create_directory_recursively(U"logs", c_allocator);

#if OS_WINDOWS
		SYSTEMTIME system_time;
		GetLocalTime(&system_time);
		Unicode_String str = format_unicode_string(c_allocator, U"logs/zazhopinsk_last_log_%.%.%-%.%.%.txt", system_time.wDay, system_time.wMonth, system_time.wYear, system_time.wHour, system_time.wMinute, system_time.wSecond);
#elif IS_POSIX
        time_t t = time(NULL);
        struct tm* local_time = localtime(&t);

		Unicode_String str = format_unicode_string(c_allocator, U"logs/zazhopinsk_last_log_%.%.%-%.%.%.txt", local_time->tm_mday, local_time->tm_mon, local_time->tm_year, local_time->tm_hour, local_time->tm_min, local_time->tm_sec);
#endif

		defer{ c_allocator.free(str.data, code_location()); };

		log_file = open_file(c_allocator, str, File_Open_Mode(FILE_WRITE | FILE_CREATE_NEW));
		if (!log_file.succeeded_to_open())
		{
			log(ctx.logger, U"Failed to open log file with name: %", str);
		}

		ctx.logger.concat_allocator = c_allocator;
		ctx.logger.log_proc = [](Unicode_String str)
		{
			ZoneScoped;

			Scoped_Lock logger_lock(main_logger_mutex);

			int utf8_length;
			char* utf8_str = str.to_utf8(c_allocator, &utf8_length);
			fwrite(utf8_str, 1, utf8_length, stdout);
			fwrite("\n", 1, 1, stdout);


			if (log_file.succeeded_to_open())
			{
				log_file.write((u8*)utf8_str, utf8_length);
				log_file.write((u8*)"\n", 1);
				
			#if OS_WINDOWS
				log_file.flush();
			#endif
			}

			c_allocator.free(utf8_str, code_location());
		};

		main_logger = ctx.logger;
	}


    // Code below starts using frame_allocator
	{
		ZoneScopedN("Allocate frame_allocator");

		create_arena_allocator(&frame_allocator, c_allocator, 12 * 1024 * 1024);
#ifdef ALLOCATOR_NAMES
		frame_allocator.name = "frame_allocator";
#endif

#if DEBUG
		frame_allocator.owning_thread = threading.current_thread_id();
#endif
	}


	TracyCZoneN(create_window_zone, "Create window", true);


#if OS_WINDOWS
	HINSTANCE hInstance = GetModuleHandle(NULL);

	const wchar_t CLASS_NAME[] = L"Typer";

	WNDCLASS wc = { };

	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;

	wc.lpfnWndProc = [](HWND h, UINT m, WPARAM wParam, LPARAM lParam) -> LRESULT
	{
		LRESULT dwm_proc_result = 0;

		switch (m)
		{
			case WM_DPICHANGED:
			{
				window_size_changed = true;
			}
			break;

			case WM_CREATE:
			{
				is_inside_window_creation = true;

				// Inform application of the frame change.
				SetWindowPos(h,
					NULL,
					0, 0,
					0, 0,
					SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);
			}
			break;


			case WM_DESTROY:
			case WM_CLOSE:
			{
				PostQuitMessage(0);
			}
			break;


			case WM_WINDOWPOSCHANGED:
			{
				WINDOWPOS* window_pos = (WINDOWPOS*)lParam;

				RECT new_client_rect;
				GetClientRect(h, &new_client_rect);

				int new_width  = new_client_rect.right  - new_client_rect.left;
				int new_height = new_client_rect.bottom - new_client_rect.top;

				handle_os_resize_event(new_width, new_height);
			}
			break;

			case WM_GETMINMAXINFO:
			{
				LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;


				float scaling = float(GetDpiForWindow(windows.hwnd)) / 96.0;

				lpMMI->ptMinTrackSize.x = scale(window_min_width, scaling);
				lpMMI->ptMinTrackSize.y = scale(window_min_height, scaling);
			}
			break;

			case WM_CHAR:
			{
				if (wParam < 32) return true;

				switch (wParam)
				{
				case VK_BACK:
				case VK_TAB:
				case VK_RETURN:
				case VK_ESCAPE:
				case 127: // Ctrl + backspace
				{
					return true;
				}
				}

				static WPARAM high_surrogate = 0;
				if (IS_HIGH_SURROGATE(wParam))
				{
					high_surrogate = wParam;
					return 0;
				}

				u64 c = (u64)wParam;

				char32_t utf32_c;
				{
					if (high_surrogate && c >= 0xDC00)
					{
						u16 lower = (u16)(c);
						u16 higher = (u16)(high_surrogate);

						utf32_c = (higher - 0xD800) << 10;

						utf32_c |= ((char32_t)lower) - 0xDC00;

						utf32_c += 0x10000;
					}
					else
					{
						utf32_c = (char32_t)c;
					}
				}

				Input_Node node;
				node.input_type = Input_Type::Char;
				node.character = utf32_c;

				input.nodes.add(node);

				high_surrogate = 0;
			}
			break;

			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
			{
				Input_Node node;
				node.input_type = Input_Type::Key;
				node.key_action = Key_Action::Down;
				node.key = map_windows_key(wParam);
				node.key_code = wParam;

				input.nodes.add(node);
			}
			break;

			case WM_KEYUP:
			case WM_SYSKEYUP:
			{
				Input_Node node;
				node.input_type = Input_Type::Key;
				node.key_action = Key_Action::Up;
				node.key = map_windows_key(wParam);
				node.key_code = wParam;

				input.nodes.add(node);
			}
			break;

			case WM_MOUSEWHEEL:
			{
				input.mouse_wheel_delta = -GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
			}
			break;



			case WM_ACTIVATE:
			{
				MARGINS margins = { 0, 0, 0, 0 };
				DwmExtendFrameIntoClientArea(h, &margins);

				DWORD huy = DWMNCRP_ENABLED;
				DwmSetWindowAttribute(h, DWMWA_NCRENDERING_POLICY, &huy, sizeof(DWORD));

				has_window_focus = LOWORD(wParam) ? true : false;
			}
			break;

			case WM_MOUSEMOVE:
			{
				input.mouse_x = GET_X_LPARAM(lParam);
				input.mouse_y = renderer.height - GET_Y_LPARAM(lParam);
			}
			break;

			case WM_SETCURSOR:
			{
				if (LOWORD(lParam) != HTCLIENT)
				{
					return DefWindowProc(h, m, wParam, lParam);
				}


				handle_wm_setcursor(desired_cursor_type);
			}
			break;

			case WM_LBUTTONDOWN:
			{
				Input_Node node;
				node.input_type = Input_Type::Key;
				node.key_action = Key_Action::Down;
				node.key = Key::LMB;
				node.key_code = VK_LBUTTON;

				input.nodes.add(node);
			}
			break;

			case WM_RBUTTONDOWN:
			{
				Input_Node node;
				node.input_type = Input_Type::Key;
				node.key_action = Key_Action::Down;
				node.key = Key::RMB;
				node.key_code = VK_RBUTTON;

				input.nodes.add(node);
			}
			break;
			case WM_MBUTTONDOWN:
			{
				Input_Node node;
				node.input_type = Input_Type::Key;
				node.key_action = Key_Action::Down;
				node.key = Key::MMB;
				node.key_code = VK_MBUTTON;

				input.nodes.add(node);
			}
			break;

			case WM_NCLBUTTONUP:
			case WM_LBUTTONUP:
			{
				Input_Node node;
				node.input_type = Input_Type::Key;
				node.key_action = Key_Action::Up;
				node.key = Key::LMB;
				node.key_code = VK_LBUTTON;

				input.nodes.add(node);
			}
			break;

			case WM_NCRBUTTONUP:
			case WM_RBUTTONUP:
			{
				Input_Node node;
				node.input_type = Input_Type::Key;
				node.key_action = Key_Action::Up;
				node.key = Key::RMB;
				node.key_code = VK_RBUTTON;

				input.nodes.add(node);
			}
			break;

			case WM_NCMBUTTONUP:
			case WM_MBUTTONUP:
			{
				Input_Node node;
				node.input_type = Input_Type::Key;
				node.key_action = Key_Action::Up;
				node.key = Key::MMB;
				node.key_code = VK_MBUTTON;

				input.nodes.add(node);
			}
			break;

			default:
			{
				return DefWindowProc(h, m, wParam, lParam);
			}
		}

		return false;
	};

	RegisterClass(&wc);


	int window_x = GetSystemMetrics(SM_CXSCREEN) / 2 - window_width / 2;
	int window_y = GetSystemMetrics(SM_CYSCREEN) / 2 - window_height / 2;

	RECT window_size = {
		window_x,
		window_y,
		window_x + window_width,
		window_y + window_height
	};


	DWORD window_style = WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;

	AdjustWindowRect(&window_size, window_style, false);

	wchar_t* utf16_window_title = window_title.to_wide_string(frame_allocator);

	HWND hwnd = CreateWindowEx(
		WS_EX_APPWINDOW,                              // Optional windowCreateWistyles.
		CLASS_NAME,                     // Window class
		utf16_window_title,                       // Window text
		window_style,            // Window style

		// Size and position
		window_size.left,
		window_size.top,
		window_size.right - window_size.left,
		window_size.bottom - window_size.top,

		NULL,       // Parent window    
		NULL,       // Menu
		hInstance,  // Instance handle
		NULL        // Additional application data
	);

	if (hwnd == NULL)
	{
		return 0;
	}


	windows.dc = GetDC(hwnd);
	windows.hwnd = hwnd;

	windows.hinstance = hInstance;

	windows.window_dpi = GetDpiForWindow(hwnd);
	windows.window_scaling = float(windows.window_dpi) / 96.0;

	renderer.scaling = windows.window_scaling;

	log(ctx.logger, U"Initial window pixel scaling factor: %", windows.window_scaling);

    #endif



	TracyCZoneEnd(create_window_zone);


	{
		Time_Measurer tm = create_time_measurer();
	
		Reflection::allow_runtime_type_generation = true;
		Reflection::init();
		log(ctx.logger, U"Reflection::init() took % ms", tm.ms_elapsed_double());
	}


	
#if OS_WINDOWS
	RECT client_rect;
	GetClientRect(windows.hwnd, &client_rect);

	renderer.init(client_rect.right - client_rect.left, client_rect.bottom - client_rect.top);
#endif


	time_measurer = create_time_measurer();

	executable_directory = get_executable_directory(c_allocator);


	// Load assets
	{

		font_storage.init(c_allocator, make_array<Unicode_String>(c_allocator, {
			path_concat(c_allocator, executable_directory, Unicode_String(U"fonts"))
		}));



		auto textures_folder = path_concat(frame_allocator, executable_directory, Unicode_String(U"assets/textures"));

		auto iter = iterate_files(textures_folder, frame_allocator);

		if (iter.succeeded_to_open())
		{
			while (iter.next().is_not_empty())
			{
				ZoneScopedN("load texture");

				if (iter.current.ends_with(U".png"))
				{
					Unicode_String file_path = path_concat(frame_allocator, textures_folder, iter.current);
					bool result = renderer.load_texture(file_path);
					if (!result)
					{
						Log(U"Failed to load texture at path: %", file_path);
					}
				}
			}
		}		
	}


	load_settings();

	key_bindings.init();


	input.init();

	ui.init();





#if OS_WINDOWS
	{
		ZoneScopedN("ShowWindow");
		// Windows will call calbacks and crash the program cause nothing is initialized if we call this earlier.
		ShowWindow(hwnd, SW_SHOWDEFAULT);
		SetForegroundWindow(hwnd);
	}
#endif


	frame_allocator.reset();


	// Main loop

#if OS_WINDOWS
	MSG msg;
	while (1)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			ZoneScopedN("Windows message");

			{
				ZoneScopedN("TranslateMessage");
				TranslateMessage(&msg);
			}

			{
#ifdef TRACY_ENABLE
				ZoneScopedN("DispatchMessage");
				String str = to_string(msg.message, c_allocator);
				
				ZoneText(str.data, str.length);

				c_allocator.free(str.data, code_location());
#endif

				DispatchMessage(&msg);
			}

			switch (msg.message)
			{
				case WM_QUIT:
				{
                    terminate();
                    
					fflush(stdout);
					fclose(stdout);

					return 0;
				}
			}
		}
		else
		{
			if (is_inside_window_creation)
				is_inside_window_creation = false;

			if (have_to_inform_window_about_changes)
			{
				SetWindowPos(windows.hwnd, 
                     NULL, 
                     0, 0,
                     0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);


				have_to_inform_window_about_changes = false;
			}

			do_frame();
		}
	}
#endif
}

inline void handle_os_resize_event(int new_width, int new_height)
{
	if (window_width != new_width ||
		window_height != new_height)
	{
		window_width = new_width;
		window_height = new_height;

	#if 0
		if (IsMaximized(h))
		{
			// window_width  -= 8;
			window_height -= 8;
		}
	#endif
		window_size_changed = true;
	}

#if OS_WINDOWS
	if (!is_inside_window_creation)
	{
		do_frame();
	}
#endif
}

