#include "Game_2.h"
#include "InputHandler.h"
#include "Menu.h"
#include "LCD.h"
#include "PWM.h"
#include "Buzzer.h"
#include "Joystick.h"
#include "Utils.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>
#include <string.h>

extern ST7789V2_cfg_t cfg0;
extern PWM_cfg_t pwm_cfg;// LED PWM control
extern Buzzer_cfg_t buzzer_cfg;// Buzzer control
extern Joystick_cfg_t joystick_cfg;// Joystick
extern Joystick_t joystick_data;// Joystick readings

// FORWARD DECLARATIONS
static void BeepFor(uint32_t freq_hz, uint32_t duration_ms);
static void UpdateBuzzer(void);

// GAME STATE

static Player_t g_player;
static Obstacle_t g_obstacles[MAX_OBSTACLES];
static NPC_t g_npcs[MAX_NPCS];
static Lightning_t g_lightnings[MAX_LIGHTNINGS];
static GameState_t g_state;

// INITIALISATION
 
static void InitGame(void) {
    // Player
    g_player.x = PLAYER_START_X;
    g_player.y = PLAYER_START_Y;
    g_player.vx = 0.0f;
    g_player.w = PLAYER_W;
    g_player.h = PLAYER_H;
    g_player.state = RUN;
    g_player.stateTimer = 0;
 
    // Clear object pools
    memset(g_obstacles, 0, sizeof(g_obstacles));
    memset(g_npcs, 0, sizeof(g_npcs));
    memset(g_lightnings, 0, sizeof(g_lightnings));
 
    // Game state (preserve best_score and playerColor)
    uint32_t previous_best = g_state.best_score;
    uint8_t previous_color = g_state.playerColor;
    memset(&g_state, 0, sizeof(g_state));
    g_state.best_score = previous_best;
    g_state.playerColor = previous_color;
 
    g_state.lives = STARTING_LIVES;
    g_state.score = 0;
    g_state.speedMult = 1.0f;
    g_state.weather = SUNNY;
    g_state.penalty = NONE;
    g_state.weatherTimer = WEATHER_INTERVAL;
    g_state.spawnTimer = SPAWN_INTERVAL;
    g_state.npcTimer = NPC_INTERVAL;
    g_state.lightningTimer = 0;
    g_state.buzzerStopTick = 0;
    g_state.roadOffset = 0.0f;
    g_state.roadCurve  = 0.0f;
    g_state.curveTimer = 0;
    g_state.gameOver = 0;
    g_state.stamina = STAMINA_MAX;
    g_state.isSprinting = 0;
    g_state.lastMilestone = 0;
 
    // Turn off weather LEDs at the start
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_RESET);
}
 
// 
 
static void MovePlayer(void) {
    g_state.isSprinting = 0;
    GPIO_PinState btn3_level = HAL_GPIO_ReadPin(BTN3_GPIO_Port, BTN3_Pin);
    if (btn3_level == GPIO_PIN_RESET && g_state.stamina > 0) {
        g_state.isSprinting = 1;
        g_state.stamina -= STAMINA_DRAIN;
        if (g_state.stamina < 0) g_state.stamina = 0;
    } else {
        // Regen when not sprinting
        g_state.stamina += STAMINA_REGEN;
        if (g_state.stamina > STAMINA_MAX) g_state.stamina = STAMINA_MAX;
    }
    
    // Sprint multiplier for movement
    float sprint_mult = g_state.isSprinting ? 1.4f : 1.0f;

    // Read joystick X axis
    float joy_x = joystick_data.coord_mapped.x;
    
    // Apply slip penalty if active
    if (g_state.penalty == SLIP) {
        joy_x *= SLIP_SENSITIVITY;
    }
    
    // Apply joystick input as acceleration
    g_player.vx += joy_x * PLAYER_ACCEL * sprint_mult;
    
    // Apply speed surge penalty if active
    if (g_state.penalty == SPEED_SURGE) {
        g_player.vx *= 1.05f;
    }
    
    // Apply friction so player slows down naturally
    g_player.vx *= PLAYER_FRICTION;

    // Clamp to road edges with a small margin, so player isn't pressed against the wall
    int16_t road_w = (g_state.weather == RAIN) ? ROAD_WIDTH_RAIN : ROAD_WIDTH_SUNNY;
    int16_t road_l = ROAD_CENTER_X + (int16_t)g_state.roadOffset - road_w / 2;
    int16_t road_r = road_l + road_w;
    
    int16_t margin = 4; // distance between player and road edge
    int16_t left_limit  = road_l + margin;
    int16_t right_limit = road_r - margin - g_player.w;
    
    if (g_player.x < left_limit) {
        g_player.x = left_limit;
        if (g_player.vx < 0) g_player.vx = 0; //only stop leftward motion
    }
    if (g_player.x > right_limit) {
        g_player.x = right_limit;
        if (g_player.vx > 0) g_player.vx = 0; //only stop rightward motion
    }
    
    // Update position
    g_player.x += g_player.vx;
    
    // Clamp to screen edges
    if (g_player.x < 0) g_player.x = 0;
    if (g_player.x > SCREEN_W - g_player.w) g_player.x = SCREEN_W - g_player.w;
    
    // Jump and Duck handling
    float joy_y = joystick_data.coord_mapped.y;
    
    if (g_player.state == RUN) {
        if (joy_y > 0.5f) {
            // Jump (joystick up)
            g_player.state = JUMP;
            g_player.stateTimer = JUMP_DURATION_MS;
        } else if (joy_y < -0.5f) {
            // Duck (joystick down)
            g_player.state = DUCK;
            g_player.stateTimer = DUCK_DURATION_MS;
        }
    } else {
        // Currently in JUMP or DUCK, count down
        if (g_player.stateTimer > FRAME_TIME_MS) {
            g_player.stateTimer -= FRAME_TIME_MS;
        } else {
            g_player.stateTimer = 0;
            g_player.state = RUN; // back to running
        }
    }
}

static void ScrollRoad(void) {
    // Update road curve direction periodically
    if (g_state.curveTimer > FRAME_TIME_MS) {
        g_state.curveTimer -= FRAME_TIME_MS;
    } else {
        // Pick a new curve direction every few seconds
        g_state.curveTimer = 2000 + (Random_U16(2000)); //2-4 seconds
        // Random curve: -1.0 to +1.0
        int16_t r = (int16_t)Random_U16(200) - 100; //-100 to +100
        g_state.roadCurve = r / 100.0f;
    }
    
    // Apply curve to road horizontal offset
    g_state.roadOffset += g_state.roadCurve * 0.5f * g_state.speedMult;
    
    // Clamp road offset so it doesn't drift off screen
    if (g_state.roadOffset > 40.0f) g_state.roadOffset = 40.0f;
    if (g_state.roadOffset < -40.0f) g_state.roadOffset = -40.0f;
}

static void SpawnObstacle(void) {
    // Move existing obstacles down
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (g_obstacles[i].active) {
            g_obstacles[i].y += OBSTACLE_SPEED * g_state.speedMult;
            // Deactivate if off screen
            if (g_obstacles[i].y > SCREEN_H) {
                g_obstacles[i].active = 0;
            }
        }
    }
    
    // Spawn new obstacle when timer expires
    if (g_state.spawnTimer > FRAME_TIME_MS) {
        g_state.spawnTimer -= FRAME_TIME_MS;
        return;
    }
    g_state.spawnTimer = SPAWN_INTERVAL;
    
    // Find an inactive slot
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (!g_obstacles[i].active) {
            ObstacleType type = (ObstacleType)(Random_U16(3));
            g_obstacles[i].type = type;
            g_obstacles[i].active = 1;
            g_obstacles[i].y = -20; //start above screen
            
            // Random x within road, depending on weather
            int16_t road_width = (g_state.weather == RAIN) ? ROAD_WIDTH_RAIN : ROAD_WIDTH_SUNNY;
            int16_t road_left = ROAD_CENTER_X + (int16_t)g_state.roadOffset - road_width/2;
            
            if (type == ROCK) {
                // Small obstacle, random horizontal position
                g_obstacles[i].w = 16;
                g_obstacles[i].h = 16;
                g_obstacles[i].x = road_left + 4 + Random_U16(road_width - 24);
            } else if (type == TRUNK) {
                // Wide low obstacle, spans full road
                g_obstacles[i].w = road_width - 8;
                g_obstacles[i].h = 8;
                g_obstacles[i].x = road_left + 4;
            } else {  // BARRIER
                // Wide tall obstacle, spans full road
                g_obstacles[i].w = road_width - 8;
                g_obstacles[i].h = 14;
                g_obstacles[i].x = road_left + 4;
            }
            break;
        }
    }
}

static void SpawnNPC(void) {
    // Move existing NPCs
    for (int i = 0; i < MAX_NPCS; i++) {
        if (!g_npcs[i].active) continue;
        
        // Move horizontally toward target
        if (g_npcs[i].x < g_npcs[i].targetX) {
            g_npcs[i].x += g_npcs[i].speed;
        } else {
            g_npcs[i].x -= g_npcs[i].speed;
        }
        
        // Also drift down with the road
        g_npcs[i].y += OBSTACLE_SPEED * g_state.speedMult * 0.5f;
        
        // Deactivate if off screen
        if (g_npcs[i].y > SCREEN_H || 
            g_npcs[i].x < -10 || g_npcs[i].x > SCREEN_W + 10) {
            g_npcs[i].active = 0;
        }
    }
    
    // Spawn new NPC when timer expires
    if (g_state.npcTimer > FRAME_TIME_MS) {
        g_state.npcTimer -= FRAME_TIME_MS;
        return;
    }
    g_state.npcTimer = NPC_INTERVAL;
    
    // Find an inactive slot
    for (int i = 0; i < MAX_NPCS; i++) {
        if (!g_npcs[i].active) {
            int16_t road_width = (g_state.weather == RAIN) ? ROAD_WIDTH_RAIN : ROAD_WIDTH_SUNNY;
            int16_t road_left = ROAD_CENTER_X + (int16_t)g_state.roadOffset - road_width/2;
            int16_t road_right = road_left + road_width;
            
            // Random spawn
            if (Random_U16(2) == 0) {
                g_npcs[i].x = road_left - 10;
                g_npcs[i].targetX = road_right + 10;
            } else {
                g_npcs[i].x = road_right + 10;
                g_npcs[i].targetX = road_left - 10;
            }           
            g_npcs[i].y = 30 + Random_U16(80);
            g_npcs[i].speed = NPC_SPEED_MIN + (Random_U16(100) / 100.0f) * (NPC_SPEED_MAX - NPC_SPEED_MIN);
            g_npcs[i].active = 1;
            break;
        }
    }
}

static void CheckCollision(void) {
    // Build player AABB
    AABB player_box;
    player_box.x = (int16_t)g_player.x;
    player_box.y = (int16_t)g_player.y;
    player_box.width  = g_player.w;
    player_box.height = g_player.h;
    
    // Adjust hitbox if jumping or ducking
    if (g_player.state == JUMP) {
        player_box.y -= 12;
    } else if (g_player.state == DUCK) {
        player_box.y += g_player.h / 2;
        player_box.height = g_player.h / 2;
    }
    
    // Check each obstacle
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (!g_obstacles[i].active) continue;
        
        AABB obs_box;
        obs_box.x = (int16_t)g_obstacles[i].x;
        obs_box.y = (int16_t)g_obstacles[i].y;
        obs_box.width = g_obstacles[i].w;
        obs_box.height = g_obstacles[i].h;
        
        // Skip collision if player can avoid this obstacle by jumping/ducking
        if (g_obstacles[i].type == TRUNK && g_player.state == DUCK) continue;
        if (g_obstacles[i].type == BARRIER && g_player.state == JUMP) continue;
        
        if (AABB_Collides(&player_box, &obs_box)) {
            // Hit, lose a life
            g_state.lives--;
            BeepFor(BUZZER_COLLISION_FREQ, BUZZER_HIT_MS);
            PWM_SetDuty(&pwm_cfg, 100);
            
            // Deactivate the obstacle so it doesn't trigger again
            g_obstacles[i].active = 0;
            
            // Trigger random penalty
            if (Random_U16(2) == 0) {
                g_state.penalty = SLIP;
                g_state.penaltyTimer = PENALTY_SLIP_MS;
            } else {
                g_state.penalty = SPEED_SURGE;
                g_state.penaltyTimer = PENALTY_SPEED_MS;
            }
            
            // Game over check
            if (g_state.lives == 0) {
                g_state.gameOver = 1;
                if (g_state.score > g_state.best_score) {
                    g_state.best_score = g_state.score;
                }
            }
            
            break;
        }
    }

    // Check each NPC
    for (int i = 0; i < MAX_NPCS; i++) {
        if (!g_npcs[i].active) continue;
        
        AABB npc_box;
        npc_box.x = (int16_t)g_npcs[i].x;
        npc_box.y = (int16_t)g_npcs[i].y;
        npc_box.width = 12;
        npc_box.height = 16;
        
        if (AABB_Collides(&player_box, &npc_box)) {
            g_state.lives--;
            BeepFor(BUZZER_COLLISION_FREQ, BUZZER_HIT_MS);
            PWM_SetDuty(&pwm_cfg, 100);
            g_npcs[i].active = 0;
            
            // Random penalty
            if (Random_U16(2) == 0) {
                g_state.penalty = SLIP;
                g_state.penaltyTimer = PENALTY_SLIP_MS;
            } else {
                g_state.penalty = SPEED_SURGE;
                g_state.penaltyTimer = PENALTY_SPEED_MS;
            }
            
            if (g_state.lives == 0) {
                g_state.gameOver = 1;
                if (g_state.score > g_state.best_score) {
                    g_state.best_score = g_state.score;
                }
            }
            
            break;
        }
    }

    // Check each lightning
    for (int i = 0; i < MAX_LIGHTNINGS; i++) {
        if (!g_lightnings[i].active) continue;
        
        AABB lt_box;
        lt_box.x = (int16_t)g_lightnings[i].x;
        lt_box.y = (int16_t)g_lightnings[i].y;
        lt_box.width = g_lightnings[i].w;
        lt_box.height = g_lightnings[i].h;
        
        if (AABB_Collides(&player_box, &lt_box)) {
            g_state.lives--;
            BeepFor(BUZZER_COLLISION_FREQ, BUZZER_HIT_MS);
            PWM_SetDuty(&pwm_cfg, 100);
            g_lightnings[i].active = 0;
            
            // Random penalty
            if (Random_U16(2) == 0) {
                g_state.penalty = SLIP;
                g_state.penaltyTimer = PENALTY_SLIP_MS;
            } else {
                g_state.penalty = SPEED_SURGE;
                g_state.penaltyTimer = PENALTY_SPEED_MS;
            }
            
            if (g_state.lives == 0) {
                g_state.gameOver = 1;
                if (g_state.score > g_state.best_score) {
                    g_state.best_score = g_state.score;
                }
            }
            
            break;
        }
    }
}

static void CycleWeather(void) {
    // Tick weather timer
    if (g_state.weatherTimer > FRAME_TIME_MS) {
        g_state.weatherTimer -= FRAME_TIME_MS;
        return;
    }
    g_state.weatherTimer = WEATHER_INTERVAL;
    
    // Cycle weather
    if (g_state.weather == SUNNY) {
        g_state.weather = RAIN;
    } else if (g_state.weather == RAIN) {
        g_state.weather = STORM;
    } else {
        g_state.weather = SUNNY;
    }
    
    // Update LEDs based on new weather
    if (g_state.weather == RAIN) {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_SET);   // green on
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_RESET); // red off
    } else if (g_state.weather == STORM) {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_RESET); // green off
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_SET);   // red on
    } else {  // SUNNY
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_RESET); // both off
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_RESET);
    }
    
    // Brief weather change beep
    BeepFor(900, 60);
}

static void UpdateLightning(void) {
    // Tick lifetime of existing lightning zones
    for (int i = 0; i < MAX_LIGHTNINGS; i++) {
        if (!g_lightnings[i].active) continue;
        
        g_lightnings[i].y += OBSTACLE_SPEED * g_state.speedMult;
        
        if (g_lightnings[i].y > SCREEN_H) {
            g_lightnings[i].active = 0;
        }
    }
    
    if (g_state.weather != STORM) {
        g_state.lightningTimer = 0;
        return;
    }
    
    if (g_state.lightningTimer > FRAME_TIME_MS) {
        g_state.lightningTimer -= FRAME_TIME_MS;
        return;
    }
    g_state.lightningTimer = LIGHTNING_INTERVAL;
    
    for (int i = 0; i < MAX_LIGHTNINGS; i++) {
        if (!g_lightnings[i].active) {
            int16_t road_width = ROAD_WIDTH_SUNNY;
            int16_t road_left  = ROAD_CENTER_X + (int16_t)g_state.roadOffset - road_width / 2;
            
            g_lightnings[i].w = 18;
            g_lightnings[i].h = 30;
            g_lightnings[i].x = road_left + 4 + Random_U16(road_width - 22);
            g_lightnings[i].y = -20;
            g_lightnings[i].active = 1;
            break;
        }
    }
}

static void UpdatePWM(void) {
    // Brightness scales with speed (20%-80%)
    uint8_t duty = (uint8_t)(20 + (g_state.speedMult - 1.0f) * 40);
    
    // Sprint boost: if joystick button pressed, max brightness
    if (g_state.isSprinting) {
        duty = 100;
    }
    
    // Speed surge penalty
    if (g_state.penalty == SPEED_SURGE) {
        if ((HAL_GetTick() / 100) % 2 == 0) {
            duty = 100;
        } else {
            duty = 60;
        }
    }
    
    // Cap range
    if (duty > 100) duty = 100;
    if (duty < 10)  duty = 10;
    
    PWM_SetDuty(&pwm_cfg, duty);
}

static void ApplyPenalty(void) {
    if (g_state.penalty == NONE) return;
    
    // Tick penalty timer
    if (g_state.penaltyTimer > FRAME_TIME_MS) {
        g_state.penaltyTimer -= FRAME_TIME_MS;
    } else {
        // Penalty expired
        g_state.penaltyTimer = 0;
        g_state.penalty = NONE;
    }
}

static void AddScore(void) {
    // Distance score, +50% when sprinting
    float bonus = g_state.isSprinting ? 1.5f : 1.0f;
    g_state.score += (uint32_t)(g_state.speedMult * bonus);
    
    // Speed gradually increases over time
    if (g_state.speedMult < 2.5f) {
        g_state.speedMult += 0.0005f;
    }
    
    // Milestone beep every 1000 points
    uint32_t current_milestone = g_state.score / 1000;
    if (current_milestone > g_state.lastMilestone) {
        g_state.lastMilestone = current_milestone;
        // Ascending melody, briefly pauses the game 600ms
        buzzer_tone(&buzzer_cfg, 784,  BUZZER_VOLUME); HAL_Delay(100);
        buzzer_tone(&buzzer_cfg, 1047, BUZZER_VOLUME); HAL_Delay(100);
        buzzer_tone(&buzzer_cfg, 1319, BUZZER_VOLUME); HAL_Delay(100);
        buzzer_tone(&buzzer_cfg, 1568, BUZZER_VOLUME); HAL_Delay(100);
        buzzer_tone(&buzzer_cfg, 2093, BUZZER_VOLUME); HAL_Delay(200);
        buzzer_off(&buzzer_cfg);
    }
}
 
// NON-BLOCKING BUZZER
 
static void BeepFor(uint32_t freq_hz, uint32_t duration_ms) {
    buzzer_tone(&buzzer_cfg, freq_hz, BUZZER_VOLUME);
    g_state.buzzerStopTick = HAL_GetTick() + duration_ms;
}
 
static void UpdateBuzzer(void) {
    if (g_state.buzzerStopTick != 0 &&
        (int32_t)(HAL_GetTick() - g_state.buzzerStopTick) >= 0) {
        buzzer_off(&buzzer_cfg);
        g_state.buzzerStopTick = 0;
    }
}

// RENDER

static void Render(void) {
    LCD_Fill_Buffer(0);

    // Calculate road boundaries
    int16_t road_width = (g_state.weather == RAIN) ? ROAD_WIDTH_RAIN : ROAD_WIDTH_SUNNY;
    int16_t road_center = ROAD_CENTER_X + (int16_t)g_state.roadOffset;
    int16_t road_left = road_center - road_width/2;
    int16_t road_right = road_center + road_width/2;

    // Curved road edges
    for (int i = 0; i < 8; i++) {
        int16_t y_top = i * (SCREEN_H/8);
        int16_t y_bottom = (i + 1) * (SCREEN_H/8);
        
        float curve_factor_top = (8 - i) * g_state.roadCurve * 1.5f;
        float curve_factor_bottom = (8 - i - 1) * g_state.roadCurve * 1.5f;
        
        int16_t left_top = road_left + (int16_t)curve_factor_top;
        int16_t left_bottom = road_left + (int16_t)curve_factor_bottom;
        int16_t right_top = road_right + (int16_t)curve_factor_top;
        int16_t right_bottom = road_right + (int16_t)curve_factor_bottom;
        
        LCD_Draw_Line(left_top, y_top, left_bottom, y_bottom, 6);
        LCD_Draw_Line(right_top, y_top, right_bottom, y_bottom, 6);
    }

    // Draw swaying jungle on both sides of the road
    uint32_t t = HAL_GetTick();
    for (int i = 0; i < 8; i++) {
        int16_t y_top = i * (SCREEN_H / 8);
        int16_t y_bottom = (i + 1) * (SCREEN_H / 8);
        int16_t y_mid = (y_top + y_bottom) / 2;

        // Match the road edge for this slice
        float curve_factor_mid = (8 - i - 0.5f) * g_state.roadCurve * 1.5f;
        int16_t left_edge = road_left + (int16_t)curve_factor_mid;
        int16_t right_edge = road_right + (int16_t)curve_factor_mid;

        // Sway offset based on time, slightly different per slice
        int16_t sway = (int16_t)((t / 100 + i * 7) % 5) - 2;

        // Left side jungle: between screen edge and road
        for (int x = 4 + sway; x < left_edge - 2; x += 12) {
            LCD_Draw_Line(x, y_mid + 3, x + 3, y_mid - 3, 3);
            LCD_Draw_Line(x + 3, y_mid - 3, x + 6, y_mid + 3, 3);
            LCD_Draw_Line(x + 1, y_mid + 3, x + 5, y_mid + 3, 3);
        }

        // Right side jungle
        for (int x = right_edge + 2; x < SCREEN_W - 4 - sway; x += 12) {
            LCD_Draw_Line(x, y_mid + 3, x + 3, y_mid - 3, 3);
            LCD_Draw_Line(x + 3, y_mid - 3, x + 6, y_mid + 3, 3);
            LCD_Draw_Line(x + 1, y_mid + 3, x + 5, y_mid + 3, 3);
        }
    }

    // Draw obstacles
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (!g_obstacles[i].active) continue;
        
        int16_t ox = (int16_t)g_obstacles[i].x;
        int16_t oy = (int16_t)g_obstacles[i].y;
        int16_t ow = g_obstacles[i].w;
        int16_t oh = g_obstacles[i].h;
        
        if (g_obstacles[i].type == ROCK) {
            // Rock: lumpy circle with cracks
            LCD_Draw_Circle(ox + ow/2, oy + oh/2, ow/2, 13, 1); // body
            LCD_Draw_Circle(ox + ow/2 - 2, oy + oh/2 - 2, ow/2 - 4, 1, 0); // highlight
            // Crack lines
            LCD_Draw_Line(ox + 3, oy + oh/2, ox + ow - 3, oy + oh/2 + 2, 0);
            LCD_Draw_Line(ox + ow/2, oy + 3, ox + ow/2 + 2, oy + oh - 3, 0);
        } else if (g_obstacles[i].type == TRUNK) {
            // Tree trunk: log with bark texture (horizontal lines + end rings)
            LCD_Draw_Rect(ox, oy, ow, oh, 12, 1);
            // End rings
            LCD_Draw_Circle(ox + 3, oy + oh/2, oh/2 - 1, 0, 0);
            LCD_Draw_Circle(ox + ow - 4, oy + oh/2, oh/2 - 1, 0, 0);
            // Bark texture lines
            for (int k = 6; k < ow - 6; k += 6) {
                LCD_Draw_Line(ox + k, oy + 1, ox + k + 2, oy + 2, 0);
                LCD_Draw_Line(ox + k, oy + oh - 2, ox + k + 2, oy + oh - 1, 0);
            }
        } else {  // BARRIER
            // Fence: outline + vertical pickets + cross bar
            LCD_Draw_Rect(ox, oy, ow, oh, 1, 0);
            // Horizontal cross bar in the middle
            LCD_Draw_Line(ox, oy + oh/2, ox + ow, oy + oh/2, 1);
            // Vertical pickets (wooden slats)
            for (int k = 8; k < ow - 4; k += 8) {
                LCD_Draw_Line(ox + k, oy, ox + k, oy + oh, 1);
            }
        }
    }

    // Draw NPCs
    for (int i = 0; i < MAX_NPCS; i++) {
        if (!g_npcs[i].active) continue;
        
        int16_t nx = (int16_t)g_npcs[i].x;
        int16_t ny = (int16_t)g_npcs[i].y;
        
        // Simple stick figure for NPC
        LCD_Draw_Circle(nx + 6, ny + 3, 3, 2, 1); // head
        LCD_Draw_Rect  (nx + 5, ny + 6, 2, 6, 2, 1);       // body
        LCD_Draw_Line  (nx + 6, ny + 12, nx + 3, ny + 16, 2); // left leg
        LCD_Draw_Line  (nx + 6, ny + 12, nx + 9, ny + 16, 2); // right leg
    }

    // Draw lightning zones (zigzag shape)
    for (int i = 0; i < MAX_LIGHTNINGS; i++) {
        if (!g_lightnings[i].active) continue;
        
        int16_t lx = (int16_t)g_lightnings[i].x;
        int16_t ly = (int16_t)g_lightnings[i].y;
        int16_t lw = g_lightnings[i].w;
        int16_t lh = g_lightnings[i].h;
        
        // Zigzag bolt
        int16_t mid_x = lx + lw / 2;
        LCD_Draw_Line(mid_x, ly, mid_x - 4, ly + lh/3, 6);
        LCD_Draw_Line(mid_x - 4, ly + lh/3, mid_x + 4, ly + lh/2, 6);
        LCD_Draw_Line(mid_x + 4, ly + lh/2, mid_x - 4, ly + lh*2/3, 6);
        LCD_Draw_Line(mid_x - 4, ly + lh*2/3, mid_x + 2, ly + lh, 6);
    }

    // Draw player as a stick figure
    int16_t px = (int16_t)g_player.x;
    int16_t py = (int16_t)g_player.y;

    // Determine player color from selection
    uint8_t player_col = 4;  // default blue
    switch (g_state.playerColor) {
        case 0: player_col = 4; break;
        case 1: player_col = 8; break;
        case 2: player_col = 3; break;
        case 3: player_col = 6; break;
    }

    // Jump offset
    int16_t jump_offset = (g_player.state == JUMP) ? -12 : 0;
    py += jump_offset;

    if (g_player.state == DUCK) {
        // Ducking, squat shape
        LCD_Draw_Circle(px + 12, py + 12, 6, player_col, 1);
        LCD_Draw_Rect(px + 8, py + 18, 8, 10, player_col, 1);
        LCD_Draw_Line(px + 4, py + 24, px + 8, py + 28, player_col);
        LCD_Draw_Line(px + 20, py + 24, px + 16, py + 28, player_col);
    } else {
        // Running or jumping, full figure
        LCD_Draw_Circle(px + 12, py + 6, 6, player_col, 1);
        LCD_Draw_Rect(px + 10, py + 12, 4, 12, player_col, 1);
        // Arms
        LCD_Draw_Line(px + 10, py + 14, px + 2, py + 20, player_col);
        LCD_Draw_Line(px + 14, py + 14, px + 22, py + 20, player_col);
        // Legs
        if (g_player.state == JUMP) {
            LCD_Draw_Line(px + 10, py + 24, px + 6, py + 28, player_col);
            LCD_Draw_Line(px + 14, py + 24, px + 18, py + 28, player_col);
        } else {
            LCD_Draw_Line(px + 10, py + 24, px + 2, py + 32, player_col);
            LCD_Draw_Line(px + 14, py + 24, px + 22, py + 32, player_col);
        }
    }

    // HUD
    char buf[32];
    sprintf(buf, "Lives:%d", g_state.lives);
    LCD_printString(buf, 5, 5, 1, 1);

    sprintf(buf, "Score:%lu", g_state.score);
    LCD_printString(buf, 80, 5, 10, 1);

    // Weather indicator
    const char *weather_str = "";
    if (g_state.weather == SUNNY) weather_str = "SUN";
    else if (g_state.weather == RAIN) weather_str = "RAIN";
    else weather_str = "STORM";

    uint8_t weather_color = 1;
    if (g_state.weather == SUNNY) weather_color = 6;
    else if (g_state.weather == RAIN) weather_color = 3;
    else weather_color = 2; 
    LCD_printString(weather_str, 180, 5, weather_color, 1);

    // Penalty indicator
    if (g_state.penalty == SLIP) {
        LCD_printString("SLIP!", 100, 220, 5, 2);
    } else if (g_state.penalty == SPEED_SURGE) {
        LCD_printString("SPEED!", 90, 220, 2, 2);
    }

    // Stamina bar (bottom of screen)
    int16_t bar_x = 10;
    int16_t bar_y = 235;
    int16_t bar_w = 100;
    int16_t bar_h = 4;
    int16_t fill_w = (int16_t)((bar_w * g_state.stamina) / STAMINA_MAX);
    
    uint8_t bar_color = (g_state.stamina < STAMINA_MAX / 4) ? 2 : 3; 
    LCD_Draw_Rect(bar_x, bar_y, bar_w, bar_h, 1, 0);
    if (fill_w > 0) {
    LCD_Draw_Rect(bar_x, bar_y, fill_w, bar_h, bar_color, 1);
    }
    LCD_printString("SPRINT", 120, 232, 1, 1);

    LCD_Refresh(&cfg0);
}


MenuState Game2_Run(void) {
    InitGame();

    // Brief startup beep
    buzzer_tone(&buzzer_cfg, 1000, 30);
    HAL_Delay(50);
    buzzer_off(&buzzer_cfg);

    // LED at 50%
    PWM_SetDuty(&pwm_cfg, 50);

    MenuState exit_state = MENU_STATE_HOME;

start_screen: 

    // Track joystick direction so colour only switches once per push
    int8_t color_prev_dir = 0;

    while (1) {
        Input_Read();
        Joystick_Read(&joystick_cfg, &joystick_data);

        if (current_input.btn2_pressed) {
            PWM_SetDuty(&pwm_cfg, 50);
            return MENU_STATE_HOME;
        }

        if (current_input.btn3_pressed) {
            BeepFor(1200, 100);
            break;
        }

        // Joystick X to switch color (only on flick, not hold)
        float jx = joystick_data.coord_mapped.x;
        int8_t cur_dir = 0;
        if (jx >  0.5f) cur_dir = 1;
        if (jx < -0.5f) cur_dir = -1;

        if (cur_dir != 0 && cur_dir != color_prev_dir) {
            if (cur_dir == 1) {
                g_state.playerColor = (g_state.playerColor + 1) % 4;
            } else {
                g_state.playerColor = (g_state.playerColor + 3) % 4;
            }
            BeepFor(1500, 30);
        }
        color_prev_dir = cur_dir;

        uint8_t color_idx;
        const char *color_name;
        switch (g_state.playerColor) {
            case 0: color_idx = 4; color_name = "BLUE"; break; 
            case 1: color_idx = 8; color_name = "PURPLE"; break;
            case 2: color_idx = 3; color_name = "GREEN"; break; 
            case 3: color_idx = 6; color_name = "YELLOW"; break; 
            default: color_idx = 4; color_name = "BLUE"; break;
        }

        // Draw start screen
        LCD_Fill_Buffer(0);
        LCD_printString("ROAD RUNNER", 25, 40, 6, 3);

        LCD_printString("Joystick: move", 30, 80, 1, 1);
        LCD_printString("Up: jump", 30, 95, 1, 1);
        LCD_printString("Down: duck", 30, 110, 1, 1);
        LCD_printString("Press: sprint", 30, 125, 1, 1);
        LCD_printString("BTN2: exit", 30, 140, 1, 1);

        char buf[32];
        sprintf(buf, "Best: %lu", g_state.best_score);
        LCD_printString(buf, 70, 160, 1, 2);

        // Color picker preview
        LCD_printString("Skin:", 30, 185, 1, 2);
        LCD_printString(color_name, 95, 185, color_idx, 2);
        LCD_printString("L/R to change", 50, 205, 1, 1);

        LCD_printString("Push joystick", 50, 220, 1, 1);
        LCD_printString("to start", 70, 232, 1, 1);

        LCD_Refresh(&cfg0);
        UpdateBuzzer();
        HAL_Delay(FRAME_TIME_MS);
    }

    // Game's own loop, runs until exit condition
    while (1) {
        uint32_t frame_start = HAL_GetTick();

        // Read input
        Input_Read();
        Joystick_Read(&joystick_cfg, &joystick_data);

        // Check if button was pressed to return to menu
        if (current_input.btn2_pressed) {
            PWM_SetDuty(&pwm_cfg, 50); // Reset LED to 50% when returning
            exit_state = MENU_STATE_HOME;
            break;
        }

        // Update game state
        MovePlayer();
        ScrollRoad();
        SpawnObstacle();
        SpawnNPC();
        CheckCollision();

        if (g_state.gameOver) {
            break;
        }

        CycleWeather();
        UpdateLightning();
        ApplyPenalty();
        AddScore();
        UpdatePWM();
        // Render
        Render();
        //Non-blocking buzzer tick
        UpdateBuzzer();
        //Frame timing
        uint32_t frame_time = HAL_GetTick() - frame_start;
        if (frame_time < FRAME_TIME_MS) {
            HAL_Delay(FRAME_TIME_MS - frame_time);
        }
    }

    // Game Over screen, press BTN2 to return to start screen
    if (g_state.gameOver) {
        // Death melody
        buzzer_tone(&buzzer_cfg, 659, BUZZER_VOLUME); HAL_Delay(150);  // E5
        buzzer_tone(&buzzer_cfg, 622, BUZZER_VOLUME); HAL_Delay(150);  // Eb5
        buzzer_tone(&buzzer_cfg, 587, BUZZER_VOLUME); HAL_Delay(150);  // D5
        buzzer_tone(&buzzer_cfg, 554, BUZZER_VOLUME); HAL_Delay(200);  // C#5
        buzzer_tone(&buzzer_cfg, 523, BUZZER_VOLUME); HAL_Delay(200);  // C5
        buzzer_tone(&buzzer_cfg, 440, BUZZER_VOLUME); HAL_Delay(300);  // A4
        buzzer_tone(&buzzer_cfg, 330, BUZZER_VOLUME); HAL_Delay(800);  // E4 (long fade)
        buzzer_off(&buzzer_cfg);


        // Show game over screen until BTN2 is pressed
        while (1) {
            Input_Read();

            if (current_input.btn2_pressed) {
                InitGame();
                goto start_screen;
            }

            LCD_Fill_Buffer(0);
            LCD_printString("GAME OVER", 60, 50, 2, 3);

            char buf[32];
            sprintf(buf, "Score: %lu", g_state.score);
            LCD_printString(buf, 60, 110, 1, 2);

            sprintf(buf, "Best: %lu", g_state.best_score);
            LCD_printString(buf, 60, 135, 1, 2);

            LCD_printString("BTN2 to restart", 40, 200, 1, 2);

            LCD_Refresh(&cfg0);
            HAL_Delay(FRAME_TIME_MS);
        }
    }

    // Cleanup on exit
    buzzer_off(&buzzer_cfg);
    PWM_SetDuty(&pwm_cfg, 0);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_RESET);

    return exit_state;
}