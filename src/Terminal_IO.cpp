#include "Terminal_IO.h"

#include "Terminal.h"
#include "Output_Processor.h"

#include "b_lib/Threading.h"

void Terminal_IO::init()
{
	data_to_write = make_array<char>(2048, c_allocator);

	can_write_data_semaphore = create_semaphore();
}

void Terminal_IO::create_pty_and_pipes()
{
#if OS_WINDOWS
 	SECURITY_ATTRIBUTES saAttr; 

	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
	saAttr.bInheritHandle = TRUE; 
	saAttr.lpSecurityDescriptor = NULL;


	if (!CreatePipe(&stdout_read_pipe, &stdout_write_pipe, &saAttr, 0))
		abort_the_mission(U"Failed to CreatePipe for stdout");

	if (!CreatePipe(&stdin_read_pipe, &stdin_write_pipe, &saAttr, 0))
		abort_the_mission(U"Failed to CreatePipe for stdin");

#if 0
	if (!SetHandleInformation(stdout_write_pipe, HANDLE_FLAG_INHERIT, 0))
		abort_the_mission(U"Failed to SetHandleInformation for stdout");

	if (!SetHandleInformation(stdin_read_pipe, HANDLE_FLAG_INHERIT, 0))
		abort_the_mission(U"Failed to SetHandleInformation for stdin");
#endif

#if USE_PTY
	if (CreatePseudoConsole({ (SHORT) terminal.terminal_width, (SHORT) terminal.terminal_height}, stdin_read_pipe, stdout_write_pipe, 0, &pty_handle) != S_OK)
	{
		abort_the_mission(U"CreatePseudoConsole have failed");
	}
#endif

#elif OS_LINUX

#if USE_PTY
	if (openpty(&pty_master, &pty_slave, NULL, NULL, NULL) != 0)
		abort_the_mission(U"Failed to openpty");

	{
		termios tio{};
		tcgetattr(pty_master, &tio);

		// Thanks Christian.
		tio.c_iflag |= IGNBRK;    // Ignore Break condition on input.
		tio.c_iflag |= IUTF8;
		tio.c_iflag &= ~IXON;     // Disable CTRL-S / CTRL-Q on output.
		tio.c_iflag &= ~IXOFF;    // Disable CTRL-S / CTRL-Q on input.
		tio.c_iflag &= ~ICRNL;    // Ensure CR isn't translated to NL.
		tio.c_iflag &= ~INLCR;    // Ensure NL isn't translated to CR.
		tio.c_iflag &= ~IGNCR;    // Ensure CR isn't ignored.
		tio.c_iflag &= ~IMAXBEL;  // Ensure beeping on full input buffer isn't enabled.
		tio.c_iflag &= ~ISTRIP;   // Ensure stripping of 8th bit on input isn't enabled.

		// output flags
		tio.c_oflag &= ~OPOST;   // Don't enable implementation defined output processing.
		tio.c_oflag &= ~ONLCR;   // Don't map NL to CR-NL.
		tio.c_oflag &= ~OCRNL;   // Don't map CR to NL.
		tio.c_oflag &= ~ONLRET;  // Don't output CR.

		// control flags

		// local flags
		tio.c_lflag &= ~IEXTEN;  // Don't enable implementation defined input processing.
		tio.c_lflag &= ~ICANON;  // Don't enable line buffering (Canonical mode).
		tio.c_lflag &=  ECHO;    // Don't echo input characters.
		tio.c_lflag &= ~ISIG;    // Don't generate signal upon receiving characters for
		                            // INTR, QUIT, SUSP, DSUSP.

		// special characters
		tio.c_cc[VMIN] = 1;   // Report as soon as 1 character is available.
		tio.c_cc[VTIME] = 0;  // Disable timeout (no need).

		tcsetattr(pty_master, TCSANOW, &tio);
	}

#else
    int stdout_pipes[2];
    if (pipe(stdout_pipes) != 0)
        abort_the_mission(U"Failed to to open pipes for stdout");

    stdout_read_pipe  = stdout_pipes[0];
    stdout_write_pipe = stdout_pipes[1];


    int stdin_pipes[2];
    if (pipe(stdin_pipes) != 0)
        abort_the_mission(U"Failed to open pipes for stdin");

    stdin_read_pipe  = stdin_pipes[0];
    stdin_write_pipe = stdin_pipes[1];
#endif
#endif
}

void Terminal_IO::update_pty_size()
{
	ZoneScoped;

#if OS_WINDOWS

#if USE_PTY

	if (pty_handle)
	{
		ResizePseudoConsole(pty_handle, { (SHORT) terminal.terminal_width, (SHORT) terminal.terminal_height });
	}

#endif

#elif OS_LINUX

	if (pty_master)
	{
		struct winsize size = {
			.ws_row = terminal.terminal_height,
			.ws_col = terminal.terminal_width,

			.ws_xpixel = terminal.terminal_width_pixels,
			.ws_ypixel = terminal.terminal_height_pixels,
		};

    	ioctl(pty_master, TIOCSWINSZ, &size);
	}

#endif
}

void Terminal_IO::start_io()
{
	create_pty_and_pipes();

	update_pty_size();

	auto reader_proc = [](void* dummy_ptr)
	{
		TRACY_THREAD_NAME("reader_thread");
		ctx.logger = typer_logger;

		terminal_io.reader_thread_proc();
	};
	reader_thread = create_thread(c_allocator, reader_proc);


	auto writer_proc = [](void* dummy_ptr)
	{
		TRACY_THREAD_NAME("writer_thread");
		ctx.logger = typer_logger;

		terminal_io.writer_thread_proc();
	};
	writer_thread = create_thread(c_allocator, writer_proc);
}

void Terminal_IO::finish_io()
{
	io_procs_can_exit = true;

	can_write_data_semaphore.increment(); // Wake up writer_thread

#if OS_WINDOWS
	CancelSynchronousIo(reader_thread.windows_handle); // Prevent possible lock at PeekNamedPipe

	// @TODO: check :CommitUserInput
	CancelSynchronousIo(writer_thread.windows_handle); // Prevent possible lock at FlushFileBuffers
#endif

	reader_thread.wait_for_finish();
	writer_thread.wait_for_finish();

	io_procs_can_exit = false;


	data_to_write.clear();


	// Close handles and pty.
	{
	#if OS_WINDOWS
		// close_handle_if_its_valid(stdin_write_pipe);
		close_handle_if_its_valid(stdin_read_pipe);
		close_handle_if_its_valid(stdout_write_pipe);
		//close_handle_if_its_valid(stdout_read_pipe);

	#if USE_PTY
		// @TODO: free attributes list
		ClosePseudoConsole(pty_handle);
		pty_handle = 0;
	#endif 

	#elif OS_LINUX

	#if USE_PTY
		ioctl(fileno(stdout), TIOCSCTTY);

		close(pty_master);
		close(pty_slave);

		pty_master = 0;
		pty_slave  = 0;

	#else

	    close(stdin_write_pipe);
		close(stdin_read_pipe);
		close(stdout_write_pipe);
		close(stdout_read_pipe);
	#endif

	#endif
	}
}


void Terminal_IO::reader_thread_proc()
{
	char read_buffer[4096];




	bool have_locked_buffer = false;

	auto maybe_lock_buffer = [&]()
	{
	#if LOCK_BUFFER_WHITE_READING
		if (!have_locked_buffer)
		{
			have_locked_buffer = true;
			terminal.characters_mutex.lock();
		}
	#endif
	};

	auto maybe_unlock_buffer = [&]()
	{
	#if LOCK_BUFFER_WHITE_READING
		if (have_locked_buffer)
		{
			have_locked_buffer = false;
			terminal.characters_mutex.unlock();
		}
	#endif
	};


	auto process_output = [&](char* read_buffer, size_t read_buffer_size)
	{
		assert(read_buffer_size);

		if (!enable_slow_output_processing)
		{
			output_processor.process_output(read_buffer, read_buffer_size);
		}
		else
		{
			char* ptr = &read_buffer[0];

			for (int i = 0; i < read_buffer_size; i++)
			{
				output_processor.process_output(ptr + i, 1);
				threading.sleep(slow_output_processing_sleep_time_ms);
			}
		}
	};


#if OS_WINDOWS

	while (true)
	{
		DWORD available_bytes_in_pipe = 0;

		// IIRC PeekNamedPipe is blocking operation so we have to call CancelSynchronousIo to cancel it.
		PeekNamedPipe(stdout_read_pipe, 0, 0, 0, &available_bytes_in_pipe, 0);

		if (!available_bytes_in_pipe) 
		{
			maybe_unlock_buffer();

			if (io_procs_can_exit)
				goto exit_reader_thread;

			// No data in pipe, so next function call(ReadFile) will block.
		}
		else
		{
			maybe_lock_buffer();
		}

		DWORD readed_count = 0; // Fuck grammar
		// Shouldn't block, because called only if data is available. .
		ReadFile(stdout_read_pipe, read_buffer, sizeof(read_buffer), &readed_count, NULL);

		if (readed_count)
		{
			process_output(read_buffer, readed_count);
		}
		else
		{
			// Shouldn've happened, but ok.
		}
	}



#elif OS_LINUX

	while (true)
	{
		// @TODO: fix CPU draining.

		// Is this neccessary ????
		int flags = fcntl(stdout_read_pipe, F_GETFL);
		fcntl(stdout_read_pipe, F_SETFL, flags | O_NONBLOCK);
		defer { fcntl(stdout_read_pipe, F_SETFL, flags); };


		fd_set read_set;
		FD_ZERO(&read_set);
		FD_SET(stdout_read_pipe, &read_set);

		timeval time_out = {};

		bool is_there_data_in_pipe = select(stdout_read_pipe + 1, &read_set, NULL, NULL, &time_out) == 1;

		if (is_there_data_in_pipe)
		{
			maybe_lock_buffer();

			int read_count = read(stdout_read_pipe, read_buffer, sizeof(read_buffer));
	
			if (read_count > 0)
			{
				process_output(read_buffer, read_count);
			}
		}
		else
		{
			maybe_unlock_buffer();

			if (io_procs_can_exit)
				goto exit_reader_thread;

			continue;
		}
	}		

#endif

exit_reader_thread:
	return;
}

void Terminal_IO::writer_thread_proc()
{
	while (true)
	{
		can_write_data_semaphore.wait_and_decrement();

		if (io_procs_can_exit)
		{
			return;
		}


		while (data_to_write.count)
		{
			assert(data_to_write.count > 0);
			char character_to_send;
			{
				Scoped_Lock lock(data_to_write_mutex);

				character_to_send = data_to_write.data[0];
			}

			defer {
				Scoped_Lock lock(data_to_write_mutex);

				data_to_write.remove_at_index(0); // @Performance: this moves all data in array to the left,
				//  which may be slow, but now i assume that there are not as much data in it.
			};

		#if OS_WINDOWS

			DWORD written_count;
			bool write_success = WriteFile(stdin_write_pipe, &character_to_send, 1, &written_count, NULL);
			
			if (!write_success || written_count) continue; // @SilentFail

			// :CommitUserInput
			FlushFileBuffers(stdin_write_pipe); // @TODO: Do we need this now???? We don't use line editing if python.is_running

		#elif OS_LINUX
			int written_count = write(stdin_write_pipe, &character_to_send, 1);

			if (written_count != 1) continue; // @SilentFail

			// :CommitUserInput
			fsync(stdin_write_pipe); // @TODO: check :CommitUserInput

		#endif
		}
	}
}

void Terminal_IO::write_string(String str)
{
	assert(threading.is_main_thread());
	
	Scoped_Lock lock(data_to_write_mutex);

	data_to_write.add_range(str.data, str.length);

	can_write_data_semaphore.increment();
}

void Terminal_IO::write_unicode_string(Unicode_String str)
{
	assert(threading.is_main_thread());

	String utf8_str = str.to_utf8_but_ascii(frame_allocator);

	write_string(utf8_str);
}

void Terminal_IO::write_char(char c)
{
	assert(threading.is_main_thread());

	write_string(String(&c, 1));
}

void Terminal_IO::write_char(char32_t c)
{
	assert(threading.is_main_thread());

	write_unicode_string(Unicode_String(&c, 1));
}