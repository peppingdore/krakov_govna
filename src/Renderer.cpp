#include "Main.h"

#include "Renderer.h"

#define STB_IMAGE_IMPLEMENTATION
#if OS_WINDOWS
#define STBI_WINDOWS_UTF8
#endif
#include "stb_image.h"


#include "Asset_Storage.h"


u64 total_allocation_size = 0;

#if 1
void* vk_allocation_function(void* pUserData, size_t size, size_t alignment, VkSystemAllocationScope allocationScope)
{
	total_allocation_size += size;

	// Log(U"Allocated: Size: %. Scope: %, total_allocation_size: %", size_to_string(size, c_allocator), allocationScope, size_to_string(total_allocation_size, c_allocator));	
	return _aligned_malloc(align(size, alignment), alignment);
};

void* vk_reallocation_function(void* pUserData, void* pOriginal, size_t size, size_t alignment, VkSystemAllocationScope allocationScope)
{
	total_allocation_size += size;

	// Log(U"Reallocated: Size: %. Scope: %, total_allocation_size: %", size_to_string(size, c_allocator), allocationScope, size_to_string(total_allocation_size, c_allocator));


	return _aligned_realloc(pOriginal, size, alignment);
};

void vk_free_function(void* pUserData, void* pMemory)
{
	_aligned_free(pMemory);
};
#endif



Arena_Allocator init_arena;

void Renderer::init(int initial_width, int initial_height)
{
	ZoneScoped;

	make_array(&imm_mask_stack, 32, c_allocator);

	width  = initial_width;
	height = initial_height;




	create_arena_allocator(&init_arena, c_allocator, 4096);
	defer { init_arena.free(); };

	init_arena.owning_thread = threading.current_thread_id();


	make_array(&swapchain_nodes,         4,  c_allocator);
	make_array(&imm_descriptor_pools,    32, c_allocator);
	make_array(&imm_commands,            32, c_allocator);

	make_array_map(&glyph_gpu_map,       32, c_allocator);

	make_bucket_array(&glyph_atlasses,   32, c_allocator);

	make_array(&imm_used_uniform_buffer_memory_allocations, 32, c_allocator);


	// Setup allocator callbacks
	{


#if 0
		auto internal_allocation_function = [](void* pUserData, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope) -> void
		{
		};

		auto internal_free_function = [](void* pUserData, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope) -> void
		{

		};
#endif

#if 1
		const static VkAllocationCallbacks allocation_callbacks = {
			.pUserData = NULL,
			.pfnAllocation = vk_allocation_function,
			.pfnReallocation = vk_reallocation_function,
			.pfnFree = vk_free_function,

#if 0
			.pfnInternalAllocation = internal_allocation_function,
			.pfnInternalFree = internal_free_function,
#endif
		};

		host_allocator = &allocation_callbacks;
#endif
	}




	create_vk_instance();

	find_suitable_gpu_and_create_device_and_queue();

	// Check for MSAA support
	{
	 	VkPhysicalDeviceProperties physicalDeviceProperties;
	    vkGetPhysicalDeviceProperties(physical_device, &physicalDeviceProperties);

	    VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;

	    // if (counts & VK_SAMPLE_COUNT_2_BIT) { msaa_samples_count = VK_SAMPLE_COUNT_2_BIT; }
	    // if (counts & VK_SAMPLE_COUNT_4_BIT) { msaa_samples_count = VK_SAMPLE_COUNT_4_BIT; }
	    // if (counts & VK_SAMPLE_COUNT_8_BIT) { msaa_samples_count = VK_SAMPLE_COUNT_8_BIT; }

	    Log(U"Sample count = %", msaa_samples_count);
	}


	vulkan_memory_allocator.init();

#if OS_WINDOWS
	{
		VkWin32SurfaceCreateInfoKHR surface_create_info =
		{
			.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
			.pNext     = NULL,
			.flags     = 0,
			.hinstance = windows.hinstance,
			.hwnd      = windows.hwnd,
		};


		VkResult result = vkCreateWin32SurfaceKHR(instance, &surface_create_info, host_allocator, &surface);
		if (result != VK_SUCCESS)
			abort_the_mission(U"Failed to vkCreateWin32SurfaceKHR");
	}
#elif OS_LINUX
    {
        VkXlibSurfaceCreateInfoKHR surface_create_info = {
            .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
            .dpy = x11.display,
            .window = x11.window,
            .flags = 0,
            .pNext = NULL,
        };

        VkResult result = vkCreateXlibSurfaceKHR(instance, &surface_create_info, host_allocator, &surface);
        if (result != VK_SUCCESS)
            abort_the_mission(U"Failed to vkCreateXlibSurfaceKHR");
    }
#elif OS_DARWIN
	{
		extern CAMetalLayer* osx_metal_layer;

		VkMetalSurfaceCreateInfoEXT surface_create_info = {
			.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
			.flags = NULL,
			.pNext = NULL,
			.pLayer = osx_metal_layer
		};

		VkResult result = vkCreateMetalSurfaceEXT(instance, &surface_create_info, host_allocator, &surface);
		if (result != VK_SUCCESS)
			abort_the_mission(U"Failed to vkCreateMetalSurfaceEXT");
	}
#endif

	{
		VkFenceCreateInfo create_info;
		create_info.flags = 0;
		create_info.pNext = 0;
		create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

		if (vkCreateFence(device, &create_info, host_allocator, &rendering_done_fence) != VK_SUCCESS)
			abort_the_mission(U"Failed to vkCreateFence");



		create_info.flags = 0;
		create_info.pNext = 0;
		create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

		if (vkCreateFence(device, &create_info, host_allocator, &image_availability_fence) != VK_SUCCESS)
			abort_the_mission(U"Failed to vkCreateFence");
	}

	create_primitives();
	create_white_texture();

	create_swapchain();
	create_main_framebuffer();

	imm_load_shaders();
	imm_create_pipelines();
}

void Renderer::imm_create_pipelines()
{
	// General pipeline
	{
		VkPipelineDepthStencilStateCreateInfo depth_stencil = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,

			.pNext = NULL,
			.flags = 0,
			.depthTestEnable = VK_FALSE,
			.depthWriteEnable = VK_FALSE,
			.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
			.depthBoundsTestEnable = VK_FALSE,
			
			.stencilTestEnable = VK_TRUE,
			.front = {
				.failOp = VK_STENCIL_OP_KEEP,
				.passOp = VK_STENCIL_OP_KEEP,
				.depthFailOp = VK_STENCIL_OP_KEEP,
				.compareOp = VK_COMPARE_OP_LESS,
				.compareMask = u32_max,
				.writeMask = 0,
				.reference = 0,
			},
			.minDepthBounds = 0.0,
			.maxDepthBounds = 1.0
		};

		depth_stencil.back = depth_stencil.front;


		VkDescriptorSetLayoutBinding bindings[] = {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_ALL,
				.pImmutableSamplers = NULL,
			},
			{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				.pImmutableSamplers = NULL,
			},
		};

		VkPipelineColorBlendAttachmentState colorBlendAttachment = {
	    	.blendEnable = VK_TRUE,
			.srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
			.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			.colorBlendOp = VK_BLEND_OP_ADD,
			.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
			.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
			.alphaBlendOp = VK_BLEND_OP_MAX,
			.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
    					  VK_COLOR_COMPONENT_G_BIT |
    					  VK_COLOR_COMPONENT_B_BIT |
    					  VK_COLOR_COMPONENT_A_BIT ,
		};


		imm_general_pipeline = imm_create_pipeline({
			.vertex_shader   = imm_shaders.glyph_rect_vertex,
			.fragment_shader = imm_shaders.glyph_rect_fragment,

			.blending_state = &colorBlendAttachment,
			.depth_stencil_state = &depth_stencil,

			.descriptor_set_layout_bindings = bindings,
			.descriptor_set_layout_bindings_count = array_count(bindings),

			.no_vertex_buffer = true,
		});
	}

	// Texture pipeline
	{
		VkPipelineDepthStencilStateCreateInfo depth_stencil = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,

			.pNext = NULL,
			.flags = 0,
			.depthTestEnable = VK_FALSE,
			.depthWriteEnable = VK_FALSE,
			.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
			.depthBoundsTestEnable = VK_FALSE,
			
			.stencilTestEnable = VK_TRUE,
			.front = {
				.failOp = VK_STENCIL_OP_KEEP,
				.passOp = VK_STENCIL_OP_KEEP,
				.depthFailOp = VK_STENCIL_OP_KEEP,
				.compareOp = VK_COMPARE_OP_LESS,
				.compareMask = u32_max,
				.writeMask = 0,
				.reference = 0,
			},
			.minDepthBounds = 0.0,
			.maxDepthBounds = 1.0
		};

		depth_stencil.back = depth_stencil.front;


		VkDescriptorSetLayoutBinding bindings[] = {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				.pImmutableSamplers = NULL,
			},
		};

		VkPipelineColorBlendAttachmentState colorBlendAttachment = {
	    	.blendEnable = VK_TRUE,
			.srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
			.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			.colorBlendOp = VK_BLEND_OP_ADD,
			.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
			.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
			.alphaBlendOp = VK_BLEND_OP_MAX,
			.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
    					  VK_COLOR_COMPONENT_G_BIT |
    					  VK_COLOR_COMPONENT_B_BIT |
    					  VK_COLOR_COMPONENT_A_BIT ,
		};


		imm_texture_pipeline = imm_create_pipeline({
			.vertex_shader   = imm_shaders.texture_vertex,
			.fragment_shader = imm_shaders.texture_fragment,

			.blending_state = &colorBlendAttachment,
			.depth_stencil_state = &depth_stencil,

			.descriptor_set_layout_bindings = bindings,
			.descriptor_set_layout_bindings_count = array_count(bindings),

			.push_constant_size = sizeof(Texture_Uniform_Block),

			.no_vertex_buffer = true,
		});	
	}



	// Mask pipeline
	{
		VkPipelineDepthStencilStateCreateInfo depth_stencil = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,

			.pNext = NULL,
			.flags = 0,
			.depthTestEnable = VK_FALSE,
			.depthWriteEnable = VK_FALSE,
			.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
			.depthBoundsTestEnable = VK_FALSE,
			
			.stencilTestEnable = VK_TRUE,
			.front = {
				.failOp = VK_STENCIL_OP_KEEP,
				.passOp = VK_STENCIL_OP_KEEP,
				.depthFailOp = VK_STENCIL_OP_ZERO,
				.compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
				.compareMask = u32_max,
				.writeMask = u32_max,
				.reference = 0,
			},

			.minDepthBounds = 0.0,
			.maxDepthBounds = 1.0
		};
		depth_stencil.back = depth_stencil.front;

		VkPipelineColorBlendAttachmentState colorBlendAttachment = {
	    	.blendEnable = VK_FALSE,
			.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
			.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			.colorBlendOp = VK_BLEND_OP_ADD,
			.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
			.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
			.alphaBlendOp = VK_BLEND_OP_ADD,
	    	.colorWriteMask = 0,
		};


		// Inversed mask pipeline
		depth_stencil.front.passOp      = VK_STENCIL_OP_ZERO;
		depth_stencil.front.failOp      = VK_STENCIL_OP_ZERO;
		depth_stencil.front.depthFailOp = VK_STENCIL_OP_ZERO;
		depth_stencil.back = depth_stencil.front;

		imm_inversed_mask_pipeline = imm_create_pipeline({
			.vertex_shader   = imm_shaders.mask_vertex,
			.fragment_shader = imm_shaders.mask_fragment,

			.blending_state = &colorBlendAttachment,
			.depth_stencil_state = &depth_stencil,

			.descriptor_set_layout_bindings_count = 0,

			.push_constant_size = sizeof(Mask_Uniform_Block),

			.no_vertex_buffer = true,
		});

		// Clear mask pipeline
		depth_stencil.front.passOp      = VK_STENCIL_OP_REPLACE;
		depth_stencil.front.failOp      = VK_STENCIL_OP_REPLACE;
		depth_stencil.front.depthFailOp = VK_STENCIL_OP_REPLACE;
		depth_stencil.front.reference   = 1;

		depth_stencil.back = depth_stencil.front;

		imm_clear_mask_pipeline = imm_create_pipeline({
			.vertex_shader   = imm_shaders.mask_vertex,
			.fragment_shader = imm_shaders.mask_fragment,

			.blending_state = &colorBlendAttachment,
			.depth_stencil_state = &depth_stencil,

			.descriptor_set_layout_bindings_count = 0,

			.push_constant_size = sizeof(Mask_Uniform_Block),

			.no_vertex_buffer = true,
		});
	}

	// Line pipeline 
	{
		VkPipelineDepthStencilStateCreateInfo depth_stencil = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,

			.pNext = NULL,
			.flags = 0,
			.depthTestEnable = VK_FALSE,
			.depthWriteEnable = VK_FALSE,
			.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
			.depthBoundsTestEnable = VK_FALSE,
			
			.stencilTestEnable = VK_TRUE,
			.front = {
				.failOp = VK_STENCIL_OP_KEEP,
				.passOp = VK_STENCIL_OP_KEEP,
				.depthFailOp = VK_STENCIL_OP_KEEP,
				.compareOp = VK_COMPARE_OP_LESS,
				.compareMask = u32_max,
				.writeMask = 0,
				.reference = 0,
			},
			.minDepthBounds = 0.0,
			.maxDepthBounds = 1.0
		};

		depth_stencil.back = depth_stencil.front;


		VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
			.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
			.primitiveRestartEnable = VK_FALSE,
		};

		VkPipelineColorBlendAttachmentState colorBlendAttachment = {
	    	.blendEnable = VK_TRUE,
			.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
			.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
			.colorBlendOp = VK_BLEND_OP_ADD,
			.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
			.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
			.alphaBlendOp = VK_BLEND_OP_ADD,
	    	.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
				  VK_COLOR_COMPONENT_G_BIT |
				  VK_COLOR_COMPONENT_B_BIT |
				  VK_COLOR_COMPONENT_A_BIT ,
		};

		imm_line_pipeline = imm_create_pipeline({
			.vertex_shader   = imm_shaders.line_vertex,
			.fragment_shader = imm_shaders.line_fragment,

			.blending_state = &colorBlendAttachment,
			.depth_stencil_state = &depth_stencil,
			.input_assembly_state = &input_assembly_state,

			.descriptor_set_layout_bindings_count = 0,

			.push_constant_size = sizeof(Line_Uniform_Block),

			.no_vertex_buffer = true,
		});
	}
}


Renderer::Imm_Pipeline Renderer::imm_create_pipeline(Renderer::Imm_Pipeline_Options options)
{
	ZoneScoped;

	Imm_Pipeline result;


	VkDescriptorSetLayoutCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.bindingCount = (u32) options.descriptor_set_layout_bindings_count,
		.pBindings    = options.descriptor_set_layout_bindings,
	};

	if (vkCreateDescriptorSetLayout(device, &create_info, host_allocator, &result.descriptor_set_layout) != VK_SUCCESS)
		abort_the_mission(U"Failed to vkCreateDescriptorSetLayout");



	VkPushConstantRange push_constant_range = {
		.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
		.offset = 0,
		.size = (u32) options.push_constant_size,
	};

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
    	.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    	.setLayoutCount = 1,
    	.pSetLayouts = &result.descriptor_set_layout,
    	.pushConstantRangeCount = (u32) (options.push_constant_size ? 1 : 0),
    	.pPushConstantRanges = options.push_constant_size ? &push_constant_range : NULL,
	};


    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, host_allocator, &result.pipeline_layout) != VK_SUCCESS)
        abort_the_mission(U"Failed to vkCreatePipelineLayout");



	VkPipelineShaderStageCreateInfo vertShaderStageInfo = {
		.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage  = VK_SHADER_STAGE_VERTEX_BIT,
		.module = options.vertex_shader,
		.pName  = "main",
	};

	VkPipelineShaderStageCreateInfo fragShaderStageInfo = {
		.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
		.module = options.fragment_shader,
		.pName  = "main",
	};

	VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };




	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
		.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE,
	};

	if (options.input_assembly_state)
	{
		inputAssembly = *options.input_assembly_state;
	}


	VkViewport viewport = {
		.x = 0,
		.y = 0,
		.width  = (float) renderer.width,
		.height = (float) renderer.height,
		.minDepth = 0.0,
		.maxDepth = 1.0
	};

	VkRect2D scissor_rect = {
		.offset = {
			.x = 0,
			.y = 0,
		},
		.extent = {
			.width  = (u32) renderer.width,
			.height = (u32) renderer.height,
		},
	};

	VkPipelineViewportStateCreateInfo viewportState = {
    	.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    	
    	.viewportCount = 1,
    	.pViewports    = &viewport,
    	.scissorCount  = 1,
    	.pScissors     = &scissor_rect,
	};

    VkPipelineRasterizationStateCreateInfo rasterizer = {
    	.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    	.depthClampEnable = VK_FALSE,
    	.rasterizerDiscardEnable = VK_FALSE,
    	.polygonMode = VK_POLYGON_MODE_FILL,
    	.cullMode = VK_CULL_MODE_BACK_BIT,
    	.frontFace = VK_FRONT_FACE_CLOCKWISE,
    	.depthBiasEnable = VK_FALSE,
    	.lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
    	.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    	.rasterizationSamples = msaa_samples_count,
    	.sampleShadingEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {
    	.blendEnable = VK_TRUE,
		.srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
		.alphaBlendOp = VK_BLEND_OP_ADD,
    	.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};

	if (options.blending_state)
	{
		colorBlendAttachment = *options.blending_state;
	}


    VkPipelineColorBlendStateCreateInfo colorBlending = {
    	.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    	.logicOpEnable = VK_FALSE,
    	.logicOp = VK_LOGIC_OP_COPY,
    	.attachmentCount = 1,
    	.pAttachments = &colorBlendAttachment,
    	.blendConstants = {
    		0.0,
    		0.0,
    		0.0,
    		0.0
    	},
	};



	VkPipelineDepthStencilStateCreateInfo depthStencil = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,

		.pNext = NULL,
		.flags = 0,
		.depthTestEnable = VK_FALSE,
		.depthWriteEnable = VK_FALSE,
		.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
		.depthBoundsTestEnable = VK_FALSE,
		
		.stencilTestEnable = VK_FALSE,
		.front = {
			.failOp = VK_STENCIL_OP_KEEP,
			.passOp = VK_STENCIL_OP_KEEP,
			.depthFailOp = VK_STENCIL_OP_KEEP,
			.compareOp = VK_COMPARE_OP_NEVER,
			.compareMask = 0,
			.writeMask = 0,
			.reference = 0,
		},
		.back = {
			.failOp = VK_STENCIL_OP_KEEP,
			.passOp = VK_STENCIL_OP_KEEP,
			.depthFailOp = VK_STENCIL_OP_KEEP,
			.compareOp = VK_COMPARE_OP_NEVER,
			.compareMask = 0,
			.writeMask = 0,
			.reference = 0,
		},

		.minDepthBounds = 0.0,
		.maxDepthBounds = 1.0
	};

	if (options.depth_stencil_state)
	{
		depthStencil = *options.depth_stencil_state;
	}



	VkVertexInputBindingDescription vertex_binding_desc = {
		.binding = 0,
		.stride = sizeof(float) * 3,
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};

	VkVertexInputAttributeDescription vertex_attribute_descriptions[] = {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = 0,
		}
	};

	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,

		.pNext = NULL,
		.flags = 0,
		
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &vertex_binding_desc,

		.vertexAttributeDescriptionCount = array_count(vertex_attribute_descriptions),
		.pVertexAttributeDescriptions = vertex_attribute_descriptions,
	};

	if (options.no_vertex_buffer)
	{
		vertexInputInfo.vertexBindingDescriptionCount = 0;
		vertexInputInfo.vertexAttributeDescriptionCount = 0;
	}



	VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,

		.pNext = NULL,
		.flags = 0,
		.dynamicStateCount = array_count(dynamic_states),
		.pDynamicStates = dynamic_states,
	};


    VkGraphicsPipelineCreateInfo pipelineInfo = {
    	.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    	
    	.stageCount          = array_count(shaderStages),
    	.pStages             = shaderStages,
    	
    	.pVertexInputState   = &vertexInputInfo,
    	.pInputAssemblyState = &inputAssembly,
    	.pViewportState      = &viewportState,
    	.pRasterizationState = &rasterizer,
    	.pMultisampleState   = &multisampling,
    	.pDepthStencilState  = &depthStencil,
    	.pColorBlendState    = &colorBlending,
    	.pDynamicState       = &dynamicState,

    	.layout              = result.pipeline_layout,
    	.renderPass          = main_render_pass,
    	.subpass             = 0,
    	.basePipelineHandle  = VK_NULL_HANDLE,
	};

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, host_allocator, &result.pipeline) != VK_SUCCESS)
        abort_the_mission(U"Failed to vkCreateGraphicsPipelines");

    return result;
}



void Renderer::create_primitives()
{
	ZoneScoped;


	// Quad
	{
		const static Vertex quad_vertices[] = {
			{
				.position = Vector3::make(-1, -1, 0),
				.normal   = Vector3::make(0, 0, -1),
				.uv       = Vector2::make(0, 0),
			},
			{
				.position = Vector3::make(-1, 1, 0),
				.normal   = Vector3::make(0, 0, -1),
				.uv       = Vector2::make(0, 1),
			},
			{
				.position = Vector3::make(1, 1, 0),
				.normal   = Vector3::make(0, 0, -1),
				.uv       = Vector2::make(1, 1),
			},
			{
				.position = Vector3::make(1, -1, 0),
				.normal   = Vector3::make(0, 0, -1),
				.uv       = Vector2::make(1, 0),
			}
		};

		const static u32 quad_indices[] = {
			0, 1, 2,
			2, 3, 0
		};

		primitives.quad.vertices = Dynamic_Array<Vertex>::from_static_array(quad_vertices);
		primitives.quad.indices  = Dynamic_Array<u32>::from_static_array(quad_indices);

		primitives.quad.allocate_on_gpu();
	}

};

void Mesh::allocate_on_gpu()
{
	ZoneScoped;

	// Vertex buffer
	{
		u64 vertices_size = sizeof(vertices.data[0]) * vertices.count;

		VkBufferCreateInfo bufferInfo = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = vertices_size,
			.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};

		if (vkCreateBuffer(renderer.device, &bufferInfo, renderer.host_allocator, &vertex_buffer) != VK_SUCCESS)
			abort_the_mission(U"Failed to vkCreateBuffer");

		vertex_buffer_memory = vulkan_memory_allocator.allocate_and_bind(vertex_buffer, VULKAN_MEMORY_SHOULD_BE_MAPPABLE, code_location());

		void* data;
		vkMapMemory(renderer.device, vertex_buffer_memory.device_memory, vertex_buffer_memory.offset, bufferInfo.size, 0, &data);
		memcpy(data, vertices.data, vertices_size);
		vkUnmapMemory(renderer.device, vertex_buffer_memory.device_memory);
	}

	// Index buffer
	{
		u64 indices_size = sizeof(indices.data[0]) * indices.count;

		VkBufferCreateInfo bufferInfo = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = indices_size,
			.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};

		if (vkCreateBuffer(renderer.device, &bufferInfo, renderer.host_allocator, &index_buffer) != VK_SUCCESS)
			abort_the_mission(U"Failed to vkCreateBuffer");

		index_buffer_memory = vulkan_memory_allocator.allocate_and_bind(index_buffer, VULKAN_MEMORY_SHOULD_BE_MAPPABLE, code_location());

		void* data;
		vkMapMemory(renderer.device, index_buffer_memory.device_memory, index_buffer_memory.offset, bufferInfo.size, 0, &data);
		memcpy(data, indices.data, indices_size);
		vkUnmapMemory(renderer.device, index_buffer_memory.device_memory);
	}

}

void Renderer::imm_load_shaders()
{
	ZoneScoped;

	Unicode_String shaders_path = path_concat(init_arena, executable_directory, Unicode_String(U"shaders"));

	auto imm_load_shader = [&](Unicode_String name, VkShaderModule* result_module)
	{
		Unicode_String shader_path = path_concat(init_arena, shaders_path, name);
	
		Buffer spirv;
		if (!read_entire_file_to_buffer(c_allocator, shader_path, &spirv))
			abort_the_mission(U"Failed to open shader file. Path: %", shader_path);

		defer { spirv.free(); };


		VkShaderModuleCreateInfo create_info = 
		{
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = spirv.occupied,
			.pCode = reinterpret_cast<const uint32_t*>(spirv.data),
		};

		if (vkCreateShaderModule(device, &create_info, host_allocator, result_module) != VK_SUCCESS)
		    abort_the_mission(U"Failed to vkCreateShaderModule for %", name);

		Log(U"Successfully loaded shader: %", name);
	};

	imm_load_shader(U"imm_glyph_rect.vert.spirv", &imm_shaders.glyph_rect_vertex);
	imm_load_shader(U"imm_glyph_rect.frag.spirv", &imm_shaders.glyph_rect_fragment);

	imm_load_shader(U"imm_mask.vert.spirv", &imm_shaders.mask_vertex);
	imm_load_shader(U"imm_mask.frag.spirv", &imm_shaders.mask_fragment);

	imm_load_shader(U"imm_line.vert.spirv", &imm_shaders.line_vertex);
	imm_load_shader(U"imm_line.frag.spirv", &imm_shaders.line_fragment);

	imm_load_shader(U"imm_texture.vert.spirv", &imm_shaders.texture_vertex);
	imm_load_shader(U"imm_texture.frag.spirv", &imm_shaders.texture_fragment);

	Log(U"");
}

void Renderer::create_white_texture()
{
	ZoneScoped;

	const static u8 white_texture_data[1] = { 255 };
	
	create_one_channel_texture((u8*) white_texture_data, 1, 1, &white_texture.image, &white_texture.image_view, &white_texture.image_sampler, &white_texture.image_memory);
}

void Renderer::create_one_channel_texture(u8* image_buffer, u32 width, u32 height, VkImage* out_image, VkImageView* out_image_view, VkSampler* out_sampler, Vulkan_Memory_Allocation* out_image_memory)
{
	ZoneScoped;

#if OS_DARWIN
	VkFormat format = VK_FORMAT_R8_UNORM;
#else
	VkFormat format = VK_FORMAT_R8_SRGB;
#endif

	VkDeviceSize glyph_size = width * height;
		
	VkBuffer staging_buffer;
	VkBufferCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.size = glyph_size,
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
	};
	vkCreateBuffer(device, &create_info, host_allocator, &staging_buffer);
	Vulkan_Memory_Allocation staging_buffer_memory = vulkan_memory_allocator.allocate_and_bind(staging_buffer, VULKAN_MEMORY_SHOULD_BE_MAPPABLE, code_location());

	{
		void* data;
	    vkMapMemory(device, staging_buffer_memory.device_memory, staging_buffer_memory.offset, glyph_size, 0, &data);
		memcpy(data, image_buffer, glyph_size);
	    vkUnmapMemory(device, staging_buffer_memory.device_memory);
	}

	VkImage image;
	Vulkan_Memory_Allocation image_memory;

	VkImageCreateInfo imageInfo = {
    	.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    	.imageType = VK_IMAGE_TYPE_2D,
    	.format = format,
    	.extent = {
    		.width  = width,
    		.height = height,
    		.depth  = 1,
    	},
    	.mipLevels = 1,
    	.arrayLayers = 1,
    	.samples = VK_SAMPLE_COUNT_1_BIT,
    	.tiling = VK_IMAGE_TILING_OPTIMAL,
    	.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    	.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    	.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

    vkCreateImage(device, &imageInfo, host_allocator, &image);
    image_memory = vulkan_memory_allocator.allocate_and_bind(image, (Vulkan_Memory_Allocation_Flags) 0, code_location());

    transition_image_layout(image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	copy_buffer_to_image(staging_buffer, image, width, height);
    transition_image_layout(image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	
	vkDestroyBuffer(device, staging_buffer, host_allocator);
    vulkan_memory_allocator.free(staging_buffer_memory);


    VkImageViewCreateInfo image_view_info = {
    	.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    	.image = image,
    	.viewType = VK_IMAGE_VIEW_TYPE_2D,
    	.format = format,
    	.subresourceRange = {
    		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    		.baseMipLevel = 0,
    		.levelCount = 1,
    		.baseArrayLayer = 0,
    		.layerCount = 1,
    	}
	};

    VkImageView image_view;
    vkCreateImageView(device, &image_view_info, host_allocator, &image_view);


    VkSampler image_sampler;
    VkSamplerCreateInfo sampler_info = {
    	.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    	.magFilter = VK_FILTER_LINEAR,
    	.minFilter = VK_FILTER_LINEAR,
    	.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    	.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    	.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    	.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    	.anisotropyEnable = VK_FALSE,
    	.maxAnisotropy = 16.0f,
    	.compareEnable = VK_FALSE,
    	.compareOp = VK_COMPARE_OP_ALWAYS,
    	.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE,
    	.unnormalizedCoordinates = VK_FALSE,
	};

    vkCreateSampler(device, &sampler_info, host_allocator, &image_sampler);

    *out_image         = image;
    *out_image_view    = image_view;
    *out_sampler       = image_sampler;
    *out_image_memory  = image_memory;
}

void Renderer::make_sure_texture_is_on_gpu(Texture* texture)
{
	ZoneScoped;

	if (texture->is_on_gpu) return;

	VkFormat format;

	switch (texture->format)
	{
		case Texture_Format::Monochrome:
			format = OS_DARWIN ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8_SRGB;
			break;

		case Texture_Format::RGB:			
			format = OS_DARWIN ? VK_FORMAT_R8G8B8_UNORM : VK_FORMAT_R8G8B8_SRGB;
			break;
	
		case Texture_Format::RGBA:			
			format = OS_DARWIN ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;
			break;

		default:
			assert(false);
	}

	VkBuffer staging_buffer;
	VkBufferCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = NULL,
		.flags = 0,
		.size = texture->size,
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
	};
	vkCreateBuffer(device, &create_info, host_allocator, &staging_buffer);
	Vulkan_Memory_Allocation staging_buffer_memory = vulkan_memory_allocator.allocate_and_bind(staging_buffer, VULKAN_MEMORY_SHOULD_BE_MAPPABLE, code_location());

	{
		void* data;
	    vkMapMemory(device, staging_buffer_memory.device_memory, staging_buffer_memory.offset, texture->size, 0, &data);
		memcpy(data, texture->image_buffer, texture->size);
	    vkUnmapMemory(device, staging_buffer_memory.device_memory);
	}

	VkImage image;
	Vulkan_Memory_Allocation image_memory;

	VkImageCreateInfo imageInfo = {
    	.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    	.imageType = VK_IMAGE_TYPE_2D,
    	.format = format,
    	.extent = {
    		.width  = (u32) texture->width,
    		.height = (u32) texture->height,
    		.depth  = 1,
    	},
    	.mipLevels = 1,
    	.arrayLayers = 1,
    	.samples = VK_SAMPLE_COUNT_1_BIT,
    	.tiling = VK_IMAGE_TILING_OPTIMAL,
    	.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    	.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    	.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

    vkCreateImage(device, &imageInfo, host_allocator, &image);
    image_memory = vulkan_memory_allocator.allocate_and_bind(image, (Vulkan_Memory_Allocation_Flags) 0, code_location());

    transition_image_layout(image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	copy_buffer_to_image(staging_buffer, image, texture->width, texture->height);
    transition_image_layout(image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	
	vkDestroyBuffer(device, staging_buffer, host_allocator);
    vulkan_memory_allocator.free(staging_buffer_memory);


    VkImageViewCreateInfo image_view_info = {
    	.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    	.image = image,
    	.viewType = VK_IMAGE_VIEW_TYPE_2D,
    	.format = format,
    	.subresourceRange = {
    		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    		.baseMipLevel = 0,
    		.levelCount = 1,
    		.baseArrayLayer = 0,
    		.layerCount = 1,
    	}
	};

    VkImageView image_view;
    vkCreateImageView(device, &image_view_info, host_allocator, &image_view);


    VkSampler image_sampler;
    VkSamplerCreateInfo sampler_info = {
    	.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    	.magFilter = VK_FILTER_LINEAR,
    	.minFilter = VK_FILTER_LINEAR,
    	.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    	.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    	.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    	.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    	.anisotropyEnable = VK_FALSE,
    	.maxAnisotropy = 16.0f,
    	.compareEnable = VK_FALSE,
    	.compareOp = VK_COMPARE_OP_ALWAYS,
    	.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE,
    	.unnormalizedCoordinates = VK_FALSE,
	};

    vkCreateSampler(device, &sampler_info, host_allocator, &image_sampler);


    texture->is_on_gpu = true;

    texture->image         = image;
    texture->image_view    = image_view;
    texture->image_sampler = image_sampler;
    texture->image_memory  = image_memory;
}



void Renderer::create_vk_instance()
{
	ZoneScoped;

	VkApplicationInfo application_info;
	VkInstanceCreateInfo instance_info;

	application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application_info.pNext = NULL;
	application_info.pApplicationName = mission_name.to_utf8(init_arena, NULL);
	application_info.pEngineName = (char*) u8"хуй";
	application_info.engineVersion = 228;
	application_info.apiVersion = VK_API_VERSION_1_1;

	instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_info.pNext = NULL;
	instance_info.flags = 0;
	instance_info.pApplicationInfo = &application_info;



	Dynamic_Array<char*> extensions = make_array<char*>(32, init_arena);

#if OS_WINDOWS
	extensions.add(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif OS_LINUX
    extensions.add(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#elif OS_DARWIN
	extensions.add(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
#endif
	extensions.add(VK_KHR_SURFACE_EXTENSION_NAME);

	extensions.add("VK_KHR_get_physical_device_properties2");

#if DEBUG
	extensions.add("VK_EXT_debug_utils");
#endif


	#ifdef NDEBUG
	static_assert(!enable_validation_layers);
	#endif

	if (enable_validation_layers)
	{

		u32 available_validation_layers_count;
		vkEnumerateInstanceLayerProperties(&available_validation_layers_count, NULL);

		Dynamic_Array<VkLayerProperties> available_validation_layers = make_array<VkLayerProperties>(available_validation_layers_count, init_arena);
		
		vkEnumerateInstanceLayerProperties(&available_validation_layers_count, available_validation_layers.data);
		available_validation_layers.count = available_validation_layers_count;


		Dynamic_Array<const char*> c_validation_layer_strings = make_array<const char*>(32, init_arena);

		for (const char* str : validation_layers)
		{
			bool found = false;
			for (VkLayerProperties available_layer: available_validation_layers)
			{
				if (strcmp(available_layer.layerName, str) == 0)
				{
					found = true;
					break;
				}
			}

			if (!found)
			{
				log(ctx.logger, U"Validation layer '%' wasn't found. Skipping it.", str);
				continue;
			}


			c_validation_layer_strings.add(str);
		}

		instance_info.ppEnabledLayerNames = c_validation_layer_strings.data;
		instance_info.enabledLayerCount = c_validation_layer_strings.count;


		extensions.add(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}
	else
	{
		instance_info.enabledLayerCount = 0;
	}



	instance_info.ppEnabledExtensionNames = extensions.data;
	instance_info.enabledExtensionCount = extensions.count;



	VkResult result;

	result = vkCreateInstance(&instance_info, host_allocator, &instance);
	if (result != VK_SUCCESS)
		abort_the_mission(U"Failed to vkCreateInstance");


	if (enable_validation_layers)
	{
		PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
		if (!vkCreateDebugUtilsMessengerEXT)
			abort_the_mission(U"Failed to get pointer to vkCreateDebugUtilsMessengerEXT");

		VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		createInfo.pUserData = NULL;

		createInfo.pfnUserCallback = [](
			VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			VkDebugUtilsMessageTypeFlagsEXT messageTypes,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void* pUserData) -> VkBool32
		{
			if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
			{
				// assert(false);
			}
			log(ctx.logger, U"Vulkan reported\n   %\n", pCallbackData->pMessage);

			return VK_FALSE;
		};

		vkCreateDebugUtilsMessengerEXT(instance, &createInfo, host_allocator, &debug_messenger);
	}
}

void Renderer::find_suitable_gpu_and_create_device_and_queue()
{
	ZoneScoped;

	VkResult result;

	u32 devices_count;
	vkEnumeratePhysicalDevices(instance, &devices_count, NULL);
	if (devices_count == 0)
		abort_the_mission(U"vkEnumeratePhysicalDevices returned 0, At least one device required to run.");


	Dynamic_Array<VkPhysicalDevice> vk_devices = make_array<VkPhysicalDevice>(devices_count, init_arena);
	vk_devices.count = devices_count;

	result = vkEnumeratePhysicalDevices(instance, &devices_count, vk_devices.data);
	if (result != VK_SUCCESS)
		abort_the_mission(U"Failed to vkEnumeratePhysicalDevices");





	for (VkPhysicalDevice& p_device : vk_devices)
	{
		VkPhysicalDeviceProperties device_properties;
		VkPhysicalDeviceFeatures   device_features;

		vkGetPhysicalDeviceProperties(p_device, &device_properties);
		vkGetPhysicalDeviceFeatures(p_device, &device_features);

		String device_name = String::from_c_string((char*) device_properties.deviceName, init_arena);

		log(ctx.logger, U"\nDevice '%'.\n  Driver version = %,\n  device type = %,\n  API version = %.%.%",
			device_name,
			device_properties.driverVersion,
			device_properties.deviceType,
			VK_VERSION_MAJOR(device_properties.apiVersion), VK_VERSION_MINOR(device_properties.apiVersion), VK_VERSION_PATCH(device_properties.apiVersion));
		
		defer{ log(ctx.logger, U"\n"); };

		// Use GPU with index 0.
		// if (device_properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) continue;



		u32 queue_family_count;
		vkGetPhysicalDeviceQueueFamilyProperties(p_device, &queue_family_count, NULL);
		
		Dynamic_Array<VkQueueFamilyProperties> families = make_array<VkQueueFamilyProperties>(queue_family_count, init_arena);
		
		families.count = queue_family_count;
		vkGetPhysicalDeviceQueueFamilyProperties(p_device, &queue_family_count, families.data);




		for (VkQueueFamilyProperties& family_properties : families)
		{
			// @TODO: probably should've supported different queues for different task, but
			//   for now just don't bother.
			if ((family_properties.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
				(family_properties.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
				(family_properties.queueFlags & VK_QUEUE_GRAPHICS_BIT))
			{
				queue_family_index = families.fast_pointer_index(&family_properties);


				VkDeviceQueueCreateInfo queue_create_info = {};
				queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queue_create_info.queueFamilyIndex = queue_family_index;
				queue_create_info.queueCount = 1;

				float queue_priorities[] = { 1.0f };
				queue_create_info.pQueuePriorities = queue_priorities;

				VkPhysicalDeviceFeatures deviceFeatures = {};

			#if DEBUG
				deviceFeatures.robustBufferAccess = VK_TRUE;
			#endif



				VkDeviceCreateInfo device_create_info = {};
				device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
				device_create_info.pQueueCreateInfos = &queue_create_info;
				device_create_info.queueCreateInfoCount = 1;
				device_create_info.pEnabledFeatures = &deviceFeatures;


				Dynamic_Array<char*> device_extensions = make_array<char*>(32, init_arena);
				device_extensions.add(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
				device_extensions.add("VK_EXT_memory_budget");
				device_extensions.add("VK_KHR_dedicated_allocation");
				device_extensions.add("VK_KHR_get_memory_requirements2");

			#if DEBUG

				u32 properties_count;
				vkEnumerateDeviceExtensionProperties(p_device, NULL, &properties_count, NULL);

				Dynamic_Array<VkExtensionProperties> fucking_bullshit = make_array<VkExtensionProperties>(properties_count, init_arena);
				vkEnumerateDeviceExtensionProperties(p_device, NULL, &properties_count, fucking_bullshit.data);

				fucking_bullshit.count = properties_count;

				for (auto& ext : fucking_bullshit)
				{
					if (!strcmp(ext.extensionName, VK_EXT_DEBUG_MARKER_EXTENSION_NAME))
					{
						is_debug_marker_extension_available = true;
					}
				}

				if (is_debug_marker_extension_available)
				{
					device_extensions.add(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
				}
			#endif




				device_create_info.ppEnabledExtensionNames = device_extensions.data;
				device_create_info.enabledExtensionCount   = device_extensions.count;

				physical_device = p_device;

				{
					ZoneScopedN("vkCreateDevice");
					result = vkCreateDevice(p_device, &device_create_info, host_allocator, &device);
					if (result != VK_SUCCESS)
						abort_the_mission(U"Failed to create vkCreateDevice on: % with queue family index: %", device_name, queue_family_index);
				}


				{
					ZoneScopedN("vkGetDeviceQueue");
					vkGetDeviceQueue(device, queue_family_index, 0, &device_queue);
				}

				VkCommandPoolCreateInfo poolInfo = {};
				poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
				poolInfo.queueFamilyIndex = queue_family_index;
				poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

				{
					ZoneScopedN("vkCreateCommandPool");
					if (vkCreateCommandPool(device, &poolInfo, host_allocator, &command_pool) != VK_SUCCESS)
						abort_the_mission(U"Failed to vkCreateCommandPool");
				}

			#if DEBUG
				if (is_debug_marker_extension_available)
				{
					pfnDebugMarkerSetObjectTag = (PFN_vkDebugMarkerSetObjectTagEXT)vkGetDeviceProcAddr(device, "vkDebugMarkerSetObjectTagEXT");
					pfnDebugMarkerSetObjectName = (PFN_vkDebugMarkerSetObjectNameEXT)vkGetDeviceProcAddr(device, "vkDebugMarkerSetObjectNameEXT");
					pfnCmdDebugMarkerBegin = (PFN_vkCmdDebugMarkerBeginEXT)vkGetDeviceProcAddr(device, "vkCmdDebugMarkerBeginEXT");
					pfnCmdDebugMarkerEnd = (PFN_vkCmdDebugMarkerEndEXT)vkGetDeviceProcAddr(device, "vkCmdDebugMarkerEndEXT");
					pfnCmdDebugMarkerInsert = (PFN_vkCmdDebugMarkerInsertEXT)vkGetDeviceProcAddr(device, "vkCmdDebugMarkerInsertEXT");
				}
			#endif

				return;
			}
		}
	}

	abort_the_mission(U"Failed to locate suitable GPU");
}


VkDescriptorSet Renderer::imm_get_descriptor_set(VkDescriptorSetLayout descriptor_set_layout)
{
	auto create_new_pool = [&]()
	{
		VkDescriptorPoolSize pool_sizes[] = {
			{
				.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				.descriptorCount = 1
			},
			{
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1
			},
		};

		VkDescriptorPoolCreateInfo pool_info{};
		pool_info.flags = 0;
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.poolSizeCount = array_count(pool_sizes);
		pool_info.pPoolSizes = pool_sizes;
		pool_info.maxSets = 256;


		if (vkCreateDescriptorPool(device, &pool_info, host_allocator, imm_descriptor_pools.add()) != VK_SUCCESS)
			abort_the_mission(U"Failed to vkCreateDescriptorPool");
	};


	if (imm_descriptor_pools.count == 0)
	{
		create_new_pool();
	}


	VkDescriptorSetAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = NULL,
		.descriptorPool = *imm_descriptor_pools[imm_current_descriptor_pool],
		.descriptorSetCount = 1,
		.pSetLayouts = &descriptor_set_layout,
	};

	VkDescriptorSet descriptor_set;

	VkResult result = vkAllocateDescriptorSets(device, &allocate_info, &descriptor_set);
	if (result != VK_SUCCESS)
	{
		imm_current_descriptor_pool += 1;
		if (imm_current_descriptor_pool == imm_descriptor_pools.count)
		{
			create_new_pool();
		}

		allocate_info.descriptorPool = *imm_descriptor_pools[imm_current_descriptor_pool];

		result = vkAllocateDescriptorSets(device, &allocate_info, &descriptor_set);
		if (result != VK_SUCCESS)
			abort_the_mission(U"Failed to vkAllocateDescriptorSets");
	}

	return descriptor_set;
}




void Renderer::create_main_framebuffer()
{
	ZoneScoped;

	main_color_image_format         = VK_FORMAT_R8G8B8A8_UNORM;
	main_depth_stencil_image_format = VK_FORMAT_D24_UNORM_S8_UINT;

	{
		VkImageCreateInfo i;
		i.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;

		i.pNext = NULL;
		i.flags = 0;
		i.imageType = VK_IMAGE_TYPE_2D;
		i.format = main_color_image_format;

		i.extent.width  = renderer.width;
		i.extent.height = renderer.height;
		i.extent.depth = 1;

		i.mipLevels = 1;
		i.arrayLayers = 1;
		i.samples = msaa_samples_count;

		i.tiling = VK_IMAGE_TILING_OPTIMAL;
		i.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		i.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		i.queueFamilyIndexCount = 1;
		i.pQueueFamilyIndices = &queue_family_index;

		i.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; 

		vkCreateImage(device, &i, host_allocator, &main_color_image);

		main_color_image_memory = vulkan_memory_allocator.allocate_and_bind(main_color_image, VULKAN_MEMORY_ONLY_DEVICE_MEMORY, code_location());



		i.format = main_depth_stencil_image_format;
		i.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

		vkCreateImage(device, &i, host_allocator, &main_depth_stencil_image);

		main_depth_stencil_image_memory = vulkan_memory_allocator.allocate_and_bind(main_depth_stencil_image, VULKAN_MEMORY_ONLY_DEVICE_MEMORY, code_location());
	}

	{
		VkImageViewCreateInfo i;
		i.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;

		i.pNext = NULL;
		i.flags = 0;

		i.image = main_color_image;
		i.viewType = VK_IMAGE_VIEW_TYPE_2D;
		i.format = main_color_image_format;

		i.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		i.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		i.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		i.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

		i.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		
		i.subresourceRange.baseMipLevel = 0;
		i.subresourceRange.levelCount = 1;

		i.subresourceRange.baseArrayLayer = 0;
		i.subresourceRange.layerCount = 1;

		vkCreateImageView(device, &i, host_allocator, &main_color_image_view);




		i.image = main_depth_stencil_image;
		i.format = main_depth_stencil_image_format;
		i.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

		vkCreateImageView(device, &i, host_allocator, &main_depth_stencil_image_view);
	}

	{
		VkAttachmentDescription color_attachment =
		{
			.flags = 0,
			.format = main_color_image_format,
			.samples = msaa_samples_count,

			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			
			.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, // We will copy this image to swapchain.
		};

		VkAttachmentDescription depth_stencil_attachment =
		{
			.flags = 0,
			.format = main_depth_stencil_image_format,
			.samples = msaa_samples_count,

			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,

			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,

			.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		};

		VkAttachmentDescription attachments[] = { color_attachment, depth_stencil_attachment };

		VkAttachmentReference color_attachment_references[] = {
			{
				.attachment = 0,
				.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			}
		};

		VkAttachmentReference depth_stencil_attachment_reference = {
			.attachment = 1,
			.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		};


		VkSubpassDescription subpasses[] = {
			{
				.flags = 0,
				.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,

				.inputAttachmentCount = 0,

				.colorAttachmentCount = array_count(color_attachment_references),
				.pColorAttachments = color_attachment_references,

				.pResolveAttachments = NULL,

				.pDepthStencilAttachment = &depth_stencil_attachment_reference,

				.preserveAttachmentCount = 0,
			}
		};

		VkSubpassDependency self_dependency = {
			.srcSubpass = 0,
			.dstSubpass = 0,

			.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,

			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, 

			.dependencyFlags = 0,
		};

		VkRenderPassCreateInfo i = {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.pNext = NULL,
			.flags = 0,

			.attachmentCount = 2,
			.pAttachments = attachments,
			
			.subpassCount = array_count(subpasses),
			.pSubpasses = subpasses,

			.dependencyCount = 0,
			.pDependencies = &self_dependency,
		};

		vkCreateRenderPass(device, &i, host_allocator, &main_render_pass);
	}

	{

		VkImageView attachments[] = { main_color_image_view, main_depth_stencil_image_view };


		VkFramebufferCreateInfo i = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.pNext = NULL,
			.flags = 0,

			.renderPass = main_render_pass,

			.attachmentCount = array_count(attachments),
			.pAttachments = attachments,

			.width  = (u32) renderer.width,
			.height = (u32) renderer.height,
			.layers = 1,
		};

		vkCreateFramebuffer(device, &i, host_allocator, &main_framebuffer);
	}

	{
		VkCommandBufferAllocateInfo i = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.pNext = NULL,
			
			.commandPool = command_pool,
			
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};

		vkAllocateCommandBuffers(device, &i, &main_command_buffer);
	}
}



void Renderer::create_swapchain()
{
	Arena_Allocator arena;
	create_arena_allocator(&arena, c_allocator, 4096);
	defer { arena.free(); };


	swapchain_is_dead = false;

	ZoneScoped;

	VkResult result;

	VkBool32 does_support_present;
	result = vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, queue_family_index, surface, &does_support_present);
	if (result != VK_SUCCESS)
		abort_the_mission(U"Failed to vkGetPhysicalDeviceSurfaceSupportKHR");

	if (!does_support_present)
		abort_the_mission(U"Vulkan surface doesn't support present");


	VkSurfaceCapabilitiesKHR surface_capabilities;
	result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_capabilities);
	if (result != VK_SUCCESS)
		abort_the_mission(U"Failed to vkGetPhysicalDeviceSurfaceCapabilitiesKHR");



	u32 surface_formats_count = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &surface_formats_count, NULL);
	
	Dynamic_Array<VkSurfaceFormatKHR> surface_formats = make_array<VkSurfaceFormatKHR>(surface_formats_count, arena);
	surface_formats.count = surface_formats_count;
	
	result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &surface_formats_count, surface_formats.data);


	u32 present_modes_count = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_modes_count, NULL);
	
	Dynamic_Array<VkPresentModeKHR> present_modes = make_array<VkPresentModeKHR>(present_modes_count, arena);
	present_modes.count = present_modes_count;

	vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_modes_count, present_modes.data);



	VkSurfaceFormatKHR* suitable_surface_format = NULL;
	for (VkSurfaceFormatKHR& surface_format : surface_formats)
	{
		if (surface_format.format == VK_FORMAT_B8G8R8A8_UNORM)
		{
			suitable_surface_format = &surface_format;
			break;
		}
	}

	if (!suitable_surface_format)
		abort_the_mission(U"No suitable surface format was found");


	VkPresentModeKHR* suitable_present_mode = NULL;
	for (VkPresentModeKHR& present_mode : present_modes)
	{
		if (present_mode == VK_PRESENT_MODE_FIFO_KHR)
		{
			suitable_present_mode = &present_mode;
			break;
		}
	}

	if (!suitable_present_mode)
		abort_the_mission(U"No suitable present mode was found");



	swapchain_image_format = suitable_surface_format->format;



	VkSwapchainKHR old_swapchain = swapchain;

	VkSwapchainCreateInfoKHR createInfo = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surface,
		.minImageCount = surface_capabilities.minImageCount,
		.imageFormat = swapchain_image_format,
		.imageColorSpace = suitable_surface_format->colorSpace,
		.imageExtent = {
			.width  = (u32) renderer.width,
			.height = (u32) renderer.height,
		},
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices = NULL,
		.preTransform = surface_capabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = *suitable_present_mode,
		.clipped = VK_TRUE,
		.oldSwapchain = old_swapchain,
	};

	{
		ZoneScopedN("vkCreateSwapchainKHR");

		if (vkCreateSwapchainKHR(device, &createInfo, host_allocator, &swapchain) != VK_SUCCESS)
			abort_the_mission(U"vkCreateSwapchainKHR failed");

		assert(swapchain);
		
		if (old_swapchain)
			vkDestroySwapchainKHR(device, old_swapchain, host_allocator);
	}
	

	u32 swapchain_images_count;
	vkGetSwapchainImagesKHR(device, swapchain, &swapchain_images_count, NULL);

	Dynamic_Array<VkImage> swapchain_images = make_array<VkImage>(swapchain_images_count, arena
		);
	swapchain_images.count = swapchain_images_count;
	
	vkGetSwapchainImagesKHR(device, swapchain, &swapchain_images_count, swapchain_images.data);


	swapchain_nodes.clear();
	swapchain_nodes.ensure_capacity(swapchain_images_count);


	u32 index = 0;
	for (VkImage& swapchain_image : swapchain_images)
	{
		Swapchain_Node* swapchain_node = swapchain_nodes.add();
		swapchain_node->image = swapchain_image;
		swapchain_node->index = index;


		#if 0
		VkImageViewCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		createInfo.image = swapchain_image;
		createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		createInfo.format = swapchain_image_format;
		createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		createInfo.subresourceRange.baseMipLevel = 0;
		createInfo.subresourceRange.levelCount = 1;
		createInfo.subresourceRange.baseArrayLayer = 0;
		createInfo.subresourceRange.layerCount = 1;


		if (vkCreateImageView(device, &createInfo, host_allocator, &swapchain_node->image_view) != VK_SUCCESS)
			abort_the_mission(U"Failed to create image view for swapchain image");

		#endif

		index += 1;
	}
}
	


void Renderer::frame_begin()
{
	ZoneScoped;

	u32 node_index;

	vkAcquireNextImageKHR(device, swapchain, u64_max, VK_NULL_HANDLE, image_availability_fence, &node_index);
	current_swapchain_node = swapchain_nodes[node_index];

	vkWaitForFences(device, 1, &image_availability_fence, VK_FALSE, u64_max);
	vkResetFences(device, 1, &image_availability_fence);






	VkCommandBufferBeginInfo begin_info;
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	begin_info.pInheritanceInfo = NULL;
	begin_info.pNext = NULL;

	vkBeginCommandBuffer(main_command_buffer, &begin_info);

	{
		VkImageMemoryBarrier color_attachment_barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.pNext = 0,
			
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			
			.image = main_color_image,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};

		vkCmdPipelineBarrier(main_command_buffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0,
			0, NULL,
			0, NULL,
			1, &color_attachment_barrier);
	}

	{
		VkImageMemoryBarrier depth_stencil_attachment_barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.pNext = 0,
			
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			
			.image = main_depth_stencil_image,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};

		vkCmdPipelineBarrier(main_command_buffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			0,
			0, NULL,
			0, NULL,
			1, &depth_stencil_attachment_barrier);
	}


	{
		VkRect2D render_area = {
			.offset = {
				.x = 0,
				.y = 0,
			},

			.extent = {
				.width  = (u32) renderer.width,
				.height = (u32) renderer.height,
			},
		};


		VkClearValue color_attachment_clear_value;
		color_attachment_clear_value.color.float32[0] = 0.0f;
		color_attachment_clear_value.color.float32[1] = 0.0f;
		color_attachment_clear_value.color.float32[2] = 0.0f;

		VkClearValue depth_attachment_clear_value;
		depth_attachment_clear_value.depthStencil.depth   = 0;
		depth_attachment_clear_value.depthStencil.stencil = 1;
		
		VkClearValue clear_values[] = { color_attachment_clear_value, depth_attachment_clear_value };


		VkRenderPassBeginInfo render_pass_begin_info = {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.pNext = NULL,
			
			.renderPass = main_render_pass,
			.framebuffer = main_framebuffer,
			.renderArea = render_area,

			.clearValueCount = array_count(clear_values),
			.pClearValues = clear_values,
		};


		vkCmdBeginRenderPass(main_command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
	}

	VkViewport viewport = {
		.x = 0,
		.y = 0,
		.width  = (float) renderer.width,
		.height = (float) renderer.height,
		.minDepth = 0.0,
		.maxDepth = 1.0
	};
	vkCmdSetViewport(main_command_buffer, 0, 1, &viewport);

	VkRect2D scissor_rect = {
		.offset = {
			.x = 0,
			.y = 0
		},
		.extent = {
			.width  = (u32) renderer.width,
			.height = (u32) renderer.height 
		}
	};

	vkCmdSetScissor(main_command_buffer, 0, 1, &scissor_rect);




	imm_current_descriptor_pool = 0;
	for (VkDescriptorPool& pool: imm_descriptor_pools)
	{
		vkResetDescriptorPool(device, pool, 0);
	}

	renderer.imm_mask_stack.count = 0;
	renderer.imm_recalculate_mask_buffer();
}

void Renderer::frame_end()
{
	ZoneScoped;


	imm_execute_commands();



	vkCmdEndRenderPass(main_command_buffer);


	{
		VkImageMemoryBarrier present_image_barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.pNext = 0,
			
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			
			.image = current_swapchain_node->image,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};

		vkCmdPipelineBarrier(main_command_buffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0,
			0, NULL,
			0, NULL,
			1, &present_image_barrier);
	}


	{
		if (msaa_samples_count == VK_SAMPLE_COUNT_1_BIT)
		{
			VkImageBlit image_blit = {
				.srcSubresource = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = 0,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
				.srcOffsets = {
					{
						.x = 0, .y = 0, .z = 0,
					},
					{
						.x = renderer.width, .y = renderer.height, .z = 1,
					}
				},
				.dstSubresource = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = 0,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
				.dstOffsets = {
					{
						.x = 0, .y = 0, .z = 0,
					},
					{
						.x = renderer.width, .y = renderer.height, .z = 1,
					}
				},
			};
			vkCmdBlitImage(main_command_buffer, main_color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, current_swapchain_node->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_blit, VK_FILTER_NEAREST);
		}
		else
		{
			VkImageResolve region = {
				.srcSubresource = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = 0,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
				.srcOffset = {
					.x = 0, .y = 0, .z = 0,
				},
				.dstSubresource = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = 0,
					.baseArrayLayer = 0,
					.layerCount = 1,
				},
				.dstOffset = {
					.x = 0, .y = 0, .z = 0,
				},
				.extent = {
					.width  = (u32) renderer.width,
					.height = (u32) renderer.height,
					.depth = 1,
				}
			};
			vkCmdResolveImage(main_command_buffer, main_color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, current_swapchain_node->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		}

#if 0
		VkImageCopy image_copy = {
			.srcSubresource = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
			.srcOffset = { .x = 0, .y = 0, .z = 0, },

			.dstSubresource = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
			.dstOffset = { .x = 0, .y = 0, .z = 0, },

			.extent = { .width = renderer.width, .height = renderer.height, .depth = 1 }
		};

		vkCmdCopyImage(main_command_buffer, main_color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, current_swapchain_node->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_copy);
#endif
	}
	

	{
		VkImageMemoryBarrier present_image_barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.pNext = 0,
			
			.srcAccessMask = 0,
			.dstAccessMask = 0, // All memory is available after we wait for fences. Right? 
			
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			
			.image = current_swapchain_node->image,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};

		vkCmdPipelineBarrier(main_command_buffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			0,
			0, NULL,
			0, NULL,
			1, &present_image_barrier);
	}

	vkEndCommandBuffer(main_command_buffer);


	VkSubmitInfo submit_info;
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &main_command_buffer;
	submit_info.pNext = NULL;
	submit_info.signalSemaphoreCount = 0;
	submit_info.waitSemaphoreCount = 0;


	{
		ZoneScopedN("Send queue and wait for it");

		vkQueueSubmit(device_queue, 1, &submit_info, rendering_done_fence);
		
		vkWaitForFences(device, 1, &rendering_done_fence, VK_FALSE, u64_max);
		vkResetFences(device, 1, &rendering_done_fence);
	}

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 0;

	VkSwapchainKHR swapChains[] = { swapchain };
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains   = swapChains;
	presentInfo.pImageIndices = &current_swapchain_node->index;
	presentInfo.pResults = NULL;



	{
		ZoneScopedN("vkQueuePresentKHR");

		VkResult present_result = vkQueuePresentKHR(device_queue, &presentInfo);
		if (present_result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			// Frame that supposed to be presented won't be presented.
			swapchain_is_dead = true;
		}
	}


	{
		for (auto& item: imm_used_uniform_buffer_memory_allocations)
		{
			vkDestroyBuffer(device, item.buffer, host_allocator);
			vulkan_memory_allocator.free(item.memory);
		}

		imm_used_uniform_buffer_memory_allocations.clear();
	}
}

void Renderer::draw_level(Level* level)
{
	
}








bool Renderer::should_resize()
{
	return swapchain_is_dead;
}


void Renderer::resize(int new_width, int new_height)
{
	ZoneScoped;


	if (new_width  == width &&
		new_height == height) return;


	width = new_width;
	height = new_height;

	recreate_swapchain();
}




void Renderer::imm_recalculate_mask_buffer()
{
	ZoneScoped;

	imm_commands.add({
		.type = Imm_Command_Type::Recalculate_Mask_Buffer,

		.recalculate_mask_buffer = {
			.mask_stack = imm_mask_stack.copy_with(frame_allocator),
		}
	});
}



void Renderer::imm_draw_text(Font::Face* face, Unicode_String str, int x, int y, rgba color)
{
	ZoneScoped;

	auto iter = string_by_glyphs(str, face);
	int iter_previous_x = iter.x;
	while (iter.next())
	{
		defer{ iter_previous_x = iter.x; };

		if (!iter.render_glyph) continue;

		// :GlyphLocalCoords:
		imm_draw_glyph(&iter.current_glyph, x + iter_previous_x + iter.current_glyph.left_offset, y - (iter.current_glyph.height - iter.current_glyph.top_offset), color);
	}
}


void Renderer::imm_draw_text_culled(Font::Face* face, Unicode_String str, int x, int y, Rect cull_rect, rgba color)
{
	ZoneScoped;

	auto iter = string_by_glyphs(str, face);
	int iter_previous_x = iter.x;
	while (iter.next())
	{
		defer{ iter_previous_x = iter.x; };

		if (!iter.render_glyph) continue;

		int local_x =  x + iter_previous_x + iter.current_glyph.left_offset;
		if (local_x > cull_rect.x_right) // Assuming left-to-right text.
		{
			break;
		}

		imm_draw_glyph(&iter.current_glyph, local_x, y - (iter.current_glyph.height - iter.current_glyph.top_offset), color);

	}
}



void Renderer::imm_draw_texture(Rect rect, Texture* texture)
{
	ZoneScoped;

	imm_commands.add({
		.type = Imm_Command_Type::Draw_Texture,

		.draw_texture = {
			.rect  = rect,
			.texture_name = texture->name,
		}
	});
}

void Renderer::imm_draw_rect(Rect rect, rgba color)
{
	ZoneScoped;

	imm_commands.add({
		.type = Imm_Command_Type::Draw_Rect,

		.draw_rect = {
			.rect  = rect,
			.color = color
		}
	});
}


void Renderer::imm_draw_rect_with_alpha_fade(Rect rect, rgba color, int alpha_left, int alpha_right)
{
	ZoneScoped;

	imm_commands.add({
		.type = Imm_Command_Type::Draw_Faded_Rect,

		.draw_faded_rect = {
			.rect  = rect,
			.color = color,

			.alpha_left  = alpha_left,
			.alpha_right = alpha_right,
		}
	});
}

void Renderer::imm_draw_line(int x0, int y0, int x1, int y1, rgba color)
{
	ZoneScoped;

	imm_commands.add({
		.type = Imm_Command_Type::Draw_Line,

		.draw_line = {
			
			.x0 = x0,
			.y0 = y0,
			.x1 = x1,
			.y1 = y1,

			.color = color,
		}
	});
}



void Renderer::imm_draw_glyph(Glyph* glyph, int x, int y, rgba color)
{
	ZoneScoped;

	if (glyph->width == 0 || glyph->height == 0) return;


	imm_commands.add({
		.type = Imm_Command_Type::Draw_Glyph,

		.draw_glyph = {
			
			.glyph = *glyph,

			.x = x,
			.y = y,

			.color = color,
		}
	});
}


void Renderer::imm_execute_commands()
{
	ZoneScoped;

	/*
		The idea behind this renderer complication is to batch immediate mode calls.
	*/ 

	defer { imm_commands.clear(); };

#if DEBUG

	defer{
		assert(imm_commands.count);

		for (auto& command : imm_commands)
		{
			assert(command.is_executed);
		}
	};
#endif


	// Upload missing glyps to the GPU.
	{
		ZoneScopedN("Upload missing glyphs");

		for (Imm_Command& command: imm_commands)
		{			
			if (command.type != Imm_Command_Type::Draw_Glyph) continue;


			Glyph& glyph = command.draw_glyph.glyph;

			Glyph_Key glyph_key = Glyph_Key::make(glyph);

			Glyph_Gpu_Region* slot = glyph_gpu_map.get(glyph_key);

			if (!slot)
			{
				// Glyph is not on GPU.

				constexpr int atlas_size = 1024;

				if (glyph_atlasses.count == 0)
				{
					glyph_atlasses.add(Texture_Atlas::make(atlas_size));
				}


				Texture_Atlas* atlas = glyph_atlasses[glyph_atlasses.count - 1];

				int uv_x_left;
				int uv_y_bottom;

				if (!atlas->put_in(glyph.image_buffer, glyph.width, glyph.height, &uv_x_left, &uv_y_bottom))
				{
					atlas = glyph_atlasses.add(Texture_Atlas::make(atlas_size));

					bool result = atlas->put_in(glyph.image_buffer, glyph.width, glyph.height, &uv_x_left, &uv_y_bottom);
					assert(result);
				}

				slot  = glyph_gpu_map.put(glyph_key, {
					.atlas = atlas,
					.offset = Vector2i::make(uv_x_left, uv_y_bottom),
				});
			}

			assert(slot);

			command.draw_glyph.glyph_gpu_region = *slot;
		}
	}



	int batch_starting_command_index = -1;
	Texture_Atlas* current_atlas = NULL;

	auto flush_at = [&](int batch_ending_command_index)
	{
		ZoneScopedN("flush_at");

		if (batch_starting_command_index == -1)
			return;

		defer{ batch_starting_command_index = -1; };

		int instance_count = batch_ending_command_index - batch_starting_command_index + 1;

		if (instance_count == 0) return;

		begin_debug_marker("Execute batched draw", rgba(0, 150, 70, 255));
		defer { end_debug_marker(); };


		assert(instance_count > 0);


		vkCmdBindPipeline(main_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, imm_general_pipeline.pipeline);

		size_t uniform_buffer_size = instance_count * sizeof(Batched_Draw_Command_Block);

		VkBufferCreateInfo buffer_create_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.pNext = NULL,	
			.flags = 0,
			.size = uniform_buffer_size,
			.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.queueFamilyIndexCount = 0,
		};

		VkBuffer uniform_buffer;
		vkCreateBuffer(device, &buffer_create_info, host_allocator, &uniform_buffer);

		auto memory = vulkan_memory_allocator.allocate_and_bind(uniform_buffer, VULKAN_MEMORY_SHOULD_BE_MAPPABLE, code_location());


		// Upload uniform data.
		{
			void* data;
			vkMapMemory(device, memory.device_memory, memory.offset, memory.size, 0, &data);

			for (int i = 0; i < instance_count; i++)
			{
				Batched_Draw_Command_Block* block = ((Batched_Draw_Command_Block*) data) + i;

				Imm_Command* command = imm_commands[batch_starting_command_index + i];

			#if DEBUG
				command->is_executed = true;
			#endif

				block->screen_size = {
					renderer.width,
					renderer.height,
				};

				switch (command->type)
				{
					case Imm_Command_Type::Draw_Rect:
					{
						block->draw_type = DRAW_TYPE_RECT;

						block->rect = Vector4i::make(
							command->draw_rect.rect.x_left,
							command->draw_rect.rect.y_bottom,
							command->draw_rect.rect.x_right,
							command->draw_rect.rect.y_top
						);
						block->color = Vector4::make(
							command->draw_rect.color.r,
							command->draw_rect.color.g,
							command->draw_rect.color.b,
							command->draw_rect.color.a
						);
					}
					break;
					case Imm_Command_Type::Draw_Faded_Rect:
					{
						block->draw_type = DRAW_TYPE_FADED_RECT;


						block->rect = Vector4i::make(
							command->draw_faded_rect.rect.x_left,
							command->draw_faded_rect.rect.y_bottom,
							command->draw_faded_rect.rect.x_right,
							command->draw_faded_rect.rect.y_top
						);
						block->color = Vector4::make(
							command->draw_faded_rect.color.r,
							command->draw_faded_rect.color.g,
							command->draw_faded_rect.color.b,
							command->draw_faded_rect.color.a
						);


						block->faded_rect_left_alpha  = float(command->draw_faded_rect.alpha_left)  / 255.0f;
						block->faded_rect_right_alpha = float(command->draw_faded_rect.alpha_right) / 255.0f; 
					}
					break;
					case Imm_Command_Type::Draw_Glyph:
					{
						block->draw_type = DRAW_TYPE_GLYPH;

						auto& glyph = command->draw_glyph.glyph;

						block->rect = Vector4i::make(
							command->draw_glyph.x,
							command->draw_glyph.y,
							command->draw_glyph.x + glyph.width,
							command->draw_glyph.y + glyph.height
						);
						
						block->color = Vector4::make(
							command->draw_glyph.color.r,
							command->draw_glyph.color.g,
							command->draw_glyph.color.b,
							command->draw_glyph.color.a
						);


						Glyph_Gpu_Region& glyph_gpu_region = command->draw_glyph.glyph_gpu_region;

						auto atlas = glyph_gpu_region.atlas;
						assert(atlas == current_atlas);

						block->atlas_x_left   = float(glyph_gpu_region.offset.x) / float(atlas->size);
						block->atlas_y_bottom = float(glyph_gpu_region.offset.y) / float(atlas->size);

						block->atlas_x_right  = float(glyph_gpu_region.offset.x + glyph.width)  / float(atlas->size);
						block->atlas_y_top    = float(glyph_gpu_region.offset.y + glyph.height) / float(atlas->size);
						
					}
					break;

					default:
						assert(false);
				}
			}

			vkUnmapMemory(device, memory.device_memory);
		}

	
		imm_used_uniform_buffer_memory_allocations.add({
			.buffer = uniform_buffer,
			.memory = memory,
		});

		auto descriptor_set = imm_get_descriptor_set(imm_general_pipeline.descriptor_set_layout);
	
		// Push uniform buffer to descriptor set.
		{
			

			VkDescriptorBufferInfo buffer_info = {
				.buffer = uniform_buffer,
				.offset = 0,
				.range = VK_WHOLE_SIZE
			};

			VkDescriptorImageInfo image_info = {
				.sampler   = current_atlas ? current_atlas->image_sampler : white_texture.image_sampler,
			    .imageView = current_atlas ? current_atlas->image_view : white_texture.image_view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			};

			VkWriteDescriptorSet write_infos[] = {
				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.pNext = NULL,
					.dstSet = descriptor_set,
					.dstBinding = 0,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					.pBufferInfo = &buffer_info,
				},

				{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.pNext = NULL,
					.dstSet = descriptor_set,
					.dstBinding = 1,
					.dstArrayElement = 0,
					.descriptorCount = 1,
					.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					.pImageInfo = &image_info,
				},
			};

			{
				ZoneScopedN("vkUpdateDescriptorSets");
				vkUpdateDescriptorSets(device, array_count(write_infos), write_infos, 0, NULL);
			}

			{
				ZoneScopedN("vkCmdBindDescriptorSets");
				vkCmdBindDescriptorSets(main_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, imm_general_pipeline.pipeline_layout, 0, 1, &descriptor_set, 0, NULL);
			}
		}


		{
			ZoneScopedN("vkCmdDrawIndexed");
		    vkCmdDraw(main_command_buffer, 6, instance_count, 0, 0);
		}	
	};


	defer {
		flush_at(imm_commands.count - 1);
	};


	#ifdef TRACY_ENABLE
		String commands_count_str = to_string(imm_commands.count, frame_allocator);
		ZoneText(commands_count_str.data, commands_count_str.length);
	#endif

	for (Imm_Command& command: imm_commands)
	{
		int index = imm_commands.index_of(&command);

		switch (command.type)
		{
			case Imm_Command_Type::Recalculate_Mask_Buffer:
			{
				flush_at(index - 1);

			#if DEBUG
				command.is_executed = true;
			#endif


				begin_debug_marker("Recalculate mask buffer", rgba(150, 0, 0, 255));
				defer { end_debug_marker(); };

			
				vkCmdBindPipeline(main_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, imm_clear_mask_pipeline.pipeline);

				Mask_Uniform_Block uniform = {
					.screen_size = {
						renderer.width,
						renderer.height
					},
					
					.rect = Vector4i::make(0, 0, renderer.width, renderer.height),
				};
				vkCmdPushConstants(main_command_buffer, imm_clear_mask_pipeline.pipeline_layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(uniform), &uniform);
			    vkCmdDraw(main_command_buffer, 6, 1, 0, 0);


				vkCmdBindPipeline(main_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, imm_inversed_mask_pipeline.pipeline);

				for (auto& mask: command.recalculate_mask_buffer.mask_stack)
				{
					// :MaskStackOuterRects
					Rect left_rect = Rect::make(
						0,
						0,
						mask.rect.x_left,
						renderer.height
					);

					Rect bottom_rect = Rect::make(
						mask.rect.x_left,
						0,
						mask.rect.x_right,
						mask.rect.y_bottom
					);
					
					Rect right_rect = Rect::make(
						mask.rect.x_right,
						0,
						renderer.width,
						renderer.height
					);

					Rect top_rect = Rect::make(
						mask.rect.x_left,
						mask.rect.y_top,
						mask.rect.x_right,
						renderer.height
					);



					auto draw_rect = [&](Rect rect)
					{
						Mask_Uniform_Block uniform = {
							.screen_size = {
								renderer.width,
								renderer.height
							},
							
							.rect = Vector4i::make(rect.x_left, rect.y_bottom, rect.x_right, rect.y_top),
						};
						
						vkCmdPushConstants(main_command_buffer, imm_inversed_mask_pipeline.pipeline_layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(uniform), &uniform);
					    vkCmdDraw(main_command_buffer, 6, 1, 0, 0);
					};


					if (mask.inversed)
					{
						draw_rect(mask.rect);
					}
					else
					{
						draw_rect(left_rect);
						draw_rect(right_rect);
						draw_rect(top_rect);
						draw_rect(bottom_rect);
					}
				}
			}
			break;

			case Imm_Command_Type::Draw_Line:
			{
				flush_at(index - 1);

			#if DEBUG
				command.is_executed = true;
			#endif

				begin_debug_marker("Draw line", rgba(0, 150, 80, 255));
				defer { end_debug_marker(); };

				vkCmdBindPipeline(main_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, imm_line_pipeline.pipeline);

				Line_Uniform_Block uniform = {
					.screen_size = {
						renderer.width,
						renderer.height
					},
					
					.line_start = Vector2i::make(command.draw_line.x0, command.draw_line.y0),
					.line_end   = Vector2i::make(command.draw_line.x1, command.draw_line.y1),

					.color = {
						float(command.draw_line.color.r) / 255,
						float(command.draw_line.color.g) / 255,
						float(command.draw_line.color.b) / 255,
						float(command.draw_line.color.a) / 255,			
					},
				};

				vkCmdPushConstants(main_command_buffer, imm_line_pipeline.pipeline_layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(uniform), &uniform);

				vkCmdDraw(main_command_buffer, 2, 1, 0, 0);
			}
			break;

			case Imm_Command_Type::Draw_Texture:
			{
				flush_at(index - 1);

			#if DEBUG
				command.is_executed = true;
			#endif

				begin_debug_marker("Draw texture", rgba(0, 150, 80, 255));
				defer { end_debug_marker(); };

				vkCmdBindPipeline(main_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, imm_texture_pipeline.pipeline);

				// Push texture to descriptor set.
				{
					Texture* texture = asset_storage.find_texture(command.draw_texture.texture_name);
					if (!texture)
					{
						break;
					}

					make_sure_texture_is_on_gpu(texture);

					auto descriptor_set = imm_get_descriptor_set(imm_texture_pipeline.descriptor_set_layout);
	
					VkDescriptorImageInfo image_info = {
						.sampler   = texture->image_sampler,
					    .imageView = texture->image_view,
						.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					};

					VkWriteDescriptorSet write_infos[] = {
						{
							.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
							.pNext = NULL,
							.dstSet = descriptor_set,
							.dstBinding = 0,
							.dstArrayElement = 0,
							.descriptorCount = 1,
							.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
							.pImageInfo = &image_info,
						},
					};

					{
						ZoneScopedN("vkUpdateDescriptorSets");
						vkUpdateDescriptorSets(device, array_count(write_infos), write_infos, 0, NULL);
					}

					{
						ZoneScopedN("vkCmdBindDescriptorSets");
						vkCmdBindDescriptorSets(main_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, imm_texture_pipeline.pipeline_layout, 0, 1, &descriptor_set, 0, NULL);
					}
				}


				Rect& rect = command.draw_texture.rect;

				Texture_Uniform_Block uniform = {
					.screen_size = {
						renderer.width,
						renderer.height
					},
					
					.rect = Vector4i::make(rect.x_left, rect.y_bottom, rect.x_right, rect.y_top),
				};
				
				vkCmdPushConstants(main_command_buffer, imm_texture_pipeline.pipeline_layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(uniform), &uniform);
				vkCmdDraw(main_command_buffer, 6, 1, 0, 0);
			}
			break;

			default:
			{
				assert(
					command.type == Imm_Command_Type::Draw_Rect ||
					command.type == Imm_Command_Type::Draw_Glyph ||
					command.type == Imm_Command_Type::Draw_Faded_Rect);


				if (batch_starting_command_index == -1)
				{
					batch_starting_command_index = index;
				}


				int number_of_batched_commands = index - batch_starting_command_index;

				if (number_of_batched_commands >= max_batched_commands)
				{
					flush_at(index - 1);
				}
				else if (command.type == Imm_Command_Type::Draw_Glyph)
				{
					Glyph_Gpu_Region& glyph_gpu_region = command.draw_glyph.glyph_gpu_region;

					if (glyph_gpu_region.atlas != current_atlas)
					{
						flush_at(index - 1);
					}
				}


				if (command.type == Imm_Command_Type::Draw_Glyph)
				{
					Glyph_Gpu_Region& glyph_gpu_region = command.draw_glyph.glyph_gpu_region;

					current_atlas = glyph_gpu_region.atlas;
				}


				if (batch_starting_command_index == -1)
				{
					batch_starting_command_index = index;
				}
			}
		}
	}
}





Texture_Atlas Texture_Atlas::make(int size)
{
	ZoneScoped;

#if OS_DARWIN
	VkFormat format = VK_FORMAT_R8_UNORM;
#else
	VkFormat format = VK_FORMAT_R8_SRGB;
#endif

	VkImage image;
	Vulkan_Memory_Allocation image_memory;

	VkImageCreateInfo imageInfo = {
    	.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    	.imageType = VK_IMAGE_TYPE_2D,
    	.format = format,
    	.extent = {
    		.width  = (u32) size,
    		.height = (u32) size,
    		.depth  = 1,
    	},
    	.mipLevels = 1,
    	.arrayLayers = 1,
    	.samples = VK_SAMPLE_COUNT_1_BIT,
    	.tiling = VK_IMAGE_TILING_OPTIMAL,
    	.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    	.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    	.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

    vkCreateImage(renderer.device, &imageInfo, renderer.host_allocator, &image);
    image_memory = vulkan_memory_allocator.allocate_and_bind(image, (Vulkan_Memory_Allocation_Flags) 0, code_location());


    VkImageViewCreateInfo image_view_info = {
    	.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    	.image = image,
    	.viewType = VK_IMAGE_VIEW_TYPE_2D,
    	.format = format,
    	.subresourceRange = {
    		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    		.baseMipLevel = 0,
    		.levelCount = 1,
    		.baseArrayLayer = 0,
    		.layerCount = 1,
    	}
	};

    VkImageView image_view;
    vkCreateImageView(renderer.device, &image_view_info, renderer.host_allocator, &image_view);


    VkSampler image_sampler;
    VkSamplerCreateInfo sampler_info = {
    	.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    	.magFilter = VK_FILTER_LINEAR,
    	.minFilter = VK_FILTER_LINEAR,
    	.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    	.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    	.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    	.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    	.anisotropyEnable = VK_FALSE,
    	.maxAnisotropy = 16.0f,
    	.compareEnable = VK_FALSE,
    	.compareOp = VK_COMPARE_OP_ALWAYS,
    	.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE,
    	.unnormalizedCoordinates = VK_FALSE,
	};

    vkCreateSampler(renderer.device, &sampler_info, renderer.host_allocator, &image_sampler);


    decltype(Texture_Atlas::free_rects) free_rects = make_array<Rect>(32, c_allocator);


    free_rects.add(Rect::make(0, 0, size, size));

    return {
    	.image = image,
    	.image_view = image_view,
    	.image_sampler = image_sampler,
    	.image_memory = image_memory,
    	.size = size,
    	.free_rects = free_rects,
    };
}


bool Texture_Atlas::put_in(void* image_buffer, int image_width, int image_height, int* out_uv_x_left, int* out_uv_y_bottom)
{
	ZoneScoped;


	int found_x_left;
	int found_y_bottom;

	// Find region to occupy
	{
		ZoneScopedN("Find free atlas region");

		String str = to_string(free_rects.count, frame_allocator);
		ZoneText(str.data, str.length);

		// Rects are guaranteed to go one after another to the right.

		constexpr bool perform_sanity_checks = false;


		auto validate_free_rects_indices = [&]()
		{
			if constexpr (perform_sanity_checks)
			{
				return;
			}
		
			for (int i = 1; i < free_rects.count; i++)
			{
				assert(free_rects[i]->x_left == free_rects[i - 1]->x_right);
			}
		};


		/*
			if free_rects.count = 100

			then, this algorithm will do 100 + 99 +  98 + 97 ... iterations.
		*/


		auto find_region = [&](int starting_index, int* out_starting_rect_index, int* out_ending_rect_index, int* out_width_sum, int* out_y_top) -> bool
		{
			int leftmost_rect_with_enough_height = -1;
			int minimum_y_top = s32_max;

			for (int i = starting_index; i < free_rects.count; i++)
			{
				Rect rect = *free_rects[i];
				assert(rect.y_bottom == 0);

				validate_free_rects_indices();

				if (rect.y_top >= image_height)
				{
					if (leftmost_rect_with_enough_height == -1)
					{
						leftmost_rect_with_enough_height = i;
					}
					

					minimum_y_top = min(minimum_y_top, rect.y_top);

				#if DEBUG
					if constexpr (perform_sanity_checks)
					{
						for (int j = leftmost_rect_with_enough_height; j <= i; j++)
						{
							assert(free_rects[j]->y_top >= minimum_y_top);
						}
					}
				#endif


					int x_left = free_rects[leftmost_rect_with_enough_height]->x_left;
					
					int width = rect.x_right - x_left;

					if (width >= image_width)
					{
						// Found enough space.

						*out_y_top = minimum_y_top;

						*out_starting_rect_index = leftmost_rect_with_enough_height;
						*out_ending_rect_index = i;
						*out_width_sum = width;

						return true;
					}
					else
					{
						// Keep going
					}
				}
				else
				{
					// Rect had not had enough height, so we begin searching height from next rectangles.
					leftmost_rect_with_enough_height = -1;
					minimum_y_top = s32_max;
				}
			}

			return false;
		};



		bool does_best_exist = false;

		int best_y_top;
		int best_starting_rect_index;
		int best_ending_rect_index;
		int best_width_sum;

		for (int i = 0; i < free_rects.count; i++)
		{
			int starting_rect_index;
			int ending_rect_index;
			int width_sum;

			int new_y_top;

			if (find_region(i, &starting_rect_index, &ending_rect_index, &width_sum, &new_y_top))
			{
				i = starting_rect_index;

				bool should_write = !does_best_exist || (new_y_top > best_y_top);
				if (should_write)
				{
					best_y_top = new_y_top;

					best_starting_rect_index = starting_rect_index;
					best_ending_rect_index   = ending_rect_index;
					best_width_sum = width_sum;

					does_best_exist = true;
				}
			}
		}




		if (does_best_exist)
		{
		#if DEBUG
			if constexpr (perform_sanity_checks)
			{
				// Making sure, that resulting rectangle is in free region.

				for (int i = best_starting_rect_index; i <= best_ending_rect_index; i++)
				{
					Rect rect = *free_rects[i];

					assert(best_y_top <= rect.y_top);
				}
			}
		#endif

			best_y_top -= image_height;


			Rect last_rect = *free_rects[best_ending_rect_index];


			for (int i = best_starting_rect_index; i <= best_ending_rect_index; i++)
			{
				free_rects[i]->y_top = best_y_top;
			}

			// Handle last rect, if it has pixels left, split it.
			if (image_width < best_width_sum)
			{
				int new_x_border = last_rect.x_right - (best_width_sum - image_width);

				free_rects.add_at_index(best_ending_rect_index + 1, Rect::make(new_x_border, 0, last_rect.x_right, last_rect.y_top));
				
				free_rects[best_ending_rect_index]->x_right = new_x_border;

				validate_free_rects_indices();
			}

			found_x_left   = free_rects[best_starting_rect_index]->x_left;
			found_y_bottom = best_y_top;

		#if DEBUG
			if constexpr (perform_sanity_checks)
			{
				// Making sure, that resulting rectangle is in now in occupied region.

				for (int i = best_starting_rect_index; i <= best_ending_rect_index; i++)
				{
					Rect rect = *free_rects[i];

					assert(best_y_top >= rect.y_top);
				}
			}
		#endif


			// Merge consecutive rects with the same height.
			{
				int merge_start = -1;
				int merge_start_y_top;

				for (int i = 0; i < free_rects.count; i++)
				{
					Rect rect = free_rects.data[i];

					if (merge_start == -1)
					{
						merge_start = i;
						merge_start_y_top = rect.y_top;

						continue;
					}
					else
					{
						if (rect.y_top != merge_start_y_top)
						{
							free_rects[merge_start]->x_right = free_rects[i - 1]->x_right;

							int remove_count = i - merge_start - 1;

							free_rects.remove_range(merge_start + 1, remove_count);

							i -= remove_count;

							merge_start = i;
							merge_start_y_top = rect.y_top;
						}
						else
						{
							continue;
						}	
					}
				}
			}


			validate_free_rects_indices();
		}
		else
		{
			return false;
		}
	}


	*out_uv_x_left   = found_x_left;
	*out_uv_y_bottom = found_y_bottom;


	assert(found_x_left >= 0);
	assert(found_x_left <= size - image_width);
	assert(found_y_bottom <= size - image_height);
	assert(found_y_bottom >= 0);



	{
		ZoneScopedN("Upload texture to atlas");


	#if OS_DARWIN
		VkFormat format = VK_FORMAT_R8_UNORM;
	#else
		VkFormat format = VK_FORMAT_R8_SRGB;
	#endif

		VkDeviceSize image_size = image_width * image_height;
			
		VkBuffer staging_buffer;
		VkBufferCreateInfo create_info = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.pNext = NULL,
			.flags = 0,
			.size = image_size,
			.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
			.queueFamilyIndexCount = 0,
		};
		vkCreateBuffer(renderer.device, &create_info, renderer.host_allocator, &staging_buffer);
		Vulkan_Memory_Allocation staging_buffer_memory = vulkan_memory_allocator.allocate_and_bind(staging_buffer, VULKAN_MEMORY_SHOULD_BE_MAPPABLE, code_location());

		{
			void* data;
		    vkMapMemory(renderer.device, staging_buffer_memory.device_memory, staging_buffer_memory.offset, image_size, 0, &data);
			memcpy(data, image_buffer, image_size);
		    vkUnmapMemory(renderer.device, staging_buffer_memory.device_memory);
		}


		renderer.transition_image_layout(image, format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		renderer.copy_buffer_to_image(staging_buffer, image, image_width, image_height, found_x_left, found_y_bottom);
		renderer.transition_image_layout(image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);


		vkDestroyBuffer(renderer.device, staging_buffer, renderer.host_allocator);
		vulkan_memory_allocator.free(staging_buffer_memory);
	}

    return true;
}


void Renderer::recreate_swapchain()
{
	ZoneScoped;

	// log(ctx.logger, U"Vulkan::recreate_swapchain. frame_index =  %", frame_index);
	
	vkDeviceWaitIdle(device);


	if (swapchain != VK_NULL_HANDLE)
	{
		vkDestroyFramebuffer(device, main_framebuffer, host_allocator);
		vkDestroyRenderPass(device, main_render_pass, host_allocator);

		vkFreeCommandBuffers(device, command_pool, 1, &main_command_buffer);


		vkDestroyImage(device, main_color_image, host_allocator);
		vkDestroyImage(device, main_depth_stencil_image, host_allocator);

		vkDestroyImageView(device, main_color_image_view, host_allocator);
		vkDestroyImageView(device, main_depth_stencil_image_view, host_allocator);


		vulkan_memory_allocator.free(main_color_image_memory);
		vulkan_memory_allocator.free(main_depth_stencil_image_memory);

		// swapchain is used as oldSwapchain in create_swapchain() below,
		//   and then destroyed as well destroyed there.
	}


	create_swapchain();
	create_main_framebuffer();
}



void Renderer::transition_image_layout(VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout)
{
	// ZoneScoped;

    VkCommandBuffer commandBuffer = begin_single_command_buffer();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
	else if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
	else
	{
        assert(false && "unsupported layout transition!");
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    end_single_command_buffer(commandBuffer);
}

void Renderer::copy_buffer_to_image(VkBuffer buffer, VkImage image, u32 width, u32 height, u32 dst_offset_x, u32 dst_offset_y)
{
	// ZoneScoped;

    VkCommandBuffer commandBuffer = begin_single_command_buffer();

    VkBufferImageCopy region = {
    	.bufferOffset = 0,
    	.bufferRowLength = 0,
    	.bufferImageHeight = 0,

    	.imageSubresource = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1,
    	},
    	.imageOffset = {
    		(s32) dst_offset_x, 
    		(s32) dst_offset_y, 
    		0
    	},
    	.imageExtent = {
			width,
			height,
			1
		},
    };

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    end_single_command_buffer(commandBuffer);
}


VkCommandBuffer Renderer::begin_single_command_buffer()
{
	ZoneScoped;

	VkCommandBufferAllocateInfo allocInfo = {
    	.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    	.commandPool = command_pool,
    	.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    	.commandBufferCount = 1,
	};

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {
    	.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    	.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void Renderer::end_single_command_buffer(VkCommandBuffer command_buffer)
{
	ZoneScoped;

	vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submitInfo = {
    	.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    	.commandBufferCount = 1,
    	.pCommandBuffers = &command_buffer,
    };


    vkQueueSubmit(device_queue, 1, &submitInfo, VK_NULL_HANDLE);

	vkQueueWaitIdle(device_queue);

    vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
}
