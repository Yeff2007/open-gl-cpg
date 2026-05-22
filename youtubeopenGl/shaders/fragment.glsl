#version 330 core
// Fragment shader con iluminación: direcciónal + puntual + spotlight
// Entradas: color, normal y posición del fragmento en espacio mundo
in vec3 vColor;
in vec3 vNormal;
in vec3 vFragPos;
in vec2 vUV;

out vec4 FragColor;

uniform sampler2D uAlbedoTex; // textura difusa

// Luces y parámetros de material/cámara
uniform vec3 uDirLightDir;
uniform vec3 uDirLightColor;
uniform vec3 uPointLightPos;
uniform vec3 uPointLightColor;
uniform vec3 uSpotPos;
uniform vec3 uSpotDir;
uniform vec3 uSpotColor;
uniform float uSpotCutOff;
uniform float uSpotOuterCutOff;
uniform vec3 uViewPos;
uniform float uAmbientStrength;
uniform float uSpecularStrength;
uniform float uShininess;

void main()
{
    vec3 N = normalize(vNormal);
    vec3 viewDir = normalize(uViewPos - vFragPos);
    vec3 albedo = texture(uAlbedoTex, vUV).rgb * vColor;

    // Ambient
    vec3 ambient = uAmbientStrength * albedo;

    // Directional (soft key)
    vec3 dirL = normalize(-uDirLightDir);
    float diffD = max(dot(N, dirL), 0.0);
    vec3 diffuseD = diffD * uDirLightColor * albedo;

    // Point light (near model)
    vec3 pointLdir = normalize(uPointLightPos - vFragPos);
    float diffP = max(dot(N, pointLdir), 0.0);
    float distP = length(uPointLightPos - vFragPos);
    float attenuationP = 1.0 / (1.0 + 0.1 * distP + 0.02 * distP * distP);
    vec3 diffuseP = diffP * uPointLightColor * albedo * attenuationP;
    vec3 reflectP = reflect(-pointLdir, N);
    float specP = pow(max(dot(viewDir, reflectP), 0.0), uShininess);
    vec3 specularP = uSpecularStrength * vec3(1.0) * specP * attenuationP;

    // Spotlight (camera-linked)
    vec3 Lspot = normalize(uSpotPos - vFragPos);
    float diffS = max(dot(N, Lspot), 0.0);
    float distS = length(uSpotPos - vFragPos);
    float attenuationS = 1.0 / (1.0 + 0.05 * distS + 0.01 * distS * distS);
    float theta = dot(normalize(uSpotDir), normalize(vFragPos - uSpotPos));
    float epsilon = max(0.0001, uSpotCutOff - uSpotOuterCutOff);
    float intensity = clamp((theta - uSpotOuterCutOff) / epsilon, 0.0, 1.0);
    vec3 diffuseS = diffS * uSpotColor * albedo * intensity * attenuationS;
    vec3 reflectS = reflect(-Lspot, N);
    float specS = pow(max(dot(viewDir, reflectS), 0.0), uShininess);
    vec3 specularS = uSpecularStrength * vec3(1.0) * specS * intensity * attenuationS;

    // Combine
    vec3 result = ambient * 0.6 + diffuseD * 0.6 + diffuseP * 0.6 + diffuseS * 1.8 + specularP + specularS;
    result = pow(max(result, vec3(0.0)), vec3(1.0/2.2)); // gamma
    FragColor = vec4(result, 1.0);
}
