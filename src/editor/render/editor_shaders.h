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
out float vDepth;

void main() {
    vFragPos = vec3(u_Model * vec4(aPos, 1.0));
    vNormal = normalize(mat3(transpose(inverse(u_Model))) * aNormal);
    vTexCoord = aTexCoord;
    vColor = aColor;
    vec4 clipPos = u_MVP * vec4(aPos, 1.0);
    vDepth = clipPos.w;
    gl_Position = clipPos;
}
)";

// Fragment Shader for 3D Geometry with directional lighting, hemisphere ambient, and depth perception
static const char* kMeshFragmentShader = R"(#version 330 core
in vec3 vFragPos;
in vec3 vNormal;
in vec2 vTexCoord;
in vec4 vColor;
in float vDepth;

uniform vec4 u_BaseColor;
uniform int u_UseTexture;
uniform sampler2D u_DiffuseTexture;
uniform float u_Alpha;
uniform int u_EnableLighting;
uniform vec3 u_CameraPos;

out vec4 FragColor;

void main() {
    vec4 col = vColor * u_BaseColor;
    if (u_UseTexture == 1) {
        col *= texture(u_DiffuseTexture, vTexCoord);
    }

    if (u_EnableLighting == 1) {
        vec3 norm = normalize(vNormal);

        // Key sunlight from top-right-front
        vec3 keyDir = normalize(vec3(0.45, 0.35, 0.82));
        float diffKey = max(dot(norm, keyDir), 0.0);
        vec3 keyLight = vec3(1.0, 0.98, 0.94) * (diffKey * 0.48);

        // Cool fill light from opposite flank
        vec3 fillDir = normalize(vec3(-0.55, -0.40, 0.40));
        float diffFill = max(dot(norm, fillDir), 0.0);
        vec3 fillLight = vec3(0.55, 0.65, 0.85) * (diffFill * 0.24);

        // Hemisphere ambient lighting (GoldSrc Z-Up: +Z is Sky, -Z is Ground)
        float hemiFactor = clamp(norm.z * 0.5 + 0.5, 0.0, 1.0);
        vec3 skyAmbient = vec3(0.44, 0.47, 0.52);
        vec3 groundAmbient = vec3(0.22, 0.22, 0.25);
        vec3 ambient = mix(groundAmbient, skyAmbient, hemiFactor);

        // Subtle specular highlight for surface angle readability
        vec3 viewDir = normalize(u_CameraPos - vFragPos);
        vec3 halfDir = normalize(keyDir + viewDir);
        float spec = pow(max(dot(norm, halfDir), 0.0), 32.0);
        vec3 specular = vec3(0.18) * spec;

        vec3 totalLighting = ambient + keyLight + fillLight + specular;
        vec3 finalRgb = col.rgb * totalLighting;

        // Distance depth cueing
        float fogStart = 2000.0;
        float fogEnd = 12000.0;
        float fogFactor = clamp((vDepth - fogStart) / (fogEnd - fogStart), 0.0, 0.55);
        vec3 fogColor = vec3(0.12, 0.12, 0.14);
        finalRgb = mix(finalRgb, fogColor, fogFactor);

        FragColor = vec4(finalRgb, col.a * u_Alpha);
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
