#include "editor/render/shader.h"
#include <cstdio>
#include <vector>

Shader::Shader() : m_program(0) {}

Shader::~Shader() {
    if (m_program != 0) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
}

GLuint Shader::CompileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> infoLog(logLen + 1, 0);
        glGetShaderInfoLog(shader, logLen, nullptr, infoLog.data());
        std::fprintf(stderr, "[Shader Error] Compilation failed:\n%s\n", infoLog.data());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool Shader::LoadFromSource(const char* vertexSrc, const char* fragmentSrc) {
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vertexSrc);
    if (!vs) return false;

    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragmentSrc);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }

    if (m_program != 0) {
        glDeleteProgram(m_program);
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);

    GLint success = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &success);
    if (!success) {
        GLint logLen = 0;
        glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> infoLog(logLen + 1, 0);
        glGetProgramInfoLog(m_program, logLen, nullptr, infoLog.data());
        std::fprintf(stderr, "[Shader Error] Linking failed:\n%s\n", infoLog.data());
        glDeleteShader(vs);
        glDeleteShader(fs);
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return true;
}

void Shader::Bind() const {
    if (m_program != 0) {
        glUseProgram(m_program);
    }
}

void Shader::Unbind() const {
    glUseProgram(0);
}

void Shader::SetInt(const char* name, int value) const {
    GLint loc = glGetUniformLocation(m_program, name);
    if (loc != -1) glUniform1i(loc, value);
}

void Shader::SetFloat(const char* name, float value) const {
    GLint loc = glGetUniformLocation(m_program, name);
    if (loc != -1) glUniform1f(loc, value);
}

void Shader::SetVec3(const char* name, float x, float y, float z) const {
    GLint loc = glGetUniformLocation(m_program, name);
    if (loc != -1) glUniform3f(loc, x, y, z);
}

void Shader::SetVec3(const char* name, const Vector3& v) const {
    SetVec3(name, v.x, v.y, v.z);
}

void Shader::SetVec4(const char* name, float x, float y, float z, float w) const {
    GLint loc = glGetUniformLocation(m_program, name);
    if (loc != -1) glUniform4f(loc, x, y, z, w);
}

void Shader::SetMat4(const char* name, const Matrix4& mat) const {
    GLint loc = glGetUniformLocation(m_program, name);
    if (loc != -1) glUniformMatrix4fv(loc, 1, GL_FALSE, mat.Data());
}
