#include "Tracy_Header.h"


#include "Renderer_Vulkan.h"

#include "Main.h"

#if OS_DARWIN
#include <vulkan/vulkan_metal.h>
#endif


void Vulkan::execute_commands()
{
	ZoneScoped;

	/*
		The idea behind this renderer complication is to batch
			Draw_Rect, Draw_Glyph and Draw_Faded_Rect draw types.
	*/ 

	defer { commands.clear(); };

#if DEBUG

	defer{
		assert(commands.count);

		for (auto& command : commands)
		{
			assert(command.is_executed);
		}
	};
#endif


	// Upload missing glyps to the GPU.
	{
		ZoneScopedN("Upload missing glyphs");

		for (Vulkan_Command& command: commands)
		{			
			if (command.type != Vulkan_Command_Type::Draw_Glyph) continue;


			Glyph& glyph = command.draw_glyph.glyph;

			Glyph_Key glyph_key = Glyph_Key::make(glyph);

			Glyph_Value* slot = glyphs.get(glyph_key);

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

				slot  = glyphs.put(glyph_key, {
					.atlas = atlas,
					.offset = Vector2i::make(uv_x_left, uv_y_bottom),
				});
			}

			assert(slot);

			command.draw_glyph.glyph_value = *slot;
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


		bind_pipeline(&general_pipeline);

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

				Vulkan_Command* command = commands[batch_starting_command_index + i];

			#if DEBUG
				command->is_executed = true;
			#endif

				block->screen_size = {
					renderer.framebuffer_width,
					renderer.framebuffer_height,
				};

				switch (command->type)
				{
					case Vulkan_Command_Type::Draw_Rect:
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
					case Vulkan_Command_Type::Draw_Faded_Rect:
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
					case Vulkan_Command_Type::Draw_Glyph:
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


						Glyph_Value& glyph_value = command->draw_glyph.glyph_value;

						auto atlas = glyph_value.atlas;
						assert(atlas == current_atlas);

						block->atlas_x_left   = float(glyph_value.offset.x) / float(atlas->size);
						block->atlas_y_bottom = float(glyph_value.offset.y) / float(atlas->size);

						block->atlas_x_right  = float(glyph_value.offset.x + glyph.width)  / float(atlas->size);
						block->atlas_y_top    = float(glyph_value.offset.y + glyph.height) / float(atlas->size);
						
					}
					break;

					default:
						assert(false);
				}
			}

			vkUnmapMemory(device, memory.device_memory);
		}

	
		used_uniform_buffer_memory_allocations.add({
			.buffer = uniform_buffer,
			.memory = memory,
		});

		auto descriptor_set = get_descriptor_set(general_pipeline.descriptor_set_layout);
	
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
				vkCmdBindDescriptorSets(main_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, general_pipeline.pipeline_layout, 0, 1, &descriptor_set, 0, NULL);
			}
		}


		{
			ZoneScopedN("vkCmdDrawIndexed");
		    vkCmdDraw(main_command_buffer, 6, instance_count, 0, 0);
		}	
	};


	defer {
		flush_at(commands.count - 1);
	};


	#ifdef TRACY_ENABLE
		String commands_count_str = to_string(commands.count, frame_allocator);
		ZoneText(commands_count_str.data, commands_count_str.length);
	#endif

	for (Vulkan_Command& command: commands)
	{
		int index = commands.index_of(&command);

		switch (command.type)
		{
			case Vulkan_Command_Type::Recalculate_Mask_Buffer:
			{
				flush_at(index - 1);

			#if DEBUG
				command.is_executed = true;
			#endif


				begin_debug_marker("Recalculate mask buffer", rgba(150, 0, 0, 255));
				defer { end_debug_marker(); };

			
				currently_bound_pipeline = NULL;

				vkCmdBindPipeline(main_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, clear_mask_pipeline.pipeline);

				Mask_Uniform_Block uniform = {
					.screen_size = {
						renderer.framebuffer_width,
						renderer.framebuffer_height
					},
					
					.rect = Vector4i::make(0, 0, renderer.framebuffer_width, renderer.framebuffer_height),
				};
				vkCmdPushConstants(main_command_buffer, clear_mask_pipeline.pipeline_layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(uniform), &uniform);
			    vkCmdDraw(main_command_buffer, 6, 1, 0, 0);


				vkCmdBindPipeline(main_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, inversed_mask_pipeline.pipeline);

				for (Renderer::Mask& mask: command.recalculate_mask_buffer.mask_stack)
				{
					// :MaskStackOuterRects
					Rect left_rect = Rect::make(
						0,
						0,
						mask.rect.x_left,
						renderer.framebuffer_height
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
						renderer.framebuffer_width,
						renderer.framebuffer_height
					);

					Rect top_rect = Rect::make(
						mask.rect.x_left,
						mask.rect.y_top,
						mask.rect.x_right,
						renderer.framebuffer_height
					);



					auto draw_rect = [&](Rect rect)
					{
						Mask_Uniform_Block uniform = {
							.screen_size = {
								renderer.framebuffer_width,
								renderer.framebuffer_height
							},
							
							.rect = Vector4i::make(rect.x_left, rect.y_bottom, rect.x_right, rect.y_top),
						};
						
						vkCmdPushConstants(main_command_buffer, inversed_mask_pipeline.pipeline_layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(uniform), &uniform);
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

			case Vulkan_Command_Type::Draw_Line:
			{
				flush_at(index - 1);

			#if DEBUG
				command.is_executed = true;
			#endif

				begin_debug_marker("Draw line", rgba(0, 150, 80, 255));
				defer { end_debug_marker(); };

				bind_pipeline(&line_pipeline);

				Line_Uniform_Block uniform = {
					.screen_size = {
						renderer.framebuffer_width,
						renderer.framebuffer_height
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

				vkCmdPushConstants(main_command_buffer, line_pipeline.pipeline_layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(uniform), &uniform);

				vkCmdDraw(main_command_buffer, 2, 1, 0, 0);
			}
			break;

			case Vulkan_Command_Type::Draw_Texture:
			{
				flush_at(index - 1);

			#if DEBUG
				command.is_executed = true;
			#endif

				begin_debug_marker("Draw texture", rgba(0, 150, 80, 255));
				defer { end_debug_marker(); };

				bind_pipeline(&texture_pipeline);

				// Push texture to descriptor set.
				{
					Texture* texture = renderer.find_texture(command.draw_texture.texture_name);
					if (!texture)
					{
						break;
					}

					make_sure_texture_is_on_gpu(texture);

					auto descriptor_set = get_descriptor_set(texture_pipeline.descriptor_set_layout);
	
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
						vkCmdBindDescriptorSets(main_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, texture_pipeline.pipeline_layout, 0, 1, &descriptor_set, 0, NULL);
					}
				}


				Rect& rect = command.draw_texture.rect;

				Texture_Uniform_Block uniform = {
					.screen_size = {
						renderer.framebuffer_width,
						renderer.framebuffer_height
					},
					
					.rect = Vector4i::make(rect.x_left, rect.y_bottom, rect.x_right, rect.y_top),
				};
				
				vkCmdPushConstants(main_command_buffer, texture_pipeline.pipeline_layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(uniform), &uniform);
				vkCmdDraw(main_command_buffer, 6, 1, 0, 0);
			}
			break;

			default:
			{
				assert(
					command.type == Vulkan_Command_Type::Draw_Rect ||
					command.type == Vulkan_Command_Type::Draw_Glyph ||
					command.type == Vulkan_Command_Type::Draw_Faded_Rect);


				if (batch_starting_command_index == -1)
				{
					batch_starting_command_index = index;
				}


				int number_of_batched_commands = index - batch_starting_command_index;

				if (number_of_batched_commands >= max_batched_commands)
				{
					flush_at(index - 1);
				}
				else if (command.type == Vulkan_Command_Type::Draw_Glyph)
				{
					Glyph_Value& glyph_value = command.draw_glyph.glyph_value;

					if (glyph_value.atlas != current_atlas)
					{
						flush_at(index - 1);
					}
				}


				if (command.type == Vulkan_Command_Type::Draw_Glyph)
				{
					Glyph_Value& glyph_value = command.draw_glyph.glyph_value;

					current_atlas = glyph_value.atlas;
				}


				if (batch_starting_command_index == -1)
				{
					batch_starting_command_index = index;
				}
			}
		}
	}
}




void Vulkan::draw_texture(Rect rect, Texture* texture)
{
	commands.add({
		.type = Vulkan_Command_Type::Draw_Texture,

		.draw_texture = {
			.rect  = rect,
			.texture_name = texture->name,
		}
	});
}

void Vulkan::draw_rect(Rect rect, rgba color)
{
	// ZoneScoped;

	commands.add({
		.type = Vulkan_Command_Type::Draw_Rect,

		.draw_rect = {
			.rect  = rect,
			.color = color
		}
	});
}


void Vulkan::draw_faded_rect(Rect rect, rgba color, int alpha_left, int alpha_right)
{
	// ZoneScoped;

	commands.add({
		.type = Vulkan_Command_Type::Draw_Faded_Rect,

		.draw_faded_rect = {
			.rect  = rect,
			.color = color,

			.alpha_left  = alpha_left,
			.alpha_right = alpha_right,
		}
	});
}

void Vulkan::draw_line(int x0, int y0, int x1, int y1, rgba color)
{
	commands.add({
		.type = Vulkan_Command_Type::Draw_Line,

		.draw_line = {
			
			.x0 = x0,
			.y0 = y0,
			.x1 = x1,
			.y1 = y1,

			.color = color,
		}
	});
}



void Vulkan::draw_glyph(Glyph* glyph, int x, int y, rgba color)
{
	// ZoneScoped;

	if (glyph->width == 0 || glyph->height == 0) return;


	commands.add({
		.type = Vulkan_Command_Type::Draw_Glyph,

		.draw_glyph = {
			
			.glyph = *glyph,

			.x = x,
			.y = y,

			.color = color,
		}
	});
}


bool Vulkan::bind_pipeline(Pipeline* pipeline)
{
	ZoneScoped;

	if (currently_bound_pipeline != pipeline)
	{
		ZoneScopedN("vkCmdBindPipeline");
		currently_bound_pipeline = pipeline;
		vkCmdBindPipeline(main_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);

		return true;
	}

	return false;
}

void Vulkan::recalculate_mask_buffer(Dynamic_Array<Renderer::Mask> mask_stack)
{
	ZoneScoped;

	commands.add({
		.type = Vulkan_Command_Type::Recalculate_Mask_Buffer,

		.recalculate_mask_buffer = {
			.mask_stack = mask_stack.copy_with(frame_allocator)
		}
	});
}





