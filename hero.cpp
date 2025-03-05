#include <raylib.h>
#include <vector>
#include <format>
#include <cmath>  // For std::isnan
#define RAYTMX_IMPLEMENTATION
#include "raytmx.h"
#include <string>

const int W = 600;
const int H = 600;
const float MAX_GRAV = 300.0f;
const float JUMP_FORCE = -250.0f;
const float MAX_JUMP_HOLD = 0.5f;
const float JUMP_BOOST = -350.0f;

enum Direction {
    LEFT = -1,
    RIGHT = 1,
};

enum CurrentState {
    DEAD = 0,
    RUNNING = 1,
    IDLE = 2,
    ROLLING = 3,
    JUMPING = 4,
    FALLING = 5,
    ATTACKING = 6
};

enum EnemyState{
    MOVING = 1,
    IDLE = 2,
    ATTACKING = 3
};

enum AnimationType {
    REPEATING,
    ONESHOT
};

struct Animation {
    int fst;
    int lst;
    int cur;
    int offset;
    int width;
    int height;
    float spd;
    float rem;
    AnimationType type;
};

struct Player {
    Rectangle rect;
    Vector2 vel;
    Texture2D sprite;
    Direction dir;
    CurrentState state;
    std::vector<Animation> animations;
    bool isJumping;
    float jumpTime;
    int health;
};

struct Enemy {
    Rectangle rect;
    Vector2 vel;
    Texture2D sprite;
    Direction dir;
    EnemyState e_state;
    std::vector<Animation> animations;
};

struct Projectile {
    Rectangle bullet;
    Vector2 vel;
    Texture2D sprite;
    Direction dir;
    CurrentState state;
    std::vector<Animation> animations;
    bool isActivate;
};

void update_animation(Animation *self) {
    float dt = GetFrameTime();
    self->rem -= dt;
    if (self->rem < 0) {
        self->rem = self->spd;
        self->cur++;
        if (self->cur > self->lst) {
            switch (self->type) {
                case REPEATING:
                    self->cur = self->fst;
                    break;
                case ONESHOT:
                    self->cur = self->lst;
                    break;
            }
        }
    }
}

Rectangle animation_frame(const Animation *self) {
    int x = (self->cur % (self->lst + 1)) * self->width;
    int y = self->offset * self->height;

    return (Rectangle){(float)x, (float)y, (float)self->width, (float)self->height};
}

void drawPlayer(const Player *player) {
    if (player->state < 0 || player->state >= player->animations.size()) {
        TraceLog(LOG_ERROR, "Invalid animation state: %d", player->state);
        return;
    }

    Rectangle source = animation_frame(&(player->animations[player->state]));
    source.width = source.width * static_cast<float>(player->dir);
    DrawTexturePro(player->sprite, source, player->rect, {0, 0}, 0.0f, WHITE);
}

void movePlayer(Player *player) {
  player->vel.x = 0.0f;
  bool changedState = false;

  // Movement left/right
  if (IsKeyDown(KEY_A)) {
      player->vel.x = -200.0f;
      player->dir = LEFT;
      if (player->vel.y == 0.0f) {
          player->state = RUNNING;
          changedState = true;
      }
  } else if (IsKeyDown(KEY_D)) {
      player->vel.x = 200.0f;
      player->dir = RIGHT;
      if (player->vel.y == 0.0f) {
          player->state = RUNNING;
          changedState = true;
      }
  }

  // Jump Logic
  if (IsKeyPressed(KEY_SPACE) && !player->isJumping) {
      player->jumpTime = 0.0f;  // Start tracking jump time
      player->vel.y = JUMP_FORCE;
      player->state = JUMPING;
      player->isJumping = true;
      changedState = true;
  }
  
  // Holding SPACE boosts jump height
  if (IsKeyDown(KEY_SPACE) && player->isJumping) {
      player->jumpTime += GetFrameTime(); // Track hold duration
      if (player->jumpTime < MAX_JUMP_HOLD) {
          player->vel.y = JUMP_BOOST;  // Apply more force for higher jump
          changedState = true;
      }
  }

  // Stop boosting when SPACE is released
  if (IsKeyReleased(KEY_SPACE) && player->isJumping) {
      player->jumpTime = MAX_JUMP_HOLD;  // Prevent further boosting
      changedState = true;
  }

  // Falling state if moving downward
  if (player->vel.y > 0) {
      player->state = FALLING;
      player->isJumping = true;
      changedState = true;
  }

  /*
  // Attack animation
  if (IsKeyDown(KEY_ENTER)) {
      player->state = CurrentState::ATTACKING;
      changedState = true;
  }
  */

  // Default to idle if no movement
  if (!changedState) {
      player->state = CurrentState::IDLE;
  }
}

void applyGravity(Vector2 *vel) {
  vel->y += 1000.0f * GetFrameTime();  // Increase gravity effect
  if (vel->y > MAX_GRAV) {
      vel->y = MAX_GRAV;  // Cap fall speed
  }
}

void moveRectByVel(Rectangle *rect, const Vector2 *vel) {
    rect->x += vel->x * GetFrameTime();
    rect->y += vel->y * GetFrameTime();
}

void keepPlayerInScreen(Player *player) {
    if (player->rect.y > (H - player->rect.height)) {
        player->vel.y = 0.0f;
        player->rect.y = (H - player->rect.height);
        player->isJumping = false; // Allow jumping again
    }
}

void checkTileCollisions(TmxMap *map, Player *player) {
  for (unsigned int i = 0; i < map->layersLength; i++) {
      if (strcmp(map->layers[i].name, "collisions") == 0 && map->layers[i].type == LAYER_TYPE_OBJECT_GROUP) {
          TmxObjectGroup &objectGroup = map->layers[i].exact.objectGroup;
          for (unsigned int j = 0; j < objectGroup.objectsLength; j++) {
              TmxObject &col = objectGroup.objects[j];
              Rectangle platform = {col.aabb.x, col.aabb.y, col.aabb.width, col.aabb.height};

              if (CheckCollisionRecs(player->rect, platform)) {
                  TraceLog(LOG_DEBUG, "Collision detected!");

                  // Compute previous position
                  float previousX = player->rect.x - player->vel.x * GetFrameTime();
                  float previousY = player->rect.y - player->vel.y * GetFrameTime();

                  // Determine collision direction
                  bool comingFromTop = previousY + player->rect.height <= platform.y;
                  bool comingFromBottom = previousY >= platform.y + platform.height;
                  bool comingFromLeft = previousX + player->rect.width <= platform.x;
                  bool comingFromRight = previousX >= platform.x + platform.width;

                  if (comingFromTop) {
                      // Standing on platform
                      player->vel.y = 0.0f;
                      player->rect.y = platform.y - player->rect.height;
                      player->isJumping = false; // Allow jumping again
                  } else if (comingFromBottom) {
                      // Hitting the bottom of the platform
                      player->vel.y = 0.0f;
                      player->rect.y = platform.y + platform.height;
                  } else if (comingFromLeft) {
                      // Hitting the left side
                      player->vel.x = 0.0f;
                      player->rect.x = platform.x - player->rect.width;
                  } else if (comingFromRight) {
                      // Hitting the right side
                      player->vel.x = 0.0f;
                      player->rect.x = platform.x + platform.width;
                  }
              }
          }
      }
  }
}

void drawHealth(int health) {
  // Draw health in the bottom-left corner
  std::string healthText = "HP: " + std::to_string(health);
  DrawText(healthText.c_str(), 10, H - 30, 20, WHITE);
}

void cameraFollow(Camera2D *camera, const Player *player) {
    if (std::isnan(player->rect.x) || std::isnan(player->rect.y)) {
        TraceLog(LOG_ERROR, "Player position is NaN! Resetting...");
        return;
    }

    camera->target = (Vector2){player->rect.x, player->rect.y};
}

int main() {
    InitWindow(W, H, "Hero Animation Example");

    const char* tmx = "map.tmx";
    TmxMap* map = LoadTMX(tmx);
    if (map == nullptr) {
        TraceLog(LOG_ERROR, "Couldn't load the map: %s", tmx);
        return EXIT_FAILURE;
    }

    Texture2D hero = LoadTexture("assets/herochar-sprites/herochar_spritesheet.png");

    Player player = {
        .rect = {16, 500, 64.0f, 64.0f},
        .vel = {0.0f, 0.0f},
        .sprite = hero,
        .dir = RIGHT,
        .state = CurrentState::IDLE,
        .animations = {
            {0, 7, 0, 0, 16, 16, 0.1f, 0.1f, ONESHOT},
            {0, 5, 0, 1, 16, 16, 0.1f, 0.1f, REPEATING},
            {0, 3, 0, 5, 16, 16, 0.1f, 0.1f, REPEATING},
            {0, 2, 0, 9, 16, 16, 0.1f, 0.1f, REPEATING},
            {0, 2, 0, 7, 16, 16, 0.1f, 0.1f, REPEATING},
            {0, 2, 0, 6, 16, 16, 0.1f, 0.1f, REPEATING},
            {0, 3, 0, 6, 32, 16, 0.15f, 0.15f, ONESHOT},
        },
        .health = 10,
    };

    Camera2D camera = {
        .offset = {W / 2.0f, H / 2.0f},
        .target = {W / 2.0f, H / 2.0f},
        .rotation = 0.0f,
        .zoom = 1.0f,
    };

    while (!WindowShouldClose()) {
        AnimateTMX(map);
        movePlayer(&player);
        applyGravity(&(player.vel));
        moveRectByVel(&(player.rect), &(player.vel));
        checkTileCollisions(map, &player);
        keepPlayerInScreen(&player);
        update_animation(&(player.animations[player.state]));
        cameraFollow(&camera, &player);

        BeginDrawing();
        ClearBackground(SKYBLUE);
        BeginMode2D(camera);
        DrawTMX(map, &camera, 0, 0, WHITE);
        drawPlayer(&player);
        EndMode2D();
        drawHealth(player.health);
        DrawFPS(5, 5);
        EndDrawing();
    }

    UnloadTMX(map);
    UnloadTexture(hero);
    CloseWindow();
    return 0;
}
