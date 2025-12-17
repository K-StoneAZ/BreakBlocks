// ============================================================
// Breakout - Phase 1+Levels
// Paddle + Ball + Bricks + Levels
// Win32 + GDI
// C++14
// ============================================================

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

// ============================================================
// Constants
// ============================================================

static const int SCREEN_W = 800;
static const int SCREEN_H = 600;

static const float PADDLE_W = 100.f;
static const float PADDLE_H = 15.f;
static const float PADDLE_SPEED = 6.f;

static const float BALL_RADIUS = 6.f;
static const float BALL_SPEED = 5.f;

static const int BRICK_ROWS = 5;
static const int BRICK_COLS = 10;
static const int BRICK_W = 70;
static const int BRICK_H = 20;
static const int BRICK_GAP = 6;

// ============================================================
// Math / Utility
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
    // Base difficulty grows slowly
    int base = 1 + level / 2;

    // Clamp base
    if (base < 1) base = 1;
    if (base > 4) base = 4;

    // Random spread around base
    int roll = rand() % 100;

    if (roll < 50)
        return base;          // 50%
    else if (roll < 80)
        return base + 1;      // 30%
    else
        return base + 2;      // 20%
}

// ============================================================
// Game Objects
// ============================================================

struct Paddle
{
    float x, y;
    float w, h;
};

struct Ball
{
    float x, y;
    float vx, vy;
    float r;
};

struct Brick
{
    RECT rect;
    int  hits;      // how many hits remaining
    COLORREF color; // visual representation
    bool alive;
};

enum PowerUpType
{
    POWER_NONE = 0,
    POWER_BALL_SPEED_UP,
    POWER_BALL_SLOW,
    POWER_BALL_BIG,
    POWER_BALL_SMALL,
    POWER_PADDLE_WIDE
};

struct PowerUp
{
    float x, y;
    float vy;
    PowerUpType type;
    bool active;
};

// ============================================================
// Global Game State
// ============================================================

static Paddle g_paddle;
static Ball g_ball;
static bool g_ballLaunched = false;
static Brick g_bricks[BRICK_ROWS * BRICK_COLS];
static int g_score = 0;
static int g_lives = 3;
static bool g_gameOver = false;
static int g_level = 1;
static const int MAX_POWERUPS = 8;
static PowerUp g_powerUps[MAX_POWERUPS];

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
    g_ball.r = BALL_RADIUS;
    g_ball.vx = BALL_SPEED;
    g_ball.vy = -BALL_SPEED;
    g_ballLaunched = false;
}

void SpawnPowerUp(float x, float y)
{
    for (int i = 0; i < MAX_POWERUPS; ++i)
    {
        if (!g_powerUps[i].active)
        {
            g_powerUps[i].active = true;
            g_powerUps[i].x = x;
            g_powerUps[i].y = y;
            g_powerUps[i].vy = 2.5f;

            // Weighted random selection
            int roll = rand() % 100;
            if (roll < 30)      g_powerUps[i].type = POWER_BALL_BIG;
            else if (roll < 50) g_powerUps[i].type = POWER_PADDLE_WIDE;
            else if (roll < 65) g_powerUps[i].type = POWER_BALL_SPEED_UP;
            else if (roll < 80) g_powerUps[i].type = POWER_BALL_SLOW;
            else                g_powerUps[i].type = POWER_NONE;

            break;
        }
    }
}

void InitBricksForLevel(int level)
{
    int index = 0;

    int totalW = BRICK_COLS * BRICK_W + (BRICK_COLS - 1) * BRICK_GAP;
    int startX = (g_backW - totalW) / 2;
    int startY = 40;

    for (int r = 0; r < BRICK_ROWS; ++r)
    {
        for (int c = 0; c < BRICK_COLS; ++c)
        {
            Brick& b = g_bricks[index++];

            int x = startX + c * (BRICK_W + BRICK_GAP);
            int y = startY + r * (BRICK_H + BRICK_GAP);

            b.rect = { x, y, x + BRICK_W, y + BRICK_H };

            // ?? RIGHT HERE ??
            int hits = RollBrickHits(level);
            if (hits > 5) hits = 5;

            b.hits = hits;
            b.color = GetBrickColor(b.hits);
            b.alive = true;
        }
    }

    for (; index < BRICK_ROWS * BRICK_COLS; ++index)
        g_bricks[index].alive = false;
}

void ApplyPowerUp(PowerUpType type)
{
    switch (type)
    {
    case POWER_BALL_SPEED_UP:
        g_ball.vx *= 1.2f;
        g_ball.vy *= 1.2f;
        break;

    case POWER_BALL_SLOW:
        g_ball.vx *= 0.8f;
        g_ball.vy *= 0.8f;
        break;

    case POWER_BALL_BIG:
        g_ball.r *= 1.5f;
        break;

    case POWER_BALL_SMALL:
        g_ball.r *= 0.75f;
        break;

    case POWER_PADDLE_WIDE:
        g_paddle.w *= 1.4f;
        break;
    }
}

void RenderPowerUps(HDC hdc)
{
    for (int i = 0; i < MAX_POWERUPS; ++i)
    {
        PowerUp& p = g_powerUps[i];
        if (!p.active) continue;

        COLORREF color = RGB(255, 255, 255);

        switch (p.type)
        {
        case POWER_BALL_BIG:      color = RGB(0, 255, 0); break;
        case POWER_BALL_SMALL:    color = RGB(255, 0, 0); break;
        case POWER_BALL_SPEED_UP: color = RGB(255, 255, 0); break;
        case POWER_BALL_SLOW:     color = RGB(0, 0, 255); break;
        case POWER_PADDLE_WIDE:   color = RGB(255, 0, 255); break;
        }

        HBRUSH brush = CreateSolidBrush(color);
        HBRUSH old = (HBRUSH)SelectObject(hdc, brush);

        Ellipse(hdc,
            (int)(p.x - 8),
            (int)(p.y - 8),
            (int)(p.x + 8),
            (int)(p.y + 8));

        SelectObject(hdc, old);
        DeleteObject(brush);
    }
}

void InitGame()
{
    g_score = 0;
    g_lives = 3;
    g_level = 1;
    InitPaddle();
    InitBall();
    InitBricksForLevel(g_level);
}

// ============================================================
// Input
// ============================================================

void HandleInput()
{
    if (GetAsyncKeyState(VK_LEFT))
        g_paddle.x -= PADDLE_SPEED;

    if (GetAsyncKeyState(VK_RIGHT))
        g_paddle.x += PADDLE_SPEED;

    g_paddle.x = Clamp(g_paddle.x, 0.f, (float)g_backW - g_paddle.w);
}

void HandleLaunchInput()
{
    if (!g_ballLaunched && GetAsyncKeyState(VK_SPACE) & 0x8000)
        g_ballLaunched = true;

    if (g_gameOver && (GetAsyncKeyState('R') & 0x8000))
    {
        InitGame();
        InitBall();
    }
}

// ============================================================
// Update
// ============================================================

void UpdateBall()
{
    if (!g_ballLaunched)
    {
        g_ball.x = g_paddle.x + g_paddle.w * 0.5f;
        g_ball.y = g_paddle.y - g_ball.r - 1.f;
        return;
    }

    g_ball.x += g_ball.vx;
    g_ball.y += g_ball.vy;

    if (g_ball.x - g_ball.r < 0) { g_ball.x = g_ball.r; g_ball.vx = -g_ball.vx; }
    if (g_ball.x + g_ball.r > g_backW) { g_ball.x = g_backW - g_ball.r; g_ball.vx = -g_ball.vx; }
    if (g_ball.y - g_ball.r < 0) { g_ball.y = g_ball.r; g_ball.vy = -g_ball.vy; }

    if (g_ball.y - g_ball.r > g_backH)
    {
        g_lives--;
        InitBall();
        if (g_lives <= 0) g_gameOver = true;
    }
}

void UpdatePowerUps()
{
    RECT paddleRect =
    {
        (int)g_paddle.x,
        (int)g_paddle.y,
        (int)(g_paddle.x + g_paddle.w),
        (int)(g_paddle.y + g_paddle.h)
    };

    for (int i = 0; i < MAX_POWERUPS; ++i)
    {
        PowerUp& p = g_powerUps[i];
        if (!p.active) continue;

        p.y += p.vy;

        // Off screen
        if (p.y > g_backH)
        {
            p.active = false;
            continue;
        }

        // Paddle catch
        RECT puRect =
        {
            (int)(p.x - 8),
            (int)(p.y - 8),
            (int)(p.x + 8),
            (int)(p.y + 8)
        };

        if (IntersectRect(nullptr, &paddleRect, &puRect))
        {
            ApplyPowerUp(p.type);
            p.active = false;
        }
    }
}

void HandlePaddleCollision()
{
    if (g_ball.vy <= 0) return;

    RECT paddleRect =
    {
        (int)g_paddle.x,
        (int)g_paddle.y,
        (int)(g_paddle.x + g_paddle.w),
        (int)(g_paddle.y + g_paddle.h)
    };

    if (!CircleRectIntersect(g_ball.x, g_ball.y, g_ball.r, paddleRect))
        return;

    // Move ball above paddle
    g_ball.y = g_paddle.y - g_ball.r;

    // Normalize hit position to [-1, 1]
    float hit = (g_ball.x - (g_paddle.x + g_paddle.w * 0.5f)) / (g_paddle.w * 0.5f);
    hit = Clamp(hit, -1.f, 1.f);

    // --- Tuning parameters ---
    const float DEAD_ZONE = 0.2f;        // 25% center zone
    const float MAX_ANGLE = 70.f * 3.14159f / 180.f;

    float angleFactor = 0.f;

    if (fabsf(hit) < DEAD_ZONE)
    {
        // Soft center: mostly vertical
        angleFactor = hit * 0.25f;
    }
    else
    {
        // Edge response (rescaled outside dead zone)
        float sign = (hit < 0.f) ? -1.f : 1.f;
        float t = (fabsf(hit) - DEAD_ZONE) / (1.f - DEAD_ZONE);

        // Quadratic curve for smoother control
        angleFactor = sign * (t * t);
    }

    float speed = sqrtf(g_ball.vx * g_ball.vx + g_ball.vy * g_ball.vy);

    float angle = angleFactor * MAX_ANGLE;

    g_ball.vx = sinf(angle) * speed;
    g_ball.vy = -cosf(angle) * speed;
}

void HandleBrickCollisions()
{
    if (!g_ballLaunched) return;

    for (int i = 0; i < BRICK_ROWS * BRICK_COLS; ++i)
    {
        Brick& b = g_bricks[i];
        if (!b.alive) continue;

        if (!CircleRectIntersect(g_ball.x, g_ball.y, g_ball.r, b.rect))
            continue;

        // Penetration resolution (unchanged)
        float left = g_ball.x - b.rect.left;
        float right = b.rect.right - g_ball.x;
        float top = g_ball.y - b.rect.top;
        float bottom = b.rect.bottom - g_ball.y;

        if (min(left, right) < min(top, bottom))
        {
            g_ball.vx = -g_ball.vx;
        }
        else
        {
            g_ball.vy = -g_ball.vy;
        }

        // Apply damage
        b.hits--;
        if (b.hits <= 0)
        {
            b.alive = false;
            g_score += 100;
            // 25% chance to spawn
            if ((rand() % 100) < 25)
            {
                float cx = (b.rect.left + b.rect.right) * 0.5f;
                float cy = (b.rect.top + b.rect.bottom) * 0.5f;
                SpawnPowerUp(cx, cy);
            }
        }
        else
        {
            b.color = GetBrickColor(b.hits);
            g_score += 25; // reward partial hits
        }

        break;
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
    if (g_gameOver) return; // skip if player lost

    static bool levelAdvancePending = false;

    // Only check when the ball is launched and there are no bricks
    if (!levelAdvancePending && AreAllBricksCleared())
    {
        levelAdvancePending = true; // mark that we need to advance next frame
        g_ballLaunched = false;     // stop ball immediately
    }

    // Advance level once per frame after pending flag
    if (levelAdvancePending && !g_ballLaunched)
    {
        g_level++;
        InitBricksForLevel(g_level);
        InitBall();
        levelAdvancePending = false;
    }
}

void UpdateGame()
{
    HandleInput();
    HandleLaunchInput();
    UpdateBall();
	UpdatePowerUps();

    if (g_ballLaunched)
    {
        HandlePaddleCollision();
        HandleBrickCollisions();
    }

    CheckLevelCompletion();
}

// ============================================================
// Render
// ============================================================

void Render(HDC hdc)
{
    PatBlt(hdc, 0, 0, g_backW, g_backH, BLACKNESS);

    // Bricks
    for (int i = 0; i < BRICK_ROWS * BRICK_COLS; ++i)
    {
        Brick& b = g_bricks[i];
        if (!b.alive) continue;

        HBRUSH brush = CreateSolidBrush(b.color);
        HBRUSH old = (HBRUSH)SelectObject(hdc, brush);

        Rectangle(hdc,
            b.rect.left,
            b.rect.top,
            b.rect.right,
            b.rect.bottom);

        SelectObject(hdc, old);
        DeleteObject(brush);
    }

    // Paddle
    Rectangle(hdc, (int)g_paddle.x, (int)g_paddle.y,
        (int)(g_paddle.x + g_paddle.w), (int)(g_paddle.y + g_paddle.h));

    // Ball
    Ellipse(hdc, (int)(g_ball.x - g_ball.r), (int)(g_ball.y - g_ball.r),
        (int)(g_ball.x + g_ball.r), (int)(g_ball.y + g_ball.r));

	// Power-Ups
	RenderPowerUps(hdc);

    // Score, Lives, Level
    char scoreText[32]; sprintf_s(scoreText, sizeof(scoreText), "Score: %d", g_score);
    TextOutA(hdc, 10, 10, scoreText, (int)strlen(scoreText));

    char livesText[32]; sprintf_s(livesText, sizeof(livesText), "Lives: %d", g_lives);
    TextOutA(hdc, 170, 10, livesText, (int)strlen(livesText));

    char levelText[32]; sprintf_s(levelText, sizeof(levelText), "Level: %d", g_level);
    TextOutA(hdc, 340, 10, levelText, (int)strlen(levelText));

    // Game Over / Win Message
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
        L"Breakout - Breakout Levels",
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

            Sleep(16);
        }
    }

    return 0;
}