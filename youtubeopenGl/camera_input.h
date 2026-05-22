#pragma once
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include <vector>

// camera_input.h - Declaraciones para control de cámara y entrada (una línea): expose externs y shims para la entrada y la ruta Bézier.
// Camera / input related types and globals shared across translation units
struct CameraController;
struct BezierCameraPath;
struct InputManager;

// Extern globals (defined in camera_input.cpp)
// These are declared in Main.cpp; we don't define them here to avoid duplicate symbols
extern CameraController* g_cameraController;
extern BezierCameraPath* g_bezierPath;
extern InputManager* g_inputManager;

// The following globals are defined in Main.cpp; declare them here if other files include this header
// Camera globals for Bezier path and projection parameters
extern bool g_followBezier;
extern float g_bezierT;
extern float g_bezierSpeed;
extern glm::vec3 g_bezierP0, g_bezierP1, g_bezierP2, g_bezierP3;
extern float g_fovDegrees;
extern float g_nearClip;
extern float g_farClip;

extern std::vector<glm::vec3> g_curvePointsWorld;
extern std::vector<float> g_curveCumLen;
extern float g_curveTotalLen;

// Shim functions called from main.cpp
void InputManager_ProcessKeyboard(GLFWwindow* window, float deltaTime);
void InputManager_OnMouseMove_Shared(GLFWwindow* window, double xpos, double ypos);
void InputManager_OnKey_Shared(GLFWwindow* window, int key, int scancode, int action, int mods);

// Note: helper functions (e.g. Bezier helpers and processInput) are
// implemented in Main.cpp; only the shim prototypes above are declared here
// for other translation units to call.
