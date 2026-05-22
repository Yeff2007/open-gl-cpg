#version 330 core
// Fragment shader con iluminación simple (Phong)
in vec3 vColor; // color interpolado desde el vertex shader
in vec3 vNormal; // normal interpolada
in vec3 vFragPos; // posición del fragmento en espacio mundo
in vec2 vUV;

out vec4 FragColor;

uniform sampler2D uAlbedoTex; // textura difusa

// Parámetros de la luz (pueden pasarse como uniformes si se quiere)
uniform vec3 uLightPos; // posición de la luz en espacio mundo
uniform vec3 uViewPos;  // posición de la cámara / ojo
uniform float uAmbientStrength; // control de intensidad ambiental
uniform float uSpecularStrength; // intensidad especular
uniform float uShininess; // shininess exponent

void main()
{
    // Normalizar normal
    vec3 N = normalize(vNormal);
    // Direcciones
    vec3 L = normalize(uLightPos - vFragPos); // dirección de la luz
    vec3 V = normalize(uViewPos - vFragPos);  // dirección hacia la cámara
    // Componente ambiental
    vec3 tex = texture(uAlbedoTex, vUV).rgb;
    vec3 ambient = uAmbientStrength * (vColor * tex);
    // Componente difusa (Lambert)
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * (vColor * tex);
    // Componente especular (Phong)
    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(R, V), 0.0), uShininess);
    vec3 specular = uSpecularStrength * vec3(1.0) * spec;

    vec3 result = ambient + diffuse + specular;
    FragColor = vec4(result, 1.0);
}
