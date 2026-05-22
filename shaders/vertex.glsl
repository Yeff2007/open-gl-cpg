#version 330 core
// Vertex shader simple: recibe la posición del vértice y la convierte a clip space.
// No maneja colores aquí: el color se toma desde un uniforme en el fragment shader.

// Ahora también recibe color por vértice y pasa al fragment shader
layout (location = 0) in vec3 aPos; // posición del vértice
layout (location = 1) in vec3 aColor; // color por vértice
layout (location = 2) in vec3 aNormal; // normal por vértice
layout (location = 3) in vec2 aUV; // coordenadas de textura

// Matriz MVP (Model * View * Projection) que transformará los vértices a clip space
uniform mat4 uMVP;
// Matriz de modelo (para transformar normales y calcular posición de fragmento)
uniform mat4 uModel;

// Color que se interpola y llega al fragment shader
out vec3 vColor;
out vec3 vNormal;
out vec3 vFragPos;
out vec2 vUV;

void main()
{
    // Aplicar la transformación completa (MVP) al vértice
    gl_Position = uMVP * vec4(aPos, 1.0);
    // Pasar color al fragment shader
    vColor = aColor;
    // Calcular posición del fragmento en espacio del mundo
    vFragPos = vec3(uModel * vec4(aPos, 1.0));
    // Transformar normal correctamente usando la matriz inversa-transpuesta del modelo
    vNormal = mat3(transpose(inverse(uModel))) * aNormal;
    vUV = aUV;
}

