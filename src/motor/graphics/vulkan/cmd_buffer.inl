static void begin_cmd_buffer(MtCmdBuffer *cb)
{
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(cb->cmd_buffer, &begin_info));
}

static void end_cmd_buffer(MtCmdBuffer *cb)
{
    VK_CHECK(vkEndCommandBuffer(cb->cmd_buffer));
    memset(cb->bound_descriptor_set_hashes, 0, sizeof(cb->bound_descriptor_set_hashes));
    memset(&cb->current_viewport, 0, sizeof(cb->current_viewport));

    for (BufferBlock *block = cb->ubo_blocks;
         block != cb->ubo_blocks + mt_array_size(cb->ubo_blocks);
         ++block)
    {
        buffer_block_reset(block);
    }

    for (BufferBlock *block = cb->vbo_blocks;
         block != cb->vbo_blocks + mt_array_size(cb->vbo_blocks);
         ++block)
    {
        buffer_block_reset(block);
    }

    for (BufferBlock *block = cb->ibo_blocks;
         block != cb->ibo_blocks + mt_array_size(cb->ibo_blocks);
         ++block)
    {
        buffer_block_reset(block);
    }
}

static void get_viewport(MtCmdBuffer *cb, MtViewport *viewport)
{
    *viewport = cb->current_viewport;
}

static void bind_descriptor_sets(MtCmdBuffer *cb)
{
    assert(cb->bound_pipeline_instance);

    for (uint32_t i = 0; i < cb->bound_pipeline_instance->pipeline->layout->set_count; i++)
    {
        uint32_t binding_count =
            cb->bound_pipeline_instance->pipeline->layout->sets[i].binding_count;
        assert(binding_count > 0);

        XXH64_state_t state = {0};
        XXH64_update(&state, &cb->bound_descriptors[i][0], sizeof(Descriptor) * binding_count);
        uint64_t descriptors_hash = (uint64_t)XXH64_digest(&state);

        uint32_t dynamic_offsets[MAX_DESCRIPTOR_BINDINGS];
        uint32_t dynamic_offset_count = 0;

        state = (XXH64_state_t){0};
        for (uint32_t b = 0; b < binding_count; ++b)
        {
            if (cb->bound_pipeline_instance->pipeline->layout->sets[i].bindings[b].descriptorType ==
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
            {
                XXH64_update(&state, &cb->dynamic_offsets[i][b], sizeof(cb->dynamic_offsets[i][b]));
                dynamic_offsets[dynamic_offset_count++] = cb->dynamic_offsets[i][b];
            }
        }
        uint64_t dynamic_offset_hash = (uint64_t)XXH64_digest(&state);

        if (cb->bound_descriptor_set_hashes[i] != descriptors_hash ||
            (dynamic_offset_count > 0 && dynamic_offset_hash != cb->dynamic_offset_hashes[i]))
        {
            cb->bound_descriptor_set_hashes[i] = descriptors_hash;
            if (dynamic_offset_count > 0)
            {
                cb->dynamic_offset_hashes[i] = dynamic_offset_hash;
            }

            DescriptorPool *pool = &cb->bound_pipeline_instance->pipeline->layout->pools[i];

            VkDescriptorSet descriptor_set =
                descriptor_pool_alloc(cb->dev, pool, cb->bound_descriptors[i], descriptors_hash);
            assert(descriptor_set);

            vkCmdBindDescriptorSets(
                cb->cmd_buffer,
                cb->bound_pipeline_instance->bind_point,
                cb->bound_pipeline_instance->pipeline->layout->layout,
                i,
                1,
                &descriptor_set,
                dynamic_offset_count,
                dynamic_offsets);
        }
    }
}

static void cmd_copy_buffer_to_buffer(
    MtCmdBuffer *cb,
    MtBuffer *src,
    size_t src_offset,
    MtBuffer *dst,
    size_t dst_offset,
    size_t size)
{
    VkBufferCopy region = {
        .srcOffset = src_offset,
        .dstOffset = dst_offset,
        .size = size,
    };
    vkCmdCopyBuffer(cb->cmd_buffer, src->buffer, dst->buffer, 1, &region);
}

static void cmd_copy_buffer_to_image(
    MtCmdBuffer *cb, const MtBufferCopyView *src, const MtImageCopyView *dst, MtExtent3D extent)
{
    VkImageSubresourceLayers subresource = {
        .aspectMask = dst->image->aspect,
        .mipLevel = dst->mip_level,
        .baseArrayLayer = dst->array_layer,
        .layerCount = 1,
    };

    VkBufferImageCopy region = {
        .bufferOffset = src->offset,
        .bufferRowLength = src->row_length,
        .bufferImageHeight = src->image_height,
        .imageSubresource = subresource,
        .imageOffset =
            (VkOffset3D){
                .x = dst->offset.x,
                .y = dst->offset.y,
                .z = dst->offset.z,
            },
        .imageExtent =
            (VkExtent3D){
                .width = extent.width,
                .height = extent.height,
                .depth = extent.depth,
            },
    };

    vkCmdCopyBufferToImage(
        cb->cmd_buffer,
        src->buffer->buffer,
        dst->image->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);
}

static void cmd_copy_image_to_buffer(
    MtCmdBuffer *cb, const MtImageCopyView *src, const MtBufferCopyView *dst, MtExtent3D extent)
{
    VkImageSubresourceLayers subresource = {
        .aspectMask = src->image->aspect,
        .mipLevel = src->mip_level,
        .baseArrayLayer = src->array_layer,
        .layerCount = 1,
    };

    VkBufferImageCopy region = {
        .bufferOffset = dst->offset,
        .bufferRowLength = dst->row_length,
        .bufferImageHeight = dst->image_height,
        .imageSubresource = subresource,
        .imageOffset =
            (VkOffset3D){
                .x = src->offset.x,
                .y = src->offset.y,
                .z = src->offset.z,
            },
        .imageExtent =
            (VkExtent3D){
                .width = extent.width,
                .height = extent.height,
                .depth = extent.depth,
            },
    };

    vkCmdCopyImageToBuffer(
        cb->cmd_buffer,
        src->image->image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dst->buffer->buffer,
        1,
        &region);
}

static void cmd_copy_image_to_image(
    MtCmdBuffer *cb, const MtImageCopyView *src, const MtImageCopyView *dst, MtExtent3D extent)
{
    VkImageSubresourceLayers src_subresource = {
        .aspectMask = src->image->aspect,
        .mipLevel = src->mip_level,
        .baseArrayLayer = src->array_layer,
        .layerCount = 1,
    };

    VkImageSubresourceLayers dst_subresource = {
        .aspectMask = dst->image->aspect,
        .mipLevel = dst->mip_level,
        .baseArrayLayer = dst->array_layer,
        .layerCount = 1,
    };

    VkImageCopy region = {
        .srcSubresource = src_subresource,
        .srcOffset = {.x = src->offset.x, .y = src->offset.y, .z = src->offset.z},
        .dstSubresource = dst_subresource,
        .dstOffset = {.x = dst->offset.x, .y = dst->offset.y, .z = dst->offset.z},
        .extent = {.width = extent.width, .height = extent.height, .depth = extent.depth},
    };

    vkCmdCopyImage(
        cb->cmd_buffer,
        src->image->image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dst->image->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);
}

static void
cmd_fill_buffer(MtCmdBuffer *cb, MtBuffer *buffer, size_t offset, size_t range, uint32_t data)
{
    vkCmdFillBuffer(cb->cmd_buffer, buffer->buffer, offset, range, data);
}

static void cmd_set_viewport(MtCmdBuffer *cb, MtViewport *viewport)
{
    cb->current_viewport = *viewport;
    vkCmdSetViewport(
        cb->cmd_buffer,
        0,
        1,
        &(VkViewport){
            .width = viewport->width,
            .height = viewport->height,
            .x = viewport->x,
            .y = viewport->y,
            .minDepth = viewport->min_depth,
            .maxDepth = viewport->max_depth,
        });
}

static void
cmd_set_scissor(MtCmdBuffer *cmd_buffer, int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    VkRect2D scissor = {.offset = {x, y}, .extent = {width, height}};
    vkCmdSetScissor(cmd_buffer->cmd_buffer, 0, 1, &scissor);
}

static void cmd_bind_pipeline(MtCmdBuffer *cb, MtPipeline *pipeline)
{
    memset(cb->bound_descriptors, 0, sizeof(cb->bound_descriptors));
    memset(cb->bound_descriptor_set_hashes, 0, sizeof(cb->bound_descriptor_set_hashes));
    memset(cb->dynamic_offsets, 0, sizeof(cb->dynamic_offsets));
    memset(cb->dynamic_offset_hashes, 0, sizeof(cb->dynamic_offset_hashes));

    switch (pipeline->bind_point)
    {
        case VK_PIPELINE_BIND_POINT_GRAPHICS: {
            cb->bound_pipeline_instance =
                request_graphics_pipeline_instance(cb->dev, pipeline, &cb->current_renderpass);

            vkCmdBindPipeline(
                cb->cmd_buffer,
                cb->bound_pipeline_instance->bind_point,
                cb->bound_pipeline_instance->vk_pipeline);
            break;
        }

        case VK_PIPELINE_BIND_POINT_COMPUTE: {
            cb->bound_pipeline_instance = request_compute_pipeline_instance(cb->dev, pipeline);

            vkCmdBindPipeline(
                cb->cmd_buffer,
                cb->bound_pipeline_instance->bind_point,
                cb->bound_pipeline_instance->vk_pipeline);
            break;
        }

        default: assert(0);
    }
}

static void cmd_begin_render_pass(MtCmdBuffer *cmd_buffer, MtRenderGraphPass *pass)
{
    cmd_buffer->current_renderpass = pass->render_pass;
    MtRenderPass *render_pass = &cmd_buffer->current_renderpass;

    VkClearValue clear_values[8] = {0};
    uint32_t clear_value_count = 0;

    for (uint32_t i = 0; i < render_pass->color_attachment_count; ++i)
    {
        MtClearColorValue color_value;
        pass->color_clearer(i, &color_value);

        VkClearColorValue *vk_color_value = &clear_values[clear_value_count++].color;
        memcpy(vk_color_value, &color_value, sizeof(*vk_color_value));
    }

    if (render_pass->has_depth_attachment)
    {
        MtClearDepthStencilValue depth_stencil_value;
        pass->depth_stencil_clearer(&depth_stencil_value);

        VkClearDepthStencilValue *vk_depth_stencil_value =
            &clear_values[clear_value_count++].depthStencil;
        memcpy(vk_depth_stencil_value, &depth_stencil_value, sizeof(*vk_depth_stencil_value));
    }

    VkRenderPassBeginInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass->renderpass,
        .framebuffer = render_pass->current_framebuffer,
        .renderArea.offset = (VkOffset2D){0, 0},
        .renderArea.extent = render_pass->extent,
        .clearValueCount = clear_value_count,
        .pClearValues = clear_values,
    };

    vkCmdBeginRenderPass(cmd_buffer->cmd_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    cmd_set_viewport(
        cmd_buffer,
        &(MtViewport){
            .x = 0.0f,
            .y = 0.0f,
            .width = (float)render_pass->extent.width,
            .height = (float)render_pass->extent.height,
            .min_depth = 0.0f,
            .max_depth = 1.0f,
        });
    cmd_set_scissor(cmd_buffer, 0, 0, render_pass->extent.width, render_pass->extent.height);
}

static void cmd_end_render_pass(MtCmdBuffer *cb)
{
    memset(&cb->current_renderpass, 0, sizeof(cb->current_renderpass));
    vkCmdEndRenderPass(cb->cmd_buffer);
}

static void
cmd_bind_uniform(MtCmdBuffer *cb, const void *data, size_t size, uint32_t set, uint32_t binding)
{
    mt_mutex_lock(&cb->dev->device_mutex);

    assert(MT_LENGTH(cb->bound_descriptors) > set);
    assert(MT_LENGTH(cb->bound_descriptors[set]) > binding);

    BufferBlock *block = ensure_buffer_block(&cb->dev->ubo_pool, &cb->ubo_blocks, size);
    assert(block->buffer->buffer);

    BufferBlockAllocation allocation = buffer_block_allocate(block, size);
    assert(allocation.mapping);

    mt_mutex_unlock(&cb->dev->device_mutex);

    memcpy(allocation.mapping, data, size);

    cb->dynamic_offsets[set][binding] = allocation.offset;

    cb->bound_descriptors[set][binding].buffer.buffer = block->buffer->buffer;
    cb->bound_descriptors[set][binding].buffer.offset = 0;
    cb->bound_descriptors[set][binding].buffer.range = VK_WHOLE_SIZE;
}

static void
cmd_bind_storage_buffer(MtCmdBuffer *cb, MtBuffer *buffer, uint32_t set, uint32_t binding)
{
    assert(MT_LENGTH(cb->bound_descriptors) > set);
    assert(MT_LENGTH(cb->bound_descriptors[set]) > binding);

    cb->bound_descriptors[set][binding].buffer.buffer = buffer->buffer;
    cb->bound_descriptors[set][binding].buffer.offset = 0;
    cb->bound_descriptors[set][binding].buffer.range = VK_WHOLE_SIZE;
}

static void cmd_bind_sampler(MtCmdBuffer *cb, MtSampler *sampler, uint32_t set, uint32_t binding)
{
    assert(MT_LENGTH(cb->bound_descriptors) > set);
    assert(MT_LENGTH(cb->bound_descriptors[set]) > binding);
    cb->bound_descriptors[set][binding].image.sampler = sampler->sampler;
}

static void cmd_bind_image(MtCmdBuffer *cb, MtImage *image, uint32_t set, uint32_t binding)
{
    assert(MT_LENGTH(cb->bound_descriptors) > set);
    assert(MT_LENGTH(cb->bound_descriptors[set]) > binding);

    memset(&cb->bound_descriptors[set][binding], 0, sizeof(cb->bound_descriptors[set][binding]));

    cb->bound_descriptors[set][binding].image.imageView = image->image_view;
    cb->bound_descriptors[set][binding].image.imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (image->aspect & VK_IMAGE_ASPECT_DEPTH_BIT)
    {
        cb->bound_descriptors[set][binding].image.imageLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    }
}

static void cmd_bind_vertex_buffer(MtCmdBuffer *cb, MtBuffer *buffer, size_t offset)
{
    vkCmdBindVertexBuffers(cb->cmd_buffer, 0, 1, &buffer->buffer, &offset);
}

static void
cmd_bind_index_buffer(MtCmdBuffer *cb, MtBuffer *buffer, MtIndexType index_type, size_t offset)
{
    vkCmdBindIndexBuffer(cb->cmd_buffer, buffer->buffer, offset, index_type_to_vulkan(index_type));
}

static void *cmd_bind_vertex_data(MtCmdBuffer *cb, size_t size)
{
    mt_mutex_lock(&cb->dev->device_mutex);

    BufferBlock *block = ensure_buffer_block(&cb->dev->vbo_pool, &cb->vbo_blocks, size);
    assert(block->buffer->buffer);

    BufferBlockAllocation allocation = buffer_block_allocate(block, size);
    assert(allocation.mapping);

    mt_mutex_unlock(&cb->dev->device_mutex);

    cmd_bind_vertex_buffer(cb, block->buffer, allocation.offset);

    return allocation.mapping;
}

static void *cmd_bind_index_data(MtCmdBuffer *cb, size_t size, MtIndexType index_type)
{
    mt_mutex_lock(&cb->dev->device_mutex);

    BufferBlock *block = ensure_buffer_block(&cb->dev->ibo_pool, &cb->ibo_blocks, size);
    assert(block->buffer->buffer);

    BufferBlockAllocation allocation = buffer_block_allocate(block, size);
    assert(allocation.mapping);

    mt_mutex_unlock(&cb->dev->device_mutex);

    cmd_bind_index_buffer(cb, block->buffer, index_type, allocation.offset);

    return allocation.mapping;
}

static void cmd_draw(
    MtCmdBuffer *cb,
    uint32_t vertex_count,
    uint32_t instance_count,
    uint32_t first_vertex,
    uint32_t first_instance)
{
    bind_descriptor_sets(cb);
    vkCmdDraw(cb->cmd_buffer, vertex_count, instance_count, first_vertex, first_instance);
}

static void cmd_draw_indexed(
    MtCmdBuffer *cb,
    uint32_t index_count,
    uint32_t instance_count,
    uint32_t first_index,
    int32_t vertex_offset,
    uint32_t first_instance)
{
    bind_descriptor_sets(cb);
    vkCmdDrawIndexed(
        cb->cmd_buffer, index_count, instance_count, first_index, vertex_offset, first_instance);
}

static void cmd_dispatch(
    MtCmdBuffer *cb, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z)
{
    bind_descriptor_sets(cb);
    vkCmdDispatch(cb->cmd_buffer, group_count_x, group_count_y, group_count_z);
}
