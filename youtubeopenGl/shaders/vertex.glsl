#version 330 core
// Vertex shader: transforma posición a clip space y pasa atributos al fragment shader
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec2 aUV;

uniform mat4 uMVP;
uniform mat4 uModel;

out vec3 vColor;
out vec3 vNormal;
out vec3 vFragPos;
out vec2 vUV;

void main()
{
    gl_Position = uMVP * vec4(aPos, 1.0);
    vColor = aColor;
    vFragPos = vec3(uModel * vec4(aPos, 1.0));
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vUV = aUV;
}

