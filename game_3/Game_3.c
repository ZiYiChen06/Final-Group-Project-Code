#include "Game_3.h"
#include "InputHandler.h"
#include "Menu.h"
#include "LCD.h"
#include "PWM.h"
#include "Buzzer.h"
#include "Joystick.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>

extern ST7789V2_cfg_t cfg0;
extern Buzzer_cfg_t buzzer_cfg;
extern PWM_cfg_t pwm_cfg;
extern Joystick_cfg_t joystick_cfg;
extern Joystick_t joystick_data;

#define SCREEN_W 240
#define SCREEN_H 240

#define PLAYER_W 24
#define PLAYER_H 26

#define MAX_OBS 3
#define FRAME_TIME 35

#define ATTACK_W 6
#define ATTACK_H 18

static int player_x;
static int player_y;
static int player_hp;
static int hit_wait;

static int obs_x[MAX_OBS];
static int obs_y[MAX_OBS];
static int obs_size[MAX_OBS];
static int obs_speed[MAX_OBS];
static int obs_colour[MAX_OBS];
static int obs_hp[MAX_OBS];

static int attack_on;
static int attack_x;
static int attack_y;
static int attack_timer;
static int auto_attack_time;

static int boom_on;
static int boom_x;
static int boom_y;
static int boom_timer;

static int boss_on;
static int boss_x;
static int boss_y;
static int boss_hp;
static int boss_dir;

static int score;
static int best_score;
static int combo;
static int game_over;
static int frame_count;
static int difficulty;

static void beep_start(void)
{
    buzzer_tone(&buzzer_cfg, 1000, 25);
    HAL_Delay(40);
    buzzer_off(&buzzer_cfg);
}

static void beep_hit(void)
{
    buzzer_tone(&buzzer_cfg, 900, 20);
    HAL_Delay(20);
    buzzer_off(&buzzer_cfg);
}

static void beep_break(void)
{
    buzzer_tone(&buzzer_cfg, 1800, 25);
    HAL_Delay(35);
    buzzer_off(&buzzer_cfg);
}

static void beep_damage(void)
{
    buzzer_tone(&buzzer_cfg, 350, 35);
    HAL_Delay(90);
    buzzer_off(&buzzer_cfg);
}

static int make_random_x(int i)
{
    int x;

    x = (score * 37 + frame_count * 13 + i * 71) % (SCREEN_W - 40);

    if (x < 5) {
        x = 5;
    }

    return x;
}

static void start_boom(int x, int y)
{
    boom_on = 1;
    boom_x = x;
    boom_y = y;
    boom_timer = 6;
}

static void update_boom(void)
{
    if (boom_on == 1) {
        boom_timer--;

        if (boom_timer <= 0) {
            boom_on = 0;
        }
    }
}

static void reset_obstacle(int i)
{
    obs_x[i] = make_random_x(i);
    obs_y[i] = -30 - i * 70;

    obs_size[i] = 12 + difficulty;

    if (obs_size[i] > 34) {
        obs_size[i] = 34;
    }

    obs_speed[i] = 3 + difficulty;

    if (obs_speed[i] > 9) {
        obs_speed[i] = 9;
    }

    if (i == 0) {
        obs_colour[i] = 2;
        obs_hp[i] = 1;
    } else if (i == 1) {
        obs_colour[i] = 5;
        obs_hp[i] = 2;
    } else {
        obs_colour[i] = 6;
        obs_hp[i] = 3;
    }
}

static void reset_boss(void)
{
    boss_on = 0;
    boss_x = 70;
    boss_y = 60;
    boss_hp = 10;
    boss_dir = 1;
}

static void reset_game(void)
{
    int i;

    player_x = SCREEN_W / 2 - PLAYER_W / 2;
    player_y = SCREEN_H - 35;

    player_hp = 3;
    hit_wait = 0;

    attack_on = 0;
    attack_x = 0;
    attack_y = 0;
    attack_timer = 0;
    auto_attack_time = 0;

    boom_on = 0;
    boom_x = 0;
    boom_y = 0;
    boom_timer = 0;

    score = 0;
    combo = 0;
    game_over = 0;
    frame_count = 0;
    difficulty = 0;

    reset_boss();

    for (i = 0; i < MAX_OBS; i++) {
        reset_obstacle(i);
    }

    PWM_SetDuty(&pwm_cfg, 20);
}

static void update_player(void)
{
    Joystick_Read(&joystick_cfg, &joystick_data);

    if (joystick_data.direction == W ||
        joystick_data.direction == NW ||
        joystick_data.direction == SW) {
        player_x -= 6;
    }

    if (joystick_data.direction == E ||
        joystick_data.direction == NE ||
        joystick_data.direction == SE) {
        player_x += 6;
    }

    if (player_x < 0) {
        player_x = 0;
    }

    if (player_x > SCREEN_W - PLAYER_W) {
        player_x = SCREEN_W - PLAYER_W;
    }
}

static int is_player_hit(int i)
{
    if (obs_x[i] < player_x + PLAYER_W &&
        obs_x[i] + obs_size[i] > player_x &&
        obs_y[i] < player_y + PLAYER_H &&
        obs_y[i] + obs_size[i] > player_y) {
        return 1;
    }

    return 0;
}

static int is_attack_hit(int i)
{
    if (attack_on == 0) {
        return 0;
    }

    if (obs_x[i] < attack_x + ATTACK_W &&
        obs_x[i] + obs_size[i] > attack_x &&
        obs_y[i] < attack_y + ATTACK_H &&
        obs_y[i] + obs_size[i] > attack_y) {
        return 1;
    }

    return 0;
}

static int is_boss_attack_hit(void)
{
    if (attack_on == 0 || boss_on == 0) {
        return 0;
    }

    if (boss_x < attack_x + ATTACK_W &&
        boss_x + 80 > attack_x &&
        boss_y < attack_y + ATTACK_H &&
        boss_y + 25 > attack_y) {
        return 1;
    }

    return 0;
}

static int is_boss_player_hit(void)
{
    if (boss_on == 0) {
        return 0;
    }

    if (boss_x < player_x + PLAYER_W &&
        boss_x + 80 > player_x &&
        boss_y < player_y + PLAYER_H &&
        boss_y + 25 > player_y) {
        return 1;
    }

    return 0;
}

static void start_attack(void)
{
    if (attack_on == 0) {
        attack_on = 1;
        attack_x = player_x + PLAYER_W / 2 - ATTACK_W / 2;
        attack_y = player_y - ATTACK_H;
        attack_timer = 7;
    }
}

static void update_attack(void)
{
    if (attack_on == 1) {
        attack_y -= 10;
        attack_timer--;

        if (attack_timer <= 0 || attack_y < 50) {
            attack_on = 0;
        }
    }
}

static void update_boss(void)
{
    if (score >= 20 && boss_on == 0) {
        boss_on = 1;
        boss_hp = 10;
        boss_x = 70;
        boss_y = 60;
        boss_dir = 1;
    }

    if (boss_on == 1) {
        boss_x += boss_dir * 3;

        if (boss_x < 5) {
            boss_x = 5;
            boss_dir = 1;
        }

        if (boss_x > SCREEN_W - 85) {
            boss_x = SCREEN_W - 85;
            boss_dir = -1;
        }

        if (is_boss_attack_hit()) {
            boss_hp--;
            attack_on = 0;
            beep_hit();

            if (boss_hp <= 0) {
                boss_on = 0;
                score += 15;
                combo += 3;
                start_boom(boss_x + 40, boss_y + 12);
                beep_break();
            }
        }

        if (is_boss_player_hit() && hit_wait == 0) {
            player_hp--;
            combo = 0;
            hit_wait = 25;
            start_boom(player_x + 12, player_y + 12);
            beep_damage();

            if (player_hp <= 0) {
                game_over = 1;

                if (score > best_score) {
                    best_score = score;
                }

                PWM_SetDuty(&pwm_cfg, 100);
            }
        }
    }
}

static void update_game(void)
{
    int i;
    uint8_t led_power;

    frame_count++;

    if (hit_wait > 0) {
        hit_wait--;
    }

    if (frame_count % 120 == 0) {
        difficulty++;

        if (difficulty > 10) {
            difficulty = 10;
        }
    }

    update_player();

    auto_attack_time++;

    if (auto_attack_time >= 15) {
        start_attack();
        auto_attack_time = 0;
    }

    update_attack();
    update_boom();
    update_boss();

    for (i = 0; i < MAX_OBS; i++) {
        obs_y[i] += obs_speed[i];

        if (obs_colour[i] == 5) {
            if (frame_count % 12 < 6) {
                obs_x[i] += 1;
            } else {
                obs_x[i] -= 1;
            }
        }

        if (obs_colour[i] == 6) {
            obs_y[i] += 1;
        }

        if (obs_x[i] < 0) {
            obs_x[i] = 0;
        }

        if (obs_x[i] > SCREEN_W - obs_size[i]) {
            obs_x[i] = SCREEN_W - obs_size[i];
        }

        if (is_attack_hit(i)) {
            obs_hp[i]--;
            attack_on = 0;

            if (obs_hp[i] <= 0) {
                combo++;
                score = score + 2 + combo;
                start_boom(obs_x[i] + obs_size[i] / 2,
                           obs_y[i] + obs_size[i] / 2);
                beep_break();
                reset_obstacle(i);
            } else {
                beep_hit();
            }
        }

        if (obs_y[i] > SCREEN_H) {
            score++;
            combo = 0;
            reset_obstacle(i);
        }

        if (is_player_hit(i) && hit_wait == 0) {
            player_hp--;
            combo = 0;
            hit_wait = 25;
            start_boom(player_x + 12, player_y + 12);
            reset_obstacle(i);
            beep_damage();

            if (player_hp <= 0) {
                game_over = 1;

                if (score > best_score) {
                    best_score = score;
                }

                PWM_SetDuty(&pwm_cfg, 100);
            }
        }
    }

    led_power = 20 + difficulty * 8;

    if (led_power > 100) {
        led_power = 100;
    }

    PWM_SetDuty(&pwm_cfg, led_power);
}

static void draw_player_man(int x, int y)
{
    int step;

    step = (frame_count / 6) % 2;

    LCD_Draw_Rect(x + 8, y, 8, 8, 14, 1);
    LCD_Draw_Line(x + 12, y + 8, x + 12, y + 17, 3);

    if (step == 0) {
        LCD_Draw_Line(x + 12, y + 11, x + 5, y + 15, 3);
        LCD_Draw_Line(x + 12, y + 11, x + 19, y + 8, 3);
        LCD_Draw_Line(x + 12, y + 17, x + 5, y + 25, 4);
        LCD_Draw_Line(x + 12, y + 17, x + 20, y + 24, 4);
    } else {
        LCD_Draw_Line(x + 12, y + 11, x + 5, y + 8, 3);
        LCD_Draw_Line(x + 12, y + 11, x + 19, y + 15, 3);
        LCD_Draw_Line(x + 12, y + 17, x + 5, y + 24, 4);
        LCD_Draw_Line(x + 12, y + 17, x + 20, y + 25, 4);
    }
}

static void draw_obstacle(int i)
{
    int x;
    int y;
    int s;

    x = obs_x[i];
    y = obs_y[i];
    s = obs_size[i];

    if (obs_colour[i] == 2) {
        LCD_Draw_Rect(x, y, s, s, 2, 1);
    } else if (obs_colour[i] == 5) {
        LCD_Draw_Line(x + s / 2, y, x + s, y + s / 2, 5);
        LCD_Draw_Line(x + s, y + s / 2, x + s / 2, y + s, 5);
        LCD_Draw_Line(x + s / 2, y + s, x, y + s / 2, 5);
        LCD_Draw_Line(x, y + s / 2, x + s / 2, y, 5);
    } else {
        LCD_Draw_Rect(x + s / 3, y, s / 3, s, 6, 1);
        LCD_Draw_Rect(x, y + s / 3, s, s / 3, 6, 1);
    }
}

static void draw_hp_bar(void)
{
    LCD_printString("HP", 5, 38, 1, 1);

    LCD_Draw_Rect(25, 38, 12, 6, 1, 0);
    LCD_Draw_Rect(40, 38, 12, 6, 1, 0);
    LCD_Draw_Rect(55, 38, 12, 6, 1, 0);

    if (player_hp >= 1) {
        LCD_Draw_Rect(25, 38, 12, 6, 2, 1);
    }

    if (player_hp >= 2) {
        LCD_Draw_Rect(40, 38, 12, 6, 2, 1);
    }

    if (player_hp >= 3) {
        LCD_Draw_Rect(55, 38, 12, 6, 2, 1);
    }
}

static void draw_boom(void)
{
    if (boom_on == 1) {
        LCD_Draw_Line(boom_x - 8, boom_y, boom_x + 8, boom_y, 15);
        LCD_Draw_Line(boom_x, boom_y - 8, boom_x, boom_y + 8, 15);
        LCD_Draw_Line(boom_x - 6, boom_y - 6, boom_x + 6, boom_y + 6, 15);
        LCD_Draw_Line(boom_x - 6, boom_y + 6, boom_x + 6, boom_y - 6, 15);
    }
}

static void draw_boss(void)
{
    char text[20];

    if (boss_on == 1) {
        LCD_Draw_Rect(boss_x, boss_y, 80, 25, 4, 1);
        LCD_Draw_Rect(boss_x + 10, boss_y + 6, 8, 8, 1, 1);
        LCD_Draw_Rect(boss_x + 60, boss_y + 6, 8, 8, 1, 1);

        sprintf(text, "BOSS:%d", boss_hp);
        LCD_printString(text, boss_x + 18, boss_y + 8, 1, 1);
    }
}

static void draw_game(void)
{
    char text[32];
    int i;
    int attack_colour;

    LCD_Fill_Buffer(0);

    LCD_printString("DODGE FIGHT", 42, 5, 1, 2);

    sprintf(text, "Score:%d", score);
    LCD_printString(text, 5, 25, 1, 1);

    sprintf(text, "Lv:%d", difficulty + 1);
    LCD_printString(text, 165, 25, 1, 1);

    draw_hp_bar();

    sprintf(text, "Combo:%d", combo);
    LCD_printString(text, 100, 38, 1, 1);

    LCD_Draw_Line(0, 52, SCREEN_W - 1, 52, 1);

    if (player_hp == 1 && frame_count % 8 < 4) {
        LCD_printString("WARNING!", 75, 60, 1, 2);
    }

    if (combo >= 3 && frame_count % 10 < 5) {
        LCD_printString("COMBO!", 85, 82, 1, 2);
    }

    if (difficulty >= 6) {
        LCD_printString("HARD", 190, 38, 1, 1);
    }

    draw_boss();

    if (hit_wait > 0 && frame_count % 4 < 2) {
        /* flash after hit */
    } else {
        draw_player_man(player_x, player_y);
    }

    if (attack_on == 1) {
        if (frame_count % 4 < 2) {
            attack_colour = 15;
        } else {
            attack_colour = 10;
        }

        LCD_Draw_Rect(attack_x, attack_y, ATTACK_W, ATTACK_H, attack_colour, 1);
    }

    for (i = 0; i < MAX_OBS; i++) {
        if (obs_y[i] > 0) {
            draw_obstacle(i);

            sprintf(text, "%d", obs_hp[i]);
            LCD_printString(text, obs_x[i] + 3, obs_y[i] + 3, 1, 1);
        }
    }

    draw_boom();

    LCD_printString("Joystick: left/right", 25, 218, 1, 1);
    LCD_printString("Auto attack  BT3 menu", 20, 230, 1, 1);

    LCD_Refresh(&cfg0);
}

static void draw_start_screen(void)
{
    LCD_Fill_Buffer(0);

    LCD_printString("DODGE FIGHT", 35, 30, 1, 3);
    LCD_printString("Move left and right", 35, 80, 1, 1);
    LCD_printString("Attack is automatic", 35, 100, 1, 1);
    LCD_printString("Avoid 3 hits", 65, 120, 1, 1);

    LCD_Draw_Rect(35, 150, 14, 14, 2, 1);
    LCD_printString("HP1", 60, 152, 1, 1);

    LCD_Draw_Line(40, 185, 47, 175, 5);
    LCD_Draw_Line(47, 175, 54, 185, 5);
    LCD_Draw_Line(54, 185, 47, 195, 5);
    LCD_Draw_Line(47, 195, 40, 185, 5);
    LCD_printString("HP2", 60, 180, 1, 1);

    LCD_Draw_Rect(38, 210, 8, 20, 6, 1);
    LCD_Draw_Rect(32, 216, 20, 8, 6, 1);
    LCD_printString("HP3", 60, 212, 1, 1);

    LCD_printString("BT2: start", 70, 232, 1, 1);

    LCD_Refresh(&cfg0);
}

static void draw_game_over(void)
{
    char text[32];

    LCD_Fill_Buffer(0);

    LCD_printString("GAME OVER", 45, 60, 1, 3);

    sprintf(text, "Score: %d", score);
    LCD_printString(text, 65, 110, 1, 2);

    sprintf(text, "Best: %d", best_score);
    LCD_printString(text, 70, 135, 1, 2);

    LCD_printString("BT2: restart", 55, 180, 1, 1);
    LCD_printString("BT3: menu", 65, 200, 1, 1);

    LCD_Refresh(&cfg0);
}

MenuState Game3_Run(void)
{
    int started;
    uint32_t start_time;

    started = 0;
    reset_game();

    while (started == 0) {
        Input_Read();

        if (current_input.btn3_pressed) {
            return MENU_STATE_HOME;
        }

        if (current_input.btn2_pressed) {
            started = 1;
            beep_start();
        }

        draw_start_screen();
        HAL_Delay(60);
    }

    while (1) {
        start_time = HAL_GetTick();

        Input_Read();

        if (current_input.btn3_pressed) {
            buzzer_off(&buzzer_cfg);
            PWM_SetDuty(&pwm_cfg, 50);
            return MENU_STATE_HOME;
        }

        if (game_over == 0) {
            update_game();
            draw_game();
        } else {
            draw_game_over();

            if (current_input.btn2_pressed) {
                reset_game();
                beep_start();
            }
        }

        if (HAL_GetTick() - start_time < FRAME_TIME) {
            HAL_Delay(FRAME_TIME - (HAL_GetTick() - start_time));
        }
    }
}