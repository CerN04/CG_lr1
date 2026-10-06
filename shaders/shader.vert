#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
} global;

layout(set = 1, binding = 0) uniform ObjectUBO {
    mat4 model;
    vec4 objectColor;
} object;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = global.proj * global.view * object.model * vec4(inPosition, 1.0);
    fragColor = inColor * object.objectColor.rgb;
}