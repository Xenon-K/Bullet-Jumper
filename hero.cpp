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

// Game states
enum GameState {
    MENU,
    GAMEPLAY,
    GAME_OVER
};

// Difficulty levels
enum Difficulty {
    EASY,
    NORMAL,
    HARD
};

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

// Add these new structures and variables
struct DeathTransition {
    bool active;
    float alpha;
    float timer;
    const float duration = 1.0f; // 1 second transition
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

// Reset camera follow system
void ResetCameraFollow() {
    // This function resets the static variables in cameraFollow
    static bool* initialized = nullptr;
    static float* highestCameraY = nullptr;
    
    // Find the static variables by address
    if (!initialized || !highestCameraY) {
        // This is a hack to reset static variables
        // We're using a dummy camera and player to call cameraFollow
        // which will initialize the static variables
        Camera2D dummyCamera = {};
        Player dummyPlayer = {};
        dummyPlayer.rect.x = 0;
        dummyPlayer.rect.y = 1700;
        cameraFollow(&dummyCamera, &dummyPlayer);
        
        // Now we need to find where these static variables are stored
        // This is a bit of a hack, but it works for our purpose
        initialized = new bool(false);
        highestCameraY = new float(1700);
    }
    
    // Reset the static variables
    *initialized = false;
}

void checkKillboxCollision(Player* player, const Rectangle& killbox, DeathTransition* transition) {
    // Only check if we're not already in a death transition
    if (!transition->active) {
        // Check if player is below the killbox (completely fallen off the map)
        if (player->rect.y > killbox.y + killbox.height) {
            player->health = 0;
            player->state = DEAD;
            transition->active = true;
            transition->alpha = 0.0f;
            transition->timer = 0.0f;
            TraceLog(LOG_INFO, "Player fell off the map!");
        }
        
        // Check if player is touching the killbox
        if (CheckCollisionRecs(player->rect, killbox)) {
            player->health = 0;
            player->state = DEAD;
            transition->active = true;
            transition->alpha = 0.0f;
            transition->timer = 0.0f;
            TraceLog(LOG_INFO, "Player touched the killbox!");
        }
    }
}

// Check if player is outside horizontal map boundaries
void checkHorizontalBoundaries(Player* player, TmxMap* map, DeathTransition* transition) {
    // Only check if we're not already in a death transition
    if (!transition->active) {
        // Get map width from TMX
        float mapWidth = 0;
        for (unsigned int i = 0; i < map->layersLength; i++) {
            if (map->layers[i].type == LAYER_TYPE_TILE_LAYER) {
                mapWidth = map->layers[i].exact.tileLayer.width * map->tileWidth;
                break;
            }
        }
        
        // Check if player is outside map boundaries
        if (player->rect.x < -100 || player->rect.x > mapWidth + 100) {
            player->health = 0;
            player->state = DEAD;
            transition->active = true;
            transition->alpha = 0.0f;
            transition->timer = 0.0f;
            TraceLog(LOG_INFO, "Player went outside horizontal map boundaries!");
        }
    }
}

// Update death transition effect
bool updateDeathTransition(DeathTransition* transition) {
    if (transition->active) {
        transition->timer += GetFrameTime();
        transition->alpha = transition->timer / transition->duration;
        
        // Clamp alpha between 0 and 1
        if (transition->alpha > 1.0f) {
            transition->alpha = 1.0f;
            // Transition complete
            return true;
        }
    }
    return false;
}

// Draw death transition effect
void drawDeathTransition(DeathTransition* transition) {
    if (transition->active) {
        // Draw a black rectangle that fades in
        DrawRectangle(0, 0, W, H, ColorAlpha(BLACK, transition->alpha));
    }
}

void LoadSpikesFromTMX(TmxMap* map, Player *player){
    for (unsigned int i = 0; i < map->layersLength; i++) {
        if (strcmp(map->layers[i].name, "spikes") == 0 && map->layers[i].type == LAYER_TYPE_OBJECT_GROUP) {
            TmxObjectGroup& objectGroup = map->layers[i].exact.objectGroup;
            // Loop through all objects in the object group (spikes)
            for (unsigned int j = 0; j < objectGroup.objectsLength; j++) {
                TmxObject& obj = objectGroup.objects[j];
                
                
                // Create a new Spike object
                Spike spike;
                spike.rect = { obj.aabb.x, obj.aabb.y, obj.aabb.width, obj.aabb.height };

                
                    spike.timer = 1;  // Random time for spike to rise/fall
                    spike.rising = true;  // Start by moving up
                    spike.yOffset = 0;    // Initial Y offset is zero
                    spikes.push_back(spike);
            }
        }
    }
}

void UpdateSpikes() {
    for (size_t i = 0; i < spikes.size(); i++) {
        // Decrease the timer for up/down movement phase
        spikes[i].timer -= GetFrameTime();

        // The time it takes for a spike to move up or down before switching
        const float MOVE_DURATION = 3.0f; // 1 second to move up or down

        // Check if the spike is currently in the "move up" phase
        if (spikes[i].rising) {
            // If the spike has finished rising, stop for the pause
            if (spikes[i].timer <= 0) {
                spikes[i].timer = MOVE_DURATION; // Pause for a while before moving down
                spikes[i].rising = false;       // Switch to the downward movement phase
            } else {
                // Move the spike up
                spikes[i].rect.y -= 1.0f;  // Move up by a small value (e.g., 10 pixels)
            }
        } else { 
            // If it's in the "move down" phase
            if (spikes[i].timer <= 0) {
                spikes[i].timer = MOVE_DURATION; // Pause before moving up again
                spikes[i].rising = true;        // Switch to the upward movement phase
            } else {
                // Move the spike down to the original position
                spikes[i].rect.y += 1.0f;  // Move down by a small value (e.g., 10 pixels)
            }
        }
    }
}

void DrawSpikes() {
    for (size_t i = 0; i < spikes.size(); i++) {
        // Draw the spike as a rectangle (you can also use a texture for spikes)
        DrawRectangleRec(spikes[i].rect, RED);  // Or use a texture for spikes
    }
}

// Draw the main menu
void DrawMainMenu(int selectedOption, Difficulty difficulty) {
    const int titleFontSize = 60;
    const int menuFontSize = 30;
    const int optionSpacing = 60;
    
    // Draw title
    const char* title = "BULLET JUMPER";
    int titleWidth = MeasureText(title, titleFontSize);
    DrawText(title, W/2 - titleWidth/2, H/4, titleFontSize, GOLD);
    
    // Draw menu options
    const char* startText = "START GAME";
    int startWidth = MeasureText(startText, menuFontSize);
    DrawText(startText, W/2 - startWidth/2, H/2, menuFontSize, 
             selectedOption == 0 ? RED : WHITE);
    
    // Draw difficulty option
    const char* difficultyText;
    switch(difficulty) {
        case EASY: difficultyText = "DIFFICULTY: EASY"; break;
        case NORMAL: difficultyText = "DIFFICULTY: NORMAL"; break;
        case HARD: difficultyText = "DIFFICULTY: HARD"; break;
        default: difficultyText = "DIFFICULTY: NORMAL";
    }
    
    int diffWidth = MeasureText(difficultyText, menuFontSize);
    DrawText(difficultyText, W/2 - diffWidth/2, H/2 + optionSpacing, menuFontSize, 
             selectedOption == 1 ? RED : WHITE);
    
    // Draw instructions
    const char* instructions = "UP/DOWN: Select Option | ENTER: Confirm | ESC: Quit";
    int instrWidth = MeasureText(instructions, 20);
    DrawText(instructions, W/2 - instrWidth/2, H - 100, 20, LIGHTGRAY);
}

// Draw the game over screen
void DrawGameOver(int score) {
    const int titleFontSize = 60;
    const int textFontSize = 30;
    
    // Draw game over text
    const char* gameOverText = "GAME OVER";
    int gameOverWidth = MeasureText(gameOverText, titleFontSize);
    DrawText(gameOverText, W/2 - gameOverWidth/2, H/3, titleFontSize, RED);
    
    // Draw score
    char scoreText[50];
    sprintf(scoreText, "FINAL SCORE: %d", score);
    int scoreWidth = MeasureText(scoreText, textFontSize);
    DrawText(scoreText, W/2 - scoreWidth/2, H/2, textFontSize, WHITE);
    
    // Draw restart instructions
    const char* restartText = "PRESS ENTER TO RESTART";
    int restartWidth = MeasureText(restartText, textFontSize);
    DrawText(restartText, W/2 - restartWidth/2, H/2 + 100, textFontSize, YELLOW);
    
    const char* menuText = "PRESS M FOR MENU";
    int menuWidth = MeasureText(menuText, textFontSize);
    DrawText(menuText, W/2 - menuWidth/2, H/2 + 150, textFontSize, YELLOW);
}

// Reset player for a new game
void ResetPlayer(Player* player, const char* mapFile) {
    // Set player to a safe starting position
    player->rect = {0, 1700, 64.0f, 64.0f};
    player->vel = {0.0f, 0.0f};
    player->dir = RIGHT;
    player->state = IDLE;
    player->isJumping = false;
    player->jumpTime = 0.0f;
    player->health = 10;
    player->score = 0;
}

// Reset camera to initial position
void ResetCamera(Camera2D* camera, const Player* player) {
    camera->target = {player->rect.x, player->rect.y};
    camera->offset = {W / 2.0f, H / 2.0f};
    camera->rotation = 0.0f;
    camera->zoom = 1.0f;
}

int main() {
    InitWindow(W, H, "Bullet Jumper");
    SetTargetFPS(60);
    // Seed the random generator for orb spawning.
    srand((unsigned)time(NULL));

    // Initialize game state
    GameState gameState = MENU;
    Difficulty difficulty = NORMAL;
    int menuSelection = 0;
    const char* mapFile = "map.tmx"; // Default map (normal difficulty)
    
    TmxMap* map = nullptr;
    
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
    
    // Modify killbox to be more reliable
    // Make it thicker and position it at the bottom of the visible screen
    Rectangle killbox = {0, 0, (float)W, 100}; 

    std::vector<Score_Orb> orbs;
    static bool spikesLoaded = false;
    
    // Variables needed for gameplay (moved outside switch statements)
    float maxFallDistance = 500.0f; // Maximum distance player can fall below camera
    float bottomOfScreen = 0.0f;
    Color killboxColor = {255, 0, 0, 128}; // Semi-transparent red
    
    // Initialize death transition
    DeathTransition deathTransition = {false, 0.0f, 0.0f};
    
    while (!WindowShouldClose()) {
        // Handle game state logic
        switch(gameState) {
            case MENU:
                // Reset death transition when entering menu
                deathTransition.active = false;
                
                // Menu navigation
                if (IsKeyPressed(KEY_DOWN)) {
                    menuSelection = (menuSelection + 1) % 2; // Cycle through menu options
                }
                if (IsKeyPressed(KEY_UP)) {
                    menuSelection = (menuSelection - 1 + 2) % 2; // Cycle through menu options
                }
                
                // Handle menu selection
                if (menuSelection == 1 && IsKeyPressed(KEY_RIGHT)) {
                    difficulty = static_cast<Difficulty>((static_cast<int>(difficulty) + 1) % 3);
                    // Update map file based on difficulty
                    switch(difficulty) {
                        case EASY: mapFile = "map.tmx"; break;
                        case NORMAL: mapFile = "map.tmx"; break; // You can create a medium difficulty map
                        case HARD: mapFile = "hard.tmx"; break;
                    }
                }
                if (menuSelection == 1 && IsKeyPressed(KEY_LEFT)) {
                    difficulty = static_cast<Difficulty>((static_cast<int>(difficulty) + 2) % 3);
                    // Update map file based on difficulty
                    switch(difficulty) {
                        case EASY: mapFile = "map.tmx"; break;
                        case NORMAL: mapFile = "map.tmx"; break; // You can create a medium difficulty map
                        case HARD: mapFile = "hard.tmx"; break;
                    }
                }
                
                // Start game
                if (IsKeyPressed(KEY_ENTER) && menuSelection == 0) {
                    // Load the map based on difficulty
                    if (map != nullptr) {
                        UnloadTMX(map);
                    }
                    map = LoadTMX(mapFile);
                    if (map == nullptr) {
                        TraceLog(LOG_ERROR, "Couldn't load the map: %s", mapFile);
                        return EXIT_FAILURE;
                    }
                    
                    // Reset player and game state
                    ResetPlayer(&player, mapFile);
                    // Reset camera follow system
                    ResetCameraFollow();
                    // Reset camera to follow the player at the new position
                    ResetCamera(&camera, &player);
                    orbs.clear();
                    spikesLoaded = false;
                    gameState = GAMEPLAY;
                }
                break;
                
            case GAMEPLAY:
                // Check if player is dead
                if (player.health <= 0 || player.state == DEAD) {
                    // Only transition to game over if death transition is complete
                    if (!deathTransition.active || updateDeathTransition(&deathTransition)) {
                        gameState = GAME_OVER;
                        break;
                    }
                }
                
                // Only update gameplay if not in death transition
                if (!deathTransition.active) {
                    AnimateTMX(map);
                    movePlayer(&player);
                    applyGravity(&(player.vel));
                    moveRectByVel(&(player.rect), &(player.vel));
                    checkTileCollisions(map, &player);
                    update_animation(&(player.animations[player.state]));
                    cameraFollow(&camera, &player);
                    spawnOrb(map, camera, orbs);
                    checkOrbCollection(&player, orbs);

                    // Update killbox position to be at the bottom of the visible screen
                    killbox.y = camera.target.y + (H / 2.0f) / camera.zoom - 50;
                    checkKillboxCollision(&player, killbox, &deathTransition);
                    
                    // Check horizontal boundaries
                    checkHorizontalBoundaries(&player, map, &deathTransition);
                    
                    // Add a secondary check for falling too far below the camera view
                    bottomOfScreen = camera.target.y + (H / 2.0f) / camera.zoom;
                    if (player.rect.y > bottomOfScreen + maxFallDistance && !deathTransition.active) {
                        player.health = 0;
                        player.state = DEAD;
                        deathTransition.active = true;
                        deathTransition.alpha = 0.0f;
                        deathTransition.timer = 0.0f;
                        TraceLog(LOG_INFO, "Player fell too far below the screen!");
                    }
                } else {
                    // Update death transition
                    updateDeathTransition(&deathTransition);
                }
                break;
                
            case GAME_OVER:
                // Reset death transition when entering game over
                deathTransition.active = false;
                
                // Handle game over inputs
                if (IsKeyPressed(KEY_ENTER)) {
                    // Restart game with same difficulty
                    ResetPlayer(&player, mapFile);
                    // Reset camera follow system
                    ResetCameraFollow();
                    // Reset camera to follow the player at the new position
                    ResetCamera(&camera, &player);
                    orbs.clear();
                    spikesLoaded = false;
                    gameState = GAMEPLAY;
                }
                else if (IsKeyPressed(KEY_M)) {
                    // Return to menu
                    gameState = MENU;
                }
                break;
        }

        BeginDrawing();
        ClearBackground(SKYBLUE);
        
        // Draw based on game state
        switch(gameState) {
            case MENU:
                DrawMainMenu(menuSelection, difficulty);
                break;
                
            case GAMEPLAY:
                BeginMode2D(camera);
                DrawTMX(map, &camera, 0, 0, WHITE);
                
                // Only draw killbox if not in death transition
                if (!deathTransition.active) {
                    DrawRectangleRec(killbox, killboxColor);
                }
                
                drawPlayer(&player);
                drawOrbs(orbs);
                if (!spikesLoaded) {
                    LoadSpikesFromTMX(map, &player);
                    spikesLoaded = true; // Ensure spikes are only loaded once
                }
                UpdateSpikes();
                DrawSpikes(); 
                EndMode2D();
                drawScore(player.score);
                drawHealth(player.health);
                
                // Draw death transition effect on top of everything
                drawDeathTransition(&deathTransition);
                break;
                
            case GAME_OVER:
                DrawGameOver(player.score);
                break;
        }
        
        DrawFPS(5, 5);
        EndDrawing();
    }

    if (map != nullptr) {
        UnloadTMX(map);
    }
    UnloadTexture(hero);
    CloseWindow();
    return 0;
}
