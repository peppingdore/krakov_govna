#pragma once

#include "b_lib/String.h"
#include "b_lib/Reflection.h"
#include "b_lib/Math.h"
#include "b_lib/Color.h"
#include "b_lib/Font.h"
#include "b_lib/Array_Map.h"


#include "Renderer.h"


#include "Main.h"


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

struct Glyph_Value
{
	Texture_Atlas* atlas;
	Vector2i       offset;
};



struct Mesh
{
	Dynamic_Array<Vector3> vertices;
	Dynamic_Array<u32>     indices;

	VkBuffer       vertex_buffer;
	VkBuffer       index_buffer;

	Vulkan_Memory_Allocation vertex_buffer_memory;
	Vulkan_Memory_Allocation index_buffer_memory;

	void allocate_on_gpu();
};





constexpr int VULKAN_MAX_PUSH_CONSTANT_SIZE = 512;


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



// Currently 1 channel only.
struct Texture_Atlas
{
	VkImage      image;
	VkImageView  image_view;
	VkSampler    image_sampler;
	Vulkan_Memory_Allocation image_memory;

	int size;

	Dynamic_Array<Rect> free_rects;

	static Texture_Atlas make(int size);

	bool put_in(void* image_buffer, int image_width, int image_height, int* out_uv_x_left, int* out_uv_y_bottom);
};


enum class Vulkan_Command_Type
{
	Unknown,

	Recalculate_Mask_Buffer,

	Draw_Texture,

	Draw_Rect,
	Draw_Faded_Rect,

	Draw_Line,

	Draw_Glyph,
};

struct Vulkan_Command
{
	Vulkan_Command_Type type;

#if DEBUG
	bool is_executed = false;
#endif

	union
	{
		struct
		{
			Dynamic_Array<Renderer::Mask> mask_stack;
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

			Glyph_Value glyph_value;
		} draw_glyph;
	};
};


struct Vulkan
{
	struct Pipeline_Options
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

	struct Pipeline
	{
		VkDescriptorSetLayout descriptor_set_layout;
		VkPipelineLayout      pipeline_layout;
		VkPipeline            pipeline;
	};

	Pipeline create_pipeline(Pipeline_Options options);



	struct
	{
		bool enable_validation_layers = false;

		const char* validation_layers[2] = {
			"VK_LAYER_LUNARG_standard_validation",
			"VK_LAYER_KHRONOS_validation",
			// "VK_LAYER_LUNARG_api_dump"
		};
		
	} parameters;

	struct 
	{
		VkShaderModule general_vertex;
		VkShaderModule general_fragment;
	
		VkShaderModule mask_vertex;
		VkShaderModule mask_fragment;

		VkShaderModule line_vertex;
		VkShaderModule line_fragment;

		VkShaderModule texture_vertex;
		VkShaderModule texture_fragment;
	} shaders;

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



	struct Uniform_Buffer 
	{
		VkBuffer buffer;
		Vulkan_Memory_Allocation memory;
	};

	Dynamic_Array<Uniform_Buffer> used_uniform_buffer_memory_allocations;



	Pipeline general_pipeline;
	Pipeline inversed_mask_pipeline;
	Pipeline clear_mask_pipeline;
	Pipeline line_pipeline;
	Pipeline texture_pipeline;

	Pipeline* currently_bound_pipeline = NULL;



	VkSampleCountFlagBits  msaa_samples_count = VK_SAMPLE_COUNT_1_BIT;

	bool swapchain_is_dead = false;



	VkInstance       instance;
	VkPhysicalDevice physical_device;
	VkDevice         device;
	VkQueue          device_queue;

	u32 queue_family_index;


	VkSurfaceKHR surface;

	VkCommandPool    command_pool;

	Dynamic_Array<VkDescriptorPool> descriptor_pools;
	int current_descriptor_pool = 0;

	VkDescriptorSet get_descriptor_set(VkDescriptorSetLayout descriptor_set_layout);



	const VkAllocationCallbacks* host_allocator = NULL;



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


	Array_Map<Glyph_Key, Glyph_Value> glyphs;

	Bucket_Array<Texture_Atlas> glyph_atlasses;




	void create_vk_instance();
	void find_suitable_gpu_and_create_device_and_queue();

	void create_swapchain();
	void create_main_framebuffer();

	void create_primitives();
	void load_shaders();
	void create_pipelines();

	void recreate_swapchain();

	void create_white_texture();

	void make_sure_texture_is_on_gpu(Texture* texture);

	void recalculate_mask_buffer(Dynamic_Array<Renderer::Mask> mask_stack);

	void draw_texture(Rect rect, Texture* texture);

	void draw_rect(Rect rect, rgba color);
	void draw_faded_rect(Rect rect, rgba color, int alpha_left, int alpha_right);
	void draw_line(int x0, int y0, int x1, int y1, rgba color);

	void draw_glyph(Glyph* glyph, int x, int y, rgba color);


	bool bind_pipeline(Pipeline* pipeline);



	void frame_begin();
	void frame_end();


	Dynamic_Array<Vulkan_Command> commands;
	void execute_commands();


	VkCommandBuffer begin_single_command_buffer();
	void end_single_command_buffer(VkCommandBuffer command_buffer);

	void transition_image_layout(VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout);
	void copy_buffer_to_image(VkBuffer buffer, VkImage image, u32 width, u32 height, u32 dst_offset_x = 0, u32 dst_offset_y = 0);

	void create_one_channel_texture(u8* image_buffer, u32 width, u32 height, VkImage* out_image, VkImageView* out_image_view, VkSampler* out_sampler, Vulkan_Memory_Allocation* out_image_memory);


	void init();







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


};

inline Vulkan vk;