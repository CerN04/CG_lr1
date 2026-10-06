#include "application.hpp"
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

namespace application {

Resources res;

namespace {

void generateParallelepiped(float width, float height, float depth) {
    const float w = width * 0.5f;
    const float h = height * 0.5f;
    const float d = depth * 0.5f;

    const glm::vec3 positions[8] = {
        {-w, -h, -d}, {+w, -h, -d}, {+w, +h, -d}, {-w, +h, -d},
        {-w, -h, +d}, {+w, -h, +d}, {+w, +h, +d}, {-w, +h, +d},
    };

    res.vertices.clear();
    for (const auto& p : positions) {
        Vertex v{};
        v.pos = p;
        v.color = glm::clamp(glm::vec3(p.x / width + 0.5f,
                                       p.y / height + 0.5f,
                                       p.z / depth + 0.5f),
                             glm::vec3(0.0f), glm::vec3(1.0f));
        res.vertices.push_back(v);
    }

    res.indices = {
        0, 1, 2,  2, 3, 0,
        4, 6, 5,  4, 7, 6,
        0, 3, 7,  0, 7, 4,
        1, 5, 6,  1, 6, 2,
        0, 4, 5,  0, 5, 1,
        3, 2, 6,  3, 6, 7,
    };
}

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                  VmaAllocationCreateFlags allocFlags,
                  VkBuffer& buffer, VmaAllocation& allocation,
                  void** mapped = nullptr) {
    auto& ctx = graphics::internal::context;

    const VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    const VmaAllocationCreateInfo allocInfo = {
        .flags = allocFlags,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VmaAllocationInfo allocResult{};
    if (vmaCreateBuffer(ctx.allocator, &bufferInfo, &allocInfo,
                        &buffer, &allocation, &allocResult) != VK_SUCCESS) {
        std::cerr << "Failed to create buffer\n";
        return false;
    }

    if (mapped != nullptr) {
        *mapped = allocResult.pMappedData;
    }
    return true;
}

VkShaderModule createShaderModule(const char* path) {
    auto& ctx = graphics::internal::context;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader: " << path << "\n";
        return VK_NULL_HANDLE;
    }

    const auto size = static_cast<size_t>(file.tellg());
    std::vector<char> code(size);
    file.seekg(0);
    file.read(code.data(), static_cast<std::streamsize>(size));

    const VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code.size(),
        .pCode = reinterpret_cast<const uint32_t*>(code.data()),
    };

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(ctx.device, &createInfo, nullptr, &module) != VK_SUCCESS) {
        std::cerr << "Failed to create shader module: " << path << "\n";
        return VK_NULL_HANDLE;
    }
    return module;
}

bool createObject(ObjectState& obj, const glm::vec3& initialPos, const glm::vec3& color) {
    auto& ctx = graphics::internal::context;

    obj.position = initialPos;
    obj.objectColor = color;

    if (!createBuffer(sizeof(ObjectUBO),
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VMA_ALLOCATION_CREATE_MAPPED_BIT |
                          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                      obj.uniformBuffer, obj.uniformAllocation, &obj.uniformMapped)) return false;

    VkDescriptorSetAllocateInfo setAllocInfo{};
    setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool = res.descriptorPool;
    setAllocInfo.descriptorSetCount = 1;
    setAllocInfo.pSetLayouts = &res.objectSetLayout;

    if (vkAllocateDescriptorSets(ctx.device, &setAllocInfo, &obj.descriptorSet) != VK_SUCCESS) {
        std::cerr << "Failed to allocate descriptor set for object\n";
        return false;
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = obj.uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(ObjectUBO);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = obj.descriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);
    return true;
}

void destroyObject(ObjectState& obj) {
    auto& ctx = graphics::internal::context;
    vmaDestroyBuffer(ctx.allocator, obj.uniformBuffer, obj.uniformAllocation);
}

} // namespace

bool initialize() {
    auto& ctx = graphics::internal::context;

    generateParallelepiped(2.0f, 1.0f, 3.0f);

    if (!createBuffer(res.vertices.size() * sizeof(Vertex),
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                      res.vertexBuffer, res.vertexAllocation)) return false;

    if (!createBuffer(res.indices.size() * sizeof(uint32_t),
                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                      res.indexBuffer, res.indexAllocation)) return false;

    if (!createBuffer(sizeof(GlobalUBO),
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VMA_ALLOCATION_CREATE_MAPPED_BIT |
                          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
                      res.globalUniformBuffer, res.globalUniformAllocation,
                      &res.globalUniformMapped)) return false;

    {
        void* data = nullptr;
        vmaMapMemory(ctx.allocator, res.vertexAllocation, &data);
        std::memcpy(data, res.vertices.data(), res.vertices.size() * sizeof(Vertex));
        vmaUnmapMemory(ctx.allocator, res.vertexAllocation);

        vmaMapMemory(ctx.allocator, res.indexAllocation, &data);
        std::memcpy(data, res.indices.data(), res.indices.size() * sizeof(uint32_t));
        vmaUnmapMemory(ctx.allocator, res.indexAllocation);
    }


    VkDescriptorSetLayoutBinding globalBinding{};
    globalBinding.binding = 0;
    globalBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    globalBinding.descriptorCount = 1;
    globalBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo globalLayoutInfo{};
    globalLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    globalLayoutInfo.bindingCount = 1;
    globalLayoutInfo.pBindings = &globalBinding;

    vkCreateDescriptorSetLayout(ctx.device, &globalLayoutInfo, nullptr, &res.globalSetLayout);

    VkDescriptorSetLayoutBinding objectBinding{};
    objectBinding.binding = 0;
    objectBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    objectBinding.descriptorCount = 1;
    objectBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo objectLayoutInfo{};
    objectLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    objectLayoutInfo.bindingCount = 1;
    objectLayoutInfo.pBindings = &objectBinding;

    vkCreateDescriptorSetLayout(ctx.device, &objectLayoutInfo, nullptr, &res.objectSetLayout);

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 3;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 3;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    vkCreateDescriptorPool(ctx.device, &poolInfo, nullptr, &res.descriptorPool);

    VkDescriptorSetAllocateInfo globalAllocInfo{};
    globalAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    globalAllocInfo.descriptorPool = res.descriptorPool;
    globalAllocInfo.descriptorSetCount = 1;
    globalAllocInfo.pSetLayouts = &res.globalSetLayout;

    vkAllocateDescriptorSets(ctx.device, &globalAllocInfo, &res.globalDescriptorSet);

    {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = res.globalUniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(GlobalUBO);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = res.globalDescriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);
    }

    res.objects.resize(2);
    if (!createObject(res.objects[0], glm::vec3(-3.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.3f, 0.3f))) return false;
    if (!createObject(res.objects[1], glm::vec3(+3.0f, 0.0f, 0.0f), glm::vec3(0.3f, 0.7f, 1.0f))) return false;
    res.objects[1].animPlaying = true;
    res.objects[1].rotationDeg = glm::vec3(0.0f, 45.0f, 0.0f);

    VkDescriptorSetLayout setLayouts[2] = {res.globalSetLayout, res.objectSetLayout};

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = setLayouts;

    vkCreatePipelineLayout(ctx.device, &layoutInfo, nullptr, &res.pipelineLayout);

    VkShaderModule vertModule = createShaderModule("shaders/shader.vert.spv");
    VkShaderModule fragModule = createShaderModule("shaders/shader.frag.spv");
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) return false;

    const VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertModule, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragModule, .pName = "main" },
    };

    const VkVertexInputBindingDescription binding = {
        .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const VkVertexInputAttributeDescription attributes[2] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, pos) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, color) },
    };
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    const VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_FALSE;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = res.pipelineLayout;
    pipelineInfo.renderPass = ctx.render_pass;
    pipelineInfo.subpass = 0;

    vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &res.pipeline);

    vkDestroyShaderModule(ctx.device, vertModule, nullptr);
    vkDestroyShaderModule(ctx.device, fragModule, nullptr);

    return true;
}

void update(double time) {
    auto& ctx = graphics::internal::context;

    ImGui::Begin("Scene Settings");
    ImGui::Text("Projection");
    ImGui::Checkbox("Orthographic", &res.useOrtho);
    ImGui::Separator();
    ImGui::Text("Object 1 (static)");
    ImGui::SliderFloat3("Position", &res.objects[0].position.x, -10.0f, 10.0f);
    ImGui::SliderFloat3("Rotation", &res.objects[0].rotationDeg.x, -180.0f, 180.0f);
    ImGui::SliderFloat3("Scale", &res.objects[0].scale.x, 0.1f, 5.0f);
    ImGui::ColorEdit3("Color", &res.objects[0].objectColor.x);
    ImGui::Separator();
    ImGui::Text("Object 2 (animated)");
    ImGui::Checkbox("Play", &res.objects[1].animPlaying);
    ImGui::SliderFloat("Speed", &res.objects[1].animSpeed, 0.0f, 5.0f);
    ImGui::SliderFloat("Radius", &res.objects[1].animRadius, 0.0f, 10.0f);
    ImGui::SliderFloat3("Base Pos", &res.objects[1].position.x, -10.0f, 10.0f);
    ImGui::ColorEdit3("Color##2", &res.objects[1].objectColor.x);
    ImGui::End();

    if (res.lastTime < 0.0) res.lastTime = time;
    const double dt = time - res.lastTime;
    res.lastTime = time;

    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 3.0f, 10.0f),
                                       glm::vec3(0.0f, 0.0f, 0.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));

    const float aspect = ctx.swapchain_extent.height > 0
        ? float(ctx.swapchain_extent.width) / float(ctx.swapchain_extent.height) : 1.0f;
    glm::mat4 proj;
    if (res.useOrtho) {
        const float f = 5.0f;
        proj = glm::ortho(-f * aspect, f * aspect, -f, f, 0.1f, 100.0f);
    } else {
        proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    }
    proj[1][1] *= -1.0f;

    res.globalUbo.view = view;
    res.globalUbo.proj = proj;

    for (auto& obj : res.objects) {
        if (obj.animPlaying) {
            obj.animTime += dt * obj.animSpeed;
        }

        glm::vec3 pos = obj.position;
        glm::vec3 rot = obj.rotationDeg;

        if (obj.animPlaying) {
            const float t = static_cast<float>(obj.animTime);
            pos.x += obj.animRadius * std::cos(t);
            pos.z += obj.animRadius * std::sin(t);
            pos.y += 0.5f * std::sin(2.0f * t);
            rot.y += glm::degrees(t);
        }

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, pos);
        model = glm::rotate(model, glm::radians(rot.x), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(rot.y), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(rot.z), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, obj.scale);

        obj.ubo.model = model;
        obj.ubo.objectColor = glm::vec4(obj.objectColor, 1.0f);
    }
}

void render(const graphics::internal::FrameData& fd) {
    auto& ctx = graphics::internal::context;

    std::memcpy(res.globalUniformMapped, &res.globalUbo, sizeof(res.globalUbo));

    for (auto& obj : res.objects) {
        std::memcpy(obj.uniformMapped, &obj.ubo, sizeof(obj.ubo));
    }

    vkResetCommandBuffer(fd.command_buffer, 0);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(fd.command_buffer, &beginInfo);

    const VkClearValue clearValues[2] = {
        VkClearValue{.color = {{0.05f, 0.05f, 0.08f, 1.0f}}},
        VkClearValue{.depthStencil = {1.0f, 0}},
    };
    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = ctx.render_pass;
    rpInfo.framebuffer = fd.framebuffer;
    rpInfo.renderArea = {{0, 0}, ctx.swapchain_extent};
    rpInfo.clearValueCount = 2;
    rpInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(fd.command_buffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, res.pipeline);

    const VkViewport viewport = { 0.0f, 0.0f,
                                  float(ctx.swapchain_extent.width),
                                  float(ctx.swapchain_extent.height),
                                  0.0f, 1.0f };
    vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
    const VkRect2D scissor = {{0, 0}, ctx.swapchain_extent};
    vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &res.vertexBuffer, &offset);
    vkCmdBindIndexBuffer(fd.command_buffer, res.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            res.pipelineLayout, 0, 1, &res.globalDescriptorSet, 0, nullptr);

    for (const auto& obj : res.objects) {
        vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                res.pipelineLayout, 1, 1, &obj.descriptorSet, 0, nullptr);
        vkCmdDrawIndexed(fd.command_buffer, uint32_t(res.indices.size()), 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(fd.command_buffer);
    vkEndCommandBuffer(fd.command_buffer);
}

void shutdown() {
    auto& ctx = graphics::internal::context;
    vkQueueWaitIdle(ctx.graphics_queue);

    for (auto& obj : res.objects) {
        destroyObject(obj);
    }

    vkDestroyPipeline(ctx.device, res.pipeline, nullptr);
    vkDestroyPipelineLayout(ctx.device, res.pipelineLayout, nullptr);
    vkDestroyDescriptorPool(ctx.device, res.descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, res.globalSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, res.objectSetLayout, nullptr);

    vmaDestroyBuffer(ctx.allocator, res.vertexBuffer, res.vertexAllocation);
    vmaDestroyBuffer(ctx.allocator, res.indexBuffer, res.indexAllocation);
    vmaDestroyBuffer(ctx.allocator, res.globalUniformBuffer, res.globalUniformAllocation);
}

} // namespace application