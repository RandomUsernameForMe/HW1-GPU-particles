/*
 * Source code for the NPGR019 lab practices. Copyright Martin Kahoun 2021.
 * Licensed under the zlib license, see LICENSE.txt in the root directory.
 */

#include <cstdio>
#include <vector>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform.hpp>

#include <MathSupport.h>
#include <Camera.h>
#include <Geometry.h>
#include <Textures.h>
#include <algorithm>

#include "shaders.h"

 // Set to 1 to create debugging context that reports errors, requires OpenGL 4.3!
#define _ENABLE_OPENGL_DEBUG 0

// ----------------------------------------------------------------------------
// GLM optional parameters:
// GLM_FORCE_LEFT_HANDED       - use the left handed coordinate system
// GLM_FORCE_XYZW_ONLY         - simplify vector types and use x, y, z, w only
// ----------------------------------------------------------------------------
// For remapping depth to [0, 1] interval use GLM option below with glClipControl
// glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE); // requires version >= 4.5
//
// GLM_FORCE_DEPTH_ZERO_TO_ONE - force projection depth mapping to [0, 1]
//                               must use glClipControl(), requires OpenGL 4.5
//
// More information about the matter here:
// https://www.khronos.org/registry/OpenGL/extensions/ARB/ARB_clip_control.txt
// ----------------------------------------------------------------------------

// Structure for holding window parameters
struct Window
{
    // Window default width
    static const int DefaultWidth = 800;
    // Window default height
    static const int DefaultHeight = 600;

    // Width in pixels
    int width;
    // Height in pixels
    int height;
    // Main window handle
    GLFWwindow* handle;
} mainWindow = { 0 };

// ----------------------------------------------------------------------------

// Near clip plane settings
float nearClipPlane = 0.1f;
// Far clip plane settings
float farClipPlane = 100.1f;
// Camera FOV
float fov = 45.0f;

// ----------------------------------------------------------------------------

// Mouse movement
struct MouseStatus
{
    // Current position
    double x, y;
    // Previous position
    double prevX, prevY;

    // Updates the status - called once per frame
    void Update(double& moveX, double& moveY)
    {
        moveX = x - prevX;
        prevX = x;
        moveY = y - prevY;
        prevY = y;
    }
} mouseStatus = { 0.0 };

// ----------------------------------------------------------------------------

// Camera movement speeds
static constexpr float CameraNormalSpeed = 5.0f;
static constexpr float CameraTurboSpeed = 50.0f;

// ----------------------------------------------------------------------------

// Maximum number of allowed instances - must match the instancing vertex shader!
static const unsigned int MAX_INSTANCES = 1024;
// Max buffer length
static const unsigned int MAX_TEXT_LENGTH = 256;
// MSAA samples
static const GLsizei MSAA_SAMPLES = 4;
// Used MSAA samples
GLsizei msaaLevel = MSAA_SAMPLES;

// Number of  particles in the scene
const int numParticles = 50;
// particle position
std::vector<glm::vec4> particlePositions;


// Camera instance
Camera camera;
// Quad instance
Mesh<Vertex_Pos_Nrm_Tgt_Tex>* quad = nullptr;
// particle instance
Mesh<Vertex_Pos_Nrm_Tgt_Tex>* particle = nullptr;
// Portal instance
Mesh<Vertex_Pos_Nrm_Tgt_Tex>* mirror = nullptr;
// Textures helper instance
Textures& textures(Textures::GetInstance());

// General use VAO
GLuint vao = 0;
// Our framebuffer object
GLuint fbo = 0;
// Our render target for rendering
GLuint renderTarget = 0;
// Our depth stencil for rendering
GLuint depthStencil = 0;

// Vsync on?
bool vsync = true;
// Depth test on?
bool depthTest = false;
// Draw wireframe?
bool wireframe = false;
// Tonemapping on?
bool tonemapping = true;
// Instancing buffer handle
GLuint instancingBuffer = 0;
// Transformation matrices uniform buffer object
GLuint transformBlockUBO = 0;

// Data for a single object instance
struct InstanceData
{
    // In this simple example just a transformation matrix, transposed for efficient storage
    glm::mat3x4 transformation;
    glm::vec4 time;    
};

// ----------------------------------------------------------------------------

// Textures we'll be using
namespace LoadedTextures
{
    enum
    {
        White, Grey, Blue, CheckerBoard, Diffuse, Normal, Specular, Occlusion, Particles, NumTextures
    };
}
GLuint loadedTextures[LoadedTextures::NumTextures] = { 0 };

// ----------------------------------------------------------------------------

// Forward declaration for the framebuffer creation
void createFramebuffer(int width, int height, GLsizei MSAA);

// Callback for handling GLFW errors
void errorCallback(int error, const char* description)
{
    printf("GLFW Error %i: %s\n", error, description);
}

#if _ENABLE_OPENGL_DEBUG
void APIENTRY debugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
    if (type == GL_DEBUG_TYPE_ERROR)
        printf("OpenGL error: %s\n", message);
}
#endif

// Callback for handling window resize events
void resizeCallback(GLFWwindow* window, int width, int height)
{
    mainWindow.width = width;
    mainWindow.height = height;
    glViewport(0, 0, width, height);
    camera.SetProjection(fov, (float)width / (float)height, nearClipPlane, farClipPlane);

    createFramebuffer(width, height, msaaLevel);
}

// Callback for handling mouse movement over the window - called when mouse movement is detected
void mouseMoveCallback(GLFWwindow* window, double x, double y)
{
    // Update the current position
    mouseStatus.x = x;
    mouseStatus.y = y;
}

// Keyboard callback for handling system switches
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    // Notify the window that user wants to exit the application
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    // Enable/disable MSAA - note that it still uses the MSAA buffer
    if (key == GLFW_KEY_F1 && action == GLFW_PRESS)
    {
        if (msaaLevel > 1)
        {
            msaaLevel = 1;
        }
        else
        {
            msaaLevel = MSAA_SAMPLES;
        }

        createFramebuffer(mainWindow.width, mainWindow.height, msaaLevel);
    }

    // Enable/disable wireframe rendering
    if (key == GLFW_KEY_F2 && action == GLFW_PRESS)
    {
        wireframe = !wireframe;
    }

    // Enable/disable backface culling
    if (key == GLFW_KEY_F3 && action == GLFW_PRESS)
    {
        if (glIsEnabled(GL_CULL_FACE))
            glDisable(GL_CULL_FACE);
        else
            glEnable(GL_CULL_FACE);
    }

    // Enable/disable depth test
    if (key == GLFW_KEY_F4 && action == GLFW_PRESS)
    {
        depthTest = !depthTest;
    }

    // Enable/disable vsync
    if (key == GLFW_KEY_F5 && action == GLFW_PRESS)
    {
        vsync = !vsync;
        if (vsync)
            glfwSwapInterval(1);
        else
            glfwSwapInterval(0);
    }

    // Enable/disable vsync
    if (key == GLFW_KEY_F6 && action == GLFW_PRESS)
    {
        tonemapping = !tonemapping;
    }

    // Zoom in
    if (key == GLFW_KEY_KP_ADD || key == GLFW_KEY_EQUAL && action == GLFW_PRESS)
    {
        fov -= 1.0f;
        if (fov < 5.0f)
            fov = 5.0f;
    }

    // Zoom out
    if (key == GLFW_KEY_KP_SUBTRACT || key == GLFW_KEY_MINUS && action == GLFW_PRESS)
    {
        fov += 1.0f;
        if (fov >= 180.0f)
            fov = 179.0f;
    }

    if (key == GLFW_KEY_BACKSPACE && action == GLFW_PRESS)
    {
        fov = 45.0f;
    }

    // Set the camera projection
    camera.SetProjection(fov, (float)mainWindow.width / (float)mainWindow.height, nearClipPlane, farClipPlane);
}

// ----------------------------------------------------------------------------

// Helper method for loading textures
void loadTextures()
{
    // Create texture samplers
    textures.CreateSamplers();

    // Prepare textures
    loadedTextures[LoadedTextures::White] = Textures::CreateSingleColorTexture(255, 255, 255);
    loadedTextures[LoadedTextures::Grey] = Textures::CreateSingleColorTexture(127, 127, 127);
    loadedTextures[LoadedTextures::Blue] = Textures::CreateSingleColorTexture(127, 127, 255);
    loadedTextures[LoadedTextures::CheckerBoard] = Textures::CreateCheckerBoardTexture(256, 16);
    loadedTextures[LoadedTextures::Diffuse] = Textures::LoadTexture("data/Terracotta_Tiles_002_Base_Color.jpg", true);
    loadedTextures[LoadedTextures::Normal] = Textures::LoadTexture("data/Terracotta_Tiles_002_Normal.jpg", false);
    loadedTextures[LoadedTextures::Specular] = Textures::LoadTexture("data/Terracotta_Tiles_002_Roughness.jpg", false);
    loadedTextures[LoadedTextures::Occlusion] = Textures::LoadTexture("data/Terracotta_Tiles_002_ambientOcclusion.jpg", false);
    loadedTextures[LoadedTextures::Particles] = Textures::LoadTexture("data/ParticleAtlas.png", true);
}

// Helper method for creating scene geometry
void createGeometry()
{
    // Create general use VAO
    glGenVertexArrays(1, &vao);

    // Prepare meshes
    quad = Geometry::CreateQuadNormalTangentTex();
    particle = Geometry::CreateQuadNormalTangentTex();
    mirror = Geometry::CreateQuadNormalTangentTex();

    {
        // Generate the instancing buffer as Uniform Buffer Object
        glGenBuffers(1, &instancingBuffer);
        glBindBuffer(GL_UNIFORM_BUFFER, instancingBuffer);

        // Obtain UBO index and size from the instancing shader program
        GLuint uboIndex = glGetUniformBlockIndex(shaderProgram[ShaderProgram::Billboarding], "InstanceBuffer");
        GLint uboSize = 0;
        glGetActiveUniformBlockiv(shaderProgram[ShaderProgram::Billboarding], uboIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &uboSize);

        // Describe the buffer data - we're going to change this every frame
        glBufferData(GL_UNIFORM_BUFFER, uboSize, nullptr, GL_DYNAMIC_DRAW);

        // Unbind the GL_UNIFORM_BUFFER target for now
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    {
        // Generate the transform UBO handle
        glGenBuffers(1, &transformBlockUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, transformBlockUBO);

        // Obtain UBO index from the default shader program:
        // we're gonna bind this UBO for all shader programs and we're making
        // assumption that all of the UBO's used by our shader programs are
        // all the same size
        GLuint uboIndex = glGetUniformBlockIndex(shaderProgram[ShaderProgram::Default], "TransformBlock");
        GLint uboSize = 0;
        glGetActiveUniformBlockiv(shaderProgram[ShaderProgram::Default], uboIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &uboSize);

        // Describe the buffer data - we're going to change this every frame
        glBufferData(GL_UNIFORM_BUFFER, uboSize, nullptr, GL_DYNAMIC_DRAW);

        // Bind the memory for usage
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, transformBlockUBO);

        // Unbind the GL_UNIFORM_BUFFER target for now
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    particlePositions.reserve(numParticles);

    // Generate end positions for the rest of the particles
    for (int i = 0; i <= numParticles; ++i)
    {
        float x = getRandom(-1.0f, 1.0f);
        float y = getRandom(1.0f, 15.0f);
        float z = getRandom(-1.0f, 1.0f);
        float a = getRandom(0.0f, 1.0f);

        particlePositions.push_back(glm::vec4(x, y, z,a));
    }
}

// Helper method for OpenGL initialization
bool initOpenGL()
{
    // Set the GLFW error callback
    glfwSetErrorCallback(errorCallback);

    // Initialize the GLFW library
    if (!glfwInit()) return false;

    // Request OpenGL 3.3 core profile upon window creation
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_SAMPLES, 0); // Disable MSAA, we'll handle it ourselves
#if _ENABLE_OPENGL_DEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Create the window
    mainWindow.handle = glfwCreateWindow(Window::DefaultWidth, Window::DefaultHeight, "", nullptr, nullptr);
    if (mainWindow.handle == nullptr)
    {
        printf("Failed to create the GLFW window!");
        return false;
    }    

    // Make the created window with OpenGL context current for this thread
    glfwMakeContextCurrent(mainWindow.handle);

    // Check that GLAD .dll loader and symbol imported is ready
    if (!gladLoadGL()) {
        printf("GLAD failed!\n");
        return false;
    }

#if _ENABLE_OPENGL_DEBUG
    // Enable error handling callback function - context must be created with DEBUG flags
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(debugCallback, nullptr);
    GLuint unusedIds = 0;
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, &unusedIds, true);
#endif

    // Check for available UBO size in bytes
    GLint maxUboSize;
    const GLint expectedUboSize = 4096 * 4 * 4;
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &maxUboSize);
    if (maxUboSize < expectedUboSize)
    {
        printf("Implementation allowed UBO size: %d B smaller than expected (%d B)!", maxUboSize, expectedUboSize);
        return false;
    }

    // Prevent crashes
    if (numParticles > MAX_INSTANCES)
    {
        printf("Trying to render more than the maximum number of particles: %d!", MAX_INSTANCES);
        return false;
    }

    // Enable vsync
    if (vsync)
        glfwSwapInterval(1);
    else
        glfwSwapInterval(0);

    // Enable backface culling
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // Enable depth test
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    // Enable automatic sRGB color conversion
    glEnable(GL_FRAMEBUFFER_SRGB);

    // Register a window resize callback
    glfwSetFramebufferSizeCallback(mainWindow.handle, resizeCallback);

    // Register keyboard callback
    glfwSetKeyCallback(mainWindow.handle, keyCallback);

    // Register mouse movement callback
    glfwSetCursorPosCallback(mainWindow.handle, mouseMoveCallback);

    // Set the OpenGL viewport and camera projection
    resizeCallback(mainWindow.handle, Window::DefaultWidth, Window::DefaultHeight);

    // Set the initial camera position and orientationhow to clean 
    camera.SetTransformation(glm::vec3(-3.0f, 3.0f, -5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    return true;
}

// Helper function for creating the HDR framebuffer
void createFramebuffer(int width, int height, GLsizei MSAA)
{
    // Bind the default framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Generate the FBO if necessary
    if (!fbo)
    {
        glGenFramebuffers(1, &fbo);
    }

    // Bind it and recreate textures
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // --------------------------------------------------------------------------
    // Render target texture:
    // --------------------------------------------------------------------------

    // Delete it if necessary
    if (glIsTexture(renderTarget))
    {
        glDeleteTextures(1, &renderTarget);
        renderTarget = 0;
    }

    // Create the texture name
    if (renderTarget == 0)
    {
        glGenTextures(1, &renderTarget);
    }

    // Bind and recreate the render target texture
    if (MSAA > 1)
    {
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, renderTarget);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, MSAA, GL_RGB16F, width, height, GL_TRUE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, renderTarget, 0);
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, renderTarget);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTarget, 0);
    }

    // --------------------------------------------------------------------------
    // Depth buffer texture:
    // --------------------------------------------------------------------------

    // Delete it if necessary
    if (glIsRenderbuffer(depthStencil))
    {
        glDeleteRenderbuffers(1, &depthStencil);
        depthStencil = 0;
    }

    // Create the depth-stencil name
    if (depthStencil == 0)
    {
        glGenRenderbuffers(1, &depthStencil);
    }

    // Bind and recreate the depth-stencil Render Buffer Object
    glBindRenderbuffer(GL_RENDERBUFFER, depthStencil);
    if (MSAA > 1)
    {
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, MSAA, GL_DEPTH_COMPONENT32F, width, height);
    }
    else
    {
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, width, height);
    }
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthStencil);

    // Set the list of draw buffers.
    GLenum drawBuffers[] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(1, drawBuffers);

    // Check for completeness
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        printf("Failed to create framebuffer: 0x%04X\n", status);
    }

    // Bind back the window system provided framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Helper method for graceful shutdown
void shutDown()
{
    // Release shader programs
    for (int i = 0; i < ShaderProgram::NumShaderPrograms; ++i)
    {
        glDeleteProgram(shaderProgram[i]);
    }

    // Delete meshes
    delete quad;
    quad = nullptr;
    delete particle;
    particle = nullptr;

    // Release the instancing buffer
    glDeleteBuffers(1, &instancingBuffer);

    // Release the framebuffer
    glDeleteTextures(1, &renderTarget);
    glDeleteTextures(1, &depthStencil);
    glDeleteFramebuffers(1, &fbo);

    // Release the generic VAO
    glDeleteVertexArrays(1, &vao);

    // Release textures
    glDeleteTextures(LoadedTextures::NumTextures, loadedTextures);

    // Release the window
    glfwDestroyWindow(mainWindow.handle);

    // Close the GLFW library
    glfwTerminate();
}

// ----------------------------------------------------------------------------

// Helper method for handling input events
void processInput(float dt)
{
    // Camera movement - keyboard events
    int direction = (int)MovementDirections::None;
    if (glfwGetKey(mainWindow.handle, GLFW_KEY_W) == GLFW_PRESS)
        direction |= (int)MovementDirections::Forward;

    if (glfwGetKey(mainWindow.handle, GLFW_KEY_S) == GLFW_PRESS)
        direction |= (int)MovementDirections::Backward;

    if (glfwGetKey(mainWindow.handle, GLFW_KEY_A) == GLFW_PRESS)
        direction |= (int)MovementDirections::Left;

    if (glfwGetKey(mainWindow.handle, GLFW_KEY_D) == GLFW_PRESS)
        direction |= (int)MovementDirections::Right;

    if (glfwGetKey(mainWindow.handle, GLFW_KEY_R) == GLFW_PRESS)
        direction |= (int)MovementDirections::Up;

    if (glfwGetKey(mainWindow.handle, GLFW_KEY_F) == GLFW_PRESS)
        direction |= (int)MovementDirections::Down;

    // Camera speed
    if (glfwGetKey(mainWindow.handle, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        camera.SetMovementSpeed(CameraTurboSpeed);
    else
        camera.SetMovementSpeed(CameraNormalSpeed);

    // Update the mouse status
    double dx, dy;
    mouseStatus.Update(dx, dy);

    // Camera orientation - mouse movement
    glm::vec2 mouseMove(0.0f, 0.0f);
    if (glfwGetMouseButton(mainWindow.handle, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
    {
        mouseMove.x = (float)(dx);
        mouseMove.y = (float)(dy);
    }

    // Update the camera movement
    camera.Move((MovementDirections)direction, mouseMove, dt);

    // Reset camera state
    if (glfwGetKey(mainWindow.handle, GLFW_KEY_ENTER) == GLFW_PRESS)
    {
        camera.SetProjection(fov, (float)mainWindow.width / (float)mainWindow.height, nearClipPlane, farClipPlane);
        camera.SetTransformation(glm::vec3(-3.0f, 3.0f, -5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    }
}

// Helper function for binding the appropriate textures
void bindTextures(const GLuint& diffuse, const GLuint& normal, const GLuint& specular, const GLuint& occlusion)
{
    // We want to bind textures and appropriate samplers
    glActiveTexture(GL_TEXTURE0 + 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, diffuse);    
    glBindSampler(0, textures.GetSampler(Sampler::Anisotropic));

    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, normal);
    glBindSampler(1, textures.GetSampler(Sampler::Anisotropic));

    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_2D, specular);
    glBindSampler(2, textures.GetSampler(Sampler::Anisotropic));

    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, occlusion);
    glBindSampler(3, textures.GetSampler(Sampler::Anisotropic));
}


class CompareDistanceFromCamera {
public:
    // after making CompareInformations a friend class to Information...
    bool operator()(const glm::vec4& rhs, const glm::vec4& lhs) {
        return (camera.GetWorldToView()*rhs).z < (camera.GetWorldToView()* lhs).z;
    }
};


// Helper function for creating and updating the instance data
void updateInstanceData()
{
    // Create transformation matrix
    static glm::mat4x4 transformation;
    // Instance data CPU side buffer
    static std::vector<InstanceData> instanceData(MAX_INSTANCES);

    // Particles
    float timeValue = glfwGetTime()/5;

    std::sort(particlePositions.begin(), particlePositions.end(), CompareDistanceFromCamera());
       
    for (int i = 0; i < numParticles; ++i)
    {
        // each particles travels on a line from beginning to end positions
        // position on the line depend on randomness and time 
        float magic1 = timeValue + particlePositions[i][3];
        float magic = (magic1)-floor(magic1);

        transformation = glm::translate(glm::vec3(magic*particlePositions[i][0], magic * particlePositions[i][1], magic * particlePositions[i][2]));
        instanceData[i].transformation = glm::transpose(transformation);

        /// We have a texture atlass and use tsese comutation to comupte the fitting sprite that chenges over time
        float newMagic = floor(magic * 64);
        float stepsX = newMagic - (floor(newMagic / 8) * 8);
        float stepsY = floor(newMagic / 8);
        float newX = stepsX;
        float newY = stepsY;
        instanceData[i].time = glm::vec4(newX,newY,0,0);
    }    

    // Bind the whole instancing buffer to the index 1
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, instancingBuffer);

    // Update the buffer data using mapping
    void* ptr = glMapBuffer(GL_UNIFORM_BUFFER, GL_WRITE_ONLY);
    memcpy(ptr, &*(instanceData.begin()), numParticles * sizeof(InstanceData));
    glUnmapBuffer(GL_UNIFORM_BUFFER);
}

void updateProgramData(GLuint program, const glm::vec3& lightPosition)
{
    // TODO: make this a transform block as well

    // Update the light position
    GLint lightLoc = glGetUniformLocation(program, "lightPosWS");
    glUniform3f(lightLoc, lightPosition.x, lightPosition.y, lightPosition.z);

    // Update the view position
    GLint viewPosLoc = glGetUniformLocation(program, "viewPosWS");
    glm::vec4 viewPos = camera.GetViewToWorld()[3];
    glUniform4f(viewPosLoc, viewPos.x, viewPos.y, viewPos.z, viewPos.w);
}

// Helper method to update transformation uniform block
void updateTransformBlock()
{
    // Tell OpenGL we want to work with our transform block
    glBindBuffer(GL_UNIFORM_BUFFER, transformBlockUBO);

    // Note: we should properly obtain block members size and offset via
    // glGetActiveUniformBlockiv() with GL_UNIFORM_SIZE, GL_UNIFORM_OFFSET,
    // I'm yoloing it here...

    // Update the world to view transformation matrix - transpose to 3 columns, 4 rows for storage in an uniform block:
    // per std140 layout column matrix CxR is stored as an array of C columns with R elements, i.e., 4x3 matrix would
    // waste space because it would require padding to vec4
    glm::mat3x4 worldToView = glm::transpose(camera.GetWorldToView());
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat3x4), static_cast<const void*>(&*glm::value_ptr(worldToView)));

    // Update the projection matrix
    glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat3x4), sizeof(glm::mat4x4), static_cast<const void*>(&*glm::value_ptr(camera.GetProjection())));

    // Unbind the GL_UNIFORM_BUFFER target for now
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void renderScene()
{
    updateTransformBlock();

    // Bind the framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Enable/disable depth test and write
    if (depthTest)
    {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
    }
    else
    {
        glDisable(GL_DEPTH_TEST);
    }

    // Enable/disable MSAA rendering
    if (msaaLevel > 1)
        glEnable(GL_MULTISAMPLE);
    else
        glDisable(GL_MULTISAMPLE);

    // Enable/disable wireframe rendering
    if (wireframe)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    else
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Clear the color and depth buffer
    glClearColor(0.1f, 0.2f, 0.4f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    // --------------------------------------------------------------------------

    // Light position
    static glm::vec3 lightPosition(-5.0f, 5.0f, 1.0f);

    // Draw the scene floor:
    {
        glUseProgram(shaderProgram[ShaderProgram::Default]);
        updateProgramData(shaderProgram[ShaderProgram::Default], lightPosition);

        // Create transformation matrix - 4 columns, 3 rows, last (0, 0, 0, 1) implicit to save space
        glm::mat4x3 transformation = glm::scale(glm::vec3(30.0f, 1.0f, 30.0f));
        glUniformMatrix4x3fv(0, 1, GL_FALSE, glm::value_ptr(transformation));

        bindTextures(loadedTextures[LoadedTextures::CheckerBoard], loadedTextures[LoadedTextures::Blue], loadedTextures[LoadedTextures::Grey], loadedTextures[LoadedTextures::White]);

        glBindVertexArray(quad->GetVAO());
        glDrawElements(GL_TRIANGLES, quad->GetIBOSize(), GL_UNSIGNED_INT, reinterpret_cast<void*>(0));
    }

    // --------------------------------------------------------------------------

    // Draw projectiles:
    {
        glUseProgram(shaderProgram[ShaderProgram::Billboarding]);

        // Update the transformation & projection matrices
        updateProgramData(shaderProgram[ShaderProgram::Billboarding], lightPosition);

        // Update instances and bind the instancing buffer
        updateInstanceData();

        bindTextures(loadedTextures[LoadedTextures::Particles], loadedTextures[LoadedTextures::Normal], loadedTextures[LoadedTextures::Specular], loadedTextures[LoadedTextures::Occlusion]);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_SRC_ALPHA);
        glBindVertexArray(particle->GetVAO());
        glDrawElementsInstanced(GL_TRIANGLES, particle->GetIBOSize(), GL_UNSIGNED_INT, reinterpret_cast<void*>(0), numParticles);

        // Unbind the instancing buffer
        glBindBufferBase(GL_UNIFORM_BUFFER, 1, 0);
    }

    // --------------------------------------------------------------------------

    // Draw the light:
    {
        glUseProgram(shaderProgram[ShaderProgram::PointRendering]);

        // Update the light position
        GLint loc = glGetUniformLocation(shaderProgram[ShaderProgram::PointRendering], "position");
        glUniform3fv(loc, 1, glm::value_ptr(lightPosition));

        // Update the color
        loc = glGetUniformLocation(shaderProgram[ShaderProgram::PointRendering], "color");
        glUniform3f(0, 1.0f, 1.0f, 1.0f);

        glPointSize(10.0f);
        glBindVertexArray(vao);
        glDrawArrays(GL_POINTS, 0, 1);
    }


    // Draw mirror
    {
        glUseProgram(shaderProgram[ShaderProgram::PointRendering]);
        updateProgramData(shaderProgram[ShaderProgram::Default], lightPosition);

        glEnable(GL_STENCIL_TEST);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glStencilFunc(GL_ALWAYS, 1, 0xFF); // all fragments should pass the stencil test
        glStencilMask(0xFF);

        glBindVertexArray(mirror->GetVAO());
        glDrawElements(GL_TRIANGLES, mirror->GetIBOSize(), GL_UNSIGNED_INT, reinterpret_cast<void*>(0));

        glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
        glStencilMask(0x00); // disable writing to the stencil buffer
        glDisable(GL_DEPTH_TEST);

    }
    // --------------------------------------------------------------------------

    // Unbind the shader program and other resources
    glBindVertexArray(0);
    glUseProgram(0);

    if (tonemapping)
    {
        // Unbind the framebuffer and bind the window system provided FBO
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Solid fill always
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        // Disable multisampling and depth test
        glDisable(GL_MULTISAMPLE);
        glDisable(GL_DEPTH_TEST);

        // Clear the color
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Tonemapping
        glUseProgram(shaderProgram[ShaderProgram::Tonemapping]);

        // Send in the required data
        glUniform1f(0, (float)msaaLevel);

        // Bind the HDR render target as texture
        GLenum target = (msaaLevel > 1) ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(target, renderTarget);
        glBindSampler(0, 0); // Very important!

        // Draw fullscreen quad
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Unbind the shader program and other resources
        glBindVertexArray(0);
        glUseProgram(0);
    }
    else
    {
        // Just copy the render target to the screen
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glDrawBuffer(GL_BACK);
        glBlitFramebuffer(0, 0, mainWindow.width, mainWindow.height, 0, 0, mainWindow.width, mainWindow.height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }
}

// Helper method for implementing the application main loop
void mainLoop()
{
    static double prevTime = 0.0;
    while (!glfwWindowShouldClose(mainWindow.handle))
    {
        // Calculate delta time
        double time = glfwGetTime();
        float dt = (float)(time - prevTime);
        prevTime = time;

        // Print it to the title bar
        static char title[MAX_TEXT_LENGTH];
        snprintf(title, MAX_TEXT_LENGTH, "dt = %.2fms, FPS = %.1f", dt * 1000.0f, 1.0f / dt);
        glfwSetWindowTitle(mainWindow.handle, title);

        // Poll the events like keyboard, mouse, etc.
        glfwPollEvents();

        // Process keyboard input
        processInput(dt);

        // Render the scene
        renderScene();

        // Swap actual buffers on the GPU
        glfwSwapBuffers(mainWindow.handle);
    }
}

int main()
{
    // Initialize the OpenGL context and create a window
    if (!initOpenGL())
    {
        printf("Failed to initialize OpenGL!\n");
        shutDown();
        return -1;
    }

    // Compile shaders needed to run
    if (!compileShaders())
    {
        printf("Failed to compile shaders!\n");
        shutDown();
        return -1;
    }

    // Create the scene geometry
    createGeometry();

    // Load & create texture
    loadTextures();

    // Enter the application main loop
    mainLoop();

    // Release used resources and exit
    shutDown();
    return 0;
}