#pragma once
#include "raylib.h"
#include <vector>

struct ShaderPass {
    Shader      shader  = {};
    const char* name    = "";
    bool        enabled = true;
};

struct AppState {
    bool showImGuiDemo = false;

    Image     img;
    Texture2D tex = {};

    std::vector<ShaderPass> passes;
    RenderTexture           pingpong[2] = {};

    float exposure = 0.0f;
    float contrast = 1.0f;
    float gamma    = 1.0f;
};

class App {
public:
    App();
    ~App();

    void Frame();
    bool ShouldClose() const;

private:
    void HandleEvents();
    void Update();
    void DrawScene();
    void DrawGui();

    int AddPass(const char* fragPath, const char* name);

    AppState state;
    int   lastW = 0, lastH = 0;
    float dpiScale = 1.0f;
};
