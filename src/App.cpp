#include "App.hpp"

#include "rlImGui.h"
#include "imgui.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include "rlgl.h"

static RenderTexture LoadRenderTextureFloat(int w, int h)
{
    RenderTexture rt = { 0 };
    rt.id = rlLoadFramebuffer();
    if (rt.id > 0) {
        rlEnableFramebuffer(rt.id);
        rt.texture.id      = rlLoadTexture(nullptr, w, h, PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1);
        rt.texture.width   = w;
        rt.texture.height  = h;
        rt.texture.format  = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;
        rt.texture.mipmaps = 1;
        rt.depth.id        = rlLoadTextureDepth(w, h, true);
        rt.depth.width     = w;
        rt.depth.height    = h;
        rt.depth.format    = 19;
        rt.depth.mipmaps   = 1;
        rlFramebufferAttach(rt.id, rt.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        rlFramebufferAttach(rt.id, rt.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
        rlDisableFramebuffer();
    }
    return rt;
}

// ─── Web (Emscripten) interop ──────────────────────────────────────────────────
// The HTML shell owns a #canvas-container that flexes to fill the page; the
// <canvas> framebuffer is sized in physical pixels for crisp HiDPI rendering,
// while its CSS display size stays in logical pixels.
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
EM_JS(int,    canvas_width,       (), { return document.getElementById('canvas-container').clientWidth;  });
EM_JS(int,    canvas_height,      (), { return document.getElementById('canvas-container').clientHeight; });
EM_JS(int,    canvas_buf_width,   (), { return document.getElementById('canvas').width;  });
EM_JS(int,    canvas_buf_height,  (), { return document.getElementById('canvas').height; });
EM_JS(double, device_pixel_ratio, (), { return window.devicePixelRatio || 1.0; });
EM_JS(void,   set_canvas_css_size, (int w, int h), {
    Module.canvas.style.width  = w + 'px';
    Module.canvas.style.height = h + 'px';
});
#endif

// Screen-size helpers: on the web, GetRenderWidth/Height track the physical
// framebuffer and are updated by SetWindowSize; GetScreenWidth is stale after a
// resize. On native, the two are equivalent.
#ifdef __EMSCRIPTEN__
static inline int SW() { return GetRenderWidth();  }
static inline int SH() { return GetRenderHeight(); }
#else
static inline int SW() { return GetScreenWidth();  }
static inline int SH() { return GetScreenHeight(); }
#endif

// ─── Lifecycle ─────────────────────────────────────────────────────────────────

App::App()
{
#ifdef __EMSCRIPTEN__
    double dpr = device_pixel_ratio();
    dpiScale   = (float)dpr;
    int cssW   = canvas_width();
    int cssH   = canvas_height();
    int w      = (int)(cssW * dpr + 0.5);
    int h      = (int)(cssH * dpr + 0.5);
#else
    SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    int w = 1280;
    int h = 720;
#endif
    SetTraceLogLevel(LOG_WARNING); // quiet raylib's INFO spam
    InitWindow(w, h, "dear_raylib");
    ChangeDirectory(GetApplicationDirectory());
    SetExitKey(KEY_NULL);          // don't let ESC close the window
    SetTargetFPS(60);
    state.img = LoadImage("assets/img.png");
    state.tex = LoadTextureFromImage(state.img);
    state.pingpong[0] = LoadRenderTextureFloat(state.tex.width, state.tex.height);
    state.pingpong[1] = LoadRenderTextureFloat(state.tex.width, state.tex.height);

    AddPass("assets/shaders/passthrough.fs", "Passthrough");

#ifdef __EMSCRIPTEN__
    // Pin the CSS display size to logical pixels; the framebuffer is already physical.
    set_canvas_css_size(cssW, cssH);
#endif

    rlImGuiSetup(true);            // true = apply the Dear ImGui dark theme

#ifdef __EMSCRIPTEN__
    if (dpr > 1.0) {
        ImGui::GetStyle().ScaleAllSizes((float)dpr);
        ImGui::GetIO().FontGlobalScale = (float)dpr;
    }
#endif

    lastW = SW();
    lastH = SH();

    // Anything printed to stdout/stderr shows up in the web console pane.
    printf("dear_raylib started: %d x %d (dpi scale %.2f)\n", SW(), SH(), dpiScale);
    fflush(stdout);
}

App::~App()
{
    rlImGuiShutdown();
    for (auto& p : state.passes) UnloadShader(p.shader);
    UnloadRenderTexture(state.pingpong[0]);
    UnloadRenderTexture(state.pingpong[1]);
    UnloadTexture(state.tex);
    UnloadImage(state.img);
    CloseWindow();
}

bool App::ShouldClose() const
{
    return WindowShouldClose();
}

// ─── Input ───────────────────────────────────────────────────────────────────

void App::HandleEvents()
{
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_Q))
        CloseWindow();

#ifdef __EMSCRIPTEN__
    // Detect canvas buffer resizes (e.g. browser window resize). canvas_buf_*
    // are physical pixels; divide by DPR before SetWindowSize, which (via GLFW)
    // treats its arguments as CSS pixels.
    int cw = canvas_buf_width(), ch = canvas_buf_height();
    if (cw != lastW || ch != lastH)
    {
        lastW = cw; lastH = ch;
        double dpr = device_pixel_ratio();
        SetWindowSize((int)(cw / dpr + 0.5), (int)(ch / dpr + 0.5));
    }
#endif
}

// ─── Update ────────────────────────────────────────────────────────────────────

void App::Update()
{
    Shader s = state.passes[0].shader;
    SetShaderValue(s, GetShaderLocation(s, "exposure"), &state.exposure, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, GetShaderLocation(s, "contrast"), &state.contrast, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s, GetShaderLocation(s, "gamma"),    &state.gamma,    SHADER_UNIFORM_FLOAT);
}

// ─── Drawing ─────────────────────────────────────────────────────────────────

int App::AddPass(const char* fragPath, const char* name)
{
    state.passes.push_back({ LoadShader(0, fragPath), name, true });
    return (int)state.passes.size() - 1;
}

void App::DrawScene()
{
    float tw = (float)state.tex.width;
    float th = (float)state.tex.height;

    // Run enabled passes: ping-pong between two render textures.
    int dst = 0;
    int ran = 0;
    for (auto& pass : state.passes) {
        if (!pass.enabled) continue;

        BeginTextureMode(state.pingpong[dst]);
        ClearBackground(BLACK);
        BeginShaderMode(pass.shader);

        if (ran == 0) {
            DrawTexture(state.tex, 0, 0, WHITE);
        } else {
            int src = 1 - dst;
            DrawTexturePro(state.pingpong[src].texture,
                {0, 0, tw, -th}, {0, 0, tw, th},
                {0, 0}, 0.0f, WHITE);
        }

        EndShaderMode();
        EndTextureMode();
        dst = 1 - dst;
        ran++;
    }

    // Blit result to screen, scaled to fit.
    float sw = (float)SW(), sh = (float)SH();
    float scale = fminf(sw / tw, sh / th);
    float dw = tw * scale, dh = th * scale;
    Rectangle destRect = {(sw - dw) * 0.5f, (sh - dh) * 0.5f, dw, dh};

    if (ran == 0) {
        DrawTexturePro(state.tex,
            {0, 0, tw, th}, destRect,
            {0, 0}, 0.0f, WHITE);
    } else {
        int last = 1 - dst;
        DrawTexturePro(state.pingpong[last].texture,
            {0, 0, tw, -th}, destRect,
            {0, 0}, 0.0f, WHITE);
    }
}

void App::DrawGui()
{
    ImGui::SetNextWindowPos({10.0f, 10.0f}, ImGuiCond_Once);
    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::Text("FPS: %d", GetFPS());
    ImGui::SameLine();
#ifdef __EMSCRIPTEN__
    {
        double dpr = device_pixel_ratio();
        ImGui::Text("  %d x %d (logical)", (int)(SW() / dpr + 0.5), (int)(SH() / dpr + 0.5));
    }
#else
    ImGui::Text("  %d x %d", SW(), SH());
#endif

    ImGui::Separator();

    ImGui::Checkbox("Passthrough", &state.passes[0].enabled);
    if (state.passes[0].enabled) {
        ImGui::SliderFloat("Exposure", &state.exposure, -5.0f, 5.0f);
        ImGui::SliderFloat("Contrast", &state.contrast,  0.0f, 3.0f);
        ImGui::SliderFloat("Gamma",    &state.gamma,     0.1f, 5.0f);
    }

    ImGui::Separator();
    ImGui::Checkbox("Show ImGui demo window", &state.showImGuiDemo);
    ImGui::TextDisabled("Ctrl+Q to quit");

    ImGui::End();

    if (state.showImGuiDemo)
        ImGui::ShowDemoWindow(&state.showImGuiDemo);
}

// ─── Frame ───────────────────────────────────────────────────────────────────

void App::Frame()
{
    HandleEvents();
    Update();

    BeginDrawing();
    ClearBackground(BLACK);

    DrawScene();

    rlImGuiBegin();
#ifdef __EMSCRIPTEN__
    // rlImGui sets io.DisplaySize from GetScreenWidth(), which is stale after a
    // resize; override it with the always-current render dimensions.
    ImGui::GetIO().DisplaySize = { (float)SW(), (float)SH() };
#endif
    DrawGui();
    rlImGuiEnd();

    EndDrawing();
}
