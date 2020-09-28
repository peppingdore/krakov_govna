#pragma once

#include "Tracy_Header.h"


#include "b_lib/Reflection.h"


#include "Renderer.h"
#include "Key_Bindings.h"

enum class Key: u32;


struct Settings
{
	bool full_crash_dump = false;
	bool show_fps = false;
};
REFLECT(Settings)
	MEMBER(full_crash_dump);
	MEMBER(show_fps);
REFLECT_END();

inline Settings settings;


bool save_settings();
bool load_settings();


