#include "imgui.h"
#include "raylib.h"
#include <assert.h>
#include <cstdlib>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "raymath.h"

#define MAX(a, b) ((a) < (b)) ? (b) : (a)

#define RAND_SEED 1
#define BALL_COUNT 500
#define RADIUS_EFFECT 30
#define RADIUS_DISPLAY 6

float gravity = 25;

float k = 20;
float density_0 = 2;
float k_near = 13;
float plasticity = 1;
float yield_ratio = 0.1;
float sigma = 10;
float beta = 100;
float wall_elasticity = 1.8;

enum BallType {
  BALL_TYPE_0,
  BALL_TYPE_1,
  BALL_TYPE_2,
  BALL_TYPE_MAX,
};

Color BallTypeColor[BALL_TYPE_MAX] = {
    COLOR1,
    COLOR2,
    COLOR3,
};

typedef struct {
  Vector2 old_pos;
  Vector2 pos;
  Vector2 vel;

  enum BallType type;
} ball_t;

typedef struct {
  bool occupied;
  size_t hash;
  size_t i;
} hmentry;

// A closed hasing table (with open addressing) with linear probing
// Adapted with use as a spatial datastructure in mind
// The main thing that changes is the fact that we expect that items with the
// same hash are within a common "cell"
// See Sequential Datastructure and Algorithms (Sanders and al.) chapter 4 of
// linear probbing
typedef struct {
  float cell_width;
  size_t item_per_cell;
  size_t entry_count;
  size_t cell_count;
  hmentry entries[];
} SpatialHashMap_t;

void shm_reset(SpatialHashMap_t *shm) {
  for (size_t i = 0; i < shm->entry_count; i++) {
    shm->entries[i].occupied = false;
  }
}

// Cell count should be a power of 4:
// 4 16 64 256 1024 4096 16384 65536...
SpatialHashMap_t *createSpatialHashMap(size_t item_per_cell, size_t cell_count,
                                       float cell_width) {
  SpatialHashMap_t *map = (SpatialHashMap_t *)malloc(
      sizeof(SpatialHashMap_t) + cell_count * item_per_cell * sizeof(hmentry));
  assert(map != NULL);

  map->item_per_cell = item_per_cell;
  map->entry_count = cell_count * item_per_cell;
  map->cell_count = cell_count;
  map->cell_width = cell_width;
  shm_reset(map);
  return map;
}

void destroySpatialHashMap(SpatialHashMap_t *shm) { free(shm); }

// abcd => a0d0c0d0 on 16bits
uint32_t do_morton_thing(uint32_t x) {
  x = x & 0xFFFF;
  x = (x ^ (x << 8)) & 0x00FF00FF;
  x = (x ^ (x << 4)) & 0x0F0F0F0F;
  x = (x ^ (x << 2)) & 0x33333333;
  x = (x ^ (x << 1)) & 0x55555555;
  return x;
}

uint32_t mortonEncode2D(uint32_t x, uint32_t y) {
  return (do_morton_thing(y) << 1) + do_morton_thing(x);
}

size_t shm_hash(SpatialHashMap_t *shm, Vector2 pos) {
  // return mortonEncode2D(pos.x / shm->cell_width, pos.y / shm->cell_width);
  return (((int32_t)(pos.x / shm->cell_width) * 73856093) ^
          ((int32_t)(pos.y / shm->cell_width) * 83492791)) %
         shm->cell_count;
}

void shm_insert(SpatialHashMap_t *shm, Vector2 pos, size_t val) {
  size_t hash = shm_hash(shm, pos);
  size_t start_index = hash * shm->item_per_cell;
  assert(start_index < shm->entry_count && "bruh");
  size_t index = start_index;

  while (true) {
    if (!shm->entries[index].occupied) {
      shm->entries[index] = (hmentry){
          .occupied = true,
          .hash = hash,
          .i = val,
      };
      return;
    }
    index = (index + 1) % shm->entry_count;
    assert(index != start_index && "OOM");
  }
}

typedef struct {
  SpatialHashMap_t *shm;
  size_t hash;
  size_t index;
  size_t start_index;
  bool fail;
} shmiter;

void init_shm_iter(shmiter *iter, SpatialHashMap_t *shm, Vector2 pos) {
  iter->hash = shm_hash(shm, pos);
  iter->shm = shm;
  iter->index = iter->start_index = iter->hash * shm->item_per_cell;
  iter->fail = false;
  assert(iter->index < shm->entry_count && "bruh");
}

// Returns NULL if iterator has no more value and a pointer to the val stored
// else
size_t *shm_iter_next(shmiter *iter) {
  while (!iter->fail) {
    hmentry *entry = &iter->shm->entries[iter->index];
    iter->index = (iter->index + 1) % iter->shm->entry_count;
    if (iter->index == iter->start_index) {
      iter->fail = true;
    }
    if (!entry->occupied) {
      iter->fail = true;
      return NULL;
    }
    if (entry->hash == iter->hash) {
      return &entry->i;
    }
  }
  return NULL;
}

enum Dir {
  DIR_NORTH_WEST,
  DIR_NORTH,
  DIR_NORTH_EAST,
  DIR_WEST,
  DIR_CENTER,
  DIR_EAST,
  DIR_SOUTH_WEST,
  DIR_SOUTH,
  DIR_SOUTH_EAST,
};

typedef struct {
  shmiter it;
  enum Dir current_dir;
  Vector2 pos;
} shmiter_neighboors;

void init_shmiter_neighboors(shmiter_neighboors *it, SpatialHashMap_t *shm,
                             Vector2 pos) {
  it->pos = pos;

  it->current_dir = DIR_NORTH_WEST;
  init_shm_iter(&it->it, shm,
                (Vector2){
                    .x = pos.x - shm->cell_width,
                    .y = pos.y - shm->cell_width,
                });
}

size_t *shmiter_neighboors_next(shmiter_neighboors *it) {
  size_t *n = shm_iter_next(&it->it);
  if (n != NULL) {
    return n;
  }

  SpatialHashMap_t *shm = it->it.shm;
  switch (it->current_dir) {
  case DIR_NORTH_WEST:
    it->current_dir = DIR_NORTH;
    init_shm_iter(&it->it, shm,
                  (Vector2){
                      .x = it->pos.x,
                      .y = it->pos.y - shm->cell_width,
                  });
    break;
  case DIR_NORTH:
    it->current_dir = DIR_NORTH_EAST;
    init_shm_iter(&it->it, shm,
                  (Vector2){
                      .x = it->pos.x + shm->cell_width,
                      .y = it->pos.y - shm->cell_width,
                  });
    break;
  case DIR_NORTH_EAST:
    it->current_dir = DIR_WEST;
    init_shm_iter(&it->it, shm,
                  (Vector2){
                      .x = it->pos.x - shm->cell_width,
                      .y = it->pos.y,
                  });
    break;
  case DIR_WEST:
    it->current_dir = DIR_CENTER;
    init_shm_iter(&it->it, shm,
                  (Vector2){
                      .x = it->pos.x,
                      .y = it->pos.y,
                  });
    break;
  case DIR_CENTER:
    it->current_dir = DIR_EAST;
    init_shm_iter(&it->it, shm,
                  (Vector2){
                      .x = it->pos.x + shm->cell_width,
                      .y = it->pos.y,
                  });
    break;
  case DIR_EAST:
    it->current_dir = DIR_SOUTH_WEST;
    init_shm_iter(&it->it, shm,
                  (Vector2){
                      .x = it->pos.x - shm->cell_width,
                      .y = it->pos.y + shm->cell_width,
                  });
    break;
  case DIR_SOUTH_WEST:
    it->current_dir = DIR_SOUTH;
    init_shm_iter(&it->it, shm,
                  (Vector2){
                      .x = it->pos.x,
                      .y = it->pos.y + shm->cell_width,
                  });
    break;
  case DIR_SOUTH:
    it->current_dir = DIR_SOUTH_EAST;
    init_shm_iter(&it->it, shm,
                  (Vector2){
                      .x = it->pos.x + shm->cell_width,
                      .y = it->pos.y + shm->cell_width,
                  });
    break;
  case DIR_SOUTH_EAST:
    return NULL;
  }

  return shmiter_neighboors_next(it);
}

ball_t *balls;
size_t ball_count = 0;
SpatialHashMap_t *m;

typedef struct {
  size_t i, j;
  float L, k;
} spring_t;

spring_t *springs = NULL;
size_t spring_count = 0;
size_t spring_capacity = 0;

size_t *ball2springs;

void DrawTextCentered(const char *text, int posX, int posY, int fontSize,
                      Color color) {
  int width = MeasureText(text, fontSize);
  DrawText(text, posX - width / 2, posY, fontSize, color);
}

Vector2 GetWindowPosition_() {
#if defined(PLATFORM_WEB)
  return Vector2{0};
#else
  return GetWindowPosition();
#endif
}

void InitGameplayScreen(void) {
  if (RAND_SEED != 0) {
    SetRandomSeed(RAND_SEED);
  }

  ball_count = BALL_COUNT;
  balls = (ball_t *)malloc(sizeof(ball_t) * ball_count);
  ball2springs = (size_t *)malloc(sizeof(size_t) * ball_count * ball_count);

  for (size_t i = 0; i < ball_count; i++) {
    balls[i] = (ball_t){
        .pos =
            {
                static_cast<float>(GetRandomValue(
                    GetWindowPosition_().x + (float)RADIUS_DISPLAY / 2,
                    GetWindowPosition_().x + GetScreenWidth() -
                        (float)RADIUS_DISPLAY / 2 - 1)),
                static_cast<float>(GetRandomValue(
                    GetWindowPosition_().y + (float)RADIUS_DISPLAY / 2,
                    GetWindowPosition_().y + GetScreenHeight() -
                        (float)RADIUS_DISPLAY / 2 - 1)),
            },
        .vel = {},
        .type = BallType(GetRandomValue(0, BALL_TYPE_MAX - 1)),
    };

    for (size_t j = 0; j < ball_count; j++) {
      ball2springs[j + i * ball_count] = (size_t)(-1);
    }
  }

  m = createSpatialHashMap(MAX(3, BALL_COUNT / 15), 4096, 2 * RADIUS_EFFECT);
}

void UpdateGameplayScreen(void) {
  float dt = GetFrameTime();
  if (dt == 0.0) {
    return;
  }
  for (size_t i = 0; i < ball_count; i++) {
    balls[i].vel.y += dt * gravity;
  }

  // Apply viscosity
  for (size_t i = 0; i < ball_count; i++) {
    shmiter_neighboors it;
    init_shmiter_neighboors(&it, m, balls[i].pos);
    size_t *v;
    while ((v = shmiter_neighboors_next(&it)) != NULL) {
      size_t j = *v;
      if (j >= i) {
        continue;
      }
      Vector2 r = Vector2Subtract(balls[j].pos, balls[i].pos);
      Vector2 rh = Vector2Normalize(r);
      float q = Vector2Length(r) / RADIUS_EFFECT;
      if (q < 1.00) {
        float u =
            Vector2DotProduct(Vector2Subtract(balls[i].vel, balls[i].vel), rh);
        if (u > 0) {
          Vector2 I = Vector2Scale(rh, dt * (1.0 - q) *
                                           (sigma * u + beta * u * u) * 0.5);
          balls[i].vel = Vector2Subtract(balls[i].vel, I);
          balls[j].vel = Vector2Add(balls[j].vel, I);
        }
      }
    }
  }

  shm_reset(m);
  for (size_t i = 0; i < ball_count; i++) {
    balls[i].old_pos = balls[i].pos;
    balls[i].pos = Vector2Add(balls[i].pos, Vector2Scale(balls[i].vel, dt));
    shm_insert(m, balls[i].pos, i);
  }

  // Adjust springs (5.2)
  for (size_t i = 0; i < ball_count; i++) {
    shmiter_neighboors it;
    init_shmiter_neighboors(&it, m, balls[i].pos);
    size_t *v;
    while ((v = shmiter_neighboors_next(&it)) != NULL) {
      size_t j = *v;
      if (j >= i) {
        continue;
      }

      float r = Vector2Distance(balls[i].pos, balls[j].pos);
      float q = r / RADIUS_EFFECT;
      size_t spring_idx = ball2springs[j + i * ball_count];
      if (q < 1.00) {
        if (spring_idx == (size_t)(-1)) {
          // printf("adding spring at %zu %zu (new spring_count: %zu)\n", i, j,
          //        spring_count + 1);
          if (spring_count == spring_capacity) {
            spring_capacity = spring_capacity > 0 ? 2 * spring_capacity : 1;
            printf("realloc spring with cap %zu!\n", spring_capacity);
            springs = (spring_t *)realloc(springs,
                                          sizeof(spring_t) * spring_capacity);
            assert(springs != NULL);
          }

          spring_idx = ball2springs[j + i * ball_count] = spring_count;
          springs[spring_count] = (spring_t){
              .i = i,
              .j = j,
              .L = RADIUS_EFFECT,
              .k = plasticity,
          };
          spring_count++;
        }
      }
      if (spring_idx == (size_t)(-1)) {
        continue;
      }

      spring_t *s = &springs[spring_idx];

      float d = yield_ratio * s->L;
      if (r > s->L + d) {
        s->L += dt * s->k * (r - s->L - d);
      } else if (r < s->L - d) {
        s->L -= dt * s->k * (s->L - r - d);
      }
    }
  }
  {
    size_t spring_index = 0;
    while (spring_index < spring_count) {
      if (springs[spring_index].L <= RADIUS_EFFECT) {
        spring_index++;
        continue;
      }
      // printf("removing spring at %zu %zu (now spring count: %zu))\n",
      //        springs[spring_index].i, springs[spring_index].j,
      //        spring_count - 1);
      ball2springs[springs[spring_index].j +
                   springs[spring_index].i * ball_count] = (size_t)(-1);

      springs[spring_index] = springs[spring_count - 1];
      ball2springs[springs[spring_index].j +
                   springs[spring_index].i * ball_count] = spring_index;
      spring_count--;

      if (spring_capacity > 4 && 4 * spring_count < spring_capacity) {
        spring_capacity = spring_capacity / 2;
        printf("realloc spring with cap %zu!\n", spring_capacity);
        springs =
            (spring_t *)realloc(springs, sizeof(spring_t) * spring_capacity);
      }

      // DO NOT i++ TO NOT  SKIP THE SWAPPED INDEX
    }
  }

  // Apply springs
  for (size_t i = 0; i < spring_count; i++) {
    Vector2 r =
        Vector2Subtract(balls[springs[i].j].pos, balls[springs[i].j].pos);
    Vector2 D = Vector2Scale(Vector2Normalize(r),
                             0.5 * dt * dt * springs[i].k *
                                 (1.0 - springs[i].L / RADIUS_EFFECT) *
                                 (springs[i].L - Vector2Length(r)));
    balls[springs[i].i].pos = Vector2Subtract(balls[springs[i].i].pos, D);
    balls[springs[i].j].pos = Vector2Add(balls[springs[i].j].pos, D);
  }

  // Double density relaxation
  // First: Incompressibility Relaxation
  // Second: Near pressure (to avoid clustering)
  for (size_t i = 0; i < ball_count; i++) {
    float near_pressure = 0.0;
    float pressure = 0.0;

    float density = 0.0;
    float near_density = 0.0;

    shmiter_neighboors it;
    init_shmiter_neighboors(&it, m, balls[i].pos);
    size_t *v;
    while ((v = shmiter_neighboors_next(&it)) != NULL) {
      size_t j = *v;
      float d =
          1.0 - Vector2Distance(balls[i].pos, balls[j].pos) / RADIUS_EFFECT;
      if (d < 0.0) {
        continue;
      }

      density += d * d;
      near_density += d * d * d;
    }

    near_pressure = k_near * near_density;
    pressure = k * (density - density_0);

    Vector2 dx = {};

    init_shmiter_neighboors(&it, m, balls[i].pos);
    while ((v = shmiter_neighboors_next(&it)) != NULL) {
      size_t j = *v;
      Vector2 r = Vector2Subtract(balls[j].pos, balls[i].pos);
      float d =
          1.0 - Vector2Distance(balls[i].pos, balls[j].pos) / RADIUS_EFFECT;
      if (d < 0.0) {
        continue;
      }

      Vector2 pressure_displacement =
          Vector2Scale(r, (pressure * d + near_pressure * d * d) * dt * dt / 2);
      dx = Vector2Subtract(dx, pressure_displacement);
      balls[j].pos = Vector2Add(balls[j].pos, pressure_displacement);
    }
    balls[i].pos = Vector2Add(balls[i].pos, dx);
  }

  typedef struct {
    Vector2 N;
    Vector2 pos;
  } wall_t;
  wall_t walls[] = {
      {
          .N = {.x = 1, .y = 0},
          .pos = {.x = GetWindowPosition_().x + (float)RADIUS_DISPLAY / 2,
                  .y = 0},
      },
      {
          .N = {.x = -1, .y = 0},
          .pos = {.x = GetWindowPosition_().x + GetScreenWidth() -
                       (float)RADIUS_DISPLAY / 2 - 1,
                  .y = 0},
      },
      {
          .N = {.x = 0, .y = -1},
          .pos = {.x = 0,
                  .y = GetWindowPosition_().y + GetScreenHeight() -
                       (float)RADIUS_DISPLAY / 2 - 1},
      },
      {
          .N = {.x = 0, .y = 1},
          .pos = {.x = 0,
                  .y = GetWindowPosition_().y + (float)RADIUS_DISPLAY / 2},
      },
  };

  // Simple custom wall collision
  // wall is just a big spring
  for (size_t i = 0; i < ball_count; i++) {
    for (size_t wi = 0; wi < sizeof(walls) / sizeof(wall_t); wi++) {
      wall_t w = walls[wi];
      float d = Vector2DotProduct(Vector2Subtract(balls[i].pos, w.pos), w.N);
      if (d < 0) {
        Vector2 VN = Vector2Scale(w.N, Vector2DotProduct(balls[i].vel, w.N));
        Vector2 VT = Vector2Subtract(balls[i].vel, VN);
        Vector2 I = Vector2Add(Vector2Scale(VN, wall_elasticity),
                               Vector2Scale(VT, +0.1));

        balls[i].pos = Vector2Add(balls[i].pos, Vector2Scale(I, -1 * dt));
        float d = Vector2DotProduct(Vector2Subtract(balls[i].pos, w.pos), w.N);
        if (d < 0) {
          balls[i].pos = Vector2Add(balls[i].pos, Vector2Scale(w.N, -d));
        }
      }
    }
  }

  for (size_t i = 0; i < ball_count; i++) {
    balls[i].vel =
        Vector2Scale(Vector2Subtract(balls[i].pos, balls[i].old_pos), 1.0 / dt);
  }
}

void DrawGameplayScreen(void) {
  for (size_t i = 0; i < ball_count; i++) {
    ball_t *ball = &balls[i];
    DrawCircleV(Vector2Subtract(ball->pos, GetWindowPosition_()),
                RADIUS_DISPLAY, BallTypeColor[ball->type]);
  }
}

// Gameplay Screen Unload logic
void UnloadGameplayScreen(void) {
  destroySpatialHashMap(m);
  free(springs);
  free(ball2springs);
}

void ImGuiGameplayScreen() {
  static bool open = true;
  if (ImGui::Begin("constants", &open, 0)) {
    ImGui::SliderFloat("gravity", &gravity, 0.0, 100);
    ImGui::SliderFloat("k", &k, 0.0, 100);
    ImGui::SliderFloat("density0", &density_0, 0.0, 100);
    ImGui::SliderFloat("k_near", &k_near, 0.0, 100);
    ImGui::SliderFloat("plasticity", &plasticity, 0.0, 100);
    ImGui::SliderFloat("yield_ratio", &yield_ratio, 0.0, 1);
    ImGui::SliderFloat("sigma", &sigma, 0.0, 1);
    ImGui::SliderFloat("beta", &beta, 0.0, 1);
    ImGui::SliderFloat("wall elasticity", &wall_elasticity, 1.0, 2.0);
  }
  ImGui::End();
}
