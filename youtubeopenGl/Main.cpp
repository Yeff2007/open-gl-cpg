#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <thread>
#include <atomic>
#include <chrono>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>

#include "stb_image.h"

constexpr int VERT_STRIDE = 11; // pos(3), color(3), normal(3), uv(2)

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifndef USE_TINYGLTF
#define USE_TINYGLTF
#endif
// Optional: tinygltf for glTF support. Define USE_TINYGLTF and add tiny_gltf.h to project/vcpkg to enable.
#ifdef USE_TINYGLTF
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"
#endif

#include "camera_input.h"
#ifdef USE_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#endif

// Main.cpp - Aplicación principal
// Este archivo crea la ventana GLFW, inicializa OpenGL (GLAD), compila shaders,
// carga modelos (glTF) de forma asíncrona, configura la cámara y entra en el bucle
// de render. También define y actualiza las luces (direccional, puntual, spotlight)
// y gestiona la lógica de la cámara que sigue una curva Bézier alrededor del modelo.
// Comentarios en el código indican la finalidad de las secciones principales.

// Función auxiliar para leer archivos de texto (shaders)
static std::string readFile(const char* filePath) {
    namespace fs = std::filesystem;
    // Try several candidate locations so the app works when Visual Studio's working
    // directory is different from the project folder or the binary output folder.
    std::vector<fs::path> candidates;
    candidates.push_back(fs::path(filePath)); // as given (working directory)
    candidates.push_back(fs::current_path() / filePath); // explicit cwd

    // Try relative to this source file location. Use one and two levels up to be robust
    fs::path src = fs::path(__FILE__).parent_path();
    candidates.push_back(src / filePath); // e.g. <project>/shaders/...
    candidates.push_back(src.parent_path() / filePath); // one level further up
    // Also try explicit "shaders" folder siblings of the source and its parent
    candidates.push_back(src / "shaders" / fs::path(filePath).filename());
    candidates.push_back(src.parent_path() / "shaders" / fs::path(filePath).filename());

    // Try executable directory (useful when running from VS where cwd is the binary folder)
#ifdef _WIN32
    char exePath[MAX_PATH] = {0};
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH)) {
        fs::path exedir = fs::path(exePath).parent_path();
        candidates.push_back(exedir / filePath);
        candidates.push_back(exedir / "shaders" / fs::path(filePath).filename());
    }

#ifdef USE_ASSIMP
// Try to load diffuse textures referenced by the scene using Assimp.
// For glTF files that reference external PNG/JPG files this resolves relative
// paths (scene folder, scene/textures, or working-folder models/textures) and
// loads the first valid diffuse texture found. We avoid flipping UVs because
// glTF uses top-left origin for images.
static bool tryLoadTexturesWithAssimp(const char* scenePath) {
    if (!scenePath) return false;
    Assimp::Importer importer;
    // Recommended flags for glTF: triangulate and generate smooth normals/tangents
    const aiScene* scene = importer.ReadFile(scenePath,
        aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);
    if (!scene) return false;
    try {
        namespace fs = std::filesystem;
        fs::path sceneDir = fs::path(scenePath).parent_path();
        for (unsigned int mi = 0; mi < scene->mNumMaterials; ++mi) {
            aiMaterial* mat = scene->mMaterials[mi];
            aiString texPath;
            if (mat->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) == AI_SUCCESS || mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
                std::string rel = texPath.C_Str();
                // Try direct relative path: sceneDir / rel
                std::vector<fs::path> candidates;
                candidates.push_back(sceneDir / rel);
                // If rel is just a filename, try sceneDir/textures/<file>
                candidates.push_back(sceneDir / "textures" / rel);
                // Also try working directory ./models/textures/<file>
                candidates.push_back(fs::current_path() / "models" / "textures" / rel);
                // Normalise and try each candidate
                for (auto &cand : candidates) {
                    try {
                        if (!cand.empty() && fs::exists(cand) && fs::is_regular_file(cand)) {
                            g_parsedImagePath = cand.string();
                            g_modelTexture = loadTextureFromFile(g_parsedImagePath, true);
                            if (g_modelTexture != 0) return true;
                        }
                    } catch(...) {}
                }
            }
        }
    } catch(...) {}
    return false;
}
#endif


#else
    // On POSIX we can try /proc/self/exe if available
    try {
        fs::path proc = "/proc/self/exe";
        if (fs::exists(proc)) {
            fs::path exedir = fs::read_symlink(proc).parent_path();
            candidates.push_back(exedir / filePath);
            candidates.push_back(exedir / "shaders" / fs::path(filePath).filename());
        }
    } catch(...) {}
#endif

    // Try all candidates and return first one that opens
    for (const auto &p : candidates) {
        try {
            if (!p.empty() && fs::exists(p) && fs::is_regular_file(p)) {
                std::ifstream file(p);
                if (file.is_open()) {
                    std::stringstream ss;
                    ss << file.rdbuf();
                    std::cout << "Loaded shader: " << p.string() << std::endl;
                    return ss.str();
                }
            }
        } catch (const std::exception &) {
            // ignore and continue trying other candidates
        }
    }

    // If none found, print diagnostics showing attempted paths
    std::cout << "Failed to open file: " << filePath << std::endl;
    std::cout << "Tried paths:" << std::endl;
    for (const auto &p : candidates) std::cout << "  " << p.string() << std::endl;
    return std::string();
}
// mouse/input/OBJ loader moved below globals

// Estado global para interacción por teclado
static GLenum g_polygonMode = GL_FILL;      // modo inicial de polígonos (GL_FILL o GL_LINE)
// rotation feature removed; keep scene static. Wireframe toggle remains.

// --- Cámara / movimiento (WASD + mouse look) ---
static glm::vec3 g_camPos = glm::vec3(0.0f, 0.0f, 3.0f);
static glm::vec3 g_camFront = glm::vec3(0.0f, 0.0f, -1.0f);
static glm::vec3 g_camUp = glm::vec3(0.0f, 1.0f, 0.0f);
static float g_yaw = -90.0f; // apunta a -Z
static float g_pitch = 0.0f;
static float g_lastX = 400.0f, g_lastY = 400.0f;
static bool g_firstMouse = true;
static float g_moveSpeed = 2.5f;
static float g_mouseSensitivity = 0.1f;

// Forward declarations for manager types (pointers declared here, definitions later)
struct CameraController; struct BezierCameraPath; struct InputManager;
static CameraController* g_cameraController = nullptr;
static BezierCameraPath* g_bezierPath = nullptr;
static InputManager* g_inputManager = nullptr;

// Define Bezier / camera path globals (external linkage expected by camera_input.h)
bool g_followBezier = false;
float g_bezierT = 0.0f;
float g_bezierSpeed = 0.25f;
glm::vec3 g_bezierP0 = glm::vec3(0.0f), g_bezierP1 = glm::vec3(0.0f), g_bezierP2 = glm::vec3(0.0f), g_bezierP3 = glm::vec3(0.0f);

// Define CameraController, BezierCameraPath and InputManager
// (full definitions placed here so later code can use them)
struct CameraController {
    glm::vec3 Position;
    glm::vec3 Front;
    glm::vec3 Up;
    float Yaw;
    float Pitch;
    float MoveSpeed;
    float MouseSensitivity;
    CameraController(): Position(g_camPos), Front(g_camFront), Up(g_camUp), Yaw(g_yaw), Pitch(g_pitch), MoveSpeed(2.5f), MouseSensitivity(0.1f) {}
    void UpdateFromGlobals() { Position = g_camPos; Front = g_camFront; Up = g_camUp; Yaw = g_yaw; Pitch = g_pitch; }
    void ApplyToGlobals() { g_camPos = Position; g_camFront = Front; g_camUp = Up; g_yaw = Yaw; g_pitch = Pitch; }
    glm::mat4 GetView() const { return glm::lookAt(Position, Position + Front, Up); }
    void ProcessMovement(const glm::vec3 &dir, float dt) { Position += dir * MoveSpeed * dt; }
    void ProcessMouseDelta(float dx, float dy) {
        Yaw += dx * MouseSensitivity; Pitch += dy * MouseSensitivity; if (Pitch>89.0f) Pitch=89.0f; if (Pitch<-89.0f) Pitch=-89.0f;
        glm::vec3 f; f.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch)); f.y = sin(glm::radians(Pitch)); f.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch)); Front = glm::normalize(f);
    }
};

struct BezierCameraPath {
    glm::vec3 P0,P1,P2,P3; float Speed; float t;
    BezierCameraPath():P0(0),P1(0),P2(0),P3(0),Speed(0.25f),t(0.0f){}
    glm::vec3 Point(float tt) const { float u=1.0f-tt; return u*u*u*P0 + 3.0f*u*u*tt*P1 + 3.0f*u*tt*tt*P2 + tt*tt*tt*P3; }
    void Update(float dt) { t += Speed*dt; if (t>1.0f) t -= floor(t); }
};


// Shim implementations for callbacks to call into InputManager methods without
// requiring the type at the callback site.
// InputManager implementation is in this translation unit (Main.cpp) so that the
// header camera_input.h can declare extern pointers without defining the type body.
struct InputManager {
    CameraController *Cam;
    BezierCameraPath *Path;
    bool CursorCaptured = false;
    InputManager(CameraController* c=nullptr, BezierCameraPath* p=nullptr):Cam(c),Path(p){}
    void ProcessKeyboard(GLFWwindow* window, float dt) {
        if (g_followBezier) return;
        if (!Cam) return;
        glm::vec3 right = glm::normalize(glm::cross(Cam->Front, Cam->Up));
        glm::vec3 dir(0.0f);
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) dir += Cam->Front;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) dir -= Cam->Front;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) dir -= right;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) dir += right;
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) dir += Cam->Up;
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) dir -= Cam->Up;
        if (glm::length(dir) > 0.0001f) Cam->ProcessMovement(glm::normalize(dir), dt);
        Cam->ApplyToGlobals();
    }
    void OnMouseMove(GLFWwindow* window, double xpos, double ypos) {
        if (!CursorCaptured || !Cam) return;
        if (g_firstMouse) { g_lastX = (float)xpos; g_lastY = (float)ypos; g_firstMouse=false; }
        float dx = (float)xpos - g_lastX; float dy = g_lastY - (float)ypos; g_lastX = (float)xpos; g_lastY = (float)ypos;
        Cam->ProcessMouseDelta(dx, dy); Cam->ApplyToGlobals();
    }
    void OnKey(GLFWwindow* window, int key, int scancode, int action, int mods) {
        if (action != GLFW_PRESS) return;
        if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, GLFW_TRUE);
        else if (key == GLFW_KEY_M) { g_polygonMode = (g_polygonMode==GL_FILL?GL_LINE:GL_FILL); glPolygonMode(GL_FRONT_AND_BACK, g_polygonMode); }
        else if (key == GLFW_KEY_TAB) { CursorCaptured = !CursorCaptured; glfwSetInputMode(window, GLFW_CURSOR, CursorCaptured?GLFW_CURSOR_DISABLED:GLFW_CURSOR_NORMAL); g_firstMouse=true; }
        else if (key == GLFW_KEY_P) { g_followBezier = !g_followBezier; g_firstMouse = true; }
        else if (key == GLFW_KEY_L) { if (Path) { Path->Speed *= 1.25f; if (Path->Speed>10.0f) Path->Speed=10.0f; } }
        else if (key == GLFW_KEY_O) { if (Path) { Path->Speed *= 0.8f; if (Path->Speed<0.01f) Path->Speed=0.01f; } }
    }
};

void InputManager_ProcessKeyboard(GLFWwindow* window, float deltaTime) { if (g_inputManager) g_inputManager->ProcessKeyboard(window, deltaTime); }
void InputManager_OnMouseMove_Shared(GLFWwindow* window, double xpos, double ypos) { if (g_inputManager) g_inputManager->OnMouseMove(window, xpos, ypos); }
void InputManager_OnKey_Shared(GLFWwindow* window, int key, int scancode, int action, int mods) { if (g_inputManager) g_inputManager->OnKey(window, key, scancode, action, mods); }

// Camera Bézier path globals (shared with camera_input.h via extern)



// Camera cinematic settings
static float g_fovDegrees = 30.0f; // narrower FOV for a closer, cinematic view
static float g_nearClip = 0.001f; // small near plane to avoid clipping when very close
static float g_farClip = 1000.0f;

// Precomputed curve samples

// Model loaded from OBJ/GLTF
static bool g_modelLoaded = false;
static unsigned int g_modelVAO = 0, g_modelVBO = 0, g_modelEBO = 0;
static int g_modelIndexCount = 0;
static glm::vec3 g_modelCenter = glm::vec3(0.0f);
static float g_modelScale = 1.0f;

// Parsed data (produced by background thread) - main thread will create GL buffers when ready
static std::vector<float> g_parsedVertices;
static std::vector<unsigned int> g_parsedIndices;
static unsigned int g_modelTexture = 0;
static std::string g_parsedImagePath;
static std::atomic<bool> g_parsedReady(false);
static std::atomic<bool> g_parsing(false);
static std::atomic<bool> g_parseError(false);
static std::thread g_parseThread;
static std::atomic<int> g_parseProgress(0); // 0..100
static std::atomic<int> g_parseETASeconds(-1); // estimated seconds remaining (-1 unknown)
static std::atomic<int> g_lastConsolePrinted(-1);
static unsigned int g_curveVAO = 0, g_curveVBO = 0;
static int g_curveVertexCount = 0;
// Precomputed curve samples (single declaration)
std::vector<glm::vec3> g_curvePointsWorld;
std::vector<float> g_curveCumLen;
float g_curveTotalLen = 0.0f;

// Load a 2D texture from file using stb_image and create an OpenGL texture.
// Load a 2D texture from file using stb_image and create an OpenGL texture.
// If srgb==true the internal format will be sRGB to preserve correct albedo color space for PBR.
static unsigned int loadTextureFromFile(const std::string &path, bool srgb = false) {
    int w,h,n;
    // glTF textures are stored with the origin at top-left; do not flip vertically.
    stbi_set_flip_vertically_on_load(0);
    unsigned char *data = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!data) {
        std::cout << "Failed to load texture: " << path << std::endl;
        return 0;
    }
    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    // Ensure correct byte alignment for tightly packed image data
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GLenum internal = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(data);
    std::cout << "Loaded texture: " << path << std::endl;
    return tex;
}

// Find first image file in the same folder as the scene or inside its subfolders
static std::string findTextureForScene(const std::string &scenePath) {
    namespace fs = std::filesystem;
    try {
        fs::path p(scenePath);
        fs::path dir = p.parent_path();
        if (fs::exists(dir) && fs::is_directory(dir)) {
            for (auto &it : fs::recursive_directory_iterator(dir)) {
                if (!it.is_regular_file()) continue;
                auto ext = it.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
                    return it.path().string();
                }
            }
        }
    } catch(...) {}
    return std::string();
}

// Mouse look callback
static void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    // When following the Bézier path, disable mouse-look to avoid fighting the automated camera
    if (g_followBezier) return;
    // Forward to InputManager if present (it may override mouse behavior)
    if (g_inputManager) { InputManager_OnMouseMove_Shared(window, xpos, ypos); return; }

    if (g_firstMouse) { g_lastX = (float)xpos; g_lastY = (float)ypos; g_firstMouse = false; }
    float xoffset = (float)xpos - g_lastX;
    float yoffset = g_lastY - (float)ypos; // reversed
    g_lastX = (float)xpos; g_lastY = (float)ypos;
    xoffset *= g_mouseSensitivity; yoffset *= g_mouseSensitivity;
    g_yaw += xoffset; g_pitch += yoffset;
    if (g_pitch > 89.0f) g_pitch = 89.0f;
    if (g_pitch < -89.0f) g_pitch = -89.0f;
    glm::vec3 front;
    front.x = cos(glm::radians(g_yaw)) * cos(glm::radians(g_pitch));
    front.y = sin(glm::radians(g_pitch));
    front.z = sin(glm::radians(g_yaw)) * cos(glm::radians(g_pitch));
    g_camFront = glm::normalize(front);
}

// (CameraController, BezierCameraPath and InputManager are defined above once)

// Process continuous input for movement
static void processInput(GLFWwindow* window, float deltaTime)
{
    // forward to input manager (use shim so InputManager can be defined later)
    InputManager_ProcessKeyboard(window, deltaTime);
}

// Evaluate cubic Bézier point and tangent
static glm::vec3 bezierPoint(float t) {
    float u = 1.0f - t;
    float b0 = u * u * u;
    float b1 = 3.0f * u * u * t;
    float b2 = 3.0f * u * t * t;
    float b3 = t * t * t;
    return b0 * g_bezierP0 + b1 * g_bezierP1 + b2 * g_bezierP2 + b3 * g_bezierP3;
}
static glm::vec3 bezierTangent(float t) {
    // derivative of cubic Bézier
    float u = 1.0f - t;
    glm::vec3 d = 3.0f * u * u * (g_bezierP1 - g_bezierP0)
                + 6.0f * u * t * (g_bezierP2 - g_bezierP1)
                + 3.0f * t * t * (g_bezierP3 - g_bezierP2);
    return d;
}

// OBJ loading removed per request: .obj parsing and GL buffer creation are no longer present.

#ifdef USE_TINYGLTF
// Parse-only GLTF loader using tinygltf
static bool parseGLTF(const char* path, std::vector<float> &outVertices, std::vector<unsigned int> &outIndices, glm::vec3 &outCenter, float &outScale)
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;
    bool ret = false;
    // try binary first, then ascii
    ret = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    if (!ret) ret = loader.LoadASCIIFromFile(&model, &err, &warn, path);
    if (!warn.empty()) std::cout << "tinygltf warning: " << warn << std::endl;
    if (!err.empty()) std::cout << "tinygltf error: " << err << std::endl;
    if (!ret) return false;

    // Iterate meshes/primitives and accumulate a single vertex/index stream
    for (const auto &mesh : model.meshes) {
        for (const auto &prim : mesh.primitives) {
            int posAccessorIndex = -1;
            int normAccessorIndex = -1;
            int uvAccessorIndex = -1;
            int idxAccessorIndex = -1;
            for (const auto &attr : prim.attributes) {
                if (attr.first == "POSITION") posAccessorIndex = attr.second;
                else if (attr.first == "NORMAL") normAccessorIndex = attr.second;
                else if (attr.first == "TEXCOORD_0") uvAccessorIndex = attr.second;
            }
            idxAccessorIndex = prim.indices;
            if (posAccessorIndex < 0) continue; // cannot handle

            // Read positions
            const tinygltf::Accessor &posAcc = model.accessors[posAccessorIndex];
            const tinygltf::BufferView &posView = model.bufferViews[posAcc.bufferView];
            const tinygltf::Buffer &posBuffer = model.buffers[posView.buffer];
            const unsigned char* posData = posBuffer.data.data() + posView.byteOffset + posAcc.byteOffset;
            size_t posStride = posView.byteStride ? posView.byteStride : (sizeof(float)*3);

            // Normals (optional)
            const tinygltf::Accessor *normAccPtr = nullptr;
            const unsigned char* normData = nullptr;
            size_t normStride = 0;
            if (normAccessorIndex >= 0) {
                normAccPtr = &model.accessors[normAccessorIndex];
                const tinygltf::BufferView &normView = model.bufferViews[normAccPtr->bufferView];
                const tinygltf::Buffer &normBuffer = model.buffers[normView.buffer];
                normData = normBuffer.data.data() + normView.byteOffset + normAccPtr->byteOffset;
                normStride = normView.byteStride ? normView.byteStride : (sizeof(float)*3);
            }

            const tinygltf::Accessor *uvAccPtr = nullptr;
            const unsigned char* uvData = nullptr;
            size_t uvStride = 0;
            if (uvAccessorIndex >= 0) {
                uvAccPtr = &model.accessors[uvAccessorIndex];
                const tinygltf::BufferView &uvView = model.bufferViews[uvAccPtr->bufferView];
                const tinygltf::Buffer &uvBuffer = model.buffers[uvView.buffer];
                uvData = uvBuffer.data.data() + uvView.byteOffset + uvAccPtr->byteOffset;
                uvStride = uvView.byteStride ? uvView.byteStride : (sizeof(float)*2);
            }

            // Indices
            std::vector<unsigned int> primIndices;
            if (idxAccessorIndex >= 0) {
                const tinygltf::Accessor &idxAcc = model.accessors[idxAccessorIndex];
                const tinygltf::BufferView &idxView = model.bufferViews[idxAcc.bufferView];
                const tinygltf::Buffer &idxBuffer = model.buffers[idxView.buffer];
                const unsigned char* idxData = idxBuffer.data.data() + idxView.byteOffset + idxAcc.byteOffset;
                for (size_t i = 0; i < idxAcc.count; ++i) {
                    unsigned int val = 0;
                    size_t offset = i * idxAcc.ByteStride(idxView);
                    if (idxAcc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        val = *((const unsigned short*)(idxData + offset));
                    } else if (idxAcc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                        val = *((const unsigned int*)(idxData + offset));
                    } else if (idxAcc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                        val = *((const unsigned char*)(idxData + offset));
                    }
                    primIndices.push_back(val);
                }
            }

            // Append vertices and indices. We will create unique vertices (one-to-one with position accessor)
            size_t baseVertex = outVertices.size() / VERT_STRIDE;
            for (size_t i = 0; i < posAcc.count; ++i) {
                size_t offset = i * posStride;
                const float* pf = (const float*)(posData + offset);
                glm::vec3 pos(pf[0], pf[1], pf[2]);
                glm::vec3 norm(0.0f, 1.0f, 0.0f);
                if (normData) {
                    size_t noff = i * normStride;
                    const float* nf = (const float*)(normData + noff);
                    norm = glm::vec3(nf[0], nf[1], nf[2]);
                }
                glm::vec2 uv(0.0f, 0.0f);
                if (uvData) {
                    size_t uoff = i * uvStride;
                    const float* uf = (const float*)(uvData + uoff);
                    uv = glm::vec2(uf[0], uf[1]);
                }
                glm::vec3 col(0.8f,0.8f,0.8f);
                outVertices.push_back(pos.x); outVertices.push_back(pos.y); outVertices.push_back(pos.z);
                outVertices.push_back(col.r); outVertices.push_back(col.g); outVertices.push_back(col.b);
                outVertices.push_back(norm.x); outVertices.push_back(norm.y); outVertices.push_back(norm.z);
                outVertices.push_back(uv.x); outVertices.push_back(uv.y);
            }
            if (!primIndices.empty()) {
                // indices reference positions for this primitive. We need to offset by baseVertex.
                for (size_t i = 0; i < primIndices.size(); ++i) outIndices.push_back(static_cast<unsigned int>(baseVertex + primIndices[i]));
            } else {
                // no indices: assume triangles sequential
                for (size_t i = 0; i < posAcc.count; ++i) outIndices.push_back(static_cast<unsigned int>(baseVertex + i));
            }
        }
    }

    if (outVertices.empty() || outIndices.empty()) return false;

    // --- Extract per-material PBR textures and factors ---
    struct MatTexSets { std::string baseColor, normal, metallicRoughness, occlusion, emissive; glm::vec4 baseColorFactor{1,1,1,1}; float metallicFactor{1.0f}; float roughnessFactor{1.0f}; };
    std::vector<MatTexSets> mats;
    mats.resize(model.materials.size());
    for (size_t mi = 0; mi < model.materials.size(); ++mi) {
        const tinygltf::Material &mat = model.materials[mi];
        // base color factor: prefer modern field, fallback to Parameter map
        if (mat.pbrMetallicRoughness.baseColorFactor.size() == 4) {
            auto &f = mat.pbrMetallicRoughness.baseColorFactor;
            mats[mi].baseColorFactor = glm::vec4((float)f[0], (float)f[1], (float)f[2], (float)f[3]);
        } else {
            auto it = mat.values.find("baseColorFactor");
            if (it != mat.values.end()) {
                auto cf = it->second.ColorFactor();
                mats[mi].baseColorFactor = glm::vec4((float)cf[0], (float)cf[1], (float)cf[2], (float)cf[3]);
            }
        }
        // metallic/roughness
        if (mat.pbrMetallicRoughness.metallicFactor >= 0.0) mats[mi].metallicFactor = (float)mat.pbrMetallicRoughness.metallicFactor;
        else {
            auto it = mat.values.find("metallicFactor"); if (it != mat.values.end()) mats[mi].metallicFactor = (float)it->second.Factor();
        }
        if (mat.pbrMetallicRoughness.roughnessFactor >= 0.0) mats[mi].roughnessFactor = (float)mat.pbrMetallicRoughness.roughnessFactor;
        else {
            auto it = mat.values.find("roughnessFactor"); if (it != mat.values.end()) mats[mi].roughnessFactor = (float)it->second.Factor();
        }

        // Gather textures: prefer core glTF fields, fallback to Parameter texture entries
        if (mat.pbrMetallicRoughness.baseColorTexture.index >= 0) {
            int ti = mat.pbrMetallicRoughness.baseColorTexture.index;
            if (ti >= 0 && ti < (int)model.textures.size()) {
                int src = model.textures[ti].source;
                if (src >= 0 && src < (int)model.images.size()) mats[mi].baseColor = model.images[src].uri;
            }
        } else {
            auto it = mat.values.find("baseColorTexture"); if (it != mat.values.end()) { int tex = it->second.TextureIndex(); if (tex>=0 && tex < (int)model.textures.size()) { int src = model.textures[tex].source; if (src>=0 && src < (int)model.images.size()) mats[mi].baseColor = model.images[src].uri; } }
        }
        if (mat.normalTexture.index >= 0) { int ti = mat.normalTexture.index; if (ti>=0 && ti < (int)model.textures.size()) { int src = model.textures[ti].source; if (src>=0 && src < (int)model.images.size()) mats[mi].normal = model.images[src].uri; } }
        if (mat.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0) { int ti = mat.pbrMetallicRoughness.metallicRoughnessTexture.index; if (ti>=0 && ti < (int)model.textures.size()) { int src = model.textures[ti].source; if (src>=0 && src < (int)model.images.size()) mats[mi].metallicRoughness = model.images[src].uri; } }
        if (mat.occlusionTexture.index >= 0) { int ti = mat.occlusionTexture.index; if (ti>=0 && ti < (int)model.textures.size()) { int src = model.textures[ti].source; if (src>=0 && src < (int)model.images.size()) mats[mi].occlusion = model.images[src].uri; } }
        if (mat.emissiveTexture.index >= 0) { int ti = mat.emissiveTexture.index; if (ti>=0 && ti < (int)model.textures.size()) { int src = model.textures[ti].source; if (src>=0 && src < (int)model.images.size()) mats[mi].emissive = model.images[src].uri; } }
    }
    // Resolve URIs relative to the scene path and pick first baseColor as parsed image path
    for (size_t mi = 0; mi < mats.size(); ++mi) {
        auto resolve = [&](const std::string &uri)->std::string{
            if (uri.empty()) return std::string();
            if (uri.rfind("data:",0) == 0) return std::string();
            try { namespace fs = std::filesystem; fs::path scene = path; fs::path cand = scene.parent_path() / uri; if (fs::exists(cand)) return cand.string(); }
            catch(...) {}
            return uri;
        };
        mats[mi].baseColor = resolve(mats[mi].baseColor);
        mats[mi].normal = resolve(mats[mi].normal);
        mats[mi].metallicRoughness = resolve(mats[mi].metallicRoughness);
        mats[mi].occlusion = resolve(mats[mi].occlusion);
        mats[mi].emissive = resolve(mats[mi].emissive);
        if (!mats[mi].baseColor.empty() && g_parsedImagePath.empty()) g_parsedImagePath = mats[mi].baseColor;
    }

    // Recompute normals if necessary (similar to OBJ loader)
    bool needRecomputeNormals = true;
    if (outVertices.size() >= VERT_STRIDE) {
        glm::vec3 ncheck(outVertices[6], outVertices[7], outVertices[8]);
        if (glm::length(ncheck) > 0.0001f) needRecomputeNormals = false;
    }
    if (needRecomputeNormals) {
        size_t vertCount = outVertices.size() / VERT_STRIDE;
        std::vector<glm::vec3> accumNormals(vertCount, glm::vec3(0.0f));
        for (size_t i = 0; i + 2 < outIndices.size(); i += 3) {
            int ia = outIndices[i + 0];
            int ib = outIndices[i + 1];
            int ic = outIndices[i + 2];
            glm::vec3 a(outVertices[ia*VERT_STRIDE + 0], outVertices[ia*VERT_STRIDE + 1], outVertices[ia*VERT_STRIDE + 2]);
            glm::vec3 b(outVertices[ib*VERT_STRIDE + 0], outVertices[ib*VERT_STRIDE + 1], outVertices[ib*VERT_STRIDE + 2]);
            glm::vec3 c(outVertices[ic*VERT_STRIDE + 0], outVertices[ic*VERT_STRIDE + 1], outVertices[ic*VERT_STRIDE + 2]);
            glm::vec3 face = glm::cross(b - a, c - a);
            float flen = glm::length(face);
            if (flen > 1e-9f) face /= flen;
            accumNormals[ia] += face;
            accumNormals[ib] += face;
            accumNormals[ic] += face;
        }
        for (size_t vi = 0; vi < vertCount; ++vi) {
            glm::vec3 n = accumNormals[vi];
            float l = glm::length(n);
            if (l > 1e-6f) n /= l; else n = glm::vec3(0.0f, 1.0f, 0.0f);
            outVertices[vi*VERT_STRIDE + 6] = n.x; outVertices[vi*VERT_STRIDE + 7] = n.y; outVertices[vi*VERT_STRIDE + 8] = n.z;
        }
    }

    // compute bounding box and set center/scale so the model fits the view
    glm::vec3 minP( std::numeric_limits<float>::infinity());
    glm::vec3 maxP(-std::numeric_limits<float>::infinity());
    for (size_t i = 0; i + 2 < outVertices.size(); i += VERT_STRIDE) {
        glm::vec3 p(outVertices[i+0], outVertices[i+1], outVertices[i+2]);
        minP = glm::min(minP, p);
        maxP = glm::max(maxP, p);
    }
    glm::vec3 center = (minP + maxP) * 0.5f;
    glm::vec3 diag = maxP - minP;
    float largest = std::max(std::max(diag.x, diag.y), diag.z);
    if (largest <= 0.0f) largest = 1.0f;
    outCenter = center;
    outScale = 1.0f / largest;
    return true;
}
#endif

// Parse-only OBJ loader: same logic as loadOBJ but does not call OpenGL. Produces vertices/indices and computes center/scale.
static bool parseOBJ(const char* path, std::vector<float> &outVertices, std::vector<unsigned int> &outIndices, glm::vec3 &outCenter, float &outScale)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    auto fileSize = (std::uintmax_t)std::filesystem::file_size(path);
    using clock = std::chrono::steady_clock;
    auto start = clock::now();
    g_parseProgress = 0;
    g_parseETASeconds = -1;
    std::vector<glm::vec3> temp_pos;
    std::vector<glm::vec3> temp_norm;
    struct Key { int p,n; };
    struct KeyComp { bool operator()(Key a, Key b) const { return std::tie(a.p,a.n)<std::tie(b.p,b.n); } };
    std::map<Key,int,KeyComp> indexMap;
    std::string line;
    while (std::getline(file,line)) {
        // estimate progress by current read position vs file size
        std::uintmax_t pos = (std::uintmax_t)file.tellg();
        if (pos > 0 && fileSize > 0) {
            int p = (int)((pos * 100) / fileSize);
            if (p < 0) p = 0; if (p > 100) p = 100;
            g_parseProgress = p;
            // ETA estimate
            auto now = clock::now();
            double elapsed = std::chrono::duration<double>(now - start).count();
            if (p > 0 && elapsed > 0.001) {
                double totalEst = (elapsed * 100.0) / (double)p;
                int eta = (int)std::round(totalEst - elapsed);
                if (eta < 0) eta = 0;
                g_parseETASeconds = eta;
            }
            // print to console when progress changes to avoid flooding
            int last = g_lastConsolePrinted.load();
            if (p != last && (p % 1 == 0)) {
                g_lastConsolePrinted = p;
                int eta = g_parseETASeconds.load();
                if (eta >= 0) std::cout << "Parsing OBJ: " << p << "% (ETA " << eta << "s)\n";
                else std::cout << "Parsing OBJ: " << p << "%\n";
            }
        }
        if (line.size() < 2) continue;
        if (line[0]=='v' && line[1]==' ') {
            std::istringstream ss(line.substr(2));
            glm::vec3 p; ss >> p.x >> p.y >> p.z; temp_pos.push_back(p);
        } else if (line[0]=='v' && line[1]=='n') {
            std::istringstream ss(line.substr(3));
            glm::vec3 n; ss >> n.x >> n.y >> n.z; temp_norm.push_back(n);
        } else if (line[0]=='f' && line[1]==' ') {
            std::istringstream ss(line.substr(2));
            std::string a,b,c;
            ss >> a >> b >> c;
            std::string verts[3] = {a,b,c};
            for (int i=0;i<3;++i) {
                std::string v = verts[i];
                int vi=0, ni=0;
                size_t p1 = v.find('/');
                if (p1==std::string::npos) { vi = std::stoi(v); }
                else {
                    vi = std::stoi(v.substr(0,p1));
                    size_t p2 = v.find('/', p1+1);
                    if (p2!=std::string::npos) {
                        if (p2!=p1+1) ni = std::stoi(v.substr(p2+1));
                        else ni = std::stoi(v.substr(p2+1));
                    }
                }
                if (vi<0) vi = (int)temp_pos.size()+1+vi;
                if (ni<0) ni = (int)temp_norm.size()+1+ni;
                Key key{vi-1, ni-1};
                auto it = indexMap.find(key);
                if (it!=indexMap.end()) {
                    outIndices.push_back(it->second);
                } else {
                    int newIndex = (int)(outVertices.size()/9);
                    indexMap[key] = newIndex;
                    glm::vec3 pos = temp_pos[key.p];
                    glm::vec3 norm = (key.n>=0 && key.n < (int)temp_norm.size()) ? temp_norm[key.n] : glm::vec3(0.0f,1.0f,0.0f);
                    glm::vec3 col(0.8f,0.8f,0.8f);
                    outVertices.push_back(pos.x); outVertices.push_back(pos.y); outVertices.push_back(pos.z);
                    outVertices.push_back(col.r); outVertices.push_back(col.g); outVertices.push_back(col.b);
                    outVertices.push_back(norm.x); outVertices.push_back(norm.y); outVertices.push_back(norm.z);
                    outIndices.push_back(newIndex);
                }
            }
        }
    }
    if (outVertices.empty() || outIndices.empty()) return false;
    // If the OBJ didn't provide vertex normals, compute smooth vertex normals
    bool needRecomputeNormals = true;
    if (outVertices.size() >= 9) {
        glm::vec3 ncheck(outVertices[6], outVertices[7], outVertices[8]);
        if (glm::length(ncheck) > 0.0001f) needRecomputeNormals = false;
    }
    if (needRecomputeNormals) {
        size_t vertCount = outVertices.size() / 9;
        std::vector<glm::vec3> accumNormals(vertCount, glm::vec3(0.0f));
        for (size_t i = 0; i + 2 < outIndices.size(); i += 3) {
            int ia = outIndices[i + 0];
            int ib = outIndices[i + 1];
            int ic = outIndices[i + 2];
            glm::vec3 a(outVertices[ia*9 + 0], outVertices[ia*9 + 1], outVertices[ia*9 + 2]);
            glm::vec3 b(outVertices[ib*9 + 0], outVertices[ib*9 + 1], outVertices[ib*9 + 2]);
            glm::vec3 c(outVertices[ic*9 + 0], outVertices[ic*9 + 1], outVertices[ic*9 + 2]);
            glm::vec3 face = glm::cross(b - a, c - a);
            float flen = glm::length(face);
            if (flen > 1e-9f) face /= flen;
            accumNormals[ia] += face;
            accumNormals[ib] += face;
            accumNormals[ic] += face;
        }
        for (size_t vi = 0; vi < vertCount; ++vi) {
            glm::vec3 n = accumNormals[vi];
            float l = glm::length(n);
            if (l > 1e-6f) n /= l; else n = glm::vec3(0.0f, 1.0f, 0.0f);
            outVertices[vi*9 + 6] = n.x; outVertices[vi*9 + 7] = n.y; outVertices[vi*9 + 8] = n.z;
        }
    }
    // compute bounding box and set center/scale so the model fits the view
    glm::vec3 minP( std::numeric_limits<float>::infinity());
    glm::vec3 maxP(-std::numeric_limits<float>::infinity());
    for (size_t i = 0; i + 2 < outVertices.size(); i += 9) {
        glm::vec3 p(outVertices[i+0], outVertices[i+1], outVertices[i+2]);
        minP = glm::min(minP, p);
        maxP = glm::max(maxP, p);
    }
    glm::vec3 center = (minP + maxP) * 0.5f;
    glm::vec3 diag = maxP - minP;
    float largest = std::max(std::max(diag.x, diag.y), diag.z);
    if (largest <= 0.0f) largest = 1.0f;
    outCenter = center;
    outScale = 1.0f / largest;
    return true;
}

// Callback de teclado: solo mantener toggle de modo (fill/line), TAB para mouse y ESC para salir
static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (g_inputManager) InputManager_OnKey_Shared(window, key, scancode, action, mods);
}


int main() {
    //INICIA GHLFW y si no se pudo iniciar, muestra un error y termina el programa
    if (!glfwInit()) {
        std::cout << "Failed to initialize GLFW" << std::endl;
        return -1;
    }
    // ussa GLFW para decir que version de openGL se esta usando
    // en este caso se esta usando la 3.3
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    //dice si GLFW est usando el perfil de CORE
    // eso significa que solo tiene las funciones modernas de OpenGL, no tiene las funciones antiguas
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // crea la ventana la vetana de 1280x720 pixeles, título ajustado para enfoque en modelos GLTF
    GLFWwindow* window = glfwCreateWindow(1280, 720, "GLTF Viewer - Model Focus", NULL, NULL);
    // salta un error si fallo al momento de crear la ventana y termina el programa
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    // introduce la ventana dentro del contexto de open GL para que se pueda usar
    glfwMakeContextCurrent(window);

    // Inicializar GLAD usando el cargador de GLFW's 
    //accede a las funciones de controladores graficos en tiempo de ejecucion 
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    // Enable multisampling (anti-aliasing) and face culling to improve visual quality
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    // Enable sRGB framebuffer if available for correct gamma
    glEnable(GL_FRAMEBUFFER_SRGB);

    // Ajustar la vista al tamaño del framebuffer (importante en pantallas HiDPI)
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);

    // Removed built-in prism; we'll render only the loaded model (room) if available.


    // Leer shaders desde archivos externos (carpeta "shaders")
    // Usamos rutas relativas al directorio de trabajo del ejecutable: "shaders/..."
    std::string vertexCode = readFile("shaders/vertex.glsl"); // leer vertex shader
    std::string fragmentCode = readFile("shaders/fragment.glsl"); // leer fragment shader

    // Embedded fallback shaders (option 1): use these when files are not available.
    static const char* embeddedVertex = R"SHADER(#version 330 core
// Vertex shader simple: recibe la posición del vértice y la convierte a clip space.
// No maneja colores aquí: el color se toma desde un uniforme en el fragment shader.

// Ahora también recibe color por vértice y pasa al fragment shader
layout (location = 0) in vec3 aPos; // posición del vértice
layout (location = 1) in vec3 aColor; // color por vértice
layout (location = 2) in vec3 aNormal; // normal por vértice

// Matriz MVP (Model * View * Projection) que transformará los vértices a clip space
uniform mat4 uMVP;
// Matriz de modelo (para transformar normales y calcular posición de fragmento)
uniform mat4 uModel;

// Color que se interpola y llega al fragment shader
out vec3 vColor;
out vec3 vNormal;
out vec3 vFragPos;

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
}
)SHADER";

    static const char* embeddedFragment = R"SHADER(#version 330 core
// Fragment shader with directional + point lighting plus a camera-linked spotlight
in vec3 vColor;
in vec3 vNormal;
in vec3 vFragPos;

out vec4 FragColor;

uniform vec3 uDirLightDir;
uniform vec3 uDirLightColor;
uniform vec3 uPointLightPos;
uniform vec3 uPointLightColor;
uniform vec3 uSpotPos;
uniform vec3 uSpotDir;
uniform vec3 uSpotColor;
uniform float uSpotCutOff;      // cos(innerAngle)
uniform float uSpotOuterCutOff; // cos(outerAngle)
uniform vec3 uViewPos;
uniform float uAmbientStrength;
uniform float uSpecularStrength;
uniform float uShininess;

void main()
{
    vec3 N = normalize(vNormal);
    vec3 viewDir = normalize(uViewPos - vFragPos);

    // Ambient
    vec3 ambient = uAmbientStrength * vColor;

    // Directional light (soft key)
    vec3 dirL = normalize(-uDirLightDir);
    float diffD = max(dot(N, dirL), 0.0);
    vec3 diffuseD = diffD * uDirLightColor * vColor;

    // Point light (near model)
    vec3 pointLdir = normalize(uPointLightPos - vFragPos);
    float diffP = max(dot(N, pointLdir), 0.0);
    float distP = length(uPointLightPos - vFragPos);
    float attenuationP = 1.0 / (1.0 + 0.1 * distP + 0.02 * distP * distP);
    vec3 diffuseP = diffP * uPointLightColor * vColor * attenuationP;
    vec3 reflectP = reflect(-pointLdir, N);
    float specP = pow(max(dot(viewDir, reflectP), 0.0), uShininess);
    vec3 specularP = uSpecularStrength * vec3(1.0) * specP * attenuationP;

    // Spotlight (camera-linked)
    vec3 Lspot = normalize(uSpotPos - vFragPos); // direction from fragment to light
    float diffS = max(dot(N, Lspot), 0.0);
    float distS = length(uSpotPos - vFragPos);
    float attenuationS = 1.0 / (1.0 + 0.05 * distS + 0.01 * distS * distS);
    // angle between spot direction and vector from spot to fragment
    float theta = dot(normalize(uSpotDir), normalize(vFragPos - uSpotPos));
    float epsilon = max(0.0001, uSpotCutOff - uSpotOuterCutOff);
    float intensity = clamp((theta - uSpotOuterCutOff) / epsilon, 0.0, 1.0);
    vec3 diffuseS = diffS * uSpotColor * vColor * intensity * attenuationS;
    vec3 reflectS = reflect(-Lspot, N);
    float specS = pow(max(dot(viewDir, reflectS), 0.0), uShininess);
    vec3 specularS = uSpecularStrength * vec3(1.0) * specS * intensity * attenuationS;

    // Compose final color with spotlight emphasized
    vec3 result = ambient * 0.6 + diffuseD * 0.6 + diffuseP * 0.6 + diffuseS * 1.8 + specularP + specularS;
    // subtle tone mapping / gamma correction
    result = pow(max(result, vec3(0.0)), vec3(1.0/2.2));
    FragColor = vec4(result, 1.0);
}
)SHADER";

    if (vertexCode.empty()) {
        vertexCode = embeddedVertex;
        std::cout << "Using embedded vertex shader" << std::endl;
    }
    if (fragmentCode.empty()) {
        fragmentCode = embeddedFragment;
        std::cout << "Using embedded fragment shader" << std::endl;
    }

    const char* vertexSource = vertexCode.c_str(); // obtener puntero a datos C
    const char* fragmentSource = fragmentCode.c_str(); // obtener puntero a datos C

    // Compilar vertex shader
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER); // crear objeto shader
    glShaderSource(vertexShader, 1, &vertexSource, NULL); // asignar código fuente
    glCompileShader(vertexShader); // compilar shader
    int success; // variable para checkear estado
    char infoLog[512]; // buffer para logs de error
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success); // obtener estado de compilación
    if (!success) { // si compilación falló
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog); // obtener log
        std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl; // imprimir error
    }

    // Compilar fragment shader
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER); // crear fragment shader 
    glShaderSource(fragmentShader, 1, &fragmentSource, NULL); // asignar código fuente
    glCompileShader(fragmentShader); // compilar
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success); // comprobar compilación
    if (!success) { // si hay error
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog); // obtener log
        std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl; // imprimir
    }

    // Linkear shaders en un programa
    unsigned int shaderProgram = glCreateProgram(); // crear programa de shaders
    glAttachShader(shaderProgram, vertexShader); // adjuntar vertex shader
    glAttachShader(shaderProgram, fragmentShader); // adjuntar fragment shader
    glLinkProgram(shaderProgram); // linkear programa
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success); // comprobar linking
    if (!success) { // si linking falló
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog); // obtener log
        std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl; // imprimir
    }
    glDeleteShader(vertexShader); // liberar objeto shader ya linkeado
    glDeleteShader(fragmentShader); // liberar fragment shader

    // Preparar matrices de cámara/proyección usando GLM ahora que conocemos width/height
    // Usar near más pequeño para evitar recorte cercano en escenas grandes. Far mayor para profundidad amplia.
    glm::mat4 proj = glm::perspective(glm::radians(g_fovDegrees), (float)width / (float)height, g_nearClip, g_farClip);
    glm::vec3 eye(0.0f, 0.0f, 1.5f);
    glm::vec3 center(0.0f, 0.0f, 0.0f);
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    glm::mat4 view = glm::lookAt(eye, center, up);

    // Obtener ubicación de los uniformes y establecer color inicial
    glUseProgram(shaderProgram);
    int uMVPLoc = glGetUniformLocation(shaderProgram, "uMVP");
    int uModelLoc = glGetUniformLocation(shaderProgram, "uModel");
    int uColorLoc = glGetUniformLocation(shaderProgram, "uColor");
    int uDirLightDirLoc = glGetUniformLocation(shaderProgram, "uDirLightDir");
    int uDirLightColorLoc = glGetUniformLocation(shaderProgram, "uDirLightColor");
    int uPointLightPosLoc = glGetUniformLocation(shaderProgram, "uPointLightPos");
    int uPointLightColorLoc = glGetUniformLocation(shaderProgram, "uPointLightColor");
    int uSpotPosLoc = glGetUniformLocation(shaderProgram, "uSpotPos");
    int uSpotDirLoc = glGetUniformLocation(shaderProgram, "uSpotDir");
    int uSpotColorLoc = glGetUniformLocation(shaderProgram, "uSpotColor");
    int uSpotCutOffLoc = glGetUniformLocation(shaderProgram, "uSpotCutOff");
    int uSpotOuterCutOffLoc = glGetUniformLocation(shaderProgram, "uSpotOuterCutOff");
    int uLightPosLoc = glGetUniformLocation(shaderProgram, "uLightPos");
    int uViewPosLoc = glGetUniformLocation(shaderProgram, "uViewPos");
    int uAmbientLoc = glGetUniformLocation(shaderProgram, "uAmbientStrength");
    int uSpecularLoc = glGetUniformLocation(shaderProgram, "uSpecularStrength");
    int uShininessLoc = glGetUniformLocation(shaderProgram, "uShininess");
    int uAlbedoLoc = glGetUniformLocation(shaderProgram, "uAlbedoTex");
    if (uColorLoc != -1) {
        glUniform3f(uColorLoc, 0.9f, 0.4f, 0.2f);
    }
    if (uAmbientLoc != -1) glUniform1f(uAmbientLoc, 0.12f);
    if (uSpecularLoc != -1) glUniform1f(uSpecularLoc, 0.6f);
    if (uShininessLoc != -1) glUniform1f(uShininessLoc, 32.0f);
    if (uAlbedoLoc != -1) glUniform1i(uAlbedoLoc, 0); // texture unit 0

    // set up default lighting values (will be updated every frame)
    if (uDirLightColorLoc != -1) glUniform3f(uDirLightColorLoc, 0.8f, 0.8f, 0.78f);
    if (uPointLightColorLoc != -1) glUniform3f(uPointLightColorLoc, 1.0f, 0.95f, 0.9f);
    // default spotlight color
    if (uSpotColorLoc != -1) glUniform3f(uSpotColorLoc, 1.0f, 0.98f, 0.9f);
    if (uSpotCutOffLoc != -1) glUniform1f(uSpotCutOffLoc, cos(glm::radians(10.0f)));
    if (uSpotOuterCutOffLoc != -1) glUniform1f(uSpotOuterCutOffLoc, cos(glm::radians(18.0f)));

    // Create controllers and input manager
    CameraController cameraCtrl;
    BezierCameraPath bezier;
    g_cameraController = &cameraCtrl; g_bezierPath = &bezier;
    InputManager inputMgr(&cameraCtrl, &bezier);
    g_inputManager = &inputMgr;

    // Initialize camera controller from globals for smooth starts
    cameraCtrl.UpdateFromGlobals();

    // Register mouse callback and start with normal cursor
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    std::cout << "Controls: WASD move, Q/E up-down, TAB toggle mouse-look, ESC quit" << std::endl;
    std::cout << "P toggles camera follow (Bézier). O/L decrease/increase path speed." << std::endl;

    // Ensure models folder is present in the working directory. If not, try to copy
    // it from the workspace path where you keep the assets.
    namespace fs = std::filesystem;
    fs::path dstModels = fs::current_path() / "models";
    fs::path srcModels = R"(C:\Users\fr840\OneDrive\Desktop\programacion\youtubeopenGl\models)";
    if (!fs::exists(dstModels) && fs::exists(srcModels)) {
        try {
            fs::copy(srcModels, dstModels, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            std::cout << "Copied models folder to working directory\n";
        } catch (const std::exception &) {
            std::cout << "Failed to copy models folder" << std::endl;
        }
    }

    // Start asynchronous parsing of model to avoid blocking the UI while large files load.
    // Build a persistent std::string to hold the chosen path (we'll pass .c_str() to the thread)
    std::string scenePathStr;
    // Try runtime working directory ./models/scene.gltf first
    fs::path candidate = fs::current_path() / "models" / "scene.gltf";
    if (fs::exists(candidate)) {
#ifdef USE_TINYGLTF
        scenePathStr = candidate.string();
#else
        std::cout << "Found scene.gltf but tinygltf support is not enabled. Define USE_TINYGLTF to load glTF files." << std::endl;
#endif
    } else {
        // Try project source folder relative to this source file: ../../models/scene.gltf
        fs::path sourceModels = fs::path(__FILE__).parent_path().parent_path() / "models" / "scene.gltf";
        if (fs::exists(sourceModels)) {
#ifdef USE_TINYGLTF
            scenePathStr = sourceModels.string();
#else
            std::cout << "Found scene.gltf in project folder but tinygltf not enabled." << std::endl;
#endif
        } else {
            // search recursively under project root (two levels up from this file) for any .gltf/.glb
            bool found = false;
            fs::path projectRoot = fs::path(__FILE__).parent_path().parent_path();
            for (auto &p : fs::recursive_directory_iterator(projectRoot)) {
                if (!p.is_regular_file()) continue;
                auto ext = p.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".gltf" || ext == ".glb") {
#ifdef USE_TINYGLTF
                    scenePathStr = p.path().string();
                    std::cout << "Found glTF model: " << scenePathStr << std::endl;
                    found = true;
                    break;
#else
                    std::cout << "Found glTF file at " << p.path().string() << " but tinygltf is not enabled. Define USE_TINYGLTF." << std::endl;
                    found = true;
                    break;
#endif
                }
            }
            if (!found) {
                std::cout << "No glTF model found (OBJ support removed). Place a .gltf/.glb into the workspace or ./models/." << std::endl;
            }
        }
    }

    const char* scenePath = scenePathStr.empty() ? nullptr : scenePathStr.c_str();
    if (scenePath) {
        g_parsing = true;
        std::cout << "Starting background parse of: " << scenePath << std::endl;
        // pass pointer to stored C string into thread; scenePathStr lives in this scope
        g_parseThread = std::thread([scenePath]() {
            std::vector<float> verts;
            std::vector<unsigned int> inds;
            glm::vec3 center(0.0f);
            float scale = 1.0f;
            bool ok = false;
#ifdef USE_TINYGLTF
            if (std::string(scenePath).size() >= 6 && std::string(scenePath).substr(std::string(scenePath).size()-5) == ".gltf") {
                ok = parseGLTF(scenePath, verts, inds, center, scale);
            } else {
                ok = false;
            }
#else
            ok = false; // OBJ support removed
#endif
            if (ok) {
                // move parsed data into global buffers
                g_parsedVertices = std::move(verts);
                // Ensure index type fits expected unsigned int
                g_parsedIndices.clear(); g_parsedIndices.reserve(inds.size());
                for (size_t i=0;i<inds.size();++i) g_parsedIndices.push_back(static_cast<unsigned int>(inds[i]));
                g_modelCenter = center;
                g_modelScale = scale;
                // parsed image path may have been set inside parseGLTF
                // (g_parsedImagePath set by parseGLTF when image found)
                g_parsedReady = true;
            } else {
                std::cout << "Failed to parse model in background thread" << std::endl;
                g_parseError = true;
            }
            g_parsing = false;
        });
    } else {
        std::cout << "No model file found at models/scene.obj or models/scene.gltf" << std::endl;
    }

    // (HUD removed: stb_easy_font and text rendering not used)

    // Establecer modo de polígonos inicial (puede ser cambiado con la tecla M)
    glPolygonMode(GL_FRONT_AND_BACK, g_polygonMode);

    // Registrar callback de teclado para interacción
    glfwSetKeyCallback(window, key_callback);

    float startTime = (float)glfwGetTime();
    float lastFrame = startTime;
    float deltaTime = 0.0f;

    // Main simplified render loop
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // process input
        processInput(window, deltaTime);

        // Follow Bézier path if enabled (guard against empty/degenerate curves and smooth the camera motion)
        if (g_followBezier && g_modelLoaded && g_curvePointsWorld.size() >= 2 && g_curveTotalLen > 1e-6f && g_curveCumLen.size() >= 2) {
            // Advance along the curve by a world-space distance
            float distanceAdvance = g_bezierSpeed * deltaTime;
            float curLen = g_bezierT * g_curveTotalLen;
            curLen += distanceAdvance;
            // wrap safely
            if (curLen >= g_curveTotalLen) curLen = fmod(curLen, g_curveTotalLen);
            if (curLen < 0.0f) curLen += g_curveTotalLen;
            g_bezierT = curLen / g_curveTotalLen;

            // find segment
            size_t idx = 0;
            while (idx + 1 < g_curveCumLen.size() && g_curveCumLen[idx+1] < curLen) ++idx;
            float segStart = g_curveCumLen[idx];
            float segEnd = (idx + 1 < g_curveCumLen.size()) ? g_curveCumLen[idx+1] : g_curveTotalLen;
            float segLen = segEnd - segStart;
            float localT = (segLen > 1e-6f) ? ((curLen - segStart) / segLen) : 0.0f;
            glm::vec3 pA = g_curvePointsWorld[idx];
            glm::vec3 pB = (idx + 1 < g_curvePointsWorld.size()) ? g_curvePointsWorld[idx+1] : g_curvePointsWorld[0];
            glm::vec3 desiredPos = glm::mix(pA, pB, localT);
            // Move the camera exactly along the curve and orient it to always look at the model center
            g_camPos = desiredPos;
            glm::vec3 modelCenterWorld = glm::vec3(0.0f);
            glm::vec3 desiredFront = glm::normalize(modelCenterWorld - desiredPos);
            if (glm::length(desiredFront) > 1e-6f) {
                g_camFront = desiredFront;
            }
            g_camUp = glm::vec3(0.0f, 1.0f, 0.0f);
            g_pitch = glm::degrees(glm::asin(glm::clamp(g_camFront.y, -1.0f, 1.0f)));
            g_yaw = glm::degrees(atan2f(g_camFront.z, g_camFront.x));
        }

        // Framebuffer size / viewport
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glm::mat4 projDynamic = glm::perspective(glm::radians(g_fovDegrees), (float)width / (float)height, g_nearClip, g_farClip);

        // Clear
        glClearColor(0.05f, 0.06f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS) glDisable(GL_CULL_FACE);
        else glEnable(GL_CULL_FACE);

        // Update window title with loading status
        if (g_parsing) {
            int p = g_parseProgress.load();
            int eta = g_parseETASeconds.load();
            char title[128];
    #ifdef _WIN32
            if (eta >= 0) sprintf_s(title, sizeof(title), "FARID OpenGL - Loading model... %d%% (ETA %ds)", p, eta);
            else sprintf_s(title, sizeof(title), "FARID OpenGL - Loading model... %d%%", p);
    #else
            if (eta >= 0) snprintf(title, sizeof(title), "FARID OpenGL - Loading model... %d%% (ETA %ds)", p, eta);
            else snprintf(title, sizeof(title), "FARID OpenGL - Loading model... %d%%", p);
    #endif
            glfwSetWindowTitle(window, title);
        } else if (g_parseError) {
            glfwSetWindowTitle(window, "FARID OpenGL - Failed to load model");
        } else if (g_modelLoaded) {
            glfwSetWindowTitle(window, "FARID OpenGL");
        }

        // Use shader program and update camera uniforms
        glUseProgram(shaderProgram);
        view = glm::lookAt(g_camPos, g_camPos + g_camFront, g_camUp);
        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 mvp = projDynamic * view * model;
        if (uMVPLoc != -1) glUniformMatrix4fv(uMVPLoc, 1, GL_FALSE, glm::value_ptr(mvp));
        if (uModelLoc != -1) glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, glm::value_ptr(model));
        if (uViewPosLoc != -1) glUniform3fv(uViewPosLoc, 1, glm::value_ptr(g_camPos));
        // update light uniforms each frame
        if (uLightPosLoc != -1) glUniform3f(uLightPosLoc, g_camPos.x, g_camPos.y + 0.5f, g_camPos.z); // place a point light near camera
        if (uPointLightPosLoc != -1) glUniform3f(uPointLightPosLoc, g_modelCenter.x + 0.0f, g_modelCenter.y + 1.0f, g_modelCenter.z + 0.5f);
        if (uDirLightDirLoc != -1) glUniform3f(uDirLightDirLoc, -0.3f, -1.0f, -0.2f);
        if (uSpotPosLoc != -1) glUniform3f(uSpotPosLoc, g_camPos.x, g_camPos.y, g_camPos.z);
        if (uSpotDirLoc != -1) glUniform3f(uSpotDirLoc, g_camFront.x, g_camFront.y, g_camFront.z);
        glPolygonMode(GL_FRONT_AND_BACK, g_polygonMode);

        // If parsing finished in background, create GL buffers on main thread
        if (!g_modelLoaded && g_parsedReady) {
            glGenVertexArrays(1, &g_modelVAO);
            glGenBuffers(1, &g_modelVBO);
            glGenBuffers(1, &g_modelEBO);
            glBindVertexArray(g_modelVAO);
            glBindBuffer(GL_ARRAY_BUFFER, g_modelVBO);
            glBufferData(GL_ARRAY_BUFFER, g_parsedVertices.size()*sizeof(float), g_parsedVertices.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_modelEBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, g_parsedIndices.size()*sizeof(unsigned int), g_parsedIndices.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,VERT_STRIDE*sizeof(float),(void*)0); glEnableVertexAttribArray(0);
            glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,VERT_STRIDE*sizeof(float),(void*)(3*sizeof(float))); glEnableVertexAttribArray(1);
            glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,VERT_STRIDE*sizeof(float),(void*)(6*sizeof(float))); glEnableVertexAttribArray(2);
            glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,VERT_STRIDE*sizeof(float),(void*)(9*sizeof(float))); glEnableVertexAttribArray(3);
            glBindVertexArray(0);
            g_modelIndexCount = (int)g_parsedIndices.size();
            if (!g_parsedImagePath.empty() && g_modelTexture == 0) {
                g_modelTexture = loadTextureFromFile(g_parsedImagePath);
                if (g_modelTexture != 0) {
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, g_modelTexture);
                }
            }

            // If parse didn't provide an image path, try to resolve textures from the scene folder.
            if (g_modelTexture == 0 && g_parsedImagePath.empty() && scenePath != nullptr) {
#ifdef USE_ASSIMP
                // First try Assimp to resolve material textures if available
                if (!tryLoadTexturesWithAssimp(scenePath)) {
                    // Fallback: search for any image file under the scene folder
                    std::string found = findTextureForScene(scenePath);
                    if (!found.empty()) {
                        g_parsedImagePath = found;
                        g_modelTexture = loadTextureFromFile(g_parsedImagePath, true);
                        if (g_modelTexture != 0) {
                            glActiveTexture(GL_TEXTURE0);
                            glBindTexture(GL_TEXTURE_2D, g_modelTexture);
                        }
                    }
                } else {
                    // tryLoadTexturesWithAssimp already loaded g_modelTexture
                    if (g_modelTexture != 0) { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_modelTexture); }
                }
#else
                std::string found = findTextureForScene(scenePath);
                if (!found.empty()) {
                    g_parsedImagePath = found;
                    g_modelTexture = loadTextureFromFile(g_parsedImagePath, true);
                    if (g_modelTexture != 0) {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, g_modelTexture);
                    }
                }
#endif
            }
            g_modelLoaded = true;
            // Reset camera to reasonable default facing the scene
            g_camPos = glm::vec3(0.0f, 0.0f, 3.0f);
            g_camFront = glm::normalize(glm::vec3(0.0f,0.0f,0.0f) - g_camPos);
            g_pitch = glm::degrees(glm::asin(g_camFront.y));
            g_yaw = glm::degrees(glm::atan(g_camFront.z, g_camFront.x));
            g_firstMouse = true;
            // Configure Bézier control points around the model (world space centered at origin)
            {
                // Make a loop around the model with varying heights so the camera sees different angles
                float radius = 1.6f; // base radius
                float h = 0.9f; // base height amplitude
                // vary heights to view top/sides/under angles
                g_bezierP0 = glm::vec3( radius,  h * 0.6f,  0.0f);
                g_bezierP1 = glm::vec3( 0.0f,    h * 1.0f,  radius);
                g_bezierP2 = glm::vec3(-radius, -h * 0.4f,  0.0f);
                g_bezierP3 = glm::vec3( 0.0f,   -h * 0.8f, -radius);

                // Sample the Bézier in world-space directly
                std::vector<glm::vec3> curvePoints;
                const int STEPS = 256;
                curvePoints.reserve(STEPS + 1);
                for (int i = 0; i <= STEPS; ++i) {
                    float tt = (float)i / (float)STEPS;
                    glm::vec3 p_world = bezierPoint(tt);
                    curvePoints.push_back(p_world);
                }
                // close loop
                if (!curvePoints.empty()) curvePoints.back() = curvePoints.front();

                // Build cumulative lengths and upload to GPU
                g_curvePointsWorld.clear(); g_curveCumLen.clear(); g_curveTotalLen = 0.0f;
                glm::vec3 prev = curvePoints.front();
                g_curvePointsWorld.push_back(prev);
                g_curveCumLen.push_back(0.0f);
                for (size_t ci = 1; ci < curvePoints.size(); ++ci) {
                    glm::vec3 curp = curvePoints[ci];
                    float seg = glm::length(curp - prev);
                    g_curveTotalLen += seg;
                    g_curveCumLen.push_back(g_curveTotalLen);
                    g_curvePointsWorld.push_back(curp);
                    prev = curp;
                }
                g_curveVertexCount = (int)curvePoints.size();
                if (g_curveVAO == 0) glGenVertexArrays(1, &g_curveVAO);
                if (g_curveVBO == 0) glGenBuffers(1, &g_curveVBO);
                glBindVertexArray(g_curveVAO);
                glBindBuffer(GL_ARRAY_BUFFER, g_curveVBO);
                glBufferData(GL_ARRAY_BUFFER, g_curvePointsWorld.size()*sizeof(glm::vec3), g_curvePointsWorld.data(), GL_STATIC_DRAW);
                glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,(void*)0); glEnableVertexAttribArray(0);
                glBindVertexArray(0);
            }
            g_parsedVertices.clear(); g_parsedVertices.shrink_to_fit();
            g_parsedIndices.clear(); g_parsedIndices.shrink_to_fit();
            g_parsedReady = false;
            std::cout << "Loaded model with " << g_modelIndexCount << " indices (async)" << std::endl;
        }

        // Render loaded model & curve
        if (g_modelLoaded) {
            glBindVertexArray(g_modelVAO);
            glm::mat4 modelScene = glm::mat4(1.0f);
            modelScene = glm::translate(modelScene, -g_modelCenter * g_modelScale);
            modelScene = glm::scale(modelScene, glm::vec3(g_modelScale));
            glm::mat4 mvpScene = projDynamic * view * modelScene;
            if (uMVPLoc != -1) glUniformMatrix4fv(uMVPLoc, 1, GL_FALSE, glm::value_ptr(mvpScene));
            if (uModelLoc != -1) glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, glm::value_ptr(modelScene));
        // Bind albedo texture if present
        if (g_modelTexture != 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, g_modelTexture);
        }
            glDrawElements(GL_TRIANGLES, g_modelIndexCount, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
            // curve visualization removed per request (do not draw the Bézier path)
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Limpieza de recursos antes de salir
    glDeleteProgram(shaderProgram); // eliminar programa de shaders
    if (g_modelLoaded) {
        glDeleteVertexArrays(1, &g_modelVAO);
        glDeleteBuffers(1, &g_modelVBO);
        glDeleteBuffers(1, &g_modelEBO);
    }

    // Ensure background thread finished
    if (g_parseThread.joinable()) g_parseThread.join();

    glfwDestroyWindow(window); // destruir la ventana
    glfwTerminate(); // terminar GLFW y liberar recursos

    return 0; // salida exitosa
}
