#ifndef SHADER_H
#define SHADER_H

#include <string>
#include "editor/glad/include/glad/glad.h"
#include "math/vector3.h"
#include "editor/math/matrix4.h"

class Shader {
public:
    Shader();
    ~Shader();

    bool LoadFromSource(const char* vertexSrc, const char* fragmentSrc);
    void Bind() const;
    void Unbind() const;

    GLuint GetProgramID() const { return m_program; }

    void SetInt(const char* name, int value) const;
    void SetFloat(const char* name, float value) const;
    void SetVec3(const char* name, float x, float y, float z) const;
    void SetVec3(const char* name, const Vector3& v) const;
    void SetVec4(const char* name, float x, float y, float z, float w) const;
    void SetMat4(const char* name, const Matrix4& mat) const;

private:
    GLuint CompileShader(GLenum type, const char* src);

    GLuint m_program;
};

#endif // SHADER_H
