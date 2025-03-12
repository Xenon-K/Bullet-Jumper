#include <raylib.h>
#include <vector>
#include <format>
#include <cmath>  // For std::isnan
#include <cstdlib>  // For rand, srand
#include <ctime>    // For time()
#define RAYTMX_IMPLEMENTATION
#include "raytmx.h"
#include <string>
#include <unordered_set>

const int W = 720;
const int H = 1280;
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

/*
enum EnemyState{
    MOVING = 1,
    IDLE = 2,
    ATTACKING = 3
};
*/

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
    int score;
};

struct Score_Orb {
    Rectangle rect;
    float score = 1;
    Color color;
    bool collected;
};

struct Enemy {
    Rectangle rect;
    Vector2 vel;
    Texture2D sprite;
    Direction dir;
    //EnemyState e_state;
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

struct Spike {
    Rectangle rect;   // Collision box
    bool active;      // Whether the spike is visible
    float timer;      // Timer for toggling state
    float yOffset;    // Controls spike movement up/down
    bool rising;      // Whether the spike is moving up
};
std::vector<Spike> spikes;





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
            player->state = CurrentState::RUNNING;
            changedState = true;
        }
    } else if (IsKeyDown(KEY_D)) {
        player->vel.x = 200.0f;
        player->dir = RIGHT;
        if (player->vel.y == 0.0f) {
            player->state = CurrentState::RUNNING;
            changedState = true;
        }
    }

    // Jump Logic
    if (IsKeyPressed(KEY_SPACE) && !player->isJumping) {
        player->jumpTime = 0.0f;  // Start tracking jump time
        player->vel.y = JUMP_FORCE;
        player->state = CurrentState::JUMPING;
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
        player->state = CurrentState::FALLING;
        player->isJumping = true;
        changedState = true;
    }

    // Default to idle if no movement
    if (!changedState) {
        player->state = CurrentState::IDLE;
    }

    
}


Color getOrbColor(float score) {
    float t = (score - 1) / 499.0f;  // t in [0, 1]
    int r = (int)(t * 255);
    int g = 0;
    int b = (int)((1 - t) * 255);
    return Color{ (unsigned char)r, (unsigned char)g, (unsigned char)b, 255 };
}

void spawnOrb(TmxMap* map, const Camera2D &camera, std::vector<Score_Orb> &orbs) {
    float viewX = camera.target.x - (W / 2.0f) / camera.zoom;
    float viewY = camera.target.y - (H / 2.0f) / camera.zoom;
    float viewW = W / camera.zoom;
    float viewH = H / camera.zoom;
    Rectangle viewRect = { viewX, viewY, viewW, viewH };

    static std::unordered_set<TmxObject*> spawnedPlatforms;

    for (unsigned int i = 0; i < map->layersLength; i++) {
        if (strcmp(map->layers[i].name, "collisions") == 0 && map->layers[i].type == LAYER_TYPE_OBJECT_GROUP) {
            TmxObjectGroup &objectGroup = map->layers[i].exact.objectGroup;
            for (unsigned int j = 0; j < objectGroup.objectsLength; j++) {
                TmxObject &col = objectGroup.objects[j];
                Rectangle platform = { col.aabb.x, col.aabb.y, col.aabb.width, col.aabb.height };

                if (CheckCollisionRecs(platform, viewRect)) {
                    if (spawnedPlatforms.find(&col) == spawnedPlatforms.end()) {
                        int orbSize = 16;
                        float orbX = platform.x;
                        if (platform.width > orbSize) {
                            orbX += (rand() % (int)(platform.width - orbSize));
                        }
                        float orbY = platform.y - orbSize;
                        float orbScore = (rand() % 500) + 1;
                        Color orbColor = getOrbColor(orbScore);
                        Score_Orb newOrb = {
                            { orbX, orbY, (float)orbSize, (float)orbSize },
                            orbScore,
                            orbColor,
                            false
                        };
                        orbs.push_back(newOrb);
                        spawnedPlatforms.insert(&col);
                    }
                }
            }
        }
    }
}

void checkOrbCollection(Player *player, std::vector<Score_Orb> &orbs) {
    for (auto it = orbs.begin(); it != orbs.end();) {
        if (CheckCollisionRecs(player->rect, it->rect)) {
            player->score += 1;
            // Optionally, you could play a sound or animation here.
            it = orbs.erase(it);
        } else {
            ++it;
        }
    }
}

void drawOrbs(const std::vector<Score_Orb> &orbs) {
    for (const auto &orb : orbs) {
        // Draw a filled circle at the center of the orb rectangle.
        DrawCircle((int)(orb.rect.x + orb.rect.width / 2), (int)(orb.rect.y + orb.rect.height / 2), (int)(orb.rect.width / 2), orb.color);
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
                Rectangle platform = { col.aabb.x, col.aabb.y, col.aabb.width, col.aabb.height };

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

void drawScore(int score) {
    // Draw score under health
    std::string scoreText = "Score: " + std::to_string(score);
    DrawText(scoreText.c_str(), 10, H - 60, 20, WHITE);
}


void cameraFollow(Camera2D *camera, const Player *player) {
    static bool initialized = false;
    static float highestCameraY = 0.0f; // Store the highest Y position

    if (std::isnan(player->rect.x) || std::isnan(player->rect.y)) {
        TraceLog(LOG_ERROR, "Player position is NaN! Resetting...");
        return;
    }

    // Initialize the highestCameraY to the player's initial position
    if (!initialized) {
        highestCameraY = player->rect.y;
        initialized = true;
    }

    // Update the highestCameraY only if the player moves higher
    if (player->rect.y < highestCameraY) {
        highestCameraY = player->rect.y;
    }

    camera->target.x = player->rect.x; // Always follow player in X
    camera->target.y = highestCameraY; // Keep camera at the highest Y position
}
void checkKillboxCollision(Player* player, const Rectangle& killbox) {
    if (CheckCollisionRecs(player->rect, killbox)) {
        player->health = 0;  // Set health to zero (player dies)
        player->state = CurrentState::DEAD;
        TraceLog(LOG_ERROR, "Player fell into the killbox!");
    }
}

void LoadSpikesFromTMX(TmxMap* map){
    for (unsigned int i = 0; i < map->layersLength; i++) {
        if (strcmp(map->layers[i].name, "spikes") == 0 && map->layers[i].type == LAYER_TYPE_OBJECT_GROUP) {
            TmxObjectGroup& objectGroup = map->layers[i].exact.objectGroup;
            // Loop through all objects in the object group (spikes)
            for (unsigned int j = 0; j < objectGroup.objectsLength; j++) {
                TmxObject& obj = objectGroup.objects[j];

                
                // Create a new Spike object
                Spike spike;
                spike.rect = { obj.aabb.x, obj.aabb.y, obj.aabb.width, obj.aabb.height };
                spike.timer = GetRandomValue(1, 3);  // Random time for spike to rise/fall
                spike.rising = true;  // Start by moving up
                spike.yOffset = 0;    // Initial Y offset is zero
                
                // Add spike to the spikes vector
                spikes.push_back(spike);
            }
        }
    }
}

// void UpdateSpikes() {
//     for (size_t i = 0; i < spikes.size(); i++) {
//         // Reduce timer each frame
//         spikes[i].timer -= GetFrameTime();
        
//         // If the timer is done, toggle the rise/fall direction and reset the timer
//         if (spikes[i].timer <= 0) {
//             spikes[i].timer = GetRandomValue(1, 3);  // Reset the timer to a new random value
//             spikes[i].rising = !spikes[i].rising;    // Toggle rising/falling direction
//         }

//         // Move spike up or down based on the rising state
//         if (spikes[i].rising) {
//             spikes[i].yOffset += 100.0f * GetFrameTime(); // Increase the Y offset (moving up)
//             if (spikes[i].yOffset >= spikes[i].rect.height) {
//                 spikes[i].yOffset = spikes[i].rect.height; // Cap at the maximum height
//             }
//         } else {
//             spikes[i].yOffset -= 100.0f * GetFrameTime(); // Decrease the Y offset (moving down)
//             if (spikes[i].yOffset <= 0) {
//                 spikes[i].yOffset = 0; // Stop at the original position
//             }
//         }

//         // Apply the offset to the spike's Y position
//         spikes[i].rect.y = spikes[i].rect.y - spikes[i].yOffset;
//     }
// }
void DrawSpikes() {
    for (size_t i = 0; i < spikes.size(); i++) {
        // Draw the spike as a rectangle (you can also use a texture for spikes)
        DrawRectangleRec(spikes[i].rect, RED);  // Or use a texture for spikes
    }
}


int main() {
    InitWindow(W, H, "Hero Animation Example");
    SetTargetFPS(60);
    // Seed the random generator for orb spawning.
    srand((unsigned)time(NULL));

    const char* tmx = "hard.tmx";
    TmxMap* map = LoadTMX(tmx);
    if (map == nullptr) {
        TraceLog(LOG_ERROR, "Couldn't load the map: %s", tmx);
        return EXIT_FAILURE;
    }

    Texture2D hero = LoadTexture("assets/herochar-sprites/herochar_spritesheet.png");

    Player player = {
        .rect = {0, 1700, 64.0f, 64.0f},
        .vel = {0.0f, 0.0f},
        .sprite = hero,
        .dir = RIGHT,
        .state = IDLE,
        .animations = {
            {0, 7, 0, 0, 16, 16, 0.1f, 0.1f, ONESHOT},
            {0, 5, 0, 1, 16, 16, 0.1f, 0.1f, REPEATING},
            {0, 3, 0, 5, 16, 16, 0.1f, 0.1f, REPEATING},
            {0, 2, 0, 9, 16, 16, 0.1f, 0.1f, REPEATING},
            {0, 2, 0, 7, 16, 16, 0.1f, 0.1f, REPEATING},
            {0, 2, 0, 6, 16, 16, 0.1f, 0.1f, REPEATING},
            {0, 3, 0, 6, 32, 16, 0.15f, 0.15f, ONESHOT},
        },
        .isJumping = false,
        .jumpTime = 0.0f,
        .health = 10,
        .score = 0,
    };

    Camera2D camera = {
        .offset = {W / 2.0f, H / 2.0f},
        .target = {W / 2.0f, H / 2.0f},
        .rotation = 0.0f,
        .zoom = 1.0f,
    };
    Rectangle killbox = {0, 0, (float)W, 40}; // Width matches screen, height can be adjusted

    std::vector<Score_Orb> orbs;

    while (!WindowShouldClose()) {
        AnimateTMX(map);
        movePlayer(&player);
        applyGravity(&(player.vel));
        moveRectByVel(&(player.rect), &(player.vel));
        checkTileCollisions(map, &player);
        //keepPlayerInScreen(&player);
        update_animation(&(player.animations[player.state]));
        cameraFollow(&camera, &player);
        spawnOrb(map, camera, orbs);
        checkOrbCollection(&player, orbs);

        killbox.y = camera.target.y + (H / 2.0f) / camera.zoom;
        checkKillboxCollision(&player, killbox);

        BeginDrawing();
        ClearBackground(SKYBLUE);
        BeginMode2D(camera);
        
        DrawTMX(map, &camera, 0, 0, WHITE);
        DrawRectangleRec(killbox, RED);
        drawPlayer(&player);
        drawOrbs(orbs);
        EndMode2D();
        drawScore(player.score);
        drawHealth(player.health);
        LoadSpikesFromTMX(map);  // Load spikes from TMX
        //UpdateSpikes();           // Update spike positions (rising and falling)
        DrawSpikes(); 
        
        DrawFPS(5, 5);
        EndDrawing();
    }

    UnloadTMX(map);
    UnloadTexture(hero);
    CloseWindow();
    return 0;
}
