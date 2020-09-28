#pragma once

#include "b_lib/Dynamic_Array.h"
#include "b_lib/Arena_Allocator.h"
#include "b_lib/Log.h"
#include "b_lib/Context.h"

enum class Sequence_Type
{
	None,

	Typer_Private,
	CSI,
	OSC,
	ESC,
};

struct Output_Processor
{
	Arena_Allocator print_output_arena;

	char32_t utf8_to_utf32_accumulator = 0;
	int utf_code_units_to_read = 0;

	char32_t process_last_character = '\0';


	Dynamic_Array<char32_t> escape_sequence_payload;

	Sequence_Type sequence_type;
	inline bool in_escape_sequence()
	{
		return sequence_type != Sequence_Type::None;
	}


	bool is_output_interactive = false;
	u32  process_output_interactive_id = 0;

	int  process_caret_before_return_caret = 0; // Set by '\r' processor

	void init();


	void process_output(char* buffer, int buffer_length);
	
	void process_typer_sequence(String payload);
	void process_csi_sequence(String payload);
	void process_osc_sequence(String payload);
	void process_esc_sequence(String payload);

	void process_sgr(String payload);

	inline void received_unknown_escape_sequence(String payload)
	{
		Log(U"Unknown escape sequence. Payload: %", payload);
	};

	void clean();
};

inline Output_Processor output_processor;