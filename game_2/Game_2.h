#ifndef GAME_1_H
#define GAME_1_H

#include "Menu.h"

/**
 * @brief Game 1 - Student can implement their own game here
 * 
 * Placeholder for Student 1's game implementation.
 * This structure allows multiple students to work on separate games
 * while sharing common utilities from the shared/ folder.
 * 
 * The menu system calls this function when Game 1 is selected.
 * The function runs its own loop and returns when the game exits.
 * 
 * @return MenuState - Where to go next (typically MENU_STATE_HOME for menu)
 */

#include "Joystick.h"
#include "Utils.h"
#include <stdint.h>


// ENUMERATIONS
typedef enum {
    RUN = 0,
    JUMP = 1,
    DUCK = 2
} PlayerState;

typedef enum {
    ROCK = 0, // dodge left/right
    TRUNK = 1, // duck under
    BARRIER = 2 // jump over
} ObstacleType;

typedef enum {
    SUNNY = 0, // normal, LEDs off
    RAIN = 1, // road narrows, green LED on
    STORM = 2 // lightning zones, red LED on
} WeatherState;

typedef enum {
    NONE = 0,
    SLIP = 1, // reduced joystick sensitivity (4s)
    SPEED_SURGE = 2 // uncontrollable acceleration (2s)
} PenaltyState;


// STRUCTS
typedef struct {
    float x;
    float y;
    float vx;
    uint8_t w;
    uint8_t h;
    PlayerState state;
    uint32_t stateTimer; // ms remaining in JUMP or DUCK
} Player_t;

typedef struct {
    float x;
    float y;
    uint8_t w;
    uint8_t h;
    ObstacleType type;
    uint8_t active;
} Obstacle_t;

typedef struct {
    float x;
    float y;
    float targetX;
    float speed;
    uint8_t active;
} NPC_t;

// Lightning zone (storm only)
typedef struct {
    float x;
    float y;
    uint8_t w;
    uint8_t h;
    uint8_t active;
} Lightning_t;

typedef struct {
    uint8_t lives;
    uint32_t score;
    uint32_t best_score;
    float speedMult;
    WeatherState weather;
    PenaltyState penalty;
    uint32_t penaltyTimer; // ms remaining on penalty
    uint32_t weatherTimer; // ms until next weather change
    uint32_t spawnTimer; // ms until next obstacle spawn
    uint32_t npcTimer; // ms until next NPC spawn
    uint32_t lightningTimer; // ms until next lightning spawn
    uint32_t buzzerStopTick; // HAL_GetTick() when buzzer stops
    int32_t  stamina;
    uint8_t  isSprinting;
    uint32_t lastMilestone;
    uint8_t playerColor; 
    float roadOffset; // horizontal road shift (curves)
    float roadCurve; // current curve direction/strength
    uint32_t curveTimer; // ms until next curve change
    uint8_t gameOver;
} GameState_t;


// CONSTANTS


#define SCREEN_W 240
#define SCREEN_H 240

#define MAX_OBSTACLES 5
#define MAX_NPCS 3
#define MAX_LIGHTNINGS 3

#define STARTING_LIVES 3
#define FRAME_TIME_MS 30

#define ROAD_CENTER_X 120
#define ROAD_WIDTH_SUNNY 120
#define ROAD_WIDTH_RAIN 90

#define PLAYER_W 12
#define PLAYER_H 16
#define PLAYER_START_X 114
#define PLAYER_START_Y 185
#define PLAYER_ACCEL 0.5f
#define PLAYER_FRICTION 0.75f
#define PLAYER_MAX_VX 4.0f

#define STAMINA_MAX 2000 
#define STAMINA_DRAIN 30 
#define STAMINA_REGEN 10

#define JUMP_DURATION_MS 500
#define DUCK_DURATION_MS 400

#define WEATHER_INTERVAL 6000
#define PENALTY_SLIP_MS 4000
#define PENALTY_SPEED_MS 2000

#define OBSTACLE_SPEED 2.5f
#define NPC_SPEED_MIN 1.0f
#define NPC_SPEED_MAX 2.5f
#define SPAWN_INTERVAL 1500
#define NPC_INTERVAL 3000
#define LIGHTNING_INTERVAL 1500 // storm only

#define SLIP_SENSITIVITY 0.3f
#define SPEED_SURGE_MULT 2.5f

#define BUZZER_COLLISION_FREQ 600
#define BUZZER_PENALTY_FREQ 200
#define BUZZER_SPRINT_FREQ 1200
#define BUZZER_VOLUME 50
#define BUZZER_HIT_MS 80

MenuState Game1_Run(void);

#endif
