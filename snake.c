/*
 * Copyright (c) 2021, Meco Jianting Man <jiantingman@foxmail.com>
 *
 * SPDX-License-Identifier: GPL
 *
 * Change Logs:
 * Date           Author       Notes
 * 2020-12-05     Meco Man     port to RT-Thread
 */
/* snake: a remake of the old nokia game
 * Copyright (C) 2013 Aleksa Sarai
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <string.h>
#include <unistd.h> /* for seeding randomness */
#include <sys/time.h>
#include <termios.h> /* for unbuffered input */
#include <fcntl.h>
#include <rtthread.h> /* for rt_thread_delay */
#include <shell.h> /* for finsh_getchar */

/* OPTIONS (CHANGE THIS BIT) */

#define SNAKE_WRAP      RT_FALSE    /* screen wrapping */
#define SPEED           100 /* refresh rate (in os Hz) */
#define BONUS_CHANCE    400 /* chance of 1/CHANCE for bonus to appear */

#define BONUS_MIN_TIME  30  /* lower range of lifespan of bonus */
#define BONUS_MAX_TIME  120 /* upper range of lifespan of bonus*/

#define START_SNAKE_LEN 5   /* the beginning length of the snake */

#define SNAKE_BODY      '*' /* char representing the snake's body */

/* chars representing the snake head, when facing a direction  */
#define SNAKE_HEAD_U    'v' /* up */
#define SNAKE_HEAD_D    '^' /* down */
#define SNAKE_HEAD_L    '>' /* left */
#define SNAKE_HEAD_R    '<' /* right  */

#define FOOD            '@' /* char representing food */
#define BONUS           '$' /* char representing */

#define FOOD_SCORE      1   /* score increase when snake eats food */
#define BONUS_SCORE     10  /* score increase when snake eats bonus */

#define SCREEN_WIDTH    30  /* the virtual screen width */
#define SCREEN_HEIGHT   20  /* the virtual screen height */

#define BORDER_CORNER   '+' /* character at corners of border */
#define BORDER_VERT     '|' /* character for vertical border */
#define BORDER_HORI     '-' /* character for horizontal border */

/* END OPTIONS (the rest is more dev stuff ) */

#define ANSI_RED        "\x1b[1;31m"
#define ANSI_GREEN      "\x1b[1;32m"
#define ANSI_YELLOW     "\x1b[1;33m"
#define ANSI_BLUE       "\x1b[1;34m"
#define ANSI_WHITE      "\x1b[1;37m"
#define ANSI_CLEAR      "\x1b[0m"

#define ANSI_SHOW_CUR   "\033[?25h"
#define ANSI_HIDE_CUR   "\033[?25l"

#define SCORE_FORMAT    "Score: " ANSI_BLUE   "%d" ANSI_CLEAR
#define TIMER_FORMAT    "Timer: " ANSI_YELLOW "%d" ANSI_CLEAR

/* for positions, (x, y) */
#define X 0
#define Y 1

#define IS_SNAKE(ch) (ch == SNAKE_BODY || ch == SNAKE_HEAD_U || ch == SNAKE_HEAD_D || ch == SNAKE_HEAD_L || ch == SNAKE_HEAD_R)

/* global states */
enum {
    win,
    loss
};

enum {
    up,
    down,
    left,
    right
};

static int quit;
static char **game_state; /* stores the string of the current game state */
static char *border;

/* snake */
static int snake_len;
static int snake_head[2]; /* position of head */
static int **snake_body; /* position of all body parts */
static int snake_direction;

/* food / score */
static int score;
static int food[2]; /* (x, y) of current food position */
static int bonus[2]; /* (x, y) of current bonus position */
static int timer;

static char getch(void) {
    static struct termios old, new;

    /* get old settings */
    tcgetattr(0, &old);
    new = old;

    /* disable buffered i/o and echo */
    new.c_lflag &= ~ICANON;
    new.c_lflag &= ~ECHO;

    /* set new terminal settings */
    tcsetattr(0, TCSANOW, &new);

    /* get char and reset terminal settings */
    char ch = finsh_getchar();
    tcsetattr(0, TCSANOW, &old);

//    /* if ctrl-c was pressed, exit */
//    if(ch == 3)
//        exit(2);

    return ch;
} /* getch_() */

static int snake_rand(int min, int max) {
    /* generate x, where min <= x <= max */
    return (rand() % (max + 1 - min)) + min;
} /* snake_rand() */

static void snake_init(void) {
    /* seed randomness */
    srand(time(RT_NULL));

    /* set defaults */
    score = 0;
    quit = RT_FALSE;
    game_state = RT_NULL;
    border = RT_NULL;
    snake_body = RT_NULL;
    snake_direction = right;

    /* snake */
    snake_len = START_SNAKE_LEN;
    snake_head[X] = SCREEN_WIDTH / 2;
    snake_head[Y] = SCREEN_HEIGHT / 2;

    snake_body = rt_malloc(snake_len * sizeof(int *));

    int i;
    for(i = 0; i < snake_len; i++) {
        snake_body[i] = rt_malloc(2 * sizeof(int));

        /* set default position */
        snake_body[i][X] = snake_head[X] - i;
        snake_body[i][Y] = snake_head[Y];
    }

    /* food and bonus */
    food[X] = snake_rand(0, SCREEN_WIDTH - 1);
    food[Y] = snake_rand(0, SCREEN_HEIGHT - 1);

    bonus[X] = -1;
    bonus[Y] = -1;
    timer = -1;

    /* game state */
    game_state = rt_calloc(SCREEN_HEIGHT * SCREEN_WIDTH, sizeof(char *));
    for(i = 0; i < SCREEN_WIDTH; i++) {
        game_state[i] = rt_calloc(SCREEN_WIDTH + 1, 1);
        game_state[i][SCREEN_WIDTH] = '\0';
    }

    /* border */
    border = rt_calloc(SCREEN_WIDTH + 3, 1);

    for(i = 0; i < SCREEN_WIDTH; i++)
        border[i + 1] = BORDER_HORI;

    border[0] = border[SCREEN_WIDTH + 1] = BORDER_CORNER;
} /* snake_init() */

static void snake_input(void) {
    /* get non-blocking input */
    int old = fcntl(0, F_GETFL);
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

    int ch = getch();

    fcntl(0, F_SETFL, old);

    /* 'normal' input switch */
    int weird = RT_FALSE;
    switch(ch) {
        case 'q':
        case 'Q':
            quit = RT_TRUE;
            break;
        case 'w':
        case 'W':
            if(snake_direction != down)
                snake_direction = up;
            break;
        case 's':
        case 'S':
            if(snake_direction != up)
                snake_direction = down;
            break;
        case 'd':
        case 'D':
            if(snake_direction != left)
                snake_direction = right;
            break;
        case 'a':
        case 'A':
            if(snake_direction != right)
                snake_direction = left;
            break;
        case 27:
            weird = RT_TRUE;
            break;
    }

    /* check if key was 'normal' */
    if(!weird)
        return;

    /* up, down, left, right keys -- specific to my machine and os */
    if(ch == 27 && getch() == 91) {
        switch(getch()) {
            case 65:
                if(snake_direction != down)
                    snake_direction = up;
                break;
            case 66:
                if(snake_direction != up)
                    snake_direction = down;
                break;
            case 67:
                if(snake_direction != left)
                    snake_direction = right;
                break;
            case 68:
                if(snake_direction != right)
                    snake_direction = left;
                break;
            default:
                break;
        }
    }
} /* snake_input() */

/* GAME STATE:
 *   Y ^
 *   0 |       @
 *     |       x
 *     |  ******
 *     |  *
 * max +------------>
 *     0        max X
 *
 * BODY:
 * [head, ..., tail]
 */

static void shift_snake(void) {
    /* shifts all snake body parts down */
    int i;
    for(i = snake_len - 1; i > 0; i--) {
        snake_body[i][X] = snake_body[i-1][X];
        snake_body[i][Y] = snake_body[i-1][Y];
    }

    /* set top of head */
    snake_body[0][X] = snake_head[X];
    snake_body[0][Y] = snake_head[Y];
} /* shift_snake() */

static int in_snake(int x, int y) {
    if(snake_head[X] == x && snake_head[Y] == y)
        return RT_TRUE;

    int i;
    for(i = 0; i < snake_len; i++) {
        if(snake_body[i][X] == x && snake_body[i][Y] == y)
            return RT_TRUE;
    }

    return RT_FALSE;
} /* in_snake() */

static void snake_update(void) {
    /* first, update snake head */
    switch(snake_direction) {
        case up:
            snake_head[Y]--;
            break;
        case down:
            snake_head[Y]++;
            break;
        case left:
            snake_head[X]--;
            break;
        case right:
            snake_head[X]++;
            break;
    }

    /* check if snake intersects body -- and quit appropriately */
    int i;
    for(i = 1; i < snake_len; i++) {
        if(snake_head[X] == snake_body[i][X] && snake_head[Y] == snake_body[i][Y]) {
            quit = RT_TRUE;
            return;
        }
    }


    /* check if snake is outside screen */
    if(snake_head[Y] < 0 || snake_head[Y] >= SCREEN_HEIGHT ||
       snake_head[X] < 0 || snake_head[X] >= SCREEN_WIDTH) {
        if(SNAKE_WRAP) {
            /* if wrapping is enabled, wrap the screen */
            snake_head[X] = (snake_head[X] < 0) ?  SCREEN_WIDTH - 1 : snake_head[X];
            snake_head[Y] = (snake_head[Y] < 0) ? SCREEN_HEIGHT - 1 : snake_head[Y];

            snake_head[X] %= SCREEN_WIDTH;
            snake_head[Y] %= SCREEN_HEIGHT;
        } else {
            /* otherwise, game over */
            quit = RT_TRUE;
            return;
        }

    }

    /* check if food has been eaten */
    if(snake_head[X] == food[X] && snake_head[Y] == food[Y]) {
        /* update score */
        score += FOOD_SCORE;

        /* reallocate body */
        snake_body = rt_realloc(snake_body, (snake_len + 1) * sizeof(int *));
        snake_body[snake_len] = rt_malloc(2 * sizeof(int));

        /* set body to defaults */
        snake_body[snake_len][X] = -1;
        snake_body[snake_len][Y] = -1;

        /* update length of snake */
        snake_len++;

        /* regenerate food */
        do {
            food[X] = snake_rand(0, SCREEN_WIDTH - 1);
            food[Y] = snake_rand(0, SCREEN_HEIGHT - 1);
        } while(in_snake(food[X], food[Y]) || (food[X] == bonus[X] && food[Y] == bonus[Y]));
    }

    /* if snake eats bonus */
    if(snake_head[X] == bonus[X] && snake_head[Y] == bonus[Y]) {
        /* update score */
        score += BONUS_SCORE;

        /* delete bonus */
        bonus[X] = -1;
        bonus[Y] = -1;
        timer = -1;
    }

    /* updating bonus */
    if(bonus[X] >= 0 && bonus[Y] >= 0) {
        /* decrement bonus timer */
        timer--;

        /* if time has run out, remove bonus food */
        if(!timer) {
            bonus[X] = -1;
            bonus[Y] = -1;
            timer = -1;
        }
    } else {
        /* no bonus -- randomly generate */
        if(snake_rand(0, BONUS_CHANCE) == BONUS_CHANCE / 2) {
            /* add bonus */
            do {
                bonus[X] = snake_rand(0, SCREEN_WIDTH - 1);
                bonus[Y] = snake_rand(0, SCREEN_HEIGHT - 1);
            } while(in_snake(bonus[X], bonus[Y]) || (bonus[X] == food[X] && bonus[Y] == food[Y]));

            /* set timer */
            timer = snake_rand(BONUS_MIN_TIME, BONUS_MAX_TIME);
        }
    }

    shift_snake();

    /* -- update string representation of game -- */

    /* clear */
    int x, y;
    for(y = 0; y < SCREEN_HEIGHT; y++) {
        for(x = 0; x < SCREEN_WIDTH; x++) {
            game_state[y][x] = ' ';
        }
    }

    /* add snake body */
    for(i = 1; i < snake_len; i++) {
        x = snake_body[i][X];
        y = snake_body[i][Y];

        /* ignore 'fake' positions */
        if(x < 0 || y < 0)
            continue;

        game_state[y][x] = SNAKE_BODY;
    }

    /* add food */
    x = food[X];
    y = food[Y];
    if(x >= 0 && y >= 0)
        game_state[y][x] = FOOD;

    /* add bonus */
    x = bonus[X];
    y = bonus[Y];
    if(x >= 0 && y >= 0)
        game_state[y][x] = BONUS;

    /* add snake head */
    x = snake_head[X];
    y = snake_head[Y];
    if(x >= 0 && y >= 0) {
        char head = 'X';
        switch(snake_direction) {
            case up:
                head = SNAKE_HEAD_U;
                break;
            case down:
                head = SNAKE_HEAD_D;
                break;
            case left:
                head = SNAKE_HEAD_L;
                break;
            case right:
                head = SNAKE_HEAD_R;
                break;
        }
        game_state[y][x] = head;
    }
} /* snake_update() */

static void clr_line(void) {
    rt_kprintf("\x1b[s"); /* save current cursor position */
    rt_kprintf("%*s", SCREEN_WIDTH, ""); /* blank out virual screen */
    rt_kprintf("\x1b[u"); /* move cursor back to saved position */
} /* clr_line() */

static void snake_redraw(void) {
    /* move virtual cursor to top left */
    rt_kprintf("\x1b[H");

    /* top border */
    rt_kprintf("%s\n", border);

    /* print game state */
    int i;
    for(i = 0; i < SCREEN_HEIGHT; i++)
        rt_kprintf("%c%s%c\n", BORDER_VERT, game_state[i], BORDER_VERT);

    /* bottom border */
    rt_kprintf("%s\n", border);

    /* print score */
    clr_line();
    rt_kprintf("\n" SCORE_FORMAT "\n", score);

    /* if a timer exists, print it */
    if(timer >= 0) {
        clr_line();
        rt_kprintf(TIMER_FORMAT "\n", timer);
    }

    clr_line();
} /* snake_redraw() */

static int snake_end_state(void) {
    int x, y;

    for(y = 0; y < SCREEN_HEIGHT; y++)
        for(x = 0; x < SCREEN_WIDTH; x++)
            if(!IS_SNAKE(game_state[y][x]))
                return loss;

    return win;
} /* snake_end_state() */

static void move_cursor(int row, int column) {
    rt_kprintf("\x1b[%d;%df", row, column);
} /* snake_redraw() */

static int snake_main(int argc, char *argv[]) {
    /* clear screen */
    rt_kprintf("\x1b[H\x1b[2J");
    rt_kprintf(ANSI_HIDE_CUR);

    /* intialise snake */
    snake_init();
    snake_redraw();

    /* main snake loop */
    while(!quit) {
        snake_input();  /* get input */
        snake_update(); /* update game state */
        snake_redraw(); /* redraw screen */

        /* wait SPEED seconds */
        rt_thread_delay(SPEED);
    }

    /* end game */
    char *msg = RT_NULL;
    int end_state = snake_end_state();

    switch(end_state) {
        case win:
            msg = "You Won!";
            break;
        case loss:
        default:
            msg = "Game Over!";
            break;
    }

    move_cursor((SCREEN_HEIGHT / 2) + 2, (SCREEN_WIDTH / 2) - (strlen(msg) / 2) + 2);
    rt_kprintf("%s%s%s", ANSI_RED, msg, ANSI_CLEAR);

    /* rt_free memory */
    int i;
    for(i = 0; i < SCREEN_HEIGHT * SCREEN_WIDTH; i++)
        rt_free(game_state[i]);
    rt_free(game_state);

    for(i = 0; i < snake_len; i++)
        rt_free(snake_body[i]);
    rt_free(snake_body);
    rt_free(border);

    move_cursor(SCREEN_HEIGHT + 5 + (timer >= 0), 0); /* move to default, screen_height + borders + spaces for score, etc. */
    rt_kprintf(ANSI_SHOW_CUR);

    return 0;
}
MSH_CMD_EXPORT_ALIAS(snake_main, snake, a remake of the old Nokia game);
