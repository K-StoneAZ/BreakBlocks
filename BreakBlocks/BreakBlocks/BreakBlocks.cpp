// ============================================================
// Breakout - Phase 1+Levels (Clean Version)
// Paddle + Ball + Bricks + Levels + PowerUps + Effects
// Win32 + GDI
// C++14
// ============================================================

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

// ============================================================
// Constants / Configuration
// ============================================================

static const int SCREEN_W = 800;
static const int SCREEN_H = 600;

static const float PADDLE_W = 100.f;
static const float PADDLE_H = 15.f;
static const float PADDLE_SPEED = 6.f;

static const float BALL_RADIUS = 6.f;
static const float BALL_SPEED = 5.5f;
static const int BALL_CAP = 6;

static const int BRICK_ROWS = 5;
static const int BRICK_COLS = 10;
static const int BRICK_W = 70;
static const int BRICK_H = 20;
static const int BRICK_GAP = 6;

// Base values for reset/restoration
static const float BASE_BALL_SPEED = BALL_SPEED;
static const float BASE_BALL_RADIUS = BALL_RADIUS;

// ============================================================
// Math / Utility Functions
// ============================================================

float Clamp(float v, float min, float max)
{
    if (v < min) return min;
    if (v > max) return max;
    return v;
}

bool CircleRectIntersect(float cx, float cy, float r, const RECT& rc)
{
    float closestX = Clamp(cx, (float)rc.left, (float)rc.right);
    float closestY = Clamp(cy, (float)rc.top, (float)rc.bottom);
    float dx = cx - closestX;
    float dy = cy - closestY;
    return (dx * dx + dy * dy) <= (r * r);
}

COLORREF GetBrickColor(int hits)
{
    switch (hits)
    {
    case 1: return RGB(200, 200, 200); // light gray
    case 2: return RGB(0, 200, 255);   // cyan
    case 3: return RGB(0, 180, 0);     // green
    case 4: return RGB(255, 180, 0);   // orange
    case 5: return RGB(255, 0, 0);     // red
    default: return RGB(255, 255, 255);
    }
}

int RollBrickHits(int level)
{
    int base = 1 + level / 2;
    if (base < 1) base = 1;
    if (base > 4) base = 4;

    int roll = rand() % 100;
    if (roll < 50) return base;
    else if (roll < 80) return base + 1;
    else return base + 2;
}

// ============================================================
// Enums / Structs
// ============================================================

struct Paddle
{
    float x, y, w, h;
};

struct Ball
{
    float x, y, vx, vy, r;
    float spin = 0.f;
	int penetrateMax = 0; // max number of bricks it can penetrate
	int penetrateCount = 0; // number of bricks it can penetrate per hit
	bool stuck = false;
	bool alive = false;
};

struct Brick
{
    RECT rect;
    int hits;
    COLORREF color;
    bool alive;
};

// ============================================================
// Global Game State
// ============================================================

static Paddle g_paddle;
static float g_paddleVX = 0.f;
static float g_paddlePrevX = 0.f;
static Ball g_ball[BALL_CAP];
static int g_ballMax = 1; // current number of active balls
static bool g_ballLaunched = false;

static Brick g_bricks[BRICK_ROWS * BRICK_COLS];

static int g_score = 0;
static int g_lives = 3;
static int g_level = 1;
static bool g_gameOver = false;
static bool g_spin = false;
static bool g_stickyPaddle = false;
static bool g_invulnerable = false;

// ============================================================
// Persistent Back Buffer
// ============================================================

static HDC g_backDC = NULL;
static HBITMAP g_backBitmap = NULL;
static HBITMAP g_backOldBitmap = NULL;
static int g_backW = 0;
static int g_backH = 0;

void DestroyBackBuffer()
{
    if (g_backDC)
    {
        SelectObject(g_backDC, g_backOldBitmap);
        DeleteObject(g_backBitmap);
        DeleteDC(g_backDC);
    }
    g_backDC = NULL;
    g_backBitmap = NULL;
    g_backOldBitmap = NULL;
    g_backW = g_backH = 0;
}

void CreateBackBuffer(HWND hwnd, int width, int height)
{
    DestroyBackBuffer();
    HDC hdc = GetDC(hwnd);
    g_backDC = CreateCompatibleDC(hdc);
    g_backBitmap = CreateCompatibleBitmap(hdc, width, height);
    g_backOldBitmap = (HBITMAP)SelectObject(g_backDC, g_backBitmap);
    g_backW = width;
    g_backH = height;
    ReleaseDC(hwnd, hdc);
}

// ============================================================
// Initialization
// ============================================================

void InitPaddle()
{
    g_paddle.w = PADDLE_W;
    g_paddle.h = PADDLE_H;
    g_paddle.x = (g_backW - g_paddle.w) * 0.5f;
    g_paddle.y = g_backH - 40.f;
}

void InitBall()
{
    for (int i = 0; i < BALL_CAP; ++i)
    {
        g_ball[i].r = BALL_RADIUS;
        g_ball[i].vx = (i == 0) ? BALL_SPEED : 0.f;  // only first ball moving
        g_ball[i].vy = (i == 0) ? -BALL_SPEED : 0.f;
        g_ball[i].penetrateMax = 0;
        g_ball[i].penetrateCount = 0;
        g_ball[i].x = g_paddle.x + g_paddle.w * 0.5f;
        g_ball[i].y = g_paddle.y - g_ball[i].r - 1.f;
        g_ball[i].alive = (i == 0);  // only the first g_ball is alive
        g_ball[i].stuck = false;
		g_ball[i].spin = 0.f;
    }
    g_ballMax = 1;
    g_ballLaunched = false;
}

void KillAllBalls()
{
    for (int i = 0; i < BALL_CAP; ++i)
    {
        g_ball[i].alive = false;
        g_ball[i].spin = 0.f;
    }
    g_ballMax = 1;

    g_ballLaunched = false;
}

int FindActiveBall(){
    for (int i = 0; i < BALL_CAP; ++i)
    {
        if (g_ball[i].alive)
            return i;
    }
    return -1;
}

void SetActiveBallCount()
{
	int src = FindActiveBall();
    if (src == -1) { return; } // no active ball to clone from

	int count = 0;// only add balls up to g_ballMax

    for (int i = 0; i < BALL_CAP; ++i)
    {
        if (count < g_ballMax)
        {
            if (!g_ball[i].alive)
            {
                // Spawn this ball by cloning ball 0
                g_ball[i] = g_ball[src];
                g_ball[i].vx = (i & 1) ? g_ball[i].vx : -g_ball[i].vx;
                g_ball[i].alive = true;
				g_ball[i].stuck = false;
            }
			count++;
        }
        else
        {
            g_ball[i].alive = false;
        }
    }
}

struct LevelDef
{
    int rows;
    int cols;
    int brickPattern[BRICK_ROWS][BRICK_COLS];

    // Power-up rule per brick
    // -1  = no power-up
    //  0  = guaranteed random power-up
    // >0  = guaranteed specific power-up index
    int mustDropPowerUp[BRICK_ROWS][BRICK_COLS];

    // Brick descent pressure
    int descendIntervalFrames;   // 0 = no descent
    int descendAmount;           // pixels per descent
};

// Example Level (you can expand for more levels)
LevelDef g_levels[] =
{
    // Level 1
    {
        5, 10,
        {
            {1,1,1,1,1,1,1,1,1,1},
            {0,1,2,2,2,2,2,2,1,0},
            {0,0,1,3,3,3,3,1,0,0},
            {0,0,0,1,4,4,1,0,0,0},
            {0,0,0,0,5,5,0,0,0,0}
        },
        {// defined power-ups in specific bricks
            { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 
            {-1, 0, 0, 0, 0, 0, 0, 0, 0,-1},
            {-1,-1, 6 -1,-1,-1,-1, 6,-1,-1},
            {-1,-1,-1,11, 0, 0,11,-1,-1,-1},
            {-1,-1,-1,-1, 0, 0,-1,-1,-1,-1}
        },
        0,0
    },

    // Level 2 - can define another pattern
};

// Convenience
static const int g_levelCount = sizeof(g_levels) / sizeof(g_levels[0]);

// ------------------------------------------------------------
// InitBricks using LevelDef
// ------------------------------------------------------------
void InitBricksForLevel(int level)
{
    if (level < 1 || level > g_levelCount) level = g_levelCount; // clamp to last level
    const LevelDef& lvl = g_levels[level - 1];

    int index = 0;
    int totalW = lvl.cols * BRICK_W + (lvl.cols - 1) * BRICK_GAP;
    int startX = (g_backW - totalW) / 2;
    int startY = 40;

    for (int r = 0; r < lvl.rows; ++r)
    {
        for (int c = 0; c < lvl.cols; ++c)
        {
            Brick& b = g_bricks[index++];
            int x = startX + c * (BRICK_W + BRICK_GAP);
            int y = startY + r * (BRICK_H + BRICK_GAP);

            b.rect = { x, y, x + BRICK_W, y + BRICK_H };

            int baseHits = lvl.brickPattern[r][c];
            if (baseHits == 0)
            {
                b.alive = false;
                continue;
            }

            // Add some randomness on top of base hits
            int hits = baseHits + (rand() % 2); // +0 or +1
            b.hits = Clamp(hits, 1, 5);

            b.color = GetBrickColor(b.hits);
            b.alive = true;

            // Optional: attach must-drop power-up flag
            if (lvl.mustDropPowerUp[r][c])
            {
                // Tag the brick or just spawn power-up when destroyed
                // We'll handle it in HandleBrickCollisions
                // Example: store in hits = -hits to mark?
                // Or just check lvl data in collision
            }
        }
    }

    // Mark remaining bricks dead
    for (; index < BRICK_ROWS * BRICK_COLS; ++index)
        g_bricks[index].alive = false;
}

void InitGame()
{
    g_score = 0;
    g_lives = 3;
    g_level = 1;
    g_gameOver = false;
    InitPaddle();
    InitBall();
    InitBricksForLevel(g_level);
}

// ============================================================
// PowerUp / Effect Logic
// ============================================================
struct PowerUpDef
{
    const char* name;                     // For debugging / display
    COLORREF color;                       // Display color
    void (*applyFunc)();                   // Function to apply effect
    void (*revertFunc)();                 // Function to revert effect
    int durationFrames;                    // 0 = instant, >0 = timed
};

// Forward Declarations (Power-Up Spawning)

void SpawnPowerUp(float x, float y);
void SpawnPowerUp(float x, float y, int index);

// Declarations of effect functions

void ForEachAliveBall(void (*func)(Ball&))
{
    for (int i = 0; i < BALL_CAP; ++i)
    {
        if (!g_ball[i].alive)
            continue;

        func(g_ball[i]);
    }
}

void EffectBallFast() {                                                //1
    ForEachAliveBall([](Ball& b)
        {
            b.vx *= 1.5f;
            b.vy *= 1.5f;
        });
}
void EffectBallSlow() {                                                 //2
    ForEachAliveBall([](Ball& b)
        {
            b.vx *= 0.7f;
            b.vy *= 0.7f;
        });
}
void EffectBallBig() {                                                  //3
    ForEachAliveBall([](Ball& b)
        {
            b.r = BASE_BALL_RADIUS * 1.5f;
            b.penetrateMax = 2;
            b.penetrateCount = 2;
		});
}
void EffectBallSmall() {                                                //4
    ForEachAliveBall([](Ball& b)
        {
            b.r = BASE_BALL_RADIUS * 0.7f;
            b.penetrateMax = 0;
            b.penetrateCount = 0;
        });
}
void EffectBallSpin() { g_spin = true; }                         //5
void EffectMultiBall() { g_ballMax = 3; SetActiveBallCount(); }         //6
void EffectMultiRare() { g_ballMax = 6; SetActiveBallCount(); }         //7
void EffectWreakingBall() {                                             //8
    ForEachAliveBall([](Ball& b)
        {
            b.r = BASE_BALL_RADIUS * 3.0f;
            b.penetrateMax = 100;
            b.penetrateCount = 100;
        });
}
void EffectPaddleWide() { g_paddle.w *= 1.5f; }                         //9
void EffectPaddleNarrow() { g_paddle.w *= 0.7f; }                       //10
void EffectSticky() { g_stickyPaddle = true; }                          //11
void invulnerable() { g_invulnerable = true; }                          //12
void EffectAddLife() { g_lives++; }                                     //13
void EffectChaos() {                                                    //14
    const int CHAOS_DROPS = 20;

    for (int i = 0; i < CHAOS_DROPS; ++i)
    {
        float x = (float)(rand() % g_backW);
        float y = 0.f; // or brick center, or paddle height
        SpawnPowerUp(x, y);
    }
}

// ------------------------------------------------------------
// Revert Functions for Timed Effects
// ------------------------------------------------------------
void RevertBallFast() {
    ForEachAliveBall([](Ball& b)
        {
            b.vx /= 1.5f;
            b.vy /= 1.5f;
        });
}
void RevertBallSlow() {
    ForEachAliveBall([](Ball& b)
        {
            b.vx /= 0.7f;
            b.vy /= 0.7f;
        });
}
void RevertBallBig() {
    ForEachAliveBall([](Ball& b)
        {
            b.r = BASE_BALL_RADIUS;
            b.penetrateMax = 0;
            b.penetrateCount = 0;
        });
}
void RevertBallSmall() {
    ForEachAliveBall([](Ball& b)
        {
            b.r = BASE_BALL_RADIUS;
        });
}
void RevertBallSpin() { g_spin = false; }
void RevertWreakingBall() {
    ForEachAliveBall([](Ball& b)
        {
            b.r = BASE_BALL_RADIUS;
            b.penetrateMax = 0;
            b.penetrateCount = 0;
        });
}
void RevertPaddleWide() { g_paddle.w /= 1.5f; }
void RevertPaddleNarrow() { g_paddle.w /= 0.7f; }
void RevertSticky()
{
    g_stickyPaddle = false;
	bool anyStuck = false;

    // Auto-launch any stuck balls
    for (int i = 0; i < g_ballMax; ++i)
    {
        Ball& b = g_ball[i];
        if (!b.alive || !b.stuck) continue;

        b.stuck = false;

		//If velocity is zero, give it an initial launch
        if (b.vx == 0.f && b.vy == 0.f) {
            b.vx = (i & 1) ? BASE_BALL_SPEED : -BASE_BALL_SPEED;
            b.vy = -BASE_BALL_SPEED;
        }
		anyStuck = true;
    }
    if (anyStuck) {
        g_ballLaunched = true;
    }
}

void RevertInvulnerable() { g_invulnerable = false; }

// All power-ups are defined here, in one array
static PowerUpDef g_powerUps[] =
{
    { "Ball Fast",    RGB(255, 0, 255),  EffectBallFast,   RevertBallFast,    600 },
    { "Ball Slow",    RGB(0, 255, 255),  EffectBallSlow,   RevertBallSlow,    600 },
    { "Ball Big",     RGB(255, 255, 0),  EffectBallBig,    RevertBallBig,     600 },
    { "Ball Small",   RGB(0, 0, 255),    EffectBallSmall,  RevertBallSmall,   600 },
    { "Ball Spin",    RGB(255, 165, 0),  EffectBallSpin,   RevertBallSpin,    600 },
    { "Multi Ball",   RGB(128, 0, 128),  EffectMultiBall,  nullptr,           0 },
    { "Multi Rare",   RGB(75, 0, 130),   EffectMultiRare,  nullptr,           0 },
    { "Wreaking Ball",RGB(255, 20, 147), EffectWreakingBall,nullptr,          0 },
    { "Paddle Wide",  RGB(0, 255, 0),    EffectPaddleWide, RevertPaddleWide,  600 },
    { "Paddle Narrow",RGB(255, 140, 0),  EffectPaddleNarrow,RevertPaddleNarrow,600 },
    { "Sticky Paddle",RGB(34, 139, 34),  EffectSticky,     RevertSticky,      600 },
    { "Invulnerable", RGB(255, 215, 0),  invulnerable,     RevertInvulnerable,600 },
	{ "Chaos",        RGB(220, 20, 60),  EffectChaos,      nullptr,           0 },
    { "Add Life",     RGB(255, 0, 0),    EffectAddLife,    nullptr,           0 },
};

static const int g_powerUpCount = sizeof(g_powerUps) / sizeof(g_powerUps[0]);

struct ActivePowerUp
{
    const PowerUpDef * def = nullptr;
    int timer = 0;
};

static ActivePowerUp g_activePowerUps[10]; // allow up to 10 concurrent timed power-ups

struct FallingPowerUp
{
    int index = -1;   // which power-up type
    float x = 0.f;
    float y = 0.f;
    bool alive = false;
};

static const int MAX_FALLING_POWERUPS = 20;
static FallingPowerUp g_fallingPowerUps[MAX_FALLING_POWERUPS];

int RandomPowerUpIndex()
{
    return rand() % g_powerUpCount;
}

void SpawnPowerUp(float x, float y)
{
    int slot = -1;
    for (int i = 0; i < MAX_FALLING_POWERUPS; ++i)
    {
        if (!g_fallingPowerUps[i].alive)
        {
            slot = i;
            break;
        }
    }
    if (slot == -1) return; // no free slot

    g_fallingPowerUps[slot].alive = true;
    g_fallingPowerUps[slot].x = x;
    g_fallingPowerUps[slot].y = y;
    g_fallingPowerUps[slot].index = RandomPowerUpIndex();
}

void SpawnPowerUp(float x, float y, int index)
{
    if (index < 0 || index >= g_powerUpCount) return;

    int slot = -1;
    for (int i = 0; i < MAX_FALLING_POWERUPS; ++i)
    {
        if (!g_fallingPowerUps[i].alive)
        {
            slot = i;
            break;
        }
    }
    if (slot == -1) return;

    g_fallingPowerUps[slot].alive = true;
    g_fallingPowerUps[slot].x = x;
    g_fallingPowerUps[slot].y = y;
    g_fallingPowerUps[slot].index = index;
}

ActivePowerUp* FindActivePowerUp(const PowerUpDef* def)
{
    for (int i = 0; i < 10; ++i)
    {
        if (g_activePowerUps[i].def == def && g_activePowerUps[i].timer > 0)
            return &g_activePowerUps[i];
    }
    return nullptr;
}

void ApplyPowerUp(int index)
{
    if (index < 0 || index >= g_powerUpCount) return;
    const PowerUpDef* def = &g_powerUps[index];

	// Instant effect
    if (def->durationFrames == 0)
    {
        def->applyFunc();
        return;
    }

    // Timed effect
    ActivePowerUp* existing = FindActivePowerUp(def);
    if (existing)
    {
        // refresh timer only
        existing->timer = def->durationFrames;
        return;
    }

    // find empty slot
    for (int i = 0; i < 10; ++i)
    {
        if (g_activePowerUps[i].timer <= 0)
        {
            g_activePowerUps[i].def = def;
            g_activePowerUps[i].timer = def->durationFrames;
            def->applyFunc();
            break;
        }
    }
}

void UpdateFallingPowerUps()
{
    const float FALL_SPEED = 2.f;

    for (int i = 0; i < MAX_FALLING_POWERUPS; ++i)
    {
        FallingPowerUp& pu = g_fallingPowerUps[i];
        if (!pu.alive) continue;

        pu.y += FALL_SPEED;

        RECT paddleRect = { (LONG)g_paddle.x, (LONG)g_paddle.y,
                            (LONG)(g_paddle.x + g_paddle.w), (LONG)(g_paddle.y + g_paddle.h) };

        if (CircleRectIntersect(pu.x, pu.y, 8.f, paddleRect))
        {
            ApplyPowerUp(pu.index);
            pu.alive = false;
        }

        // Remove if it falls off screen
        if (pu.y > g_backH + 10.f)
            pu.alive = false;
    }
}

void UpdateActivePowerUps()
{
    for (int i = 0; i < 10; ++i)
    {
        ActivePowerUp& apu = g_activePowerUps[i];
        if (apu.timer > 0)
        {
            apu.timer--;
            if (apu.timer == 0)
            {
                // Remove effect when timer ends
                if (apu.def && apu.def->revertFunc)
                    apu.def->revertFunc();

                apu.def = nullptr;
            }
        }
    }
}

// ============================================================
// Update / Game Logic
// ============================================================

void HandleInput()
{
    g_paddlePrevX = g_paddle.x;
    if (GetAsyncKeyState(VK_LEFT)) g_paddle.x -= PADDLE_SPEED;
    if (GetAsyncKeyState(VK_RIGHT)) g_paddle.x += PADDLE_SPEED;
    g_paddle.x = Clamp(g_paddle.x, 0.f, (float)g_backW - g_paddle.w);
    g_paddleVX = g_paddle.x - g_paddlePrevX;
}

void HandleLaunchInput()
{
    if (g_gameOver && (GetAsyncKeyState('R') & 0x8000))
        InitGame();

    if (!g_gameOver && !g_ballLaunched && (GetAsyncKeyState(VK_SPACE) & 0x8000)){

        g_ballLaunched = true;
    }
}

void UpdateBall()
{
    int aliveCount = 0;

    if (g_ballLaunched)
    {
        for (int i = 0; i < g_ballMax; ++i)
        {
            Ball& b = g_ball[i];
            if (b.alive && b.stuck)
            {
                b.stuck = false;
                //Force launch direction and velocity
				b.vx = (i & 1) ? BASE_BALL_SPEED * 0.5f : BASE_BALL_SPEED * 0.7f;
				b.vy = -BASE_BALL_SPEED;
            }
        }
    }
    for (int i = 0; i < g_ballMax; ++i)
    {
        Ball& b = g_ball[i];
        if (!b.alive) continue;

        // --- Sticky paddle hold ---
        if (b.stuck || !g_ballLaunched)
        {
            b.x = g_paddle.x + g_paddle.w * 0.5f;
            b.y = g_paddle.y - b.r - 1.f;
            continue; // skip motion
        }

        // Apply spin curve
        b.vx += b.spin * 0.02f;
        // Spin decay
        b.spin *= 0.995f;
 
        // --- Normal movement ---
        b.x += b.vx;
        b.y += b.vy;
        // Left / Right walls
        if (b.x - b.r < 0)
        {
            b.x = b.r;
            b.vx = -b.vx;
        }
        else if (b.x + b.r > g_backW)
        {
            b.x = g_backW - b.r;
            b.vx = -b.vx;
        }
        // Top wall
        if (b.y - b.r < 0)
        {
            b.y = b.r;
            b.vy = -b.vy;
        }
        // Bottom (ball lost)
        if (b.y - b.r > g_backH)
        {
            b.alive = false;
            continue;
        }
        // Only count balls that are alive and on-screen
		aliveCount++;
    }
    // If ALL balls are gone ? lose life
    if (aliveCount == 0 && g_ballLaunched)
    {
        g_lives--;

        if (g_lives <= 0)
        {
            g_lives = 0;
            g_gameOver = true;
            g_ballLaunched = false;
        }
        else
        {
			KillAllBalls();
            InitBall();       // respawn base balls
            g_ballLaunched = false;
        }
    }
}

void HandlePaddleCollision()
{
    if (!g_ballLaunched) return;

    RECT paddleRect = { (LONG)g_paddle.x, (LONG)g_paddle.y,
                        (LONG)(g_paddle.x + g_paddle.w), (LONG)(g_paddle.y + g_paddle.h) };

    for (int i = 0; i < g_ballMax; ++i)
    {
        Ball& ball = g_ball[i];
        if (!ball.alive) continue;
        if (ball.vy <= 0.f) continue;

        if (!CircleRectIntersect(ball.x, ball.y, ball.r, paddleRect))
            continue;

        // Sticky paddle: stop ball until relaunch
        if (g_stickyPaddle)
        {
		    ball.stuck = true;
            g_ballLaunched = false;
            continue;
        }
        // Calculate hit position (-1 .. 1)
        float hit =
            (ball.x - (g_paddle.x + g_paddle.w * 0.5f)) /
            (g_paddle.w * 0.5f);
        hit = Clamp(hit, -1.f, 1.f);

        const float DEAD_ZONE = 0.2f;
        const float MAX_ANGLE = 70.f * 3.14159f / 180.f;
        float angleFactor = 0.f;

        if (fabsf(hit) < DEAD_ZONE) { angleFactor = hit * 0.25f; }
        else
        {
            float sign = (hit < 0.f) ? -1.f : 1.f;
            float t = (fabsf(hit) - DEAD_ZONE) / (1.f - DEAD_ZONE);
            angleFactor = sign * (t * t);
        }

        float speed = sqrtf(ball.vx * ball.vx + ball.vy * ball.vy);
        float angle = angleFactor * MAX_ANGLE;

        ball.vx = sinf(angle) * speed;
        ball.vy = -cosf(angle) * speed;

        if (g_spin)
        {
            ball.spin += g_paddleVX * 0.05f;
            ball.spin = Clamp(ball.spin, -1.f, 1.f);
        }
    }
}

void HandleBrickCollisions()
{
    if (!g_ballLaunched) return;

    int lvlIndex = g_level - 1;
    if (lvlIndex < 0) lvlIndex = 0;
    if (lvlIndex >= g_levelCount) lvlIndex = g_levelCount - 1;

    const LevelDef& lvl = g_levels[lvlIndex];

    for (int b = 0; b < g_ballMax; ++b)
    {
        Ball& ball = g_ball[b];
        if (!ball.alive) continue;

        for (int i = 0; i < BRICK_ROWS * BRICK_COLS; ++i)
        {
            Brick& brick = g_bricks[i];
            if (!brick.alive) continue;
            if (!CircleRectIntersect(ball.x, ball.y, ball.r, brick.rect)) continue;


            // Handle brick penetration and destruction
            if (ball.penetrateCount > 0) {
                brick.alive = false;
                brick.hits = 0;
                ball.penetrateCount--; // decrement penetration
                g_score += 100;
            }

            //Handle normal brick hits
            else {
                brick.hits--;
                if (brick.hits <= 0)
                {
                    brick.alive = false;
                    g_score += 100;
                }
                else
                {
                    brick.color = GetBrickColor(brick.hits);
                    g_score += 25;
                }
                if (!brick.alive) {
                    int row = i / BRICK_COLS;
                    int col = i % BRICK_COLS;

                    // Get the power-up rule for this brick
                    int puRule = -1;
                    if (row < lvl.rows && col < lvl.cols)
                    { puRule = lvl.mustDropPowerUp[row][col];}

                    // Decide which power-up to spawn, if any
                    bool shouldDrop = false;

                    if (puRule > 0){
                            shouldDrop = true;
                    }
                    else if (puRule == 0) {
                        shouldDrop = (rand() % 5) == 0; // 20%
                       }
                    else {shouldDrop = false;}

                    if (shouldDrop)
                    {
                        float px = (brick.rect.left + brick.rect.right) * 0.5f;
                        float py = (brick.rect.top + brick.rect.bottom) * 0.5f;

                        if (puRule > 0) {
                            SpawnPowerUp(px, py, puRule - 1);
                        }
                        else {
                            SpawnPowerUp(px, py);
                        }
                    }
                }

                // Reflect ball only if no penetrateCount left
                if (ball.penetrateCount == 0)
                {

                    // Determine collision side
                    float left = ball.x - brick.rect.left;
                    float right = brick.rect.right - ball.x;
                    float top = ball.y - brick.rect.top;
                    float bottom = brick.rect.bottom - ball.y;

                    if (min(left, right) < min(top, bottom)) { ball.vx = -ball.vx; }
                    else
                    {
                        ball.vy = -ball.vy;
                        ball.vx += ball.spin * 0.2f;
                        ball.penetrateCount = ball.penetrateMax; // reset penetrate count after reflection
                    }
                }
            }
        }
    }
}

bool AreAllBricksCleared()
{
    for (int i = 0; i < BRICK_ROWS * BRICK_COLS; ++i)
        if (g_bricks[i].alive) return false;
    return true;
}

void CheckLevelCompletion()
{
    if (g_gameOver) return;

    static bool levelAdvancePending = false;

    if (!levelAdvancePending && AreAllBricksCleared())
    {
        levelAdvancePending = true;
        g_ballLaunched = false;
    }

    if (levelAdvancePending && !g_ballLaunched)
    {
        g_level++;
        InitBricksForLevel(g_level);
		KillAllBalls();
        InitBall();
        levelAdvancePending = false;
    }
}

void UpdateGame()
{
    HandleInput();
    HandleLaunchInput();
    UpdateBall();

    if (g_ballLaunched)
    {
        HandlePaddleCollision();
        HandleBrickCollisions();
    }
    UpdateFallingPowerUps();      // <--- added
    UpdateActivePowerUps();       // <--- added
    CheckLevelCompletion();
}

// ============================================================
// Rendering
// ============================================================

void Render(HDC hdc){

// Clear background
PatBlt(hdc, 0, 0, g_backW, g_backH, BLACKNESS);

// Draw bricks
for (int i = 0; i < BRICK_ROWS * BRICK_COLS; ++i)
{
    Brick& b = g_bricks[i];
    if (!b.alive) continue;

    HBRUSH brush = CreateSolidBrush(b.color);
    HBRUSH old = (HBRUSH)SelectObject(hdc, brush);
    Rectangle(hdc, b.rect.left, b.rect.top, b.rect.right, b.rect.bottom);
    SelectObject(hdc, old);
    DeleteObject(brush);
}

// Draw paddle
Rectangle(hdc, (int)g_paddle.x, (int)g_paddle.y,
    (int)(g_paddle.x + g_paddle.w), (int)(g_paddle.y + g_paddle.h));

// Draw all active balls
for (int i = 0; i < g_ballMax; ++i)
{
    Ball& ball = g_ball[i];
    if (!ball.alive) continue;

    Ellipse(hdc,
        (int)(ball.x - ball.r),
        (int)(ball.y - ball.r),
        (int)(ball.x + ball.r),
        (int)(ball.y + ball.r));
}

// Draw power-ups

for (int i = 0; i < MAX_FALLING_POWERUPS; ++i)
{
    FallingPowerUp& pu = g_fallingPowerUps[i];
    if (!pu.alive) continue;

    HBRUSH brush = CreateSolidBrush(g_powerUps[pu.index].color);
    HBRUSH old = (HBRUSH)SelectObject(hdc, brush);
    Ellipse(hdc, (int)(pu.x - 8), (int)(pu.y - 8), (int)(pu.x + 8), (int)(pu.y + 8));
    SelectObject(hdc, old);
    DeleteObject(brush);
}

// Draw score, lives, level
char buf[64];
SetBkMode(hdc, TRANSPARENT);
SetTextColor(hdc, RGB(250, 250, 250));
sprintf_s(buf, sizeof(buf), "Score: %d", g_score);
TextOutA(hdc, 10, 10, buf, (int)strlen(buf));
sprintf_s(buf, sizeof(buf), "Lives: %d", g_lives);
TextOutA(hdc, 170, 10, buf, (int)strlen(buf));
sprintf_s(buf, sizeof(buf), "Level: %d", g_level);
TextOutA(hdc, 340, 10, buf, (int)strlen(buf));

// Game Over message
if (g_gameOver)
{
    const char* msg = "GAME OVER! Press R to Restart";
    int len = (int)strlen(msg);
    int x = g_backW / 2 - (len * 4);
    int y = g_backH / 2;
    TextOutA(hdc, x, y, msg, len);
}
}

// ============================================================
// Win32 Boilerplate
// ============================================================

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
    {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        if (w > 0 && h > 0) CreateBackBuffer(hwnd, w, h);
        return 0;
    }
    case WM_DESTROY:
        DestroyBackBuffer();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"BreakoutWindow";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClass(&wc);

    HWND hwnd = CreateWindow(
        wc.lpszClassName,
        L"Breakout - Clean Version",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT,
        SCREEN_W, SCREEN_H,
        NULL, NULL, hInst, NULL);

    ShowWindow(hwnd, SW_SHOW);

    RECT rc; GetClientRect(hwnd, &rc);
    CreateBackBuffer(hwnd, rc.right, rc.bottom);

    srand((unsigned int)time(NULL));
    InitGame();

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            UpdateGame();
            Render(g_backDC);

            HDC hdc = GetDC(hwnd);
            BitBlt(hdc, 0, 0, g_backW, g_backH, g_backDC, 0, 0, SRCCOPY);
            ReleaseDC(hwnd, hdc);

            Sleep(16); // ~60 FPS
        }
    }

    return 0;
}


