Comentarios breves del proyecto (una línea por archivo)

shaders/vertex.glsl - Vertex shader: transforma vértices y pasa atributos al fragment shader.
shaders/fragment.glsl - Fragment shader: calcula color final con iluminación y texturizado.

Main.cpp - Programa principal: inicializa OpenGL/GLFW, carga modelos/texturas, controla la cámara y renderiza la escena.
camera_input.h - Declaraciones y externs para controlar la cámara y manejar la entrada de usuario.
glad.c - Cargador de funciones OpenGL (GLAD), archivo generado de terceros.
glm_stub.h - Stub/compatibilidad mínima para tipos GLM si hace falta.
json.hpp - Librería nlohmann::json para parseo/serialización JSON (single-header).
stb_image.h - Cabecera de stb_image para cargar imágenes (PNG/JPG/TGA).
stb_image_impl.cpp - Implementación de stb_image (unidad de compilación que define STB_IMAGE_IMPLEMENTATION).
tiny_gltf.h - Cargador glTF en una sola cabecera para leer mallas, materiales y texturas.
youtubeopenGl.vcxproj / .filters - Archivos de proyecto de Visual Studio y organización de filtros.

Carpetas útiles:
- shaders/ : contiene los shaders GLSL usados por la aplicación.
- models/  : ubicación prevista para archivos .gltf/.obj y sus texturas (no incluida en el repo).

Si quieres, inserto estas líneas breves como comentarios directamente dentro de los archivos fuente (Main.cpp, camera_input.h, shaders, etc.). Indica qué archivos prefieres que edite.
