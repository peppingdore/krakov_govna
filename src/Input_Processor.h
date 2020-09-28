#pragma once

#include "Typer_UI.h"

struct Input_Processor
{
	void check_if_macro_bindings_triggered();
	void check_copy_paste_and_interrupt_bindings();

	void process_input_for_terminal_outside_process();
	void process_input_for_terminal_inside_process();

	void process_input();
};

inline Input_Processor input_processor;