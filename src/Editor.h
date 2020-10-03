#pragma once

#include "Entity.h"

struct Editor
{
	bool is_open = false;

	void init();
	void do_frame();
};

inline Editor editor;