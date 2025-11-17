#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_EASY_FONT_IMPLEMENTATION
#include "stb_easy_font.h"

// Window dimensions
const int WINDOW_WIDTH = 800;
const int WINDOW_HEIGHT = 600;

// Function Prototypes
GLuint compileShader(GLenum type, const char* source);
GLuint createTextShaderProgram();
GLuint createShaderProgram(const char* vtxSrc, const char* fragSrc);
void ortho(float left, float right, float bottom, float top, float near, float far, float* mat);
void updateFish(struct Fish& f, float dt);
void initFishes(int count);
bool checkButtonClick(const struct Button& btn, float mx, float my);
void renderBar(GLuint shader, GLuint vao, float x, float y, float width, float height, float r, float g, float b, float max_width = 1.0f, bool with_background = false);
void renderText(float x, float y, const char* text, float r, float g, float b, GLuint textProgram, float scale, bool bold);
void saveStatus(float oxygen, float food);
bool loadStatus(float& oxygen, float& food);
void initTextRender();

// Fish and Button Structures
struct Fish {
    float x, y;
    float dx, dy;
    float size;
    bool facingRight;
    float happiness; // 0..1
    bool isDying = false;
};

struct Button {
    float x, y, width, height;
    const char* label;
};

// Global Variables
std::vector<Fish> fishes;
float oxygenLevel = 1.0f;
float foodLevel = 1.0f;
float lastTime;
bool areFishesDying = false;

Button feedButton = { 0.45f, -0.85f, 0.4f, 0.12f, "Feed Food" };
Button oxygenButton = { -0.85f, -0.85f, 0.4f, 0.12f, "Give Oxygen" };

// Shaders
const char* vertexShaderSrc = R"glsl(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform vec2 offset;
uniform float scale;
uniform int facingRight;
uniform mat4 projection;

void main() {
    float flip = facingRight == 1 ? 1.0 : -1.0;
    vec2 pos = vec2(aPos.x * flip, aPos.y) * scale + offset;
    gl_Position = projection * vec4(pos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)glsl";

const char* fragmentShaderSrc = R"glsl(
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D fishTexture;
uniform float happiness; // 0..1

void main() {
    vec4 texColor = texture(fishTexture, TexCoord);
    float tint = 1.0 - happiness;
    vec3 colorTint = mix(vec3(1.0,1.0,1.0), vec3(1.0,0.3,0.3), tint);
    FragColor = vec4(texColor.rgb * colorTint, texColor.a);
    if (FragColor.a < 0.1) discard;
}
)glsl";

const char* uiVertexShaderSrc = R"glsl(
#version 330 core
layout(location=0) in vec2 aPos;

uniform mat4 projection;
uniform vec2 buttonPos;
uniform vec2 buttonSize;

void main() {
    vec2 pos = aPos * buttonSize + buttonPos;
    gl_Position = projection * vec4(pos, 0.0, 1.0);
}
)glsl";

const char* uiFragmentShaderSrc = R"glsl(
#version 330 core
out vec4 FragColor;

uniform vec3 color;

void main() {
    FragColor = vec4(color, 1.0);
}
)glsl";

// Background shaders
const char* bgVertexShaderSrc = R"glsl(
#version 330 core
layout(location=0) in vec2 aPos;
out vec2 vPos;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vPos = aPos;
}
)glsl";

const char* bgFragmentShaderSrc = R"glsl(
#version 330 core
in vec2 vPos;
out vec4 FragColor;

uniform float u_time;
uniform vec3 u_baseColor;
uniform vec3 u_waveColor;
uniform float u_resolution_x;
uniform float u_resolution_y;

void main() {
    vec2 pos = vPos * vec2(u_resolution_x / u_resolution_y, 1.0);
    
    // Simple wave effect
    float wave1 = sin(pos.x * 5.0 + u_time * 0.5) * 0.1;
    float wave2 = sin(pos.y * 3.0 + u_time * 0.3) * 0.05;
    float wave_mix = (wave1 + wave2);
    
    // Mix colors for a dynamic water effect
    vec3 finalColor = mix(u_baseColor, u_waveColor, abs(wave_mix));
    
    FragColor = vec4(finalColor, 1.0);
}
)glsl";

// Text rendering global variables
GLuint textVAO, textVBO;

void initTextRender() {
    glGenVertexArrays(1, &textVAO);
    glGenBuffers(1, &textVBO);
    glBindVertexArray(0);
}

void renderText(float x, float y, const char* text, float r, float g, float b, GLuint textProgram, float scale, bool bold) {
    // We can ignore the 'bold' parameter for stb_easy_font as it doesn't directly support it.
    // If bold text is required, it usually involves drawing the text multiple times with slight offsets.
    static char buffer[99999];
    int num_quads = stb_easy_font_print(x, y, const_cast<char*>(text), NULL, buffer, sizeof(buffer));

    if (num_quads == 0) return;

    // Use a vector to store the scaled vertices for cleaner drawing
    std::vector<float> text_verts;
    text_verts.reserve(num_quads * 4 * 2); // 4 vertices per quad, 2 floats per vertex

    for (int i = 0; i < num_quads * 4; ++i) {
        float* vert_data = (float*)(buffer + i * 16); // Each vertex is 16 bytes (x,y,z,w) - we only need x,y
        text_verts.push_back(vert_data[0] * scale);
        text_verts.push_back(vert_data[1] * scale);
    }

    glBindVertexArray(textVAO);
    glBindBuffer(GL_ARRAY_BUFFER, textVBO);
    glBufferData(GL_ARRAY_BUFFER, text_verts.size() * sizeof(float), text_verts.data(), GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glUseProgram(textProgram);
    GLint projectionLoc = glGetUniformLocation(textProgram, "projection");
    GLint colorLoc = glGetUniformLocation(textProgram, "color");

    float textProjection[16];
    ortho(0.0f, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT, 0.0f, -1.0f, 1.0f, textProjection);
    glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, textProjection);
    glUniform3f(colorLoc, r, g, b);

    glDrawArrays(GL_QUADS, 0, num_quads * 4);
    glBindVertexArray(0);
}

void renderBar(GLuint shader, GLuint vao, float x, float y, float width, float height, float r, float g, float b, float max_width, bool with_background) {
    glUseProgram(shader);
    glBindVertexArray(vao);
    GLint projectionLoc = glGetUniformLocation(shader, "projection");
    GLint colorLoc = glGetUniformLocation(shader, "color");
    GLint posLoc = glGetUniformLocation(shader, "buttonPos");
    GLint sizeLoc = glGetUniformLocation(shader, "buttonSize");

    float projection[16];
    ortho(-1.f, 1.f, -1.f, 1.f, -1.f, 1.f, projection);
    glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, projection);

    if (with_background) {
        // Draw the dark background bar
        glUniform2f(posLoc, x, y);
        glUniform2f(sizeLoc, max_width, height);
        glUniform3f(colorLoc, 0.2f, 0.2f, 0.2f);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    // Draw the main bar
    glUniform2f(posLoc, x, y);
    glUniform2f(sizeLoc, width, height);
    glUniform3f(colorLoc, r, g, b);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glBindVertexArray(0);
}

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW\n";
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);

    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Smart Aquarium Eco-System Manager", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to init GLAD\n";
        return -1;
    }

    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
    loadStatus(oxygenLevel, foodLevel);

    // Create shaders and initialize VAOs/VBOs
    GLuint fishShader = createShaderProgram(vertexShaderSrc, fragmentShaderSrc);
    GLuint uiShader = createShaderProgram(uiVertexShaderSrc, uiFragmentShaderSrc);
    GLuint textShader = createTextShaderProgram();
    GLuint bgShader = createShaderProgram(bgVertexShaderSrc, bgFragmentShaderSrc);

    initTextRender();

    float fishVertices[] = {
        -0.5f, -0.5f,  0.f, 0.f,
        -0.5f,  0.5f,  0.f, 1.f,
         0.5f,  0.5f,  1.f, 1.f,
        -0.5f, -0.5f,  0.f, 0.f,
         0.5f,  0.5f,  1.f, 1.f,
         0.5f, -0.5f,  1.f, 0.f,
    };

    GLuint fishVAO, fishVBO;
    glGenVertexArrays(1, &fishVAO);
    glGenBuffers(1, &fishVBO);
    glBindVertexArray(fishVAO);
    glBindBuffer(GL_ARRAY_BUFFER, fishVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(fishVertices), fishVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    float uiQuad[] = {
        0.f, 0.f,
        1.f, 0.f,
        1.f, 1.f,
        0.f, 1.f,
    };

    GLuint uiVAO, uiVBO;
    glGenVertexArrays(1, &uiVAO);
    glGenBuffers(1, &uiVBO);
    glBindVertexArray(uiVAO);
    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(uiQuad), uiQuad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    float bgQuad[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f,
    };
    GLuint bgVAO, bgVBO;
    glGenVertexArrays(1, &bgVAO);
    glGenBuffers(1, &bgVBO);
    glBindVertexArray(bgVAO);
    glBindBuffer(GL_ARRAY_BUFFER, bgVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(bgQuad), bgQuad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    stbi_set_flip_vertically_on_load(true);
    int texW, texH, texChannels;
    unsigned char* data = stbi_load("fish.png", &texW, &texH, &texChannels, 0);
    if (!data) {
        std::cerr << "Failed to load fish.png\n";
        return -1;
    }
    GLuint fishTex;
    glGenTextures(1, &fishTex);
    glBindTexture(GL_TEXTURE_2D, fishTex);
    GLenum format = (texChannels == 4) ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, format, texW, texH, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(data);

    float projection[16];
    ortho(-1.f, 1.f, -1.f, 1.f, -1.f, 1.f, projection);

    initFishes(8);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glfwSetMouseButtonCallback(window, [](GLFWwindow* win, int button, int action, int mods) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            double mx, my;
            glfwGetCursorPos(win, &mx, &my);
            float nx = (float)(mx / WINDOW_WIDTH) * 2.0f - 1.0f;
            float ny = 1.0f - (float)(my / WINDOW_HEIGHT) * 2.0f;

            if (checkButtonClick(feedButton, nx, ny)) {
                // INSTANT REACTION: Add a large amount of food with one click
                foodLevel += 0.8f;
                if (foodLevel > 1.f) foodLevel = 1.f;

                // INSTANT REACTION: Boost happiness for all fish
                for (auto& f : fishes) {
                    f.happiness += 0.4f;
                    if (f.happiness > 1.f) f.happiness = 1.f;
                }
            }
            else if (checkButtonClick(oxygenButton, nx, ny)) {
                // INSTANT REACTION: Add a large amount of oxygen with one click
                oxygenLevel += 0.8f;
                if (oxygenLevel > 1.f) oxygenLevel = 1.f;
            }
        }
        });

    lastTime = (float)glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        float currentTime = (float)glfwGetTime();
        float dt = currentTime - lastTime;
        lastTime = currentTime;

        // Decrease levels
        oxygenLevel -= dt * 0.02f;
        foodLevel -= dt * 0.04f;

        // Clamp levels to prevent negative values
        if (oxygenLevel < 0.f) oxygenLevel = 0.f;
        if (foodLevel < 0.f) foodLevel = 0.f;

        // Centralized logic to check if fishes should be dying (based on oxygen or food)
        if ((foodLevel <= 0.0f || oxygenLevel <= 0.0f) && !areFishesDying) {
            areFishesDying = true;
        }
        else if ((foodLevel > 0.4f && oxygenLevel > 0.4f) && areFishesDying) {
            areFishesDying = false;
            for (auto& f : fishes) {
                f.isDying = false;
                f.dx = ((rand() % 200) / 100.f - 1.f) * 0.5f;
                f.dy = ((rand() % 200) / 100.f - 1.f) * 0.3f;
            }
        }

        for (auto& f : fishes) {
            if (areFishesDying) {
                f.isDying = true;
            }
            f.happiness -= dt * 0.02f * (1.f - foodLevel);
            if (f.happiness > 1.f) f.happiness = 1.f;
            if (f.happiness < 0.f) f.happiness = 0.f;
            updateFish(f, dt);
        }

        // Render background first
        glUseProgram(bgShader);
        glBindVertexArray(bgVAO);
        GLint timeLoc = glGetUniformLocation(bgShader, "u_time");
        GLint baseColorLoc = glGetUniformLocation(bgShader, "u_baseColor");
        GLint waveColorLoc = glGetUniformLocation(bgShader, "u_waveColor");
        GLint resXLoc = glGetUniformLocation(bgShader, "u_resolution_x");
        GLint resYLoc = glGetUniformLocation(bgShader, "u_resolution_y");

        glUniform1f(timeLoc, (float)glfwGetTime());
        glUniform1f(resXLoc, (float)WINDOW_WIDTH);
        glUniform1f(resYLoc, (float)WINDOW_HEIGHT);

        // Dynamic background colors based on oxygen
        float base_r = 0.0f;
        float base_g = 0.3f + 0.7f * oxygenLevel;
        float base_b = 0.7f * oxygenLevel + 0.2f;
        glUniform3f(baseColorLoc, base_r, base_g, base_b);
        glUniform3f(waveColorLoc, 0.0f, 0.4f, 0.8f);

        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glBindVertexArray(0);

        // Render fishes
        glUseProgram(fishShader);
        glUniformMatrix4fv(glGetUniformLocation(fishShader, "projection"), 1, GL_FALSE, projection);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fishTex);
        glUniform1i(glGetUniformLocation(fishShader, "fishTexture"), 0);
        glBindVertexArray(fishVAO);

        for (auto& f : fishes) {
            glUniform2f(glGetUniformLocation(fishShader, "offset"), f.x, f.y);
            glUniform1f(glGetUniformLocation(fishShader, "scale"), f.size);
            glUniform1i(glGetUniformLocation(fishShader, "facingRight"), f.facingRight ? 1 : 0);
            glUniform1f(glGetUniformLocation(fishShader, "happiness"), f.happiness);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        glBindVertexArray(0);

        // Render UI Elements
        float barHeight = 0.05f;
        float barWidth = 0.5f;
        float barY = 0.9f;
        float barX = -0.9f;

        // Render food level bar
        renderBar(uiShader, uiVAO, barX, barY, barWidth * foodLevel, barHeight, 1.0f, 0.6f, 0.0f, barWidth, true);
        renderText(30, (1.0f - (barY + 1.0f) / 2.0f) * WINDOW_HEIGHT, "Food", 1.0f, 1.0f, 1.0f, textShader, 1.0f, false);
        barY -= barHeight + 0.05f;

        // Render oxygen level bar
        renderBar(uiShader, uiVAO, barX, barY, barWidth * oxygenLevel, barHeight, 0.0f, 0.8f, 0.8f, barWidth, true);
        renderText(30, (1.0f - (barY + 1.0f) / 2.0f) * WINDOW_HEIGHT, "Oxygen", 1.0f, 1.0f, 1.0f, textShader, 1.0f, false);

        // Render buttons
        renderBar(uiShader, uiVAO, feedButton.x, feedButton.y, feedButton.width, feedButton.height, 1.0f, 0.6f, 0.0f, feedButton.width, true);
        renderText((feedButton.x + 1.0f) / 2.0f * WINDOW_WIDTH + 10,
            (1.0f - (feedButton.y + 1.0f) / 2.0f) * WINDOW_HEIGHT - 35,
            feedButton.label, 1.f, 1.f, 1.f, textShader, 1.5f, false);

        renderBar(uiShader, uiVAO, oxygenButton.x, oxygenButton.y, oxygenButton.width, oxygenButton.height, 0.0f, 0.8f, 0.8f, oxygenButton.width, true);
        renderText((oxygenButton.x + 1.0f) / 2.0f * WINDOW_WIDTH + 10,
            (1.0f - (oxygenButton.y + 1.0f) / 2.0f) * WINDOW_HEIGHT - 35,
            oxygenButton.label, 1.f, 1.f, 1.f, textShader, 1.5f, false);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    saveStatus(oxygenLevel, foodLevel);

    // Cleanup
    glDeleteVertexArrays(1, &fishVAO);
    glDeleteBuffers(1, &fishVBO);
    glDeleteVertexArrays(1, &uiVAO);
    glDeleteBuffers(1, &uiVBO);
    glDeleteVertexArrays(1, &bgVAO);
    glDeleteBuffers(1, &bgVBO);
    glDeleteProgram(fishShader);
    glDeleteProgram(uiShader);
    glDeleteProgram(textShader);
    glDeleteProgram(bgShader);
    glDeleteTextures(1, &fishTex);
    glDeleteVertexArrays(1, &textVAO);
    glDeleteBuffers(1, &textVBO);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

// Shader Compilation and Program Linking
GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info[512];
        glGetShaderInfoLog(shader, 512, nullptr, info);
        std::cerr << "Shader compile error:\n" << info << std::endl;
    }
    return shader;
}

GLuint createShaderProgram(const char* vtxSrc, const char* fragSrc) {
    GLuint vertex = compileShader(GL_VERTEX_SHADER, vtxSrc);
    GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragSrc);
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    int success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char info[512];
        glGetProgramInfoLog(program, 512, nullptr, info);
        std::cerr << "Shader link error:\n" << info << std::endl;
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

GLuint createTextShaderProgram() {
    const char* vertexShaderSource = R"(
        #version 330 core
        layout(location=0) in vec2 aPos;
        uniform mat4 projection;
        void main() {
            gl_Position = projection * vec4(aPos, 0.0, 1.0);
        }
    )";
    const char* fragmentShaderSource = R"(
        #version 330 core
        out vec4 FragColor;
        uniform vec3 color;
        void main() {
            FragColor = vec4(color, 1.0);
        }
    )";
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

// Orthographic Projection Matrix
void ortho(float left, float right, float bottom, float top, float near, float far, float* mat) {
    for (int i = 0; i < 16; i++) mat[i] = 0;
    mat[0] = 2.f / (right - left);
    mat[5] = 2.f / (top - bottom);
    mat[10] = -2.f / (far - near);
    mat[12] = -(right + left) / (right - left);
    mat[13] = -(top + bottom) / (top - bottom);
    mat[14] = -(far + near) / (far - near);
    mat[15] = 1.f;
}

// Fish Logic
void updateFish(Fish& f, float dt) {
    if (f.isDying) {
        f.dx = 0;
        f.dy = -0.1f; // Sink slowly
        f.x += f.dx * dt;
        f.y += f.dy * dt;
        if (f.y < -1.0f) f.y = -1.0f; // Stop at the bottom
    }
    else {
        f.x += f.dx * dt;
        f.y += f.dy * dt;

        float halfSizeX = f.size / 2.0f;
        float halfSizeY = halfSizeX * ((float)WINDOW_HEIGHT / (float)WINDOW_WIDTH);

        if (f.y - halfSizeY < -1.f) {
            f.y = -1.f + halfSizeY;
            f.dy = -f.dy;
        }
        else if (f.y + halfSizeY > 1.f) {
            f.y = 1.f - halfSizeY;
            f.dy = -f.dy;
        }

        if (f.x - halfSizeX < -1.f) {
            f.x = -1.f + halfSizeX;
            f.dx = -f.dx;
            f.facingRight = true;
        }
        else if (f.x + halfSizeX > 1.f) {
            f.x = 1.f - halfSizeX;
            f.dx = -f.dx;
            f.facingRight = false;
        }
    }
}

void initFishes(int count) {
    fishes.clear();
    for (int i = 0; i < count; i++) {
        Fish f;
        f.size = 0.15f + (rand() % 90) / 1000.f;
        f.x = ((rand() % 2000) / 1000.f) - 1.f;
        f.y = ((rand() % 2000) / 1000.f) - 1.f;

        do {
            f.dx = ((rand() % 200) / 100.f - 1.f) * 0.5f;
            f.dy = ((rand() % 200) / 100.f - 1.f) * 0.3f;
        } while (f.dx == 0.0f || f.dy == 0.0f);

        f.facingRight = f.dx > 0;
        f.happiness = 1.f;
        fishes.push_back(f);
    }
}

// UI Logic and Rendering
bool checkButtonClick(const Button& btn, float mx, float my) {
    return mx >= btn.x && mx <= btn.x + btn.width &&
        my >= btn.y && my <= btn.y + btn.height;
}

// File I/O for State Saving
void saveStatus(float oxygen, float food) {
    std::ofstream file("aquarium_status.txt");
    if (file) {
        file << oxygen << " " << food << "\n";
    }
}

bool loadStatus(float& oxygen, float& food) {
    std::ifstream file("aquarium_status.txt");
    if (file) {
        file >> oxygen >> food;
        if (oxygen < 0.f) oxygen = 0.f;
        if (oxygen > 1.f) oxygen = 1.f;
        if (food < 0.f) food = 0.f;
        if (food > 1.f) food = 1.f;
        return true;
    }
    return false;
}