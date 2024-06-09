#include "raylib.h"

#include "common.h"

#include "imgui.h"
#include "rlImGui.h"

#if defined(PLATFORM_WEB)
#include <emscripten/emscripten.h>
#endif

void UnloadGameplayScreen();
void InitGameplayScreen();
void UpdateGameplayScreen();
void DrawGameplayScreen();
void DrawGameplayScreen();
void ImGuiGameplayScreen();

static const int screenWidth = 800;
static const int screenHeight = 450;

static void UpdateDrawFrame(void); // Update and draw one frame

int main(void) {
#if defined(PLATFORM_WEB)
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
#else
  SetConfigFlags(FLAG_VSYNC_HINT);
#endif
  InitWindow(screenWidth, screenHeight, "raylib game template");

  TraceLog(LOG_INFO, "window size: %d %d", GetScreenWidth(), GetScreenHeight());

  rlImGuiSetup(true);
  InitGameplayScreen();

#if defined(PLATFORM_WEB)
  emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
  while (!WindowShouldClose()) // Detect window close button or ESC key
  {
    UpdateDrawFrame();
  }
#endif

  UnloadGameplayScreen();
  rlImGuiShutdown();

  CloseWindow();
  return 0;
}

void ImGuiMain() {}

static void UpdateDrawFrame(void) {
  UpdateGameplayScreen();

  BeginDrawing();
  ClearBackground(COLOR_DARK);

  DrawGameplayScreen();

  Color color = LIME; // Good FPS
  int fps = GetFPS();
  float frame_time = 1.0 / (float)fps;

  if ((fps < 30) && (fps >= 15))
    color = ORANGE; // Warning FPS
  else if (fps < 15)
    color = RED; // Low FPS

  DrawText(TextFormat("FPS: %2i", fps), 10, 10, 20, color);
  DrawText(TextFormat("ms: %.1f", frame_time * 1000), 10, 30, 20, color);

  rlImGuiBegin();
  ImGuiGameplayScreen();
  rlImGuiEnd();

  EndDrawing();
}
