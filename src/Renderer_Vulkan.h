#pragma once

#include "b_lib/String.h"
#include "b_lib/Reflection.h"
#include "b_lib/Math.h"
#include "b_lib/Color.h"
#include "b_lib/Font.h"
#include "b_lib/Array_Map.h"


#include "Renderer.h"


#include "Main.h"











struct Vulkan
{
	







	void make_sure_texture_is_on_gpu(Texture* texture);

	bool bind_pipeline(Pipeline* pipeline);



	void frame_begin();
	void frame_end();







	void init();






};

inline Vulkan vk;