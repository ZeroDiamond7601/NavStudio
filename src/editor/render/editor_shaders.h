#ifndef EDITOR_SHADERS_H
#define EDITOR_SHADERS_H

// Vertex Shader for 3D Geometry (BSP surfaces and NavMesh quads)
static const char* kMeshVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aColor;

uniform mat4 u_MVP;
uniform mat4 u_Model;

out vec3 vFragPos;
out vec3 vNormal;
out vec2 vTexCoord;
out vec4 vColor;

void main() {
    vFragPos = vec3(u_Model * vec4(aPos, 1.0));
    vNormal = mat3(transpose(inverse(u_Model))) * aNormal;
    vTexCoord = aTexCoord;
    vColor = aColor;
    gl_Position = u_MVP * vec4(aPos, 1.0);
}
)";

// Fragment Shader for 3D Geometry with directional lighting and transparency support
static const char* kMeshFragmentShader = R"(#version 330 core
in vec3 vFragPos;
in vec3 vNormal;
in vec2 vTexCoord;
in vec4 vColor;

uniform vec4 u_BaseColor;
uniform int u_UseTexture;
uniform sampler2D u_DiffuseTexture;
uniform float u_Alpha;
uniform int u_EnableLighting;

out vec4 FragColor;

void main() {
    vec4 col = vColor * u_BaseColor;
    if (u_UseTexture == 1) {
        col *= texture(u_DiffuseTexture, vTexCoord);
    }

    if (u_EnableLighting == 1) {
        vec3 lightDir = normalize(vec3(0.4, 0.3, 0.8));
        vec3 norm = normalize(vNormal);
        float diff = max(dot(norm, lightDir), 0.0);
        float ambient = 0.45;
        vec3 lighting = vec3(ambient + diff * 0.55);
        FragColor = vec4(col.rgb * lighting, col.a * u_Alpha);
    } else {
        FragColor = vec4(col.rgb, col.a * u_Alpha);
    }
}
)";

// Vertex Shader for Unlit Lines and Wireframes
static const char* kLineVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;

uniform mat4 u_MVP;

out vec4 vColor;

void main() {
    vColor = aColor;
    gl_Position = u_MVP * vec4(aPos, 1.0);
}
)";

// Fragment Shader for Unlit Lines and Wireframes
static const char* kLineFragmentShader = R"(#version 330 core
in vec4 vColor;

uniform vec4 u_Color;

out vec4 FragColor;

void main() {
    FragColor = vColor * u_Color;
}
)";

#endif // EDITOR_SHADERS_H
