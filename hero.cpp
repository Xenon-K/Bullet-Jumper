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

// Sound effect and music variables
Music menuMusic;
Sound jumpSound;
Sound collectSound;
Sound deathSound;
Sound menuSelectSound;
Sound gameStartSound;
Sound landSound;
Sound hitSound;
Sound spiked;
Sound winner;



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
    GAME_OVER,
    WIN_SCREEN
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

enum CurrentState
{
    DEAD = 0,
    RUNNING = 1,
    IDLE = 2,
    ROLLING = 3,
    JUMPING = 4,
    FALLING = 5,
    HIT = 6
};

enum EnemyState
{
    E_MOVING = 1,
    E_STILL = 2,
    E_ATTACKING = 3
};


enum AnimationType
{
    REPEATING,
    ONESHOT
};

struct Animation
{
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

struct Player
{
    Rectangle rect;
    Rectangle hitbox;
    Vector2 vel;
    Vector2 knockbackVel;  // Knockback velocity
    float knockbackTime;   // Duration of knockback
    Texture2D sprite;
    Direction dir;
    CurrentState state;
    std::vector<Animation> animations;
    bool isJumping;
    float jumpTime;
    int health;
    int score;
    bool invulnerable;
};


struct Score_Orb {
    Rectangle rect;
    float score = 1;
    Color color;
    bool collected;
};

struct Enemy
{
    Rectangle rect;
    Rectangle hitbox;
    Vector2 vel;
    Texture2D sprite;
    Direction dir;
    EnemyState e_state;
    std::vector<Animation> animations;
};

std::vector <Enemy> enemies; // Store enemies in a vector



struct Spike {
    Rectangle rect;   // Collision box
    bool active;      // Whether the spike is visible
    float timer;      // Timer for toggling state
    float startY;
    float endY;    // Controls spike movement up/down
    bool rising;  
    bool moving;
    Texture2D texture;    // Whether the spike is moving up
};

struct fallingPlat{
    Rectangle rect;
    Rectangle Pos;
    Vector2 vel;
    Texture2D text;
    bool isFalling;
    float timer;
};

std::vector<Spike> spikes;
std::vector<fallingPlat> falling_Plat;
std::vector<Rectangle> platforms;
static std::unordered_set<TmxObject*> spawnedPlatforms;

// Add these new structures and variables
struct DeathTransition {
    bool active;
    float alpha;
    float timer;
    const float duration = 1.0f; // 1 second transition
};


double timer = GetTime();
double finishTime = timer + 1.0;

void update_animation(Animation *self)
{
    float dt = GetFrameTime();
    self->rem -= dt;
    if (self->rem < 0)
    {
        self->rem = self->spd;
        self->cur++;
        if (self->cur > self->lst)
        {
            switch (self->type)
            {
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

Rectangle animation_frame(const Animation *self)
{
    int x = (self->cur % (self->lst + 1)) * self->width;
    int y = self->offset * self->height;
    return (Rectangle){(float)x, (float)y, (float)self->width, (float)self->height};
}

void drawPlayer(const Player *player)
{
    if (player->state < 0 || player->state >= player->animations.size())
    {
        TraceLog(LOG_ERROR, "Invalid animation state: %d", player->state);
        return;
    }
    Rectangle source = animation_frame(&(player->animations[player->state]));
    source.width = source.width * static_cast<float>(player->dir);
    
    //DrawRectangleRec(player->rect, GREEN);//Debug for player collision and spritebox
    //DrawRectangleRec(player->hitbox, RED);//Debug for Hitbox
    DrawTexturePro(player->sprite, source, player->rect, {0, 0}, 0.0f, WHITE);
}

void movePlayer(Player *player)
{
    player->vel.x = 0.0f;
    bool changedState = false;

    // Handle knockback smoothly
    if (player->knockbackTime > 0) {
        player->rect.x += player->knockbackVel.x * GetFrameTime();
        player->knockbackTime -= GetFrameTime();

        if (player->knockbackTime <= 0) {
            player->knockbackVel.x = 0;  // Stop knockback after time runs out
        }
        return;  // Prevent other movement while knockback is active
    }

    // Regular movement
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
        player->jumpTime = 0.0f;
        player->vel.y = JUMP_FORCE;
        player->state = CurrentState::JUMPING;
        player->isJumping = true;
        changedState = true;
        PlaySound(jumpSound);
    }

    // Holding SPACE boosts jump height
    if (IsKeyDown(KEY_SPACE) && player->isJumping) {
        player->jumpTime += GetFrameTime();
        if (player->jumpTime < MAX_JUMP_HOLD) {
            player->vel.y = JUMP_BOOST;
            changedState = true;
        }
    }

    // Stop boosting when SPACE is released
    if (IsKeyReleased(KEY_SPACE) && player->isJumping) {
        player->jumpTime = MAX_JUMP_HOLD;
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

    player->hitbox.x = player->rect.x + 16;
    player->hitbox.y = player->rect.y;
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
            // Play collect sound
            PlaySound(collectSound);
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

void moveRectByVel(Rectangle *rect, const Vector2 *vel)
{
    rect->x += vel->x * GetFrameTime();
    rect->y += vel->y * GetFrameTime();
}
void movePlatByVel(Rectangle *rect, const Vector2 *vel, bool falling) {
    if (falling == true){
        rect->y += vel->y * GetFrameTime();
    }
}
void keepPlayerInScreen(Player *player) {
    if (player->rect.y > (H - player->rect.height)) {
        player->vel.y = 0.0f;
        player->rect.y = (H - player->rect.height);
        player->isJumping = false; // Allow jumping again
    }
}

void checkTileCollisions(TmxMap *map, Player *player) {
    bool wasJumping = player->isJumping;
    
    for (unsigned int i = 0; i < map->layersLength; i++) {
        if (strcmp(map->layers[i].name, "collisions") == 0 && map->layers[i].type == LAYER_TYPE_OBJECT_GROUP) {
            TmxObjectGroup &objectGroup = map->layers[i].exact.objectGroup;
            for (unsigned int j = 0; j < objectGroup.objectsLength; j++) {
                TmxObject &col = objectGroup.objects[j];
                Rectangle platform = { col.aabb.x, col.aabb.y, col.aabb.width, col.aabb.height };
                platforms.push_back(platform);
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
                        
                        // Play landing sound if player was jumping before
                        if (wasJumping) {
                            PlaySound(landSound);
                        }
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


void drawScore(int score) {
    // Draw score under health
    std::string scoreText = "Score: " + std::to_string(score);
    DrawText(scoreText.c_str(), 10, H - 60, 20, WHITE);
}

// Draw enemy
void drawEnemy()
{
    for(size_t i = 0; i < enemies.size(); i++){
        if (enemies[i].e_state < 0 || enemies[i].e_state >= static_cast<int>(enemies[i].animations.size()))
        {
            TraceLog(LOG_ERROR, "Invalid animation state: %d", enemies[i].e_state);
            return;
        }

        Rectangle source = animation_frame(&(enemies[i].animations[enemies[i].e_state]));
        source.width = source.width * static_cast<float>(enemies[i].dir);
        
        DrawTexturePro(enemies[i].sprite, source, enemies[i].rect, {0, 0}, 0.0f, WHITE);
        //DrawRectangleRec(enemies[i].hitbox, RED);
    }
}



// Spawn Enemy either on the left or right side of the screen to which they will move to the opposite side
void spawnEnemy(Camera2D camera, Texture2D enemyTexture)
{   
    Enemy enemy = {
        .rect = {0, 0, 64.0f, 64.0f},
        .hitbox = {0, 0, 48.0f, 48.0f},
        .vel = {0.0f, 0.0f},
        .sprite = enemyTexture,
        .dir = (GetRandomValue(0, 1) == 0) ? LEFT : RIGHT,
        .e_state = EnemyState::E_MOVING,
        .animations = {
            {0, 4, 0, 0, 48, 48, 0.1f, 0.1f, ONESHOT},
            {0, 4, 0, 0, 48, 48, 0.1f, 0.1f, REPEATING},
        }
    };

    // Get camera boundaries (view area)
    float camX = camera.target.x - (W / 2.0f) / camera.zoom;
    float camY = camera.target.y - (H / 2.0f) / camera.zoom;
    float camW = W / camera.zoom;
    float camH = H / camera.zoom;

    // Randomly spawn left or right of the camera view
    if (enemy.dir == RIGHT) {
        enemy.rect.x = camX - 100;  // Spawn slightly off-screen left
    } else {
        enemy.rect.x = camX + camW + 100;  // Spawn slightly off-screen right
    }

    // Spawn at a random height within the camera view
    enemy.rect.y = GetRandomValue(camY, camY + camH - enemy.rect.height);

    // Assign a random speed
    enemy.vel.x = GetRandomValue(100, 300) * ((enemy.dir == RIGHT) ? 1 : -1);

    enemies.push_back(enemy);
}

// Spawn Bullet at enemy position firing towards the enemy direction
// void spawnBullet(const Enemy *enemy)
// {
//     //Projectile *bullet = new Projectile();
//     bullet->bullet.x = enemy->rect.x + enemy->rect.width / 2;
//     bullet->bullet.y = enemy->rect.y;
//     bullet->isActivate = true;
//     bullet->dir = enemy->dir;
//     bullet->vel.x = 600.0f * bullet->dir;
//     bullet->vel.y = 0.0f;
// }

// Move enemy
void moveEnemy(TmxMap *map) {
    float mapWidth = 0;
    float mapHeight = 0;

    // Get map dimensions
    for (unsigned int i = 0; i < map->layersLength; i++) {
        if (map->layers[i].type == LAYER_TYPE_TILE_LAYER) {
            mapWidth = map->layers[i].exact.tileLayer.width * map->tileWidth;
            mapHeight = map->layers[i].exact.tileLayer.height * map->tileHeight;
            break;
        }
    }

    // Iterate through enemies and remove those that leave the map
    for (size_t i = 0; i < enemies.size();) {
        if (enemies[i].e_state == EnemyState::E_MOVING) {
            // Move enemy
            moveRectByVel(&(enemies[i].rect), &(enemies[i].vel));

            // Bounce off walls
            

            // **Check if the enemy is completely outside the map area**
            float despawnMargin = 200.0f;  // Extra margin before despawning
            if (enemies[i].rect.x < -despawnMargin || enemies[i].rect.x > mapWidth + despawnMargin ||
                enemies[i].rect.y < -despawnMargin || enemies[i].rect.y > mapHeight + despawnMargin) {
                
                TraceLog(LOG_INFO, "Despawning enemy at (%.2f, %.2f)", enemies[i].rect.x, enemies[i].rect.y);
                enemies.erase(enemies.begin() + i);  // Remove enemy
                continue;  // Skip incrementing 'i' since an enemy was removed
            }
        }

        // Update hitbox
        enemies[i].hitbox.x = enemies[i].rect.x + 12;
        enemies[i].hitbox.y = enemies[i].rect.y + 12;

        i++;  // Increment only if no enemy was removed
    }
}

// Draw bullet
// void drawBullet(const Projectile *bullet)
// {
//     if (bullet->isActivate)
//     {
//         Rectangle source = animation_frame(&(bullet->animations[bullet->state]));
//         source.width = source.width * static_cast<float>(bullet->dir);
//         DrawTexturePro(bullet->sprite, source, bullet->bullet, {0, 0}, 0.0f, WHITE);
//     }
// }

void enableInvulnerability(Player *player)
{
    player->invulnerable = true;

    timer = GetTime();
    if (timer >= finishTime)
    {
        player->invulnerable = false;
    }
}

// Checks Collisions between player and enemy and bullets
void hitCheck(Player *player, DeathTransition *transition)
{
    for(size_t i = 0; i < enemies.size(); i++) {
        if (CheckCollisionRecs(player->hitbox, enemies[i].hitbox)) {
            if (!player->invulnerable) {
                player->health -= 5;
                player->state = CurrentState::HIT;
                
                // Apply smooth knockback instead of teleporting
                float knockbackForce = 300.0f;  // Adjust force as needed
                if (player->dir == RIGHT) {
                    player->knockbackVel.x = -knockbackForce;  // Push left
                } else {
                    player->knockbackVel.x = knockbackForce;   // Push right
                }

                player->knockbackTime = 0.3f;  // Knockback lasts 0.3 seconds
                timer = GetTime();
                finishTime = timer + 1.0;
                // Play hit sound
                PlaySound(hitSound);
            }
            
            enableInvulnerability(player);
        }
    }

    // If player's health reaches 0, start death transition
    if (player->health <= 0) {
        player->state = DEAD;
        transition->active = true;
        transition->alpha = 0.0f;
        transition->timer = 0.0f;
        TraceLog(LOG_INFO, "Player died from enemy hit!");
    }
}

void drawHealth(int health)
{
    // Draw health in the bottom-left corner
    std::string healthText = "HP: " + std::to_string(health);
    DrawText(healthText.c_str(), 10, H - 30, 20, WHITE);
}

void drawScoreGoal(int scoreGoal){
    std::string healthText = "GOAL: " + std::to_string(scoreGoal);
    DrawText(healthText.c_str(), 10, H - 90, 20, WHITE);
}

void cameraFollow(Camera2D *camera, const Player *player)
{
    if (std::isnan(player->rect.x) || std::isnan(player->rect.y))
    {
        TraceLog(LOG_ERROR, "Player position is NaN! Resetting...");
        return;
    }

    
    // Follow player's X position
    camera->target.x = player->rect.x;
    camera->target.y = player->rect.y;  // Camera only moves up, never down
}

// Reset camera follow system
void ResetCameraFollow(Camera2D* camera, Player* player) {
    camera->target = {player->rect.x, player->rect.y};
}


// Check if player is outside horizontal map boundaries
void checkHorizontalBoundaries(Player* player, TmxMap* map, DeathTransition* transition) {
    // Only check if we're not already in a death transition
    if (!transition->active) {
        // Get map width from TMX
        float mapWidth = 0;
        float mapHeight = 0;
        for (unsigned int i = 0; i < map->layersLength; i++) {
            if (map->layers[i].type == LAYER_TYPE_TILE_LAYER) {
                mapWidth = map->layers[i].exact.tileLayer.width * map->tileWidth;
                mapHeight = map->layers[i].exact.tileLayer.height * map->tileHeight;
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
            // Play death sound
            PlaySound(deathSound);
            TraceLog(LOG_INFO, "Player went outside horizontal map boundaries!");
        }
        if (player->rect.y < -100 || player->rect.y > mapHeight + 100) {
            player->health = 0;
            player->state = DEAD;
            transition->active = true;
            transition->alpha = 0.0f;
            transition->timer = 0.0f;
            // Play death sound
            PlaySound(deathSound);
            TraceLog(LOG_INFO, "Player went outside vertical map boundaries!");
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

                
                    spike.timer = 0.5f;  // Random time for spike to rise/fall
                    spike.rising = true;  // Start by moving up
                    spike.startY = spike.rect.y; 
                    spike.texture = LoadTexture("assets/tiles-and-background-foreground/spike.png");
                    spike.moving = true;   
                    spikes.push_back(spike);
            }
        }
    }
}

void checkSpikeCol(Player* player, DeathTransition* transition){
    for (size_t i = 0; i < spikes.size(); i++){
        if(CheckCollisionRecs(player->hitbox, spikes[i].rect)){
            player->health = 0;
            player->state = DEAD;
            transition->active = true;
            transition->alpha = 0.0f;
            transition->timer = 0.0f;
            // Play death sound
            PlaySound(spiked);
            //PlaySound(deathSound);
            TraceLog(LOG_INFO, "Player died to spikes!");
        }
    }
}

void UpdateSpikes(Player *player) {
    for (size_t i = 0; i < spikes.size(); i++) {
        // Decrease the timer
        spikes[i].timer -= GetFrameTime();

        // Constants for timing
        const float MOVE_DURATION = 1.0f;  // 1 second to move fully
        const float PAUSE_DURATION = 1.0f; // Pause time before switching
        const float MOVE_DISTANCE = 20.0f; // Pixels to move up or down
        
       

        if (spikes[i].moving) {
            // If moving, interpolate position based on time progress
            float progress = (MOVE_DURATION - spikes[i].timer) / MOVE_DURATION;
            if (spikes[i].rising) {
                spikes[i].rect.y = spikes[i].startY - (progress * MOVE_DISTANCE);
                spikes[i].endY = spikes[i].rect.y;
            } else {
                spikes[i].rect.y = spikes[i].endY + (progress * MOVE_DISTANCE);
            }

            // Check if movement is complete
            if (spikes[i].timer <= 0) {
                spikes[i].timer = PAUSE_DURATION + (GetRandomValue(0, 200) / 200.0f); // Add a small random pause
                spikes[i].moving = false; // Enter pause state
            }
        } else {
            // If paused, wait until the pause timer runs out
            if (spikes[i].timer <= 0) {
                spikes[i].timer = MOVE_DURATION; // Reset timer for movement phase
                spikes[i].moving = true;  // Resume movement
                spikes[i].rising = !spikes[i].rising; // Switch direction
            }
        }
    }
}



void LoadFallingPlat(TmxMap* map){
        for (unsigned int i = 0; i < map->layersLength; i++) {
            if (strcmp(map->layers[i].name, "fallingPlat") == 0 && map->layers[i].type == LAYER_TYPE_OBJECT_GROUP) {
                TmxObjectGroup& objectGroup = map->layers[i].exact.objectGroup;
                // Loop through all objects in the object group (spikes)
                for (unsigned int j = 0; j < objectGroup.objectsLength; j++) {
                    TmxObject& obj = objectGroup.objects[j];
                    
                    
                    // Create a new plat object
                    fallingPlat platform;
                    platform.rect = { obj.aabb.x, obj.aabb.y, obj.aabb.width, obj.aabb.height };
                    platform.Pos = platform.rect;
                    platform.isFalling = false;
                    const float PAUSE_DURATION = 0.5f;
                    platform.timer = PAUSE_DURATION;
                    
                    falling_Plat.push_back(platform);
                }
        }
    }
}


void resetFallingPlat() {
    for (size_t i = 0; i < falling_Plat.size(); i++){
        falling_Plat[i].isFalling = false;
        falling_Plat[i].rect = falling_Plat[i].Pos;
        falling_Plat[i].timer = 0.5f;
    }
}


void updateFallingPlat(Player *player){
    for (size_t i = 0; i < falling_Plat.size(); i++){
        
        if (CheckCollisionRecs(player->rect, falling_Plat[i].rect)) {
            TraceLog(LOG_DEBUG, "Collision detected!");

            // Compute previous position
            float previousX = player->rect.x - player->vel.x * GetFrameTime();
            float previousY = player->rect.y - player->vel.y * GetFrameTime();

            // Determine collision direction
            bool comingFromTop = previousY + player->rect.height <= falling_Plat[i].rect.y;
            bool comingFromBottom = previousY >= falling_Plat[i].rect.y + falling_Plat[i].rect.height;
            bool comingFromLeft = previousX + player->rect.width <= falling_Plat[i].rect.x;
            bool comingFromRight = previousX >= falling_Plat[i].rect.x + falling_Plat[i].rect.width;

            if (comingFromTop) {
                // Standing on platform
                player->vel.y = 0.0f;
                player->rect.y = falling_Plat[i].rect.y - player->rect.height;

            
                player->isJumping = false; // Allow jumping again

                falling_Plat[i].timer -= GetFrameTime();
                if (falling_Plat[i].timer <= 0){
                    falling_Plat[i].isFalling = true;
                }
                // Play landing sound if player was jumping before
                if (player->isJumping) {
                    PlaySound(landSound);
                }
            } else if (comingFromBottom) {
                // Hitting the bottom of the platform
                player->vel.y = 0.0f;
                player->rect.y = falling_Plat[i].rect.y + falling_Plat[i].rect.height;
            } else if (comingFromLeft) {
                // Hitting the left side
                player->vel.x = 0.0f;
                player->rect.x = falling_Plat[i].rect.x - player->rect.width;
            } else if (comingFromRight) {
                // Hitting the right side
                player->vel.x = 0.0f;
                player->rect.x = falling_Plat[i].rect.x + falling_Plat[i].rect.width;
            }
        
        }

    }
}

void DrawSpikes() {
    for (size_t i = 0; i < spikes.size(); i++) {
        // Draw the spike texture at the correct position
        //DrawRectangleRec(spikes[i].rect, RED);
        DrawTexturePro(
            spikes[i].texture,                 // Texture
            { 0, 0, (float)spikes[i].texture.width, (float)spikes[i].texture.height }, // Source Rectangle (Full texture)
            spikes[i].rect,                     // Destination Rectangle
            { 0, 0 },                           // Origin (top-left corner)
            0,                                  // Rotation
            WHITE                               // Color tint
        );
    }
}

void drawFallingPlat(Texture2D text){
    for (size_t i = 0; i < falling_Plat.size(); i++) {
        // Draw the fallingplat texture at the correct position
        //DrawRectangleRec(falling_Plat[i].rect, RED);
        DrawTexturePro(
            text,
            {0,0, (float)text.width, (float)text.height},
            falling_Plat[i].rect,
            {0,0},
            0,
            WHITE
        );
    }
}

void drawSolidPlat(Texture2D floor){
    for (size_t i = 0; i < platforms.size(); i++) {
        // Draw the fallingplat texture at the correct position
        //DrawRectangleRec(platforms[i], RED);
        DrawTexturePro(
            floor,
            {0,0, (float)floor.width*(platforms[i].width/64), (float)floor.height},
            platforms[i],
            {0,0},
            0,
            WHITE
        );
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

void DrawWinScreen(int score) {
    const int titleFontSize = 60;
    const int textFontSize = 30;
    
    // Draw "You Win" text
    const char* winText = "YOU WIN!";
    int winWidth = MeasureText(winText, titleFontSize);
    DrawText(winText, W/2 - winWidth/2, H/3, titleFontSize, GOLD);

    // Display final score
    char scoreText[50];
    sprintf(scoreText, "FINAL SCORE: %d", score);
    int scoreWidth = MeasureText(scoreText, textFontSize);
    DrawText(scoreText, W/2 - scoreWidth/2, H/2, textFontSize, WHITE);

    // Show restart instructions
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
    player->hitbox = {0, 1700, 32.0f, 64.0f};
    player->vel = {0.0f, 0.0f};
    player->knockbackVel = {0.0f, 0.0f};
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

// Load all game sounds
void LoadGameSounds() {
    // Load sound effects
    jumpSound = LoadSound("assets/sfx/player-jump.wav");
    TraceLog(LOG_INFO, "Loaded jump sound");
    SetSoundVolume(jumpSound, 1.0f); // Full volume
    
    collectSound = LoadSound("assets/sfx/got-coin.wav");
    TraceLog(LOG_INFO, "Loaded collect sound");
    SetSoundVolume(collectSound, 1.0f); // Full volume
    
    deathSound = LoadSound("assets/sfx/player-lost.wav");
    TraceLog(LOG_INFO, "Loaded death sound");
    SetSoundVolume(deathSound, 1.0f); // Full volume
    
    menuSelectSound = LoadSound("assets/sfx/menu-select.wav");
    TraceLog(LOG_INFO, "Loaded menu select sound");
    SetSoundVolume(menuSelectSound, 1.0f); // Full volume
    
    // Use a different sound for game start
    gameStartSound = LoadSound("assets/sfx/menu-select.wav");
    TraceLog(LOG_INFO, "Loaded game start sound");
    SetSoundVolume(gameStartSound, 1.0f); // Full volume
    
    landSound = LoadSound("assets/sfx/land.wav");
    TraceLog(LOG_INFO, "Loaded land sound");
    SetSoundVolume(landSound, 1.0f); // Full volume

    hitSound = LoadSound("assets/sfx/hurt.wav");
    TraceLog(LOG_INFO, "Loaded Hit sound");
    SetSoundVolume(hitSound, 1.0f); // Full volume

    spiked = LoadSound("assets/sfx/spiked.wav");
    TraceLog(LOG_INFO, "Loaded spiked sound");
    SetSoundVolume(spiked, 2.0f); // Full volume

    winner = LoadSound("assets/sfx/winner.wav");
    TraceLog(LOG_INFO, "Loaded win sound");
    SetSoundVolume(spiked, 2.0f); // Full volume

    
    // Load music
    menuMusic = LoadMusicStream("assets/sfx/level-music.wav");
    TraceLog(LOG_INFO, "Loaded menu music");
    SetMusicVolume(menuMusic, 0.7f); // Set volume to 70%
}

// Unload all game sounds
void UnloadGameSounds() {
    // Unload sound effects
    UnloadSound(jumpSound);
    UnloadSound(collectSound);
    UnloadSound(deathSound);
    UnloadSound(menuSelectSound);
    UnloadSound(gameStartSound);
    UnloadSound(landSound);
    UnloadSound(hitSound);
    UnloadSound(spiked);
    // Unload music
    UnloadMusicStream(menuMusic);
}
float enemySpawnTimer = 0.0f;
float enemySpawnInterval = 2.0f; // Start with a 2-second interval
int main() {
    InitWindow(W, H, "Bullet Jumper");
    SetTargetFPS(60);
    
    // Initialize audio device
    InitAudioDevice();
    
    // Check if audio device is initialized
    if (IsAudioDeviceReady()) {
        TraceLog(LOG_INFO, "Audio device initialized successfully");
    } else {
        TraceLog(LOG_ERROR, "Failed to initialize audio device");
    }
    
    // Load all game sounds
    LoadGameSounds();
    
    // Start playing menu music
    PlayMusicStream(menuMusic);
    
    // Seed the random generator for orb spawning.
    srand((unsigned)time(NULL));

    // Initialize game state
    GameState gameState = MENU;
    Difficulty difficulty = NORMAL;
    int menuSelection = 0;
    const char* mapFile = "normal.tmx"; // Default map (normal difficulty)
    
    TmxMap* map = nullptr;
    
    Texture2D hero = LoadTexture("assets/herochar-sprites/herochar_spritesheet.png");
    Texture2D floorText = LoadTexture("assets/tiles-and-background-foreground/floor.png");
    Texture2D fallinText = LoadTexture("assets/tiles-and-background-foreground/falling.png");
    Texture2D enemyText = LoadTexture("assets/herochar-sprites/fly-eye.png");

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
            {0, 3, 0, 8, 16, 16, 0.1f, 0.1f, REPEATING}
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
    static bool fallingPlatLoaded = false;
    static bool orbsSpawned = false;
    static bool enemiesSpawned = false;
    int scoreGoal = 10;  // Default score goal

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
                // Update menu music
                UpdateMusicStream(menuMusic);
                
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
                    // Play menu selection sound
                    PlaySound(menuSelectSound);
                    // Update map file based on difficulty
                    switch(difficulty) {
                        case EASY: mapFile = "easy.tmx"; break;
                        case NORMAL: mapFile = "normal.tmx"; break; // You can create a medium difficulty map
                        case HARD: mapFile = "hard.tmx"; break;
                    }
                }
                if (menuSelection == 1 && IsKeyPressed(KEY_LEFT)) {
                    difficulty = static_cast<Difficulty>((static_cast<int>(difficulty) + 2) % 3);
                    // Play menu selection sound
                    PlaySound(menuSelectSound);
                    // Update map file based on difficulty
                    switch(difficulty) {
                        case EASY: mapFile = "easy.tmx";  break;
                        case NORMAL: mapFile = "normal.tmx";  break; // You can create a medium difficulty map
                        case HARD: mapFile = "hard.tmx";  break;
                    }
                }
                
                // Menu navigation
                if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_UP)) {
                    PlaySound(menuSelectSound);
                }
                
                // Start game
                if (IsKeyPressed(KEY_ENTER) && menuSelection == 0) {
                    // Play game start sound
                    PlaySound(gameStartSound);
    
                    // Stop menu music
                    StopMusicStream(menuMusic);
                    
                    // Ensure the previous map is fully unloaded before loading a new one
                    if (map != nullptr) {
                        UnloadTMX(map);
                        map = nullptr;
                    }
                
                    // Clear all game objects before loading a new map
                    enemies.clear();
                    spikes.clear();
                    falling_Plat.clear();
                    platforms.clear();
                    orbs.clear();
                    spawnedPlatforms.clear();

                    switch (difficulty) {
                        case EASY: scoreGoal = 8; break;
                        case NORMAL: scoreGoal = 12; break;
                        case HARD: scoreGoal = 25; break;
                    }
                    
                    // Load the selected map
                    map = LoadTMX(mapFile);
                    if (map == nullptr) {
                        TraceLog(LOG_ERROR, "Couldn't load the map: %s", mapFile);
                        return EXIT_FAILURE;
                    }
                
                    // Reset player and game state
                    ResetPlayer(&player, mapFile);
                    ResetCameraFollow(&camera, &player);
                    ResetCamera(&camera, &player);
                
                    spikesLoaded = false;
                    fallingPlatLoaded = false;
                    orbsSpawned = false;
                    enemiesSpawned = false;
                
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

                if (player.score >= scoreGoal) {
                    PlaySound(winner);
                    gameState = WIN_SCREEN;
                }
                
                // Only update gameplay if not in death transition
                if (!deathTransition.active) {
                    AnimateTMX(map);
                    movePlayer(&player);
                    applyGravity(&(player.vel));
                    enemySpawnTimer -= GetFrameTime();
                    if (enemySpawnTimer <= 0 && enemies.size() < 20) {  // Increase limit if needed
                        int numEnemies = GetRandomValue(1, 5);  // Spawn 1-3 enemies
                        for (int i = 0; i < numEnemies; i++) {
                            spawnEnemy(camera, enemyText);  // Use camera for positioning
                        }
                
                        enemySpawnInterval = GetRandomValue(1, 2);
                        enemySpawnTimer = enemySpawnInterval;
                    }
                    for (size_t i = 0; i < falling_Plat.size(); i++){
                        applyGravity(&falling_Plat[i].vel);
                        movePlatByVel(&falling_Plat[i].rect, &falling_Plat[i].vel, falling_Plat[i].isFalling);
                    }
                    
                    moveRectByVel(&(player.rect), &(player.vel));
                    checkTileCollisions(map, &player);
                    checkSpikeCol(&player, &deathTransition);
                    update_animation(&(player.animations[player.state]));
                    for (size_t i = 0; i < enemies.size(); i++){
                        update_animation(&(enemies[i].animations[enemies[i].e_state]));
                    }
                
                    hitCheck(&player, &deathTransition);
                    cameraFollow(&camera, &player);
                    
                    UpdateSpikes(&player);
                    updateFallingPlat(&player);
                    checkOrbCollection(&player, orbs);

                    
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
                        // Play death sound
                        PlaySound(deathSound);
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
                    // Play game start sound
                    
                    PlaySound(gameStartSound);
                    
                    if (map != nullptr) {
                        UnloadTMX(map);
                        map = nullptr;
                    }
                
                    // Clear all objects before restarting
                    enemies.clear();
                    spikes.clear();
                    falling_Plat.clear();
                    platforms.clear();
                    orbs.clear();
                    spawnedPlatforms.clear();
                
                    // Load the same map again
                    map = LoadTMX(mapFile);
                    if (map == nullptr) {
                        TraceLog(LOG_ERROR, "Couldn't load the map: %s", mapFile);
                        return EXIT_FAILURE;
                    }
                
                    // Reset player and camera
                    ResetPlayer(&player, mapFile);
                    ResetCameraFollow(&camera, &player);
                    ResetCamera(&camera, &player);
                    
                    spikesLoaded = false;
                    fallingPlatLoaded = false;
                    orbsSpawned = false;
                    enemiesSpawned = false;
                
                    gameState = GAMEPLAY;
                }
                else if (IsKeyPressed(KEY_M)) {
                    // Play menu select sound
                    PlaySound(menuSelectSound);
                    
                    // Start playing menu music again
                    PlayMusicStream(menuMusic);

                    //UnloadTMX(map);
                    // Return to menu
                    gameState = MENU;
                }
                break;

            case WIN_SCREEN:
                if (IsKeyPressed(KEY_ENTER)) {
                    PlaySound(gameStartSound);

                    if (map != nullptr) {
                        UnloadTMX(map);
                        map = nullptr;
                    }

                    // Clear game objects before restarting
                    enemies.clear();
                    spikes.clear();
                    falling_Plat.clear();
                    platforms.clear();
                    orbs.clear();
                    spawnedPlatforms.clear();

                    // Reload the level
                    map = LoadTMX(mapFile);
                    if (map == nullptr) {
                        TraceLog(LOG_ERROR, "Couldn't load the map: %s", mapFile);
                        return EXIT_FAILURE;
                    }

                    // Reset player and camera
                    ResetPlayer(&player, mapFile);
                    ResetCameraFollow(&camera, &player);
                    ResetCamera(&camera, &player);

                    spikesLoaded = false;
                    fallingPlatLoaded = false;
                    orbsSpawned = false;
                    enemiesSpawned = false;

                    gameState = GAMEPLAY;
                }
                else if (IsKeyPressed(KEY_M)) {
                    PlaySound(menuSelectSound);
                    PlayMusicStream(menuMusic);
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
                

                DrawSpikes(); 
                drawFallingPlat(fallinText);
                drawSolidPlat(floorText);
                drawPlayer(&player);

                if (!orbsSpawned){
                    spawnOrb(map, camera, orbs);
                    //orbsSpawned = true;
                }
                
                drawOrbs(orbs);
                if (!spikesLoaded) {
                    LoadSpikesFromTMX(map, &player);
                    spikesLoaded = true; // Ensure spikes are only loaded once
                }
                if (!fallingPlatLoaded){
                    LoadFallingPlat(map);
                    fallingPlatLoaded = true;
                }
                drawEnemy();
                
                moveEnemy(map);
                EndMode2D();
                drawScore(player.score);
                drawHealth(player.health);
                drawScoreGoal(scoreGoal);
                
                // Draw death transition effect on top of everything
                drawDeathTransition(&deathTransition);
                break;
                
            case GAME_OVER:
                DrawGameOver(player.score);
                break;

            case WIN_SCREEN:
                DrawWinScreen(player.score);
                break;
        }
        
        DrawFPS(5, 5);
        EndDrawing();
    }

    if (map != nullptr) {
        UnloadTMX(map);
    }
    UnloadTexture(hero);
    

    // Unload all game sounds
    UnloadGameSounds();

    // Close audio device
    CloseAudioDevice();

    CloseWindow();
    return 0;
}