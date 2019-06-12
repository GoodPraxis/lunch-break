#include <genesis.h>

#include "gfx.h"
#include "sprite.h"
#include "sound.h"

#define SFX_FANFARE     64
#define SFX_BACKTOWORK  65

#define MUSIC_TEMPO     50

#define ANIM_BRAKE      4
#define ANIM_CROUCH     2
#define ANIM_JUMP       3
#define ANIM_RUN        1
#define ANIM_SAD        7
#define ANIM_STAND      0
#define ANIM_TALK       5
#define ANIM_WALK       6

#define MAX_SPEED       FIX32(8)
#define RUN_SPEED       FIX32(3)
#define BRAKE_SPEED     FIX32(2)

#define JUMP_SPEED      FIX32(-7)
#define GRAVITY         FIX32(0.3)
#define ACCEL           FIX32(0.1)
#define DE_ACCEL        FIX32(0.15)

#define MIN_POSX        FIX32(0)
#define MAX_POSX        FIX32(470)
#define MAX_POSY        FIX32(156)

#define START_POSITION  FIX32(120);

#define BOSS_SPEED      FIX32(1.3)
#define BOSS_POSITION   FIX32(300)

#define WORKER_SPEED    FIX32(1);
#define WORKER_TYPES    3

#define SPEECH_BUBBLE_TYPES 4

#define CHARACTER_COUNT 5

static void handleInput();
static void joyEvent(u16 joy, u16 changed, u16 state);

static void gameOver();
static void resetGame();
static void updateAnim();
static void updateCamera();
static void updateCharacterFlip();
static void updatePhysics();
static void updateScore();
static void updateScroll(fix32 x, fix32 y);

// sprites structure (pointer of Sprite)
Sprite* sprites[1 + CHARACTER_COUNT];
Sprite* number_sprites[4];
Sprite* gameover_sprites[2];
Sprite* speech_bubble;

const SpriteDefinition* worker_sprites[] = {&worker_sprite, &worker2_sprite, &worker3_sprite};

fix32 camposx;
fix32 camposy;
fix32 posx;
fix32 posy;
fix32 movx;
fix32 movy;
fix32 talking;
s16 xorder;
s16 yorder;
u16 score;

u8 game_over;
u8 game_started;

const fix32 wait_time = FIX32(50);

u16 palette[64];

const u16 palette_alt[16] =
{
    0x0000,
    0x0E00,
    0x000E,
    0x0E00,
    0x0003,
    0x0000,
    0x0E00,
    0x070B,

    0x0000,
    0x0000,
    0x0D00,
    0xFFFF,
    0xFFFF,
    0x0E00,
    0xFFFF,
    0x0C00
};

struct Character {
    fix32 posX;
    fix32 posY;
    fix32 speed;
    s16 direction;
    u16 type;
    Sprite* sprite;
    fix32 wait;
    fix32 spawn_delay;
    s16 convinced;
};

struct Character* characters[CHARACTER_COUNT];

struct Character boss = {BOSS_POSITION, FIX32(154), BOSS_SPEED, 1, 0};
struct Character worker_template = {0, FIX32(154), FIX32(1), 1, 1};


int main() {
    u16 ind;
    u8 i;

    camposx = -1;
    camposy = -1;
    game_over = 0;
    game_started = 0;
    movx = 0;
    movy = 0;
    posx = START_POSITION;
    posy = MAX_POSY;
    score = 0;
    talking = 0;
    xorder = 0;
    yorder = 0;

    // Disable interrupt when accessing VDP
    SYS_disableInts();
    VDP_setScreenWidth320();

    // Init SFX
    SND_setPCM_XGM(SFX_FANFARE, fanfare_sfx, sizeof(fanfare_sfx));
    SND_setPCM_XGM(SFX_BACKTOWORK, backtowork_sfx, sizeof(backtowork_sfx));

    // Init sprite engine with default parameters
    SPR_init();

    // Set all palette to black for fade in
    VDP_setPaletteColors(0, (u16*) palette_black, 64);

    // Copy palette data from sprite data
    memcpy(&palette[0], worker_sprite.palette->data, 16 * 2);
    memcpy(&palette[16], bgb_image.palette->data, 16 * 2);
    memcpy(&palette[32], player_sprite.palette->data, 16 * 2);
    memcpy(&palette[48], boss_sprite.palette->data, 16 * 2);

    // Load start screen background
    ind = TILE_USERINDEX;
    VDP_drawImageEx(PLAN_B, &start_image, TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, ind), 0, 0, FALSE, TRUE);

    // VDP process done, we can re enable interrupts
    SYS_enableInts();

    JOY_setEventHandler(joyEvent);

    // Show "press start" screen
    VDP_fadeIn(0, (4 * 16) - 1, palette, 20, FALSE);

    SND_startPlay_XGM(bg_music);
    SND_setMusicTempo_XGM(MUSIC_TEMPO);

    // Start screen loop
    while (game_started == 0) {
        handleInput();
        VDP_waitVSync();
    }

    // At this stage, start has been pressed and the game is beginning

    VDP_fadeOutAll(30, TRUE);

    SYS_disableInts();

    // Load background
    VDP_drawImageEx(PLAN_B, &bgb_image, TILE_ATTR_FULL(PAL1, FALSE, FALSE, FALSE, ind), 0, 0, FALSE, TRUE);
    ind += bgb_image.tileset->numTile;
    VDP_drawImageEx(PLAN_A, &bga_image, TILE_ATTR_FULL(PAL1, FALSE, FALSE, FALSE, ind), 0, 0, FALSE, TRUE);
    ind += bga_image.tileset->numTile;

    // VDP process done, we can re enable interrupts
    SYS_enableInts();

    // Init scrolling
    updateScroll(0, 0);

    // Init player sprite
    sprites[0] = SPR_addSprite(&player_sprite, fix32ToInt(posx - camposx), fix32ToInt(posy - camposy), TILE_ATTR(PAL2, TRUE, FALSE, FALSE));

    for (i = 0; i < CHARACTER_COUNT - 1; i++) {
        // Allocate memory and copy worker template struct
        characters[i] = MEM_alloc(sizeof(struct Character));
        memcpy(characters[i], &worker_template, sizeof(struct Character));
        characters[i]->spawn_delay = FIX32(50 + 100 * i * i);
        characters[i]->convinced = -1;
        // Add worker sprite
        sprites[i + 1] = SPR_addSprite(worker_sprites[i % WORKER_TYPES], 0, 0, TILE_ATTR(PAL0, TRUE, FALSE, FALSE));
        characters[i]->sprite = sprites[i + 1];
        SPR_setVisibility(characters[i]->sprite, TRUE);
    }

    // Boss
    characters[i] = &boss;
    sprites[i + 1] = SPR_addSprite(&boss_sprite, 0, 0, TILE_ATTR(PAL3, TRUE, FALSE, FALSE));
    characters[i]->sprite = sprites[i + 1];
    boss.wait = wait_time;

    // Confusingly, TRUE means hidden
    SPR_setVisibility(sprites[i + 1], TRUE);

    // Score
    SPR_addSprite(&score_sprite, 0, 0, TILE_ATTR(PAL1, TRUE, FALSE, FALSE));

    for (i = 0; i < 4; i++) {
        number_sprites[i] = SPR_addSprite(&number_sprite, 50 + i * 8, 0, TILE_ATTR(PAL1, TRUE, FALSE, FALSE));
    }

    // Game Over
    gameover_sprites[0] = SPR_addSprite(&gameover_sprite, 112, 100, TILE_ATTR(PAL1, TRUE, FALSE, FALSE));
    gameover_sprites[1] = SPR_addSprite(&gameover_sprite, 160, 100, TILE_ATTR(PAL1, TRUE, FALSE, FALSE));
    SPR_setAnim(gameover_sprites[1], 1);

    SPR_setVisibility(gameover_sprites[0], TRUE);
    SPR_setVisibility(gameover_sprites[1], TRUE);

    // Speech bubble
    speech_bubble = SPR_addSprite(&bubble_sprite, 0, 0, TILE_ATTR(PAL1, TRUE, FALSE, FALSE));
    SPR_setVisibility(speech_bubble, TRUE);

    // Final initialization
    updateCharacterFlip();
    SPR_update();

    VDP_fadeIn(0, (4 * 16) - 1, palette, 20, TRUE);

    SPR_setVisibility(sprites[i + 1], FALSE);

    while (TRUE) {
        handleInput();
        updatePhysics();
        updateCamera();
        updateAnim();
        updateScore();
        SPR_update();
        VDP_waitVSync();
    }

    return 0;
}

static void updateCharacter(struct Character *character) {
    s16 direction;
    if (character->spawn_delay > FIX32(1)) {
        SPR_setVisibility(character->sprite, TRUE);
        if (game_over == 0) {
            character->spawn_delay -= FIX32(1);
        }
        return;
    } else if (character->spawn_delay == FIX32(1)) {
        if (character->direction > 0) {
            character->posX = MIN_POSX;
        } else {
            character->posX = MAX_POSX;
        }
        character->spawn_delay = 0;
    }

    SPR_setVisibility(character->sprite, FALSE);

    if (character->wait > 0) {
        character->wait -= FIX32(1);

        if (character->type == 1 && character->wait == FIX32(1) && game_over == 0) {
            SND_startPlayPCM_XGM(SFX_FANFARE, 1, SOUND_PCM_CH2);
            score += 1;
        }
        return;
    }

    if (character->type == 1) {
        // Worker
        if (character->posX > MAX_POSX || character->posX < MIN_POSX) {
            // Respawn
            character->spawn_delay = FIX32(200);
            character->direction = -character->direction;
            SPR_setVisibility(character->sprite, TRUE);
            character->convinced = -1;
            return;
        }

        if (character->convinced < 0) {
            if (movx == 0 && game_over == 0 && talking == 0 && posy == MAX_POSY &&
                character->posX - posx < FIX32(25) && character->posX - posx > FIX32(-25)) {
                // Listening
                character->wait = wait_time;
                SPR_setAnim(character->sprite, 2);

                character->convinced = -character->convinced;

                boss.speed += FIX32(0.1);

                talking = wait_time;
                return;
            }

            SPR_setAnim(character->sprite, 0);
        } else if (character->convinced > 0) {
            SPR_setAnim(character->sprite, 1);
        }
    } else {
        // Boss
        if (character->posX - posx > FIX32(25) || character->posX - posx < FIX32(-25)) {
            SPR_setAnim(character->sprite, 1);
            if (character->posX < posx) {
                direction = 1;
            } else {
                direction = -1;
            }

            if (character->direction != direction) {
                character->direction = direction;
                character->wait = wait_time;
                SPR_setAnim(character->sprite, 0);
                return;
            }
        } else if (posy == MAX_POSY && game_over == 0) {
            // Boss caught player
            SPR_setAnim(character->sprite, 0);
            gameOver();
            return;
        }
    }

    if (character->direction > 0) {
        character->posX += character->speed;
    } else {
        character->posX -= character->speed;
    }
}

static void updateCharacterFlip() {
    u16 i;
    for (i = 0; i < CHARACTER_COUNT; i++) {
        if (characters[i]->direction > 0) {
            SPR_setHFlip(sprites[i + 1], FALSE);
        } else {
            SPR_setHFlip(sprites[i + 1], TRUE);
        }
    }
}

static void updatePhysics() {
    if (xorder > 0) {
        movx += ACCEL;
        // Going other way, quick breaking
        if (movx < 0) {
            movx += ACCEL;
        }
        if (movx >= MAX_SPEED) {
            movx = MAX_SPEED;
        }
    } else if (xorder < 0) {
        movx -= ACCEL;
        // Going other way, quick breaking
        if (movx > 0) {
            movx -= ACCEL;
        }
        if (movx <= -MAX_SPEED) {
            movx = -MAX_SPEED;
        }
    } else {
        if (movx < FIX32(0.1) && movx > FIX32(-0.1)) {
            movx = 0;
        } else if (movx < FIX32(0.3) && movx > FIX32(-0.3)) {
            movx -= movx >> 2;
        } else if (movx < FIX32(1) && movx > FIX32(-1)) {
            movx -= movx >> 3;
        } else {
            movx -= movx >> 4;
        }
    }

    posx += movx;
    posy += movy;

    if (movy) {
        if (posy > MAX_POSY) {
            posy = MAX_POSY;
            movy = 0;
        } else {
            movy += GRAVITY;
        }
    }

    if (posx >= MAX_POSX) {
        posx = MAX_POSX;
        movx = 0;
    } else if (posx <= MIN_POSX) {
        posx = MIN_POSX;
        movx = 0;
    }

    // Set sprites position
    SPR_setPosition(sprites[0], fix32ToInt(posx - camposx), fix32ToInt(posy - camposy));
    SPR_setPosition(speech_bubble, fix32ToInt(posx - camposx), fix32ToInt(posy - camposy) - 48);

    // @TODO: Move to better location
    for (u8 i = 0; i < CHARACTER_COUNT; i++) {
        updateCharacter(characters[i]);
        SPR_setPosition(sprites[i + 1], fix32ToInt(characters[i]->posX - camposx), fix32ToInt(characters[i]->posY - camposy));
    }
}

static void updateCamera() {
    fix32 px_scr, npx_cam;

    // Get player position on screen
    px_scr = posx - camposx;

    // Calculate new camera position
    if (px_scr > FIX32(240)) {
        npx_cam = posx - FIX32(240);
    } else if (px_scr < FIX32(40)) {
        npx_cam = posx - FIX32(40);
    } else {
        npx_cam = camposx;
    }

    // Clip camera position
    if (npx_cam < FIX32(0)) {
        npx_cam = FIX32(0);
    } else if (npx_cam > FIX32(200)) {
        npx_cam = FIX32(200);
    }

    // Set scroll position
    updateScroll(npx_cam, camposy);
}

static void updateScore() {
    u16 m = 1000, c = 0, r = 0;
    // Calculate index of each digit
    for (u16 i = 0; i < 4; i++) {
        r = (score - c) / m;
        SPR_setAnim(number_sprites[i], r);
        c += r * m;
        m = m / 10;
    }
}

static void updateAnim() {
    if (talking > 0) {
        talking -= FIX32(1);
        SPR_setAnim(sprites[0], ANIM_TALK);
    } else if (game_over > 0) {
        SPR_setAnim(sprites[0], ANIM_SAD);
    } else if (movy) {
        SPR_setAnim(sprites[0], ANIM_JUMP);
    } else {
        if ((movx >= BRAKE_SPEED && xorder < 0) || (movx <= -BRAKE_SPEED && xorder > 0)) {
            if (sprites[0]->animInd != ANIM_BRAKE) {
                SPR_setAnim(sprites[0], ANIM_BRAKE);
            }
        } else if (movx >= RUN_SPEED || movx <= -RUN_SPEED) {
            SPR_setAnim(sprites[0], ANIM_RUN);
        } else if (movx != 0) {
            SPR_setAnim(sprites[0], ANIM_WALK);
        } else {
            if (yorder > 0) {
                SPR_setAnim(sprites[0], ANIM_CROUCH);
            } else {
                SPR_setAnim(sprites[0], ANIM_STAND);
            }
        }
    }

    if (talking > 0 && game_over == 0) {
        SPR_setVisibility(speech_bubble, FALSE);
        // We re-use the score value to display different speech bubbles
        SPR_setAnim(speech_bubble, score % SPEECH_BUBBLE_TYPES);
    } else {
        SPR_setVisibility(speech_bubble, TRUE);
    }

    // Make sure the player sprite is facing the right direction
    if (movx > 0) {
        SPR_setHFlip(sprites[0], FALSE);
    } else if (movx < 0) {
        SPR_setHFlip(sprites[0], TRUE);
    }

    updateCharacterFlip();
}

static void updateScroll(fix32 x, fix32 y) {
    if (x != camposx) {
        camposx = x;
        VDP_setHorizontalScroll(PLAN_A, fix32ToInt(-camposx));
        // Move PLAN_B at a slower rate
        VDP_setHorizontalScroll(PLAN_B, fix32ToInt(-camposx) >> 2);
    }
}


static void handleInput() {
    u16 value = JOY_readJoypad(JOY_1);

    if (game_over == 0 && game_started > 0) {
        if (value & BUTTON_UP) {
            yorder = -1;
        } else if (value & BUTTON_DOWN) {
            yorder = 1;
        } else {
            yorder = 0;
        }

        if (talking == 0 && value & BUTTON_LEFT) {
            xorder = -1;
        } else if (talking == 0 && value & BUTTON_RIGHT) {
            xorder = 1;
        } else {
            xorder = 0;
        }
    } else {
        xorder = 0;
        yorder = 0;
    }

   if (value & BUTTON_START) {
       game_started = 1;
   }
}

static void joyEvent(u16 joy, u16 changed, u16 state) {
    if (changed & BUTTON_START && game_over > 0) {
        resetGame();
    }

    if (game_over == 0 && movy == 0 && talking == 0 && changed & state & (BUTTON_A | BUTTON_B | BUTTON_C)) {
        movy = JUMP_SPEED;
    }
}

static void gameOver() {
    SND_startPlayPCM_XGM(SFX_BACKTOWORK, 1, SOUND_PCM_CH2);
    game_over = 1;
    VDP_fadePal(PAL1, palette, palette_alt, 80, TRUE);
    SND_setMusicTempo_XGM(30);
    for (u8 i = 0; i < CHARACTER_COUNT - 1; i++) {
        characters[i]->speed = FIX32(0.4);
        characters[i]->convinced = -1;
        characters[i]->direction = -characters[i]->direction;
    }
    SPR_setVisibility(gameover_sprites[0], FALSE);
    SPR_setVisibility(gameover_sprites[1], FALSE);
}

static void resetGame() {
    score = 0;
    game_over = 0;
    VDP_fadeOutAll(30, FALSE);
    SND_setMusicTempo_XGM(MUSIC_TEMPO);

    for (u8 i = 0; i < CHARACTER_COUNT - 1; i++) {
        characters[i]->spawn_delay = FIX32(50 + 100 * i * i);
        characters[i]->convinced = -1;
        characters[i]->direction = 1;
        characters[i]->posX = 0;
        characters[i]->speed = WORKER_SPEED;
    }

    boss.posX = BOSS_POSITION;
    boss.speed = BOSS_SPEED;
    boss.direction = 1;
    boss.wait = wait_time;

    posx = START_POSITION;

    camposx = -1;

    SPR_setVisibility(gameover_sprites[0], TRUE);
    SPR_setVisibility(gameover_sprites[1], TRUE);

    VDP_fadeInAll(palette, 120, TRUE);
}
