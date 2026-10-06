#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "graphics_internal.hpp"

#include <vector>
#include <glm/glm.hpp>

namespace application {

struct Vertex {
	glm::vec3 pos;
	glm::vec3 color;
};

struct GlobalUBO {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};

struct ObjectUBO {
    alignas(16) glm::mat4 model;
    alignas(16) glm::vec4 objectColor;
};

struct ObjectState {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotationDeg{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
    glm::vec3 objectColor{1.0f, 1.0f, 1.0f};
    bool animPlaying = false;
    float animSpeed = 1.0f;
    float animRadius = 2.0f;
    double animTime = 0.0;

    VkBuffer uniformBuffer = VK_NULL_HANDLE;
    VmaAllocation uniformAllocation = nullptr;
    void* uniformMapped = nullptr;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    ObjectUBO ubo{};
};

struct Resources {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAllocation = nullptr;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VmaAllocation indexAllocation = nullptr;

    VkBuffer globalUniformBuffer = VK_NULL_HANDLE;
    VmaAllocation globalUniformAllocation = nullptr;
    void* globalUniformMapped = nullptr;
    GlobalUBO globalUbo{};

    VkDescriptorSetLayout globalSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout objectSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet globalDescriptorSet = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    bool useOrtho = false;

    std::vector<ObjectState> objects;

    double lastTime = -1.0;
};

extern Resources res;


bool initialize();
void shutdown();

void update(double time);
void render(const graphics::internal::FrameData& fd);

} // namespace application