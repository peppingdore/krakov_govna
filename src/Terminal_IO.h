#pragma once

#include "b_lib/Basic.h"

#define LOCK_BUFFER_WHITE_READING 0
#define USE_PTY 1



#if USE_PTY

#if OS_LINUX
#include <pty.h>
#endif

#if OS_DARWIN
#include <util.h>
#endif

#if OS_WINDOWS
#include <Windows.h>
#include <ConsoleApi.h>
#endif

#endif



#include "Main.h"

struct Terminal_IO
{
#if 0
	Mutex               data_to_process_mutex;
	Dynamic_Array<char> data_to_process;
#endif

	Mutex               data_to_write_mutex;
	Dynamic_Array<char> data_to_write;
	Semaphore           can_write_data_semaphore;


#if OS_WINDOWS

#if USE_PTY

	HPCON pty_handle = NULL;

	PPROC_THREAD_ATTRIBUTE_LIST pty_attr_list;

#endif

	HANDLE stdout_read_pipe;
	HANDLE stdout_write_pipe;
	HANDLE stdin_read_pipe;
	HANDLE stdin_write_pipe;


#elif IS_POSIX

#if USE_PTY
	union
	{
	    int stdout_write_pipe;
	    int stdin_read_pipe;
		// int pty_master;
		int pty_slave = 0;
	};

	union
	{
	    int stdin_write_pipe;
	    int stdout_read_pipe;
		// int pty_slave;
		int pty_master = 0;
	};
#else
    int stdout_read_pipe;
    int stdout_write_pipe;
    int stdin_read_pipe;
    int stdin_write_pipe;
#endif
#endif


    Thread reader_thread;
    Thread writer_thread;

    bool io_procs_can_exit = false;


	void update_pty_size();

	void init();

	void create_pty_and_pipes();

	void start_io();
	void finish_io();

	void reader_thread_proc();
	void writer_thread_proc();


	void write_string(String str);
	void write_unicode_string(Unicode_String str);
	void write_char(char c);
	void write_char(char32_t c);
};

inline Terminal_IO terminal_io;