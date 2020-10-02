#pragma once

#include "b_lib/Basic.h"
#include "b_lib/Font.h"
#include "b_lib/Math.h"
#include "b_lib/Color.h"

#include "Level.h"


// Rendering API definitions should be here.
#define RENDERER_VK 1


#if RENDERER_VK
#if OS_WINDOWS
#define VK_USE_PLATFORM_WIN32_KHR
#elif OS_LINUX
#define VK_USE_PLATFORM_XLIB_KHR
#endif

#include "vulkan/vulkan.h"

#include "Vulkan_Memory_Allocator.h"

#if OS_WINDOWS
#pragma comment(lib, "vulkan-1.lib")
#endif



REFLECT(VkPhysicalDeviceType)
	ENUM_VALUE(VK_PHYSICAL_DEVICE_TYPE_OTHER);
	ENUM_VALUE(VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
	ENUM_VALUE(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
	ENUM_VALUE(VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU);
	ENUM_VALUE(VK_PHYSICAL_DEVICE_TYPE_CPU);
REFLECT_END();

REFLECT(VkQueueFlagBits)
    ENUM_VALUE(VK_QUEUE_GRAPHICS_BIT);
    ENUM_VALUE(VK_QUEUE_COMPUTE_BIT);
    ENUM_VALUE(VK_QUEUE_TRANSFER_BIT);
    ENUM_VALUE(VK_QUEUE_SPARSE_BINDING_BIT);
    ENUM_VALUE(VK_QUEUE_PROTECTED_BIT);
REFLECT_END();

REFLECT(VkSampleCountFlagBits)
	ENUM_FLAGS(true);

	ENUM_VALUE(VK_SAMPLE_COUNT_1_BIT);
	ENUM_VALUE(VK_SAMPLE_COUNT_2_BIT);
	ENUM_VALUE(VK_SAMPLE_COUNT_4_BIT);
	ENUM_VALUE(VK_SAMPLE_COUNT_8_BIT);
	ENUM_VALUE(VK_SAMPLE_COUNT_16_BIT);
	ENUM_VALUE(VK_SAMPLE_COUNT_32_BIT);
	ENUM_VALUE(VK_SAMPLE_COUNT_64_BIT);
REFLECT_END();

REFLECT(VkSystemAllocationScope)
    ENUM_VALUE(VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
    ENUM_VALUE(VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
REFLECT_END();

#endif

constexpr int default_fade_width = 36;

constexpr u32 RECT_OUTLINE_LEFT_EDGE   = 1;
constexpr u32 RECT_OUTLINE_RIGHT_EDGE  = 1 << 1;
constexpr u32 RECT_OUTLINE_BOTTOM_EDGE = 1 << 2;
constexpr u32 RECT_OUTLINE_TOP_EDGE    = 1 << 3;
constexpr u32 RECT_ALL_OUTLINE_EDGES = RECT_OUTLINE_LEFT_EDGE | RECT_OUTLINE_RIGHT_EDGE | RECT_OUTLINE_TOP_EDGE | RECT_OUTLINE_BOTTOM_EDGE;






struct Vertex
{
	Vector3 position;
	Vector3 normal;
	Vector2 uv;
};


struct Mesh
{
	Unicode_String name;

	Dynamic_Array<Vertex>  vertices;
	Dynamic_Array<u32>     indices;

	VkBuffer       vertex_buffer;
	VkBuffer       index_buffer;

	Vulkan_Memory_Allocation vertex_buffer_memory;
	Vulkan_Memory_Allocation index_buffer_memory;

	void allocate_on_gpu();
};

enum class Texture_Format
{
	Monochrome,
	RGB,
	RGBA,
};

struct Texture
{
	Unicode_String name;

	void* image_buffer;
	int width;
	int height;

	u64 size;

	Texture_Format format;

	bool is_on_gpu = false;

	VkImage      image;
	VkImageView  image_view;
	VkSampler    image_sampler;
	Vulkan_Memory_Allocation image_memory;
};

struct Material
{
	Unicode_String name;


};

struct Shader
{
	Unicode_String name;


};



// Currently 1 channel only.
struct Texture_Atlas
{
#if RENDERER_VK
	VkImage      image;
	VkImageView  image_view;
	VkSampler    image_sampler;
	Vulkan_Memory_Allocation image_memory;
#endif

	int size;

	Dynamic_Array<Rect> free_rects;

	static Texture_Atlas make(int size);

	bool put_in(void* image_buffer, int image_width, int image_height, int* out_uv_x_left, int* out_uv_y_bottom);
};



struct Glyph_Key
{
	u8* image_buffer; // Image buffer should be unique.
	int glyph_index;


	static inline Glyph_Key make(const Glyph& glyph)
	{
		return {
			.image_buffer = glyph.image_buffer,
			.glyph_index = glyph.freetype_glyph_index,
		};
	}

	bool operator!=(const Glyph_Key other) const
	{
		return image_buffer != other.image_buffer;
	}

	bool operator==(const Glyph_Key other) const
	{
		return image_buffer == other.image_buffer;
	}
};


struct Texture_Atlas;

struct Glyph_Gpu_Region
{
	Texture_Atlas* atlas;
	Vector2i       offset;
};


struct Rect_Mask
{
	Rect rect;
	bool inversed;
};


enum class Imm_Command_Type
{
	Unknown,

	Recalculate_Mask_Buffer,

	Draw_Texture,

	Draw_Rect,
	Draw_Faded_Rect,

	Draw_Line,

	Draw_Glyph,
};

struct Imm_Command
{
	Imm_Command_Type type;

#if DEBUG
	bool is_executed = false;
#endif

	union
	{
		struct
		{
			Dynamic_Array<Rect_Mask> mask_stack;
		} recalculate_mask_buffer;


		struct
		{
			Rect rect;
			rgba color;
		} draw_rect;

		struct
		{
			Rect rect;
			Unicode_String texture_name;
		} draw_texture;

		struct
		{
			Rect rect;
			rgba color;
			int alpha_left;
			int alpha_right;
		} draw_faded_rect;


		struct
		{
			int x0;
			int y0;
			int x1;
			int y1;

			rgba color;
		} draw_line;


		struct
		{
			Glyph glyph;
			int x;
			int y;
			rgba color;

			Glyph_Gpu_Region glyph_gpu_region;
		} draw_glyph;
	};
};



#if RENDERER_VK
constexpr int VULKAN_MAX_PUSH_CONSTANT_SIZE = 128;


struct Line_Uniform_Block {
	alignas(8) Vector2i screen_size;
	alignas(8) Vector2i line_start;
	alignas(8) Vector2i line_end;
	alignas(16) Vector4 color;
};
static_assert(sizeof(Line_Uniform_Block) <= VULKAN_MAX_PUSH_CONSTANT_SIZE);

struct Mask_Uniform_Block
{
	alignas(8)  Vector2i screen_size;
	alignas(16) Vector4i rect;
};
static_assert(sizeof(Mask_Uniform_Block) <= VULKAN_MAX_PUSH_CONSTANT_SIZE);


struct Texture_Uniform_Block
{
	alignas(8)  Vector2i screen_size;
	alignas(16) Vector4i rect;
};
static_assert(sizeof(Texture_Uniform_Block) <= VULKAN_MAX_PUSH_CONSTANT_SIZE);

const int DRAW_TYPE_RECT       = 0;
const int DRAW_TYPE_GLYPH      = 1;
const int DRAW_TYPE_FADED_RECT = 2;

const int max_batched_commands = 512; // Keep in sync with C++


struct Batched_Draw_Command_Block
{
	alignas(8)  Vector2i screen_size;
	alignas(16) Vector4i rect;
	alignas(4)  float faded_rect_left_alpha;
	alignas(4)  float faded_rect_right_alpha;
	alignas(4)  int draw_type;
	alignas(4)  float atlas_x_left;
	alignas(4)  float atlas_x_right;
	alignas(4)  float atlas_y_bottom;
	alignas(4)  float atlas_y_top;
	alignas(16) Vector4 color;
};
#endif


struct Renderer
{
#if RENDERER_VK
	bool enable_validation_layers = false;

	const char* validation_layers[2] = {
		"VK_LAYER_LUNARG_standard_validation",
		"VK_LAYER_KHRONOS_validation",
		// "VK_LAYER_LUNARG_api_dump"
	};

#endif



#if RENDERER_VK

	struct
	{
		Mesh quad;
	} primitives;


	struct
	{
		VkImage      image;
		VkImageView  image_view;
		VkSampler    image_sampler;
		Vulkan_Memory_Allocation image_memory;
	} white_texture;







	VkSampleCountFlagBits  msaa_samples_count = VK_SAMPLE_COUNT_1_BIT;

	bool swapchain_is_dead = false;


	VkInstance       instance;
	VkPhysicalDevice physical_device;
	VkDevice         device;
	VkQueue          device_queue;

	u32 queue_family_index;


	VkSurfaceKHR surface;

	VkCommandPool    command_pool;








	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VkFormat       swapchain_image_format;

	struct Swapchain_Node
	{
		u32 index;

		VkImage         image;
		//VkImageView     image_view;
	};


	Dynamic_Array<Swapchain_Node> swapchain_nodes;
	Swapchain_Node* current_swapchain_node;
	
	VkFence rendering_done_fence;
	VkFence image_availability_fence;

	struct
	{
		VkRenderPass main_render_pass;
		VkFramebuffer main_framebuffer;
	
		VkFormat main_color_image_format;
		VkFormat main_depth_stencil_image_format;



		Vulkan_Memory_Allocation main_color_image_memory;
		Vulkan_Memory_Allocation main_depth_stencil_image_memory;

		VkImage main_color_image;
		VkImage main_depth_stencil_image;

		VkImageView main_color_image_view;
		VkImageView main_depth_stencil_image_view;
	
		VkCommandBuffer main_command_buffer;
	};


	VkDebugUtilsMessengerEXT debug_messenger;


	void create_vk_instance();
	void find_suitable_gpu_and_create_device_and_queue();

	void create_swapchain();
	void create_main_framebuffer();

	void create_primitives();

	void recreate_swapchain();

	void create_white_texture();


#endif



	// For usage from outside, framebuffer size shrinked by margin.
	int height;
	int width;

	
	void resize(int new_width, int new_height);

	void init(int initial_width, int initial_height);

	bool should_resize();

	void frame_begin();
	void frame_end();


	void draw_level(Level* level);


	void make_sure_texture_is_on_gpu(Texture* texture);



	// Scaling is neccessary for UI.

	float scaling = 1.0;
	template <typename T>
	inline T scaled(T number)
	{
		return scale<T>(number, scaling);		
	}




	Array_Map<Glyph_Key, Glyph_Gpu_Region> glyph_gpu_map;
	Bucket_Array<Texture_Atlas> glyph_atlasses;


	// Immediate mode stuff.

#if RENDERER_VK
	void imm_load_shaders();
	void imm_create_pipelines();
#endif


	Dynamic_Array<Imm_Command> imm_commands;
	void imm_execute_commands();

	Dynamic_Array<Rect_Mask> imm_mask_stack;


	void imm_recalculate_mask_buffer();

	void imm_push_mask(Rect_Mask mask)
	{
		imm_mask_stack.add(mask);
		imm_recalculate_mask_buffer();
	}
	void imm_pop_mask()
	{
		assert(imm_mask_stack.count > 0);
		imm_mask_stack.count -= 1;
		imm_recalculate_mask_buffer();
	}	


	void imm_draw_texture(Rect rect, Texture* texture);

	void imm_draw_rect(Rect rect, rgba rgba);
	void imm_draw_rect_with_alpha_fade(Rect rect, rgba rgba, int alpha_left, int alpha_right);

	void imm_draw_line(int x_start, int y_start, int x_end, int y_end, rgba color);

	inline void imm_draw_rect_outline(Rect rect, rgba color, u32 edges = RECT_ALL_OUTLINE_EDGES)
	{
		if (edges & RECT_OUTLINE_TOP_EDGE)
			imm_draw_line(rect.x_left, rect.y_top,    rect.x_right, rect.y_top,    color);
		
		if (edges & RECT_OUTLINE_BOTTOM_EDGE)
			imm_draw_line(rect.x_left, rect.y_bottom - 1, rect.x_right, rect.y_bottom - 1, color);


		if (edges & RECT_OUTLINE_LEFT_EDGE)
			imm_draw_line(rect.x_left,  rect.y_bottom, rect.x_left,  rect.y_top, color);

		if (edges & RECT_OUTLINE_RIGHT_EDGE)
			imm_draw_line(rect.x_right + 1, rect.y_bottom, rect.x_right + 1, rect.y_top, color);
	}



	void imm_draw_glyph(Glyph* glyph, int x, int y, rgba color);

	void imm_draw_text(Font::Face* face, Unicode_String str, int x, int y, rgba color = rgba(255, 255, 255, 255));
	void imm_draw_text_culled(Font::Face* face, Unicode_String str, int x, int y, Rect cull_rect, rgba color = rgba(255, 255, 255, 255));


#if RENDERER_VK

	struct Imm_Pipeline_Options
	{
		VkShaderModule vertex_shader;
		VkShaderModule fragment_shader;

		VkPipelineColorBlendAttachmentState*    blending_state = NULL;
		VkPipelineDepthStencilStateCreateInfo*  depth_stencil_state = NULL;
		VkPipelineInputAssemblyStateCreateInfo* input_assembly_state = NULL;

		VkDescriptorSetLayoutBinding* descriptor_set_layout_bindings = NULL;
		int                           descriptor_set_layout_bindings_count;

		int push_constant_size = 0;

		bool no_vertex_buffer = false;
	};

	struct Imm_Pipeline
	{
		VkDescriptorSetLayout descriptor_set_layout;
		VkPipelineLayout      pipeline_layout;
		VkPipeline            pipeline;
	};

	Imm_Pipeline imm_create_pipeline(Imm_Pipeline_Options options);


	struct 
	{
		VkShaderModule glyph_rect_vertex;
		VkShaderModule glyph_rect_fragment;
	
		VkShaderModule mask_vertex;
		VkShaderModule mask_fragment;

		VkShaderModule line_vertex;
		VkShaderModule line_fragment;

		VkShaderModule texture_vertex;
		VkShaderModule texture_fragment;
	} imm_shaders;

	struct Imm_Uniform_Buffer 
	{
		VkBuffer buffer;
		Vulkan_Memory_Allocation memory;
	};

	Dynamic_Array<Imm_Uniform_Buffer> imm_used_uniform_buffer_memory_allocations;



	Imm_Pipeline imm_general_pipeline;
	Imm_Pipeline imm_inversed_mask_pipeline;
	Imm_Pipeline imm_clear_mask_pipeline;
	Imm_Pipeline imm_line_pipeline;
	Imm_Pipeline imm_texture_pipeline;


	Dynamic_Array<VkDescriptorPool> imm_descriptor_pools;
	int imm_current_descriptor_pool = 0;

	VkDescriptorSet imm_get_descriptor_set(VkDescriptorSetLayout descriptor_set_layout);
	

	const VkAllocationCallbacks* host_allocator = NULL;


	VkCommandBuffer begin_single_command_buffer();
	void end_single_command_buffer(VkCommandBuffer command_buffer);

	void transition_image_layout(VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout);
	void copy_buffer_to_image(VkBuffer buffer, VkImage image, u32 width, u32 height, u32 dst_offset_x = 0, u32 dst_offset_y = 0);

	void create_one_channel_texture(u8* image_buffer, u32 width, u32 height, VkImage* out_image, VkImageView* out_image_view, VkSampler* out_sampler, Vulkan_Memory_Allocation* out_image_memory);



	// Debug marker stuff
#if DEBUG
	PFN_vkDebugMarkerSetObjectTagEXT pfnDebugMarkerSetObjectTag;
	PFN_vkDebugMarkerSetObjectNameEXT pfnDebugMarkerSetObjectName;
	PFN_vkCmdDebugMarkerBeginEXT pfnCmdDebugMarkerBegin;
	PFN_vkCmdDebugMarkerEndEXT pfnCmdDebugMarkerEnd;
	PFN_vkCmdDebugMarkerInsertEXT pfnCmdDebugMarkerInsert;

	bool is_debug_marker_extension_available = false;

	int debug_marker_stack = 0;
#endif

	inline void begin_debug_marker(String str, rgba color)
	{
	#if DEBUG
		if (!is_debug_marker_extension_available) return;

		VkDebugMarkerMarkerInfoEXT info = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT,
			.pMarkerName = str.data,
		};

		auto color_vec4 = color.as_vector4();
		memcpy(info.color, &color_vec4, sizeof(color_vec4));

		pfnCmdDebugMarkerInsert(main_command_buffer, &info);

		debug_marker_stack += 1;
	#endif
	}

	inline void end_debug_marker()
	{
	#if DEBUG
		if (!is_debug_marker_extension_available) return;

		assert(debug_marker_stack > 0);
		debug_marker_stack -= 1;

		pfnCmdDebugMarkerEnd(main_command_buffer);
	#endif
	}



#endif
};
inline Renderer renderer;

struct Scoped_Imm_Renderer_Mask
{
	Scoped_Imm_Renderer_Mask(Rect_Mask mask)
	{
		renderer.imm_push_mask(mask);
	}

	~Scoped_Imm_Renderer_Mask()
	{
		renderer.imm_pop_mask();
	}
};

#define scoped_imm_renderer_mask( __rect, __inversed ) Scoped_Imm_Renderer_Mask CONCAT(__scoped, __LINE__) ( { .rect = __rect, .inversed = __inversed} );\


