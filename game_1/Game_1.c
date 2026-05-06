#include "Game_1.h"
#include "InputHandler.h"
#include "Menu.h"
#include "LCD.h"
#include "Joystick.h"
#include "adc.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>
#include "Buzzer.h"

extern ST7789V2_cfg_t cfg0;
extern InputState current_input;
extern ADC_HandleTypeDef hadc1;
extern Buzzer_cfg_t buzzer_cfg;
extern Joystick_cfg_t joystick_cfg;
extern Joystick_t joystick_data;
 
static uint8_t exit_to_menu = 0;
 
#define MAP_W 10
#define MAP_H 8
#define TILE_EMPTY 0
#define TILE_WALL  1
#define TILE_ITEM  2
#define MAX_GUARDS 2
#define MAX_LEVELS 3
 
#define START_OPT_START 0
#define START_OPT_HELP  1
#define START_OPT_EXIT  2
#define START_OPT_COUNT 3
 
// LCD colours
#define COL_BG       0   
#define COL_WALL     13  
#define COL_ITEM     6   
#define COL_PLAYER   3   
#define COL_GUARD    2   
#define COL_TITLE    5    
#define COL_HUD      1   
#define COL_WIN      3   
#define COL_LOSE     2   
#define COL_SELECT   6    
#define COL_OPTION   1   
#define COL_LEVEL    14  
 
static const int level_maps[MAX_LEVELS][MAP_H][MAP_W] = {
    // level 1 
    {
        {1,1,1,1,1,1,1,1,1,1},
        {1,0,0,2,0,0,0,0,0,1},
        {1,0,1,1,0,1,1,0,0,1},
        {1,0,0,0,0,0,1,0,0,1},
        {1,0,1,1,0,0,1,0,0,1},
        {1,2,0,0,0,2,0,0,0,1},
        {1,0,0,0,1,0,0,0,0,1},
        {1,1,1,1,1,1,1,1,1,1}
    },
    // level 2 
    {
        {1,1,1,1,1,1,1,1,1,1},
        {1,0,0,2,0,0,2,0,0,1},
        {1,0,1,1,0,1,1,0,1,1},
        {1,0,0,0,0,0,0,0,0,1},
        {1,1,1,0,1,0,1,1,0,1},
        {1,2,0,0,0,2,0,0,2,1},
        {1,0,0,1,1,0,0,1,0,1},
        {1,1,1,1,1,1,1,1,1,1}
    },
    // level 3 
    {
        {1,1,1,1,1,1,1,1,1,1},
        {1,0,2,0,1,0,0,2,0,1},
        {1,0,1,0,1,0,1,1,0,1},
        {1,0,1,0,0,0,1,0,0,1},
        {1,0,1,1,1,0,1,0,1,1},
        {1,2,0,0,0,0,0,2,0,1},
        {1,0,1,1,0,1,0,1,2,1},
        {1,1,1,1,1,1,1,1,1,1}
    }
};
 
typedef struct { int x, y, dir, move_axis; } GuardConfig;
 
static const GuardConfig level_guards[MAX_LEVELS][MAX_GUARDS] = {
    { {7,3,1,0}, {0,0,0,0} },
    { {7,3,1,0}, {2,5,1,1} },
    { {7,2,1,0}, {2,4,1,1} }
};
 
static const int level_guard_count[MAX_LEVELS] = {1, 2, 2};
static const uint32_t level_guard_speed[MAX_LEVELS] = {400, 280, 180};
 
static int game_map[MAP_H][MAP_W];
static int player_x, player_y;
static int items_left;
static uint8_t game_win, game_over;
static uint32_t last_move_time, last_guard_move_time;
static int current_level;
 
typedef struct { int x, y, dir, move_axis; uint8_t active; } Guard;
static Guard guards[MAX_GUARDS];
 
static int is_wall(int x, int y) {
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return 1;
    return (game_map[y][x] == TILE_WALL);
}
 
static void move_player(int dx, int dy) {
    int nx = player_x + dx, ny = player_y + dy;
    if (!is_wall(nx, ny)) { player_x = nx; player_y = ny; }
}
 
static void check_collect_item(void) {
    if (game_map[player_y][player_x] == TILE_ITEM) {
        game_map[player_y][player_x] = TILE_EMPTY;
        if (items_left > 0) items_left--;
        buzzer_tone(&buzzer_cfg, 1800, 70);
        HAL_Delay(200);
        buzzer_off(&buzzer_cfg);
    }
}
 
static void update_guards(void) {
    for (int i = 0; i < level_guard_count[current_level]; i++) {
        if (!guards[i].active) continue;
        int nx = guards[i].x, ny = guards[i].y;
        if (guards[i].move_axis == 0) nx += guards[i].dir;
        else                          ny += guards[i].dir;
        if (is_wall(nx, ny)) guards[i].dir = -guards[i].dir;
        else { guards[i].x = nx; guards[i].y = ny; }
    }
}
 
static void check_guard_collision(void) {
    for (int i = 0; i < level_guard_count[current_level]; i++)
        if (guards[i].active && player_x == guards[i].x && player_y == guards[i].y)
            game_over = 1;
}
 
static void load_level(int level) {
    current_level = level;
    player_x = 1; player_y = 1;
    game_win = 0; game_over = 0;
    last_move_time = 0; last_guard_move_time = 0;
 
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            game_map[y][x] = level_maps[level][y][x];
 
    items_left = 0;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            if (game_map[y][x] == TILE_ITEM) items_left++;
 
    for (int i = 0; i < MAX_GUARDS; i++) {
        if (i < level_guard_count[level]) {
            guards[i].x = level_guards[level][i].x;
            guards[i].y = level_guards[level][i].y;
            guards[i].dir = level_guards[level][i].dir;
            guards[i].move_axis = level_guards[level][i].move_axis;
            guards[i].active = 1;
        } else {
            guards[i].active = 0;
        }
    }
}
 
// Draw maze
static void draw_maze(void) {
    int tile_w = 22;    //pixel width per tile
    int tile_h = 18;        // pixel height per tile
    int offset_x = 5;
    int offset_y = 35;
 
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            int px = offset_x + x * tile_w;
            int py = offset_y + y * tile_h;
 
            uint8_t is_guard = 0;
            for (int i = 0; i < MAX_GUARDS; i++)
                if (guards[i].active && x == guards[i].x && y == guards[i].y)
                    is_guard = 1;
 
            if (x == player_x && y == player_y) {
             // draw different objects with different colours
                LCD_Draw_Rect(px+2, py+2, tile_w-4, tile_h-4, COL_PLAYER, 1);
                LCD_printString("P", px+6, py+4, COL_BG, 1);
            } else if (is_guard) {
               
                LCD_Draw_Rect(px+2, py+2, tile_w-4, tile_h-4, COL_GUARD, 1);
                LCD_printString("G", px+6, py+4, COL_BG, 1);
            } else if (game_map[y][x] == TILE_WALL) {
               
                LCD_Draw_Rect(px, py, tile_w, tile_h, COL_WALL, 1);
            } else if (game_map[y][x] == TILE_ITEM) {
               
                LCD_Draw_Circle(px + tile_w/2, py + tile_h/2, 4, COL_ITEM, 1);
            }
        }
    }
}
 
static void play_level_clear(void) {
    buzzer_tone(&buzzer_cfg, 1047, 70); HAL_Delay(120);
    buzzer_tone(&buzzer_cfg, 1319, 70); HAL_Delay(120);
    buzzer_tone(&buzzer_cfg, 1568, 70); HAL_Delay(250);
    buzzer_off(&buzzer_cfg);
}
 
static void play_win(void) {
    buzzer_tone(&buzzer_cfg, 1047, 70); HAL_Delay(120);
    buzzer_tone(&buzzer_cfg, 1319, 70); HAL_Delay(120);
    buzzer_tone(&buzzer_cfg, 1568, 70); HAL_Delay(120);
    buzzer_tone(&buzzer_cfg, 2093, 70); HAL_Delay(450);
    buzzer_off(&buzzer_cfg);
}
 
static void render_start_screen(int selected) {
    LCD_Fill_Buffer(COL_BG);
 
    // title in orange
    LCD_printString("GUARD ESCAPE", 25, 15, COL_TITLE, 2);
 
    // draw a line under title
    LCD_Draw_Line(10, 35, 220, 35, COL_GUARD);
 
    const char* options[] = {"Start Game", "Help", "Exit"};
    // option colours 
    uint8_t opt_cols[] = {3, 14, 2};  // green, cyan, red
 
    for (int i = 0; i < START_OPT_COUNT; i++) {
        uint16_t y = 70 + i * 50;
        if (i == selected) {
            //highlight selected with yellow arrow and box
            LCD_Draw_Rect(20, y-3, 200, 20, COL_SELECT, 0);
            LCD_printString(">", 25, y, COL_SELECT, 2);
        }
        LCD_printString((char*)options[i], 45, y, opt_cols[i], 2);
    }
 
    LCD_printString("U/D:Move  R/BT2:Pick", 10, 235, COL_HUD, 1);
    LCD_Refresh(&cfg0);
}
 
static void render_help_screen(void) {
    LCD_Fill_Buffer(COL_BG);
 
    LCD_printString("HOW TO PLAY", 40, 8, COL_TITLE, 2);
    LCD_Draw_Line(10, 28, 220, 28, COL_TITLE);
 
    // use colours to make each line stand out
    LCD_printString("Joystick = move", 10, 38, COL_HUD, 1);
    LCD_printString("P = you", 10, 55, COL_PLAYER, 1);
    LCD_printString("G = guard (avoid!)", 10, 70, COL_GUARD, 1);
    LCD_printString(". = item (collect!)", 10, 85, COL_ITEM, 1);
    LCD_printString("# = wall", 10, 100, COL_WALL, 1);
    LCD_Draw_Line(10, 115, 220, 115, COL_HUD);
    LCD_printString("Collect all items", 10, 122, COL_HUD, 1);
    LCD_printString("to beat each level", 10, 137, COL_HUD, 1);
    LCD_printString("3 levels total", 10, 152, COL_LEVEL, 1);
    LCD_printString("Guards get faster!", 10, 167, COL_GUARD, 1);
 
    LCD_printString("BT2: Back", 65, 235, COL_SELECT, 1);
    LCD_Refresh(&cfg0);
}
 
static int run_start_screen(void) {
    int selected = 0;
    Direction last_dir = CENTRE;
    Input_Read();
 
    while (1) {
        Input_Read();
        Joystick_Read(&joystick_cfg, &joystick_data);
        Direction cur = joystick_data.direction;
 
        if (cur == S && last_dir != S)
            selected = (selected + 1) % START_OPT_COUNT;
        else if (cur == N && last_dir != N)
            selected = (selected + START_OPT_COUNT - 1) % START_OPT_COUNT;
 
        if (current_input.btn2_pressed || (cur == E && last_dir != E)) {
            if (selected == START_OPT_START) return 0;
            if (selected == START_OPT_EXIT)  return 1;
            if (selected == START_OPT_HELP) {
                render_help_screen();
                while (1) {
                    Input_Read();
                    if (current_input.btn2_pressed) break;
                    HAL_Delay(30);
                }
            }
        }
 
        last_dir = cur;
        render_start_screen(selected);
        HAL_Delay(30);
    }
}
 
void Game1_Init(void) {
    exit_to_menu = 0;
    buzzer_tone(&buzzer_cfg, 1000, 70);
    HAL_Delay(300);
    buzzer_off(&buzzer_cfg);
    load_level(0);
}
 
void Game1_Update(void) {
    if (game_win || game_over) {
        if (current_input.btn2_pressed) exit_to_menu = 1;
        return;
    }
 
    if (current_input.btn2_pressed) { exit_to_menu = 1; return; }
 
    Joystick_Read(&joystick_cfg, &joystick_data);
    if (HAL_GetTick() - last_move_time > 150) {
        switch (joystick_data.direction) {
            case N: move_player(0,-1); last_move_time = HAL_GetTick(); break;
            case S: move_player(0, 1); last_move_time = HAL_GetTick(); break;
            case W: move_player(-1,0); last_move_time = HAL_GetTick(); break;
            case E: move_player( 1,0); last_move_time = HAL_GetTick(); break;
            default: break;
        }
    }
 
    if (HAL_GetTick() - last_guard_move_time > level_guard_speed[current_level]) {
        update_guards();
        last_guard_move_time = HAL_GetTick();
    }
 
    check_collect_item();
    check_guard_collision();
 
    if (items_left == 0 && !game_over) {
        if (current_level < MAX_LEVELS - 1) {
            play_level_clear();
            load_level(current_level + 1);
        } else {
            game_win = 1;
            play_win();
        }
    }
}
 
void Game1_Render(void) {
    LCD_Fill_Buffer(COL_BG);
 
    // Head-Up Display
    char buf[32];
    sprintf(buf, "LVL %d/%d", current_level + 1, MAX_LEVELS);
    LCD_printString(buf, 5, 5, COL_LEVEL, 2);
 
    if (!game_win && !game_over) {
        sprintf(buf, "Items:%d", items_left);
        LCD_printString(buf, 150, 5, COL_ITEM, 2);
    }
 
    // thin line separating HUD from maze
    LCD_Draw_Line(0, 28, 240, 28, COL_WALL);
 
    draw_maze();
 
    if (game_win) {
        // green win banner
        LCD_Draw_Rect(30, 200, 180, 25, COL_PLAYER, 1);
        LCD_printString("YOU WIN!", 60, 205, COL_BG, 2);
        LCD_printString("BT2: Menu", 70, 232, COL_HUD, 1);
    } else if (game_over) {
        // red game over banner
        LCD_Draw_Rect(20, 200, 200, 25, COL_GUARD, 1);
        LCD_printString("GAME OVER", 45, 205, COL_BG, 2);
        LCD_printString("BT2: Menu", 70, 232, COL_HUD, 1);
    } else {
        LCD_printString("BT2:Exit", 5, 232, COL_HUD, 1);
    }
 
    LCD_Refresh(&cfg0);
}
 
MenuState Game1_Run(void) {
    buzzer_tone(&buzzer_cfg, 1000, 70);
    HAL_Delay(300);
    buzzer_off(&buzzer_cfg);
 
    int result = run_start_screen();
    if (result == 1) return MENU_STATE_HOME;
 
    load_level(0);
    exit_to_menu = 0;
    Input_Read();
 
    while (1) {
        Input_Read();
        Game1_Update();
        Game1_Render();
        if (exit_to_menu) break;
        HAL_Delay(30);
    }
 
    return MENU_STATE_HOME;
}