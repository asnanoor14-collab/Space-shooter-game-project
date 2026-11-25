// Space Shooter - Raylib edition (fixed for raylib 6.x)
// C++17 + raylib 6.2. Single-source-file implementation for the PF mini project.
// Controls: A/D or arrows = move, SPACE = fire/continue, P = pause, ESC = restart level, M = main menu.
// Touch / Mouse: Tap menu items, buttons, or on-screen touch controls (Left, Right, Fire, Pause, Restart).

#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <utility>
#include <vector>

static constexpr int SCREEN_W = 1200;
static constexpr int SCREEN_H = 800;
static constexpr int PLAY_X = 35;
static constexpr int PLAY_Y = 85;
static constexpr int PLAY_W = 850;
static constexpr int PLAY_H = 670;
static constexpr int PANEL_X = 910;
static constexpr int PANEL_W = 255;

static constexpr int MAX_BULLETS = 160;
static constexpr int MAX_ENEMY_BULLETS = 90;
static constexpr int MAX_ENEMIES = 30;
static constexpr int MAX_POWERUPS = 12;
static constexpr int MAX_POPUPS = 40;
static constexpr int MAX_STARS = 240;
static constexpr int MAX_EFFECTS = 90;
static constexpr int MAX_LEVEL = 10;
static constexpr int MAX_HIGH_SCORES = 5;
static constexpr int MISS_LIMIT = 7;          // escaped enemies that cost one life
static constexpr int MAX_LIVES = 5;

static constexpr float AID_NORMAL_DELAY = 18.0f;
static constexpr float AID_URGENT_DELAY = 11.0f; // used when only 1 life is left
static constexpr float AID_WARNING_TIME = 5.0f;  // "AID CRATE INCOMING" starts this many seconds before
static constexpr float COMBO_DURATION = 3.2f;

struct Player {
    Vector2 pos;
    float speed;
    int lives;
    float fireTimer;
    float invulnerable;
    float rapidFireTimer;
    bool hasShield;
};

struct Bullet { Vector2 pos; Vector2 vel; bool active; };
struct EnemyBullet { Vector2 pos; Vector2 vel; bool active; Color color; };
struct Enemy { Vector2 pos; int type; bool active; float speed; float phase; float shootTimer; };
struct PowerUp { Vector2 pos; int type; bool active; float speed; float pulse; }; // 0 = Rapid Fire, 1 = Shield
struct ScorePopup { Vector2 pos; char text[24]; float age; bool active; Color color; };
struct Aid { Vector2 pos; bool active; float speed; };
struct Star { Vector2 pos; float speed; float radius; float twinkle; int layer; };
struct Effect { Vector2 pos; float age; bool active; Color color; };

enum GameScreen { MENU, INSTRUCTIONS, HIGH_SCORE, PLAYING, PAUSED, LEVEL_COMPLETE, GAME_OVER, YOU_WIN };

Player player{};
Bullet bullets[MAX_BULLETS]{};
EnemyBullet enemyBullets[MAX_ENEMY_BULLETS]{};
Enemy enemies[MAX_ENEMIES]{};
PowerUp powerUps[MAX_POWERUPS]{};
ScorePopup popups[MAX_POPUPS]{};
Star stars[MAX_STARS]{};
Effect effects[MAX_EFFECTS]{};
Aid aid{};

GameScreen screen = MENU;
int menuChoice = 0;
int level = 1;
int score = 0;
int levelStartScore = 0;
int levelStartLives = 3;
int highScore = 0;
std::vector<int> highScores;
int missedEnemies = 0;
int specialHitsRequired = 10;
bool newHighScore = false;
int specialHitsTaken = 0;
bool specialSpawnedThisLevel = false;
bool specialAlive = false;

int comboCount = 0;
int comboMultiplier = 1;
float comboTimer = 0.0f;
float shakeTimer = 0.0f;
float shakeIntensity = 0.0f;

float spawnTimer = 0.0f;
float aidTimer = AID_NORMAL_DELAY;
float gameTime = 0.0f;       // advances only while the game is actually running (not paused)
int lastLevelReached = 1;

// Filled triangle that works whatever the vertex order is.
void fillTri(Vector2 a, Vector2 b, Vector2 c, Color color) {
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross > 0.0f) std::swap(b, c);
    DrawTriangle(a, b, c, color);
}

// One distinct colour per level (used for border, panel accents and nebula glow).
Color levelColor(int lv) {
    float hue = fmodf(210.0f + (lv - 1) * 36.0f, 360.0f);
    return ColorFromHSV(hue, 0.68f, 1.0f);
}

Color enemyColor(int type) {
    if (type == 0) return Color{ 255, 75, 85, 255 };    // red spike drone
    if (type == 1) return Color{ 175, 195, 215, 255 };  // grey crystal rock
    return Color{ 195, 80, 255, 255 };                  // purple flower bug
}

int targetForLevel(int lv) { return 200 + (lv - 1) * 100; }   // 200 ... 1100

void syncHighScores() {
    std::sort(highScores.begin(), highScores.end(), std::greater<int>());
    if ((int)highScores.size() > MAX_HIGH_SCORES) {
        highScores.resize(MAX_HIGH_SCORES);
    }
    highScore = highScores.empty() ? 0 : highScores.front();
}

void loadHighScore() {
    highScores.clear();
    std::ifstream in("highscore.txt");
    int val = 0;
    while (in >> val) {
        if (val >= 0) highScores.push_back(val);
    }
    syncHighScores();
}

void saveHighScore() {
    syncHighScores();
    std::ofstream out("highscore.txt");
    if (out) {
        for (int s : highScores) {
            out << s << "\n";
        }
    }
}

void recordRunScore() {
    int prevBest = highScore;
    if (score > 0) {
        highScores.push_back(score);
        syncHighScores();
        if (score > prevBest) newHighScore = true;
    }
    saveHighScore();
}

void addScore(int points) {
    score += points;
    if (score > highScore) {
        highScore = score;
        newHighScore = true;
    }
}

void triggerShake(float duration, float intensity) {
    shakeTimer = std::max(shakeTimer, duration);
    shakeIntensity = std::max(shakeIntensity, intensity);
}

void spawnPopup(Vector2 p, const char* txt, Color c) {
    for (auto& pop : popups) {
        if (!pop.active) {
            pop.active = true;
            pop.pos = p;
            pop.age = 0.0f;
            pop.color = c;
            snprintf(pop.text, sizeof(pop.text), "%s", txt);
            return;
        }
    }
}

// --- Touch & Pointer Helpers ---
bool isPointInRect(Vector2 p, Rectangle r) {
    return CheckCollisionPointRec(p, r);
}

bool wasRectPressed(Rectangle r) {
    static int prevTouches = 0;
    int touches = GetTouchPointCount();
    bool triggered = false;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && isPointInRect(GetMousePosition(), r)) {
        triggered = true;
    }

    if (touches > prevTouches) {
        for (int i = 0; i < touches; ++i) {
            if (isPointInRect(GetTouchPosition(i), r)) {
                triggered = true;
                break;
            }
        }
    }
    return triggered;
}

bool isRectHeld(Rectangle r) {
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && isPointInRect(GetMousePosition(), r)) {
        return true;
    }
    int touches = GetTouchPointCount();
    for (int i = 0; i < touches; ++i) {
        if (isPointInRect(GetTouchPosition(i), r)) {
            return true;
        }
    }
    return false;
}

// --- Raylib Vector Icons ---
void drawPlayIcon(float cx, float cy, float size, Color c) {
    fillTri(
        Vector2{ cx - size * 0.35f, cy - size * 0.45f },
        Vector2{ cx - size * 0.35f, cy + size * 0.45f },
        Vector2{ cx + size * 0.50f, cy },
        c
    );
}

void drawBookIcon(float cx, float cy, float size, Color c) {
    float w = size * 0.85f;
    float h = size * 0.68f;
    DrawRectangleRounded(Rectangle{ cx - w * 0.5f, cy - h * 0.5f, w * 0.48f, h }, 0.15f, 4, c);
    DrawRectangleRounded(Rectangle{ cx + w * 0.02f, cy - h * 0.5f, w * 0.48f, h }, 0.15f, 4, c);
    DrawLineEx(Vector2{ cx, cy - h * 0.55f }, Vector2{ cx, cy + h * 0.55f }, 2.0f, DARKBLUE);
}

void drawTrophyIcon(float cx, float cy, float size, Color c) {
    float cupW = size * 0.62f;
    float cupH = size * 0.48f;
    DrawRectangleRounded(Rectangle{ cx - cupW * 0.5f, cy - size * 0.42f, cupW, cupH }, 0.35f, 6, c);
    DrawCircleLines((int)(cx - cupW * 0.55f), (int)(cy - size * 0.22f), size * 0.15f, c);
    DrawCircleLines((int)(cx + cupW * 0.55f), (int)(cy - size * 0.22f), size * 0.15f, c);
    DrawRectangle((int)(cx - size * 0.08f), (int)(cy + cupH - size * 0.42f), (int)(size * 0.16f), (int)(size * 0.22f), c);
    DrawRectangleRounded(Rectangle{ cx - size * 0.32f, cy + size * 0.26f, size * 0.64f, size * 0.14f }, 0.2f, 4, c);
}

void drawExitIcon(float cx, float cy, float size, Color c) {
    float r = size * 0.38f;
    DrawCircleLines((int)cx, (int)cy, r, c);
    DrawLineEx(Vector2{ cx, cy - r * 1.15f }, Vector2{ cx, cy + r * 0.05f }, 2.5f, c);
}

void drawPauseIcon(float cx, float cy, float size, Color c) {
    float barW = size * 0.22f;
    float barH = size * 0.72f;
    DrawRectangleRounded(Rectangle{ cx - size * 0.32f, cy - barH * 0.5f, barW, barH }, 0.2f, 4, c);
    DrawRectangleRounded(Rectangle{ cx + size * 0.10f, cy - barH * 0.5f, barW, barH }, 0.2f, 4, c);
}

void drawRestartIcon(float cx, float cy, float size, Color c) {
    float r = size * 0.36f;
    DrawCircleLines((int)cx, (int)cy, r, c);
    fillTri(
        Vector2{ cx + r * 0.6f, cy - r * 0.8f },
        Vector2{ cx + r * 1.25f, cy - r * 0.15f },
        Vector2{ cx + r * 0.45f, cy - r * 0.05f },
        c
    );
}

void drawArrowIcon(float cx, float cy, float size, int dir, Color c) {
    float d = (float)dir;
    fillTri(
        Vector2{ cx + d * size * 0.35f, cy - size * 0.42f },
        Vector2{ cx + d * size * 0.35f, cy + size * 0.42f },
        Vector2{ cx - d * size * 0.40f, cy },
        c
    );
}

void drawFireIcon(float cx, float cy, float size, Color c) {
    DrawCircleLines((int)cx, (int)cy, size * 0.44f, c);
    DrawCircle((int)cx, (int)cy, size * 0.20f, c);
}

void drawBoltIcon(float cx, float cy, float size, Color c) {
    fillTri(
        Vector2{ cx + size * 0.12f, cy - size * 0.48f },
        Vector2{ cx - size * 0.32f, cy + size * 0.06f },
        Vector2{ cx + size * 0.06f, cy + size * 0.06f },
        c
    );
    fillTri(
        Vector2{ cx - size * 0.12f, cy + size * 0.48f },
        Vector2{ cx + size * 0.32f, cy - size * 0.06f },
        Vector2{ cx - size * 0.06f, cy - size * 0.06f },
        c
    );
}

void drawShieldIcon(float cx, float cy, float size, Color c) {
    DrawPoly(Vector2{ cx, cy }, 6, size * 0.46f, 30.0f, c);
    DrawPolyLines(Vector2{ cx, cy }, 6, size * 0.46f, 30.0f, WHITE);
}

void initStars() {
    for (int i = 0; i < MAX_STARS; ++i) {
        int layer = GetRandomValue(0, 2);
        stars[i].layer = layer;
        stars[i].pos = Vector2{ (float)GetRandomValue(0, SCREEN_W), (float)GetRandomValue(0, SCREEN_H) };
        stars[i].speed = (22.0f + layer * 24.0f) + (float)GetRandomValue(0, 15);
        stars[i].radius = 1.0f + layer * 0.75f;
        stars[i].twinkle = (float)GetRandomValue(0, 628) / 100.0f;
    }
}

void clearObjects() {
    for (auto& b : bullets) b.active = false;
    for (auto& eb : enemyBullets) eb.active = false;
    for (auto& e : enemies) e.active = false;
    for (auto& p : powerUps) p.active = false;
    for (auto& pop : popups) pop.active = false;
    for (auto& fx : effects) fx.active = false;
    aid.active = false;
    specialAlive = false;
    specialSpawnedThisLevel = false;
    specialHitsTaken = 0;
    missedEnemies = 0;
    comboCount = 0;
    comboMultiplier = 1;
    comboTimer = 0.0f;
    shakeTimer = 0.0f;
    spawnTimer = 0.7f;
}

float nextAidDelay() { return (player.lives <= 1) ? AID_URGENT_DELAY : AID_NORMAL_DELAY; }

void setupLevel() {
    clearObjects();
    player.pos = Vector2{ PLAY_X + PLAY_W / 2.0f, PLAY_Y + PLAY_H - 75.0f };
    player.speed = 435.0f;
    player.fireTimer = 0.0f;
    player.invulnerable = 1.2f;
    player.rapidFireTimer = 0.0f;
    aidTimer = nextAidDelay();
}

void startNewGame() {
    level = 1;
    score = 0;
    levelStartScore = 0;
    player.lives = 3;
    player.hasShield = false;
    levelStartLives = 3;
    lastLevelReached = 1;
    specialHitsRequired = 10;
    newHighScore = false;
    setupLevel();
    screen = PLAYING;
}

void restartCurrentLevel() {
    score = levelStartScore;
    player.lives = levelStartLives;
    player.hasShield = false;
    setupLevel();
    screen = PLAYING;
}

void exitToMainMenu() {
    recordRunScore();
    clearObjects();
    level = 1;
    score = 0;
    levelStartScore = 0;
    player.lives = 3;
    player.hasShield = false;
    player.rapidFireTimer = 0.0f;
    screen = MENU;
}

void spawnEffect(Vector2 p, Color c) {
    for (auto& fx : effects) if (!fx.active) { fx.pos = p; fx.age = 0; fx.active = true; fx.color = c; break; }
}

// Fires a single centered bullet per shot (faster cooldown during Rapid Fire power-up).
void fireBullets() {
    if (player.fireTimer > 0) return;
    for (auto& b : bullets) {
        if (!b.active) {
            b.active = true;
            b.pos = Vector2{ player.pos.x, player.pos.y - 46.0f };
            b.vel = Vector2{ 0.0f, player.rapidFireTimer > 0.0f ? -780.0f : -660.0f };
            player.fireTimer = (player.rapidFireTimer > 0.0f) ? 0.11f : 0.22f;
            return;
        }
    }
}

void spawnEnemyBullet(Vector2 origin, Vector2 vel, Color col) {
    for (auto& eb : enemyBullets) {
        if (!eb.active) {
            eb.active = true;
            eb.pos = origin;
            eb.vel = vel;
            eb.color = col;
            return;
        }
    }
}

void spawnPowerUp(Vector2 origin) {
    if (GetRandomValue(0, 99) >= 16) return; // 16% drop chance from defeated enemies
    for (auto& p : powerUps) {
        if (!p.active) {
            p.active = true;
            p.pos = origin;
            p.type = GetRandomValue(0, 1); // 0 = Rapid Fire, 1 = Shield
            p.speed = 105.0f;
            p.pulse = 0.0f;
            return;
        }
    }
}

void spawnGenericEnemy() {
    for (auto& e : enemies) if (!e.active) {
        e.active = true;
        e.type = GetRandomValue(0, 2);
        e.pos = Vector2{ (float)GetRandomValue(PLAY_X + 55, PLAY_X + PLAY_W - 55), (float)(PLAY_Y - 35) };
        e.speed = 65.0f + level * 12.0f + (float)GetRandomValue(0, 35);
        e.phase = (float)GetRandomValue(0, 628) / 100.0f;
        e.shootTimer = (float)GetRandomValue(14, 28) / 10.0f;
        return;
    }
}

void spawnSpecialEnemy() {
    for (auto& e : enemies) if (!e.active) {
        e.active = true;
        e.type = 3;
        e.pos = Vector2{ PLAY_X + PLAY_W / 2.0f, PLAY_Y + 135.0f };
        e.speed = 28.0f + level * 2.0f;
        e.phase = 0;
        e.shootTimer = 1.4f;
        specialAlive = true;
        specialSpawnedThisLevel = true;
        specialHitsTaken = 0;
        return;
    }
}

bool circleHit(Vector2 a, float ar, Vector2 b, float br) {
    float dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy <= (ar + br) * (ar + br);
}

void drawPlayerAt(float x, float y, bool showShieldRing) {
    float flame = 18.0f + (float)GetRandomValue(0, 10);
    Color flameCol = (player.rapidFireTimer > 0.0f) ? GOLD : SKYBLUE;
    fillTri(Vector2{ x - 24, y + 27 }, Vector2{ x - 11, y + 27 }, Vector2{ x - 18, y + flame + 28 }, flameCol);
    fillTri(Vector2{ x + 11, y + 27 }, Vector2{ x + 24, y + 27 }, Vector2{ x + 18, y + flame + 28 }, flameCol);
    fillTri(Vector2{ x, y - 42 }, Vector2{ x - 70, y + 35 }, Vector2{ x - 30, y + 29 }, WHITE);
    fillTri(Vector2{ x, y - 42 }, Vector2{ x + 70, y + 35 }, Vector2{ x + 30, y + 29 }, WHITE);
    fillTri(Vector2{ x - 70, y + 35 }, Vector2{ x - 31, y + 8 }, Vector2{ x - 28, y + 39 }, BLUE);
    fillTri(Vector2{ x + 70, y + 35 }, Vector2{ x + 31, y + 8 }, Vector2{ x + 28, y + 39 }, BLUE);
    fillTri(Vector2{ x, y - 48 }, Vector2{ x - 26, y + 34 }, Vector2{ x + 26, y + 34 }, Color{ 225, 240, 255, 255 });
    DrawEllipse((int)x, (int)(y - 2), 12, 29, Color{ 25, 155, 255, 255 });
    DrawEllipse((int)x, (int)(y - 5), 7, 20, Color{ 100, 220, 255, 255 });
    DrawRectangle((int)x - 42, (int)y + 16, 15, 31, DARKBLUE);
    DrawRectangle((int)x + 27, (int)y + 16, 15, 31, DARKBLUE);
    DrawLineEx(Vector2{ x - 55, y + 30 }, Vector2{ x - 12, y + 9 }, 3, RED);
    DrawLineEx(Vector2{ x + 55, y + 30 }, Vector2{ x + 12, y + 9 }, 3, RED);

    if (showShieldRing && player.hasShield) {
        float pulse = 62.0f + sinf((float)GetTime() * 6.0f) * 3.0f;
        DrawCircleV(Vector2{ x, y }, pulse, Fade(SKYBLUE, 0.12f));
        DrawCircleLines((int)x, (int)y, pulse, SKYBLUE);
    }
    if (showShieldRing && player.invulnerable > 0) {
        DrawCircleLines((int)x, (int)y, 58, Fade(WHITE, 0.85f));
    }
}

void drawPlayer() {
    drawPlayerAt(player.pos.x, player.pos.y, true);
}

void drawGenericEnemy(const Enemy& e) {
    float x = e.pos.x, y = e.pos.y;
    Color c = enemyColor(e.type);
    if (e.type == 0) {
        DrawPoly(Vector2{ x, y }, 8, 27, 0, Color{ 70, 20, 45, 255 });
        DrawPoly(Vector2{ x, y }, 8, 21, 0, c);
        fillTri(Vector2{ x - 15, y - 8 }, Vector2{ x - 42, y - 29 }, Vector2{ x - 29, y + 8 }, c);
        fillTri(Vector2{ x + 15, y - 8 }, Vector2{ x + 42, y - 29 }, Vector2{ x + 29, y + 8 }, c);
        DrawCircle((int)x, (int)y, 7, ORANGE);
        DrawCircle((int)x, (int)y, 3, YELLOW);
    }
    else if (e.type == 1) {
        DrawPoly(Vector2{ x, y }, 10, 25, 0, Color{ 45, 65, 100, 255 });
        DrawPoly(Vector2{ x, y }, 8, 19, 0, c);
        for (int i = 0; i < 8; ++i) {
            float a = i * PI / 4.0f;
            fillTri(Vector2{ x + cosf(a) * 17, y + sinf(a) * 17 }, Vector2{ x + cosf(a - 0.25f) * 34, y + sinf(a - 0.25f) * 34 }, Vector2{ x + cosf(a + 0.25f) * 34, y + sinf(a + 0.25f) * 34 }, Color{ 75, 190, 255, 255 });
        }
        DrawCircle((int)x, (int)y, 9, LIME);
    }
    else {
        DrawEllipse((int)x, (int)y, 25, 18, Color{ 65, 20, 100, 255 });
        DrawEllipse((int)x, (int)y, 19, 14, c);
        for (int i = 0; i < 4; ++i) {
            float side = (i < 2) ? -1.0f : 1.0f;
            float yy = y + (i % 2 ? 13 : -13);
            DrawLineEx(Vector2{ x + side * 14, yy }, Vector2{ x + side * 31, yy + (i % 2 ? 14 : -14) }, 5, Color{ 125, 45, 205, 255 });
        }
        DrawCircle((int)(x - 7), (int)y, 4, ORANGE);
        DrawCircle((int)(x + 7), (int)y, 4, ORANGE);
    }
}

void drawSpecialEnemy(const Enemy& e) {
    float x = e.pos.x, y = e.pos.y;
    Color glow = (level == 4) ? VIOLET : (level == 7 ? ORANGE : SKYBLUE);
    DrawCircle((int)x, (int)y, 58, Fade(glow, 0.14f));
    if (level == 4) {
        DrawEllipse((int)x, (int)y, 43, 48, Color{ 55, 25, 100, 255 });
        DrawEllipse((int)x, (int)y - 12, 29, 37, Color{ 145, 65, 230, 255 });
        fillTri(Vector2{ x - 30, y + 10 }, Vector2{ x - 65, y + 65 }, Vector2{ x - 14, y + 38 }, VIOLET);
        fillTri(Vector2{ x + 30, y + 10 }, Vector2{ x + 65, y + 65 }, Vector2{ x + 14, y + 38 }, VIOLET);
        DrawCircle((int)x, (int)y - 9, 12, Color{ 230, 90, 255, 255 });
    }
    else if (level == 7) {
        DrawEllipse((int)x, (int)y, 48, 40, Color{ 100, 35, 25, 255 });
        fillTri(Vector2{ x - 35, y - 5 }, Vector2{ x - 75, y - 65 }, Vector2{ x - 25, y - 37 }, MAROON);
        fillTri(Vector2{ x + 35, y - 5 }, Vector2{ x + 75, y - 65 }, Vector2{ x + 25, y - 37 }, MAROON);
        DrawPoly(Vector2{ x, y }, 7, 33, 0, Color{ 180, 55, 35, 255 });
        DrawCircle((int)x, (int)y + 4, 12, ORANGE);
        DrawCircle((int)x, (int)y + 4, 6, YELLOW);
    }
    else {
        DrawEllipse((int)x, (int)y - 5, 42, 37, Color{ 20, 60, 115, 255 });
        for (int i = 0; i < 6; ++i) {
            float a = i * PI / 3.0f;
            Vector2 p1{ x + cosf(a) * 28, y + sinf(a) * 24 };
            Vector2 p2{ x + cosf(a + 0.4f) * 73, y + sinf(a + 0.4f) * 65 };
            DrawLineEx(p1, p2, 10, Color{ 35, 110, 220, 255 });
            DrawCircle((int)p2.x, (int)p2.y, 5, SKYBLUE);
        }
        DrawCircle((int)x, (int)y - 5, 17, Color{ 20, 175, 255, 255 });
        DrawCircle((int)x, (int)y - 5, 8, WHITE);
    }
    DrawCircle((int)x - 17, (int)y - 5, 5, RED);
    DrawCircle((int)x + 17, (int)y - 5, 5, RED);
}

void drawBackground(float dt, bool inGame, int lv) {
    Color base = (lv <= 2) ? Color{ 5, 12, 38, 255 } : (lv <= 4) ? Color{ 18, 7, 42, 255 } : (lv <= 6) ? Color{ 35, 8, 18, 255 } : (lv <= 8) ? Color{ 5, 30, 30, 255 } : Color{ 4, 8, 24, 255 };
    ClearBackground(base);
    Color nebula = levelColor(lv);
    if (inGame) BeginScissorMode(PLAY_X, PLAY_Y, PLAY_W, PLAY_H);
    if (inGame) {
        DrawCircleGradient(Vector2{ (float)(PLAY_X + 220), (float)(PLAY_Y + 210) }, 260, Fade(nebula, 0.18f), Fade(base, 0));
        DrawCircleGradient(Vector2{ (float)(PLAY_X + 680), (float)(PLAY_Y + 480) }, 310, Fade(nebula, 0.12f), Fade(base, 0));
    }
    else {
        DrawCircleGradient(Vector2{ SCREEN_W / 2.0f, 250.0f }, 320, Fade(nebula, 0.20f), Fade(base, 0));
    }
    for (auto& s : stars) {
        s.pos.y += s.speed * dt * (screen == MENU ? 0.55f : 1.0f);
        if (s.pos.y > SCREEN_H) { s.pos.y = 0; s.pos.x = (float)GetRandomValue(0, SCREEN_W); }
        float pulse = 0.50f + 0.50f * sinf((float)GetTime() * 2.2f + s.twinkle);
        Color starCol = (s.layer == 2) ? Color{ 210, 235, 255, 255 } : WHITE;
        DrawCircleV(s.pos, s.radius, Fade(starCol, pulse));
    }
    if (inGame) {
        EndScissorMode();
        DrawRectangleLinesEx(Rectangle{ (float)PLAY_X - 2, (float)PLAY_Y - 2, (float)PLAY_W + 4, (float)PLAY_H + 4 }, 4, Fade(levelColor(lv), 0.28f));
        DrawRectangleLinesEx(Rectangle{ (float)PLAY_X, (float)PLAY_Y, (float)PLAY_W, (float)PLAY_H }, 2.5f, Fade(levelColor(lv), 0.95f));
    }
}

void drawBar(int x, int y, int w, int h, float fraction, Color fill) {
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    DrawRectangleRounded(Rectangle{ (float)x, (float)y, (float)w, (float)h }, 0.45f, 6, Color{ 24, 34, 62, 255 });
    if (fraction > 0.01f) {
        DrawRectangleRounded(Rectangle{ (float)x, (float)y, w * fraction, (float)h }, 0.45f, 6, fill);
    }
}

Rectangle pauseTouchRect() {
    return Rectangle{ (float)(PANEL_X + 20), 695.0f, 102.0f, 42.0f };
}
Rectangle restartTouchRect() {
    return Rectangle{ (float)(PANEL_X + 132), 695.0f, 103.0f, 42.0f };
}
Rectangle leftTouchRect() {
    return Rectangle{ (float)(PLAY_X + 18), (float)(PLAY_Y + PLAY_H - 86), 84.0f, 70.0f };
}
Rectangle rightTouchRect() {
    return Rectangle{ (float)(PLAY_X + 114), (float)(PLAY_Y + PLAY_H - 86), 84.0f, 70.0f };
}
Rectangle fireTouchRect() {
    return Rectangle{ (float)(PLAY_X + PLAY_W - 104), (float)(PLAY_Y + PLAY_H - 86), 86.0f, 70.0f };
}

void drawTopStatusBar() {
    Rectangle bar{ (float)PLAY_X, 24.0f, (float)PLAY_W, 46.0f };
    DrawRectangleRounded(bar, 0.25f, 8, Color{ 9, 17, 42, 235 });
    DrawRectangleRoundedLinesEx(bar, 0.25f, 8, 1.5f, Fade(levelColor(level), 0.75f));

    // Combo indicator
    Color comboCol = (comboMultiplier >= 4) ? GOLD : (comboMultiplier >= 2 ? LIME : LIGHTGRAY);
    DrawText(TextFormat("COMBO  x%i", comboMultiplier), PLAY_X + 18, 37, 20, comboCol);
    if (comboTimer > 0.0f) {
        drawBar(PLAY_X + 145, 42, 95, 10, comboTimer / COMBO_DURATION, comboCol);
    }

    // Active Shield badge
    if (player.hasShield) {
        DrawRectangleRounded(Rectangle{ (float)(PLAY_X + 275), 32.0f, 130.0f, 30.0f }, 0.35f, 6, Fade(SKYBLUE, 0.22f));
        DrawRectangleRoundedLinesEx(Rectangle{ (float)(PLAY_X + 275), 32.0f, 130.0f, 30.0f }, 0.35f, 6, 1.5f, SKYBLUE);
        drawShieldIcon(PLAY_X + 295.0f, 47.0f, 18.0f, SKYBLUE);
        DrawText("SHIELD ON", PLAY_X + 312, 39, 15, WHITE);
    }

    // Active Rapid Fire badge
    if (player.rapidFireTimer > 0.0f) {
        DrawRectangleRounded(Rectangle{ (float)(PLAY_X + 420), 32.0f, 165.0f, 30.0f }, 0.35f, 6, Fade(GOLD, 0.22f));
        DrawRectangleRoundedLinesEx(Rectangle{ (float)(PLAY_X + 420), 32.0f, 165.0f, 30.0f }, 0.35f, 6, 1.5f, GOLD);
        drawBoltIcon(PLAY_X + 438.0f, 47.0f, 18.0f, GOLD);
        DrawText(TextFormat("RAPID %.1fs", player.rapidFireTimer), PLAY_X + 455, 39, 15, GOLD);
    }

    // Boss indicator
    if (specialAlive) {
        DrawText("BOSS ALERT!", PLAY_X + PLAY_W - 145, 37, 19, RED);
    }
    else {
        DrawText(TextFormat("SECTOR %02i", level), PLAY_X + PLAY_W - 125, 37, 19, levelColor(level));
    }
}

void drawPanel() {
    DrawRectangleRounded(Rectangle{ (float)PANEL_X, 24.0f, (float)PANEL_W, 731.0f }, 0.06f, 12, Color{ 9, 17, 42, 238 });
    DrawRectangleRoundedLinesEx(Rectangle{ (float)PANEL_X, 24.0f, (float)PANEL_W, 731.0f }, 0.06f, 12, 2.0f, Fade(levelColor(level), 0.85f));
    DrawText("SPACE SHOOTER", PANEL_X + 18, 42, 22, SKYBLUE);
    DrawLine(PANEL_X + 18, 74, PANEL_X + PANEL_W - 18, 74, Fade(SKYBLUE, 0.5f));
    DrawText(TextFormat("LEVEL  %02i / %02i", level, MAX_LEVEL), PANEL_X + 20, 92, 22, levelColor(level));
    DrawText(TextFormat("SCORE  %05i", score), PANEL_X + 20, 134, 21, WHITE);
    DrawText(TextFormat("BEST   %05i", highScore), PANEL_X + 20, 170, 18, GOLD);
    DrawText("LEVEL PROGRESS", PANEL_X + 20, 212, 15, GRAY);
    int gained = std::max(0, score - levelStartScore);
    drawBar(PANEL_X + 20, 236, PANEL_W - 40, 13, (float)gained / (float)targetForLevel(level), levelColor(level));
    DrawText(TextFormat("%i / %i", gained, targetForLevel(level)), PANEL_X + 20, 256, 15, LIGHTGRAY);

    DrawText("LIVES", PANEL_X + 20, 294, 16, GRAY);
    for (int i = 0; i < player.lives; ++i) DrawPoly(Vector2{ (float)(PANEL_X + 34 + i * 34), 333 }, 3, 13, 0, SKYBLUE);

    DrawText(TextFormat("MISSED: %i / %i", missedEnemies, MISS_LIMIT), PANEL_X + 20, 366, 17, missedEnemies >= 5 ? ORANGE : LIGHTGRAY);
    drawBar(PANEL_X + 20, 387, PANEL_W - 40, 8, (float)missedEnemies / (float)MISS_LIMIT, missedEnemies >= 5 ? ORANGE : SKYBLUE);

    DrawText("ENEMY & DROPS", PANEL_X + 20, 412, 16, GRAY);
    DrawCircle(PANEL_X + 29, 444, 7, RED); DrawText("Spike drone   +10", PANEL_X + 45, 434, 15, WHITE);
    DrawCircle(PANEL_X + 29, 471, 7, LIGHTGRAY); DrawText("Crystal rock  +20", PANEL_X + 45, 461, 15, WHITE);
    DrawCircle(PANEL_X + 29, 498, 7, VIOLET); DrawText("Flower bug    +30", PANEL_X + 45, 488, 15, WHITE);
    drawBoltIcon(PANEL_X + 29.0f, 526.0f, 16.0f, GOLD); DrawText("Rapid laser   8s", PANEL_X + 45, 517, 15, GOLD);
    drawShieldIcon(PANEL_X + 29.0f, 554.0f, 16.0f, SKYBLUE); DrawText("Energy shield 1x", PANEL_X + 45, 545, 15, SKYBLUE);

    DrawText("AID CRATE", PANEL_X + 20, 588, 16, SKYBLUE);
    if (aid.active) {
        DrawText("CATCH IT!  +1 LIFE", PANEL_X + 20, 610, 15, WHITE);
        drawBar(PANEL_X + 20, 634, PANEL_W - 40, 8, 1.0f, SKYBLUE);
    }
    else {
        bool warning = aidTimer <= AID_WARNING_TIME;
        DrawText(TextFormat("IN: %02i SEC", (int)ceilf(aidTimer)), PANEL_X + 20, 610, 15, warning ? YELLOW : WHITE);
        float total = (player.lives <= 1) ? AID_URGENT_DELAY : AID_NORMAL_DELAY;
        drawBar(PANEL_X + 20, 634, PANEL_W - 40, 8, 1.0f - aidTimer / total, warning ? YELLOW : SKYBLUE);
    }

    // Touch-friendly Pause & Restart buttons
    Rectangle pBtn = pauseTouchRect();
    Rectangle rBtn = restartTouchRect();
    bool pHeld = isRectHeld(pBtn);
    bool rHeld = isRectHeld(rBtn);
    DrawRectangleRounded(pBtn, 0.25f, 8, pHeld ? Fade(SKYBLUE, 0.45f) : Color{ 18, 34, 72, 230 });
    DrawRectangleRoundedLinesEx(pBtn, 0.25f, 8, 1.5f, SKYBLUE);
    drawPauseIcon(pBtn.x + 22, pBtn.y + pBtn.height * 0.5f, 18, WHITE);
    DrawText("PAUSE", (int)(pBtn.x + 38), (int)(pBtn.y + 13), 14, WHITE);

    DrawRectangleRounded(rBtn, 0.25f, 8, rHeld ? Fade(ORANGE, 0.45f) : Color{ 18, 34, 72, 230 });
    DrawRectangleRoundedLinesEx(rBtn, 0.25f, 8, 1.5f, ORANGE);
    drawRestartIcon(rBtn.x + 20, rBtn.y + rBtn.height * 0.5f, 18, WHITE);
    DrawText("RESET", (int)(rBtn.x + 36), (int)(rBtn.y + 13), 14, WHITE);
}

void drawTouchControls() {
    Rectangle lBtn = leftTouchRect();
    Rectangle rBtn = rightTouchRect();
    Rectangle fBtn = fireTouchRect();
    bool lHeld = isRectHeld(lBtn);
    bool rHeld = isRectHeld(rBtn);
    bool fHeld = isRectHeld(fBtn);

    DrawRectangleRounded(lBtn, 0.25f, 8, lHeld ? Fade(SKYBLUE, 0.48f) : Fade(Color{ 12, 24, 54, 255 }, 0.65f));
    DrawRectangleRoundedLinesEx(lBtn, 0.25f, 8, 2.0f, Fade(SKYBLUE, lHeld ? 1.0f : 0.65f));
    drawArrowIcon(lBtn.x + lBtn.width * 0.5f, lBtn.y + lBtn.height * 0.5f, 34, -1, WHITE);

    DrawRectangleRounded(rBtn, 0.25f, 8, rHeld ? Fade(SKYBLUE, 0.48f) : Fade(Color{ 12, 24, 54, 255 }, 0.65f));
    DrawRectangleRoundedLinesEx(rBtn, 0.25f, 8, 2.0f, Fade(SKYBLUE, rHeld ? 1.0f : 0.65f));
    drawArrowIcon(rBtn.x + rBtn.width * 0.5f, rBtn.y + rBtn.height * 0.5f, 34, 1, WHITE);

    DrawRectangleRounded(fBtn, 0.25f, 8, fHeld ? Fade(RED, 0.52f) : Fade(Color{ 40, 14, 26, 255 }, 0.70f));
    DrawRectangleRoundedLinesEx(fBtn, 0.25f, 8, 2.0f, Fade(RED, fHeld ? 1.0f : 0.75f));
    drawFireIcon(fBtn.x + fBtn.width * 0.5f, fBtn.y + fBtn.height * 0.5f, 36, fHeld ? YELLOW : WHITE);
}

// White "+" on a cyan badge.
void drawAidIcon(Vector2 p) {
    DrawCircleV(p, 19, SKYBLUE);
    DrawRectangle((int)p.x - 3, (int)p.y - 11, 6, 22, WHITE);
    DrawRectangle((int)p.x - 11, (int)p.y - 3, 22, 6, WHITE);
}
void drawAid() {
    if (aid.active) drawAidIcon(aid.pos);
}

void drawPowerUps() {
    for (const auto& p : powerUps) {
        if (!p.active) continue;
        float glowR = 20.0f + sinf(p.pulse) * 3.0f;
        Color baseCol = (p.type == 0) ? GOLD : SKYBLUE;
        DrawCircleV(p.pos, glowR, Fade(baseCol, 0.24f));
        DrawCircleLines((int)p.pos.x, (int)p.pos.y, 18.0f, baseCol);
        if (p.type == 0) drawBoltIcon(p.pos.x, p.pos.y, 24.0f, GOLD);
        else drawShieldIcon(p.pos.x, p.pos.y, 24.0f, SKYBLUE);
    }
}

void drawPopups() {
    for (const auto& pop : popups) {
        if (!pop.active) continue;
        float alpha = std::clamp(1.0f - pop.age / 0.85f, 0.0f, 1.0f);
        DrawText(pop.text, (int)pop.pos.x - MeasureText(pop.text, 16) / 2, (int)pop.pos.y, 16, Fade(pop.color, alpha));
    }
}

void drawEffects() {
    for (auto& fx : effects) if (fx.active) {
        float r = 8 + fx.age * 65;
        float alpha = std::clamp(1.0f - fx.age / 0.55f, 0.0f, 1.0f);
        DrawCircleLines((int)fx.pos.x, (int)fx.pos.y, r, Fade(fx.color, alpha));
        for (int i = 0; i < 7; ++i) {
            float a = i * PI * 2 / 7.0f;
            DrawCircle((int)(fx.pos.x + cosf(a) * r), (int)(fx.pos.y + sinf(a) * r), 3, Fade(fx.color, alpha));
        }
    }
}

void hitPlayer(float protection) {
    comboCount = 0;
    comboMultiplier = 1;
    comboTimer = 0.0f;

    if (player.hasShield) {
        player.hasShield = false;
        player.invulnerable = 1.2f;
        spawnEffect(player.pos, SKYBLUE);
        spawnPopup(Vector2{ player.pos.x, player.pos.y - 45.0f }, "SHIELD BLOCKED!", SKYBLUE);
        triggerShake(0.20f, 5.0f);
        return;
    }

    player.lives--;
    player.invulnerable = protection;
    spawnEffect(player.pos, RED);
    triggerShake(0.32f, 8.0f);
}

void updateGame(float dt) {
    if (IsKeyPressed(KEY_P) || wasRectPressed(pauseTouchRect())) { screen = PAUSED; return; }
    if (IsKeyPressed(KEY_ESCAPE) || wasRectPressed(restartTouchRect())) { restartCurrentLevel(); return; }

    gameTime += dt;
    if (player.fireTimer > 0) player.fireTimer -= dt;
    if (player.invulnerable > 0) player.invulnerable -= dt;
    if (player.rapidFireTimer > 0) player.rapidFireTimer = std::max(0.0f, player.rapidFireTimer - dt);
    if (shakeTimer > 0) shakeTimer = std::max(0.0f, shakeTimer - dt);

    if (comboTimer > 0.0f) {
        comboTimer -= dt;
        if (comboTimer <= 0.0f) {
            comboCount = 0;
            comboMultiplier = 1;
        }
    }

    bool moveLeft = IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A) || isRectHeld(leftTouchRect());
    bool moveRight = IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D) || isRectHeld(rightTouchRect());
    bool firing = IsKeyDown(KEY_SPACE) || isRectHeld(fireTouchRect());

    if (moveLeft) player.pos.x -= player.speed * dt;
    if (moveRight) player.pos.x += player.speed * dt;
    player.pos.x = std::clamp(player.pos.x, (float)(PLAY_X + 75), (float)(PLAY_X + PLAY_W - 75));
    if (firing) fireBullets();

    for (auto& b : bullets) if (b.active) {
        b.pos.y += b.vel.y * dt;
        if (b.pos.y < PLAY_Y) b.active = false;
    }

    for (auto& eb : enemyBullets) if (eb.active) {
        eb.pos.x += eb.vel.x * dt;
        eb.pos.y += eb.vel.y * dt;
        if (eb.pos.y > PLAY_Y + PLAY_H || eb.pos.x < PLAY_X || eb.pos.x > PLAY_X + PLAY_W) {
            eb.active = false;
        }
        else if (player.invulnerable <= 0.0f && circleHit(eb.pos, 7.0f, player.pos, 32.0f)) {
            eb.active = false;
            hitPlayer(1.5f);
        }
    }

    // Progressive difficulty: faster spawns each level, plus a growing chance of a second enemy.
    spawnTimer -= dt;
    float spawnGap = std::max(0.38f, 1.25f - level * 0.075f);
    if (spawnTimer <= 0) {
        spawnGenericEnemy();
        if (level >= 3 && GetRandomValue(0, 99) < level * 4) spawnGenericEnemy();
        spawnTimer = spawnGap;
    }

    // Special enemies arrive on levels 4, 7 and 10.
    if ((level == 4 || level == 7 || level == 10) && !specialSpawnedThisLevel && !specialAlive && score - levelStartScore >= targetForLevel(level) / 2) {
        specialHitsRequired = (level == 4) ? 10 : (level == 7) ? 15 : 20;
        spawnSpecialEnemy();
    }

    for (auto& e : enemies) if (e.active) {
        if (e.type == 3) {
            e.pos.x = PLAY_X + PLAY_W / 2.0f + sinf(gameTime * 1.1f) * (PLAY_W * 0.28f);
            e.pos.y += sinf(gameTime * 2.0f) * 9.0f * dt;
            e.shootTimer -= dt;
            if (e.shootTimer <= 0.0f) {
                float spd = 250.0f + level * 10.0f;
                spawnEnemyBullet(Vector2{ e.pos.x - 22.0f, e.pos.y + 30.0f }, Vector2{ -65.0f, spd }, ORANGE);
                spawnEnemyBullet(Vector2{ e.pos.x, e.pos.y + 35.0f }, Vector2{ 0.0f, spd + 25.0f }, RED);
                spawnEnemyBullet(Vector2{ e.pos.x + 22.0f, e.pos.y + 30.0f }, Vector2{ 65.0f, spd }, ORANGE);
                e.shootTimer = std::max(0.95f, 1.85f - level * 0.07f);
            }
        }
        else {
            e.pos.y += e.speed * dt;
            e.pos.x += sinf(gameTime * 1.7f + e.phase) * 18.0f * dt;
            if (e.type == 2 && level >= 2) { // Purple flower bugs fire plasma shots
                e.shootTimer -= dt;
                if (e.shootTimer <= 0.0f && e.pos.y < player.pos.y - 120.0f) {
                    spawnEnemyBullet(Vector2{ e.pos.x, e.pos.y + 18.0f }, Vector2{ 0.0f, 240.0f + level * 14.0f }, VIOLET);
                    e.shootTimer = std::max(1.5f, 3.0f - level * 0.12f);
                }
            }
        }

        if (e.type != 3 && e.pos.y > PLAY_Y + PLAY_H - 5) {
            e.active = false;
            ++missedEnemies;
            comboCount = 0;
            comboMultiplier = 1;
            comboTimer = 0.0f;
            if (missedEnemies >= MISS_LIMIT) { missedEnemies = 0; hitPlayer(1.4f); }
        }
        if (e.active && player.invulnerable <= 0 && circleHit(e.pos, e.type == 3 ? 48 : 22, player.pos, 35)) {
            if (e.type != 3) e.active = false;
            hitPlayer(1.8f);
        }
    }

    for (auto& b : bullets) if (b.active) {
        for (auto& e : enemies) if (e.active && circleHit(b.pos, 7, e.pos, e.type == 3 ? 48 : 22)) {
            b.active = false;
            if (e.type == 3) {
                ++specialHitsTaken;
                spawnEffect(b.pos, ORANGE);
                if (specialHitsTaken >= specialHitsRequired) {
                    e.active = false;
                    specialAlive = false;
                    addScore(150);
                    spawnEffect(e.pos, levelColor(level));
                    spawnPopup(e.pos, "+150 BOSS!", GOLD);
                    triggerShake(0.45f, 10.0f);
                }
            }
            else {
                comboCount++;
                comboTimer = COMBO_DURATION;
                comboMultiplier = std::min(5, 1 + comboCount / 3);

                int basePts = (e.type == 0) ? 10 : (e.type == 1) ? 20 : 30;
                int earned = basePts * comboMultiplier;
                addScore(earned);

                char popTxt[24];
                if (comboMultiplier > 1) snprintf(popTxt, sizeof(popTxt), "+%i (x%i)", earned, comboMultiplier);
                else snprintf(popTxt, sizeof(popTxt), "+%i", earned);
                spawnPopup(e.pos, popTxt, comboMultiplier > 1 ? GOLD : WHITE);

                spawnEffect(e.pos, enemyColor(e.type));
                spawnPowerUp(e.pos);
                triggerShake(0.10f, 3.0f);
                e.active = false;
            }
            break;
        }
    }

    // Power-up pickups
    for (auto& p : powerUps) {
        if (!p.active) continue;
        p.pos.y += p.speed * dt;
        p.pulse += dt * 7.0f;
        if (circleHit(p.pos, 18.0f, player.pos, 38.0f)) {
            p.active = false;
            if (p.type == 0) {
                player.rapidFireTimer = 8.0f;
                spawnPopup(player.pos, "RAPID FIRE!", GOLD);
                spawnEffect(player.pos, GOLD);
            }
            else {
                if (!player.hasShield) {
                    player.hasShield = true;
                    spawnPopup(player.pos, "SHIELD READY!", SKYBLUE);
                }
                else {
                    addScore(50);
                    spawnPopup(player.pos, "+50 SHIELD BONUS", SKYBLUE);
                }
                spawnEffect(player.pos, SKYBLUE);
            }
        }
        else if (p.pos.y > PLAY_Y + PLAY_H) {
            p.active = false;
        }
    }

    // Aid crate: timed countdown (keeps running while the ship is protected), sooner at 1 life.
    if (!aid.active && player.lives <= 1 && aidTimer > AID_URGENT_DELAY) aidTimer = AID_URGENT_DELAY;
    aidTimer -= dt;
    if (!aid.active && aidTimer <= 0) {
        aid.active = true;
        aid.pos = Vector2{ (float)GetRandomValue(PLAY_X + 40, PLAY_X + PLAY_W - 40), (float)(PLAY_Y - 20) };
        aid.speed = 85;
    }
    if (aid.active) {
        aid.pos.y += aid.speed * dt;
        if (circleHit(aid.pos, 18, player.pos, 38)) {
            aid.active = false;
            if (player.lives < MAX_LIVES) {
                player.lives++;
                spawnPopup(player.pos, "+1 LIFE!", LIME);
            }
            else {
                addScore(50);
                spawnPopup(player.pos, "+50 MAX LIFE!", LIME);
            }
            aidTimer = nextAidDelay();
            spawnEffect(player.pos, SKYBLUE);
        }
        else if (aid.pos.y > PLAY_Y + PLAY_H) { aid.active = false; aidTimer = nextAidDelay(); }
    }

    for (auto& fx : effects) if (fx.active) { fx.age += dt; if (fx.age > 0.55f) fx.active = false; }
    for (auto& pop : popups) if (pop.active) {
        pop.age += dt;
        pop.pos.y -= 36.0f * dt;
        if (pop.age > 0.85f) pop.active = false;
    }

    if (player.lives <= 0) {
        player.lives = 0;
        screen = GAME_OVER;
        lastLevelReached = level;
        recordRunScore();
    }
    else if (score - levelStartScore >= targetForLevel(level) && !specialAlive) {
        screen = LEVEL_COMPLETE;
        lastLevelReached = level;
        saveHighScore();
    }
}

void drawCentered(const char* text, int y, int size, Color color) {
    DrawText(text, SCREEN_W / 2 - MeasureText(text, size) / 2, y, size, color);
}

void drawGameplay(float dt) {
    drawBackground(dt, true, level);
    drawTopStatusBar();

    float ox = 0.0f, oy = 0.0f;
    if (shakeTimer > 0.0f) {
        ox = (float)GetRandomValue(-100, 100) / 100.0f * shakeIntensity;
        oy = (float)GetRandomValue(-100, 100) / 100.0f * shakeIntensity;
    }

    BeginScissorMode(PLAY_X, PLAY_Y, PLAY_W, PLAY_H);
    for (const auto& b : bullets) if (b.active) {
        Color bCol = (player.rapidFireTimer > 0.0f) ? GOLD : SKYBLUE;
        DrawCircleV(Vector2{ b.pos.x + ox, b.pos.y + oy }, 8, Fade(bCol, 0.25f));
        DrawRectangle((int)(b.pos.x + ox) - 3, (int)(b.pos.y + oy) - 11, 6, 22, bCol);
        DrawRectangle((int)(b.pos.x + ox) - 1, (int)(b.pos.y + oy) - 8, 2, 14, WHITE);
    }
    for (const auto& eb : enemyBullets) if (eb.active) {
        DrawCircleV(Vector2{ eb.pos.x + ox, eb.pos.y + oy }, 10, Fade(eb.color, 0.28f));
        DrawCircleV(Vector2{ eb.pos.x + ox, eb.pos.y + oy }, 6, eb.color);
        DrawCircleV(Vector2{ eb.pos.x + ox, eb.pos.y + oy }, 2.5f, WHITE);
    }
    for (const auto& e : enemies) if (e.active) {
        Enemy shifted = e;
        shifted.pos.x += ox;
        shifted.pos.y += oy;
        if (e.type == 3) {
            drawSpecialEnemy(shifted);
            int barW = 160;
            DrawRectangle((int)shifted.pos.x - barW / 2, (int)shifted.pos.y - 83, barW, 10, DARKGRAY);
            float hp = 1.0f - (float)specialHitsTaken / (float)specialHitsRequired;
            DrawRectangle((int)shifted.pos.x - barW / 2, (int)shifted.pos.y - 83, (int)(barW * hp), 10, RED);
            DrawText(TextFormat("BOSS  %i/%i", specialHitsTaken, specialHitsRequired), (int)shifted.pos.x - 52, (int)shifted.pos.y - 105, 14, WHITE);
        }
        else drawGenericEnemy(shifted);
    }
    drawPowerUps();
    drawAid();
    drawEffects();
    drawPopups();

    if (player.invulnerable <= 0 || ((int)(GetTime() * 12) % 2 == 0)) {
        drawPlayerAt(player.pos.x + ox, player.pos.y + oy, true);
    }
    drawTouchControls();
    EndScissorMode();

    if (!aid.active && aidTimer <= AID_WARNING_TIME && ((int)(GetTime() * 4) % 2 == 0)) {
        const char* msg = "AID CRATE INCOMING!";
        DrawText(msg, PLAY_X + PLAY_W / 2 - MeasureText(msg, 26) / 2, PLAY_Y + 14, 26, YELLOW);
    }
    drawPanel();
}

Rectangle menuItemRect(int index) {
    int y = 395 + index * 58;
    return Rectangle{ SCREEN_W / 2.0f - 180.0f, (float)y - 8.0f, 360.0f, 46.0f };
}

void drawMenu() {
    drawBackground(GetFrameTime(), false, 1);

    // Decorative title banner
    drawCentered("SPACE SHOOTER", 110, 66, SKYBLUE);
    drawCentered("GALACTIC DEFENSE EDITION", 184, 20, GOLD);

    // Animated player flagship & enemy showcase
    drawPlayerAt(SCREEN_W / 2.0f, 315.0f + sinf((float)GetTime() * 2.2f) * 6.0f, false);

    Enemy displayEnemy{};
    displayEnemy.active = true;
    displayEnemy.type = 0;
    displayEnemy.pos = Vector2{ SCREEN_W / 2.0f - 280.0f + sinf((float)GetTime() * 1.2f) * 14.0f, 265.0f + sinf((float)GetTime() * 1.7f) * 9.0f };
    drawGenericEnemy(displayEnemy);
    displayEnemy.type = 1;
    displayEnemy.pos = Vector2{ SCREEN_W / 2.0f - 160.0f + sinf((float)GetTime() * 1.0f + 1.0f) * 12.0f, 240.0f + sinf((float)GetTime() * 1.4f + 1.0f) * 8.0f };
    drawGenericEnemy(displayEnemy);
    displayEnemy.type = 2;
    displayEnemy.pos = Vector2{ SCREEN_W / 2.0f + 220.0f + sinf((float)GetTime() * 1.4f + 2.0f) * 14.0f, 255.0f + sinf((float)GetTime() * 1.8f + 2.0f) * 9.0f };
    drawGenericEnemy(displayEnemy);

    const char* items[] = { "START GAME", "INSTRUCTIONS", "HIGH SCORES", "EXIT" };
    for (int i = 0; i < 4; ++i) {
        Rectangle r = menuItemRect(i);
        bool active = (menuChoice == i) || isRectHeld(r);
        DrawRectangleRounded(r, 0.3f, 8, active ? Fade(levelColor(1), 0.38f) : Fade(Color{ 12, 22, 52, 255 }, 0.72f));
        DrawRectangleRoundedLinesEx(r, 0.3f, 8, active ? 2.0f : 1.0f, active ? SKYBLUE : Fade(SKYBLUE, 0.35f));

        float iconX = r.x + 30.0f;
        float iconY = r.y + r.height * 0.5f;
        if (i == 0) drawPlayIcon(iconX, iconY, 22, LIME);
        else if (i == 1) drawBookIcon(iconX, iconY, 22, SKYBLUE);
        else if (i == 2) drawTrophyIcon(iconX, iconY, 22, GOLD);
        else drawExitIcon(iconX, iconY, 22, RED);

        int y = 395 + i * 58;
        DrawText(items[i], (int)(r.x + 58.0f), y, 24, active ? WHITE : LIGHTGRAY);
    }

    if (highScore > 0) {
        drawCentered(TextFormat("TOP RECORD: %05i", highScore), 648, 20, GOLD);
    }
    drawCentered("UP / DOWN or W / S: SELECT     ENTER / SPACE / TAP: CONFIRM", 692, 16, LIGHTGRAY);
}

void drawInstructions() {
    drawBackground(GetFrameTime(), false, 1);
    drawCentered("HOW TO PLAY", 65, 42, SKYBLUE);
    const char* lines[] = {
        "A / D, LEFT / RIGHT, or touch arrows  -  Move ship",
        "SPACE or touch FIRE button  -  Fire single-cannon laser",
        "P or PAUSE button  -  Pause game (includes Main Menu option)",
        "ESC or RESET button  -  Restart the current level",
        "Chain consecutive kills quickly to build up to a x5 COMBO multiplier!",
        "Red drone: 10 pts  |  Crystal rock: 20 pts  |  Purple bug (shoots): 30 pts",
        "GOLD BOLT DROP: Rapid-fire laser boost for 8 seconds",
        "CYAN SHIELD DROP: Absorbs 1 enemy collision or plasma shot",
        "AID CRATE (white + on cyan badge): +1 life, or +50 points at full lives",
        "7 enemies escaping off the bottom cost you 1 life.",
        "Bosses appear on levels 4, 7 and 10 and fire 3-way plasma spreads!",
        "Clear all 10 levels to save the galaxy!"
    };
    for (int i = 0; i < 12; ++i) {
        Color col = WHITE;
        if (i == 4 || i == 6) col = GOLD;
        else if (i == 7) col = SKYBLUE;
        else if (i == 10) col = ORANGE;
        DrawText(lines[i], 190, 130 + i * 37, 20, col);
    }
    drawAidIcon(Vector2{ 150.0f, 130.0f + 8 * 37 + 10.0f });

    Rectangle startBtn{ SCREEN_W / 2.0f - 290.0f, 625.0f, 260.0f, 44.0f };
    Rectangle backBtn{ SCREEN_W / 2.0f + 30.0f, 625.0f, 260.0f, 44.0f };
    DrawRectangleRounded(startBtn, 0.25f, 8, Fade(LIME, 0.22f));
    DrawRectangleRoundedLinesEx(startBtn, 0.25f, 8, 1.5f, LIME);
    drawPlayIcon(startBtn.x + 26.0f, startBtn.y + 22.0f, 18, LIME);
    DrawText("SPACE / TAP: START", (int)(startBtn.x + 46.0f), (int)(startBtn.y + 12.0f), 19, WHITE);

    DrawRectangleRounded(backBtn, 0.25f, 8, Fade(SKYBLUE, 0.22f));
    DrawRectangleRoundedLinesEx(backBtn, 0.25f, 8, 1.5f, SKYBLUE);
    drawArrowIcon(backBtn.x + 26.0f, backBtn.y + 22.0f, 18, -1, SKYBLUE);
    DrawText("ESC / TAP: MENU", (int)(backBtn.x + 48.0f), (int)(backBtn.y + 12.0f), 19, WHITE);
}

void drawHighScore() {
    drawBackground(GetFrameTime(), false, 1);
    drawTrophyIcon(SCREEN_W / 2.0f, 155.0f, 68, GOLD);
    drawCentered("HIGH SCORES", 205, 46, SKYBLUE);

    if (highScores.empty()) {
        drawCentered("No scores recorded yet", 330, 26, LIGHTGRAY);
    }
    else {
        for (int i = 0; i < (int)highScores.size(); ++i) {
            char lineBuf[64];
            snprintf(lineBuf, sizeof(lineBuf), "#%i     %05i", i + 1, highScores[i]);
            Color c = (i == 0) ? GOLD : (i == 1 ? SKYBLUE : WHITE);
            drawCentered(lineBuf, 285 + i * 48, 30, c);
        }
    }

    Rectangle backBtn{ SCREEN_W / 2.0f - 150.0f, 585.0f, 300.0f, 46.0f };
    DrawRectangleRounded(backBtn, 0.25f, 8, Fade(SKYBLUE, 0.22f));
    DrawRectangleRoundedLinesEx(backBtn, 0.25f, 8, 1.5f, SKYBLUE);
    drawArrowIcon(backBtn.x + 28.0f, backBtn.y + 23.0f, 18, -1, SKYBLUE);
    DrawText("ENTER / ESC / TAP: MENU", (int)(backBtn.x + 48.0f), (int)(backBtn.y + 13.0f), 18, WHITE);
}

Rectangle pauseResumeRect() { return Rectangle{ SCREEN_W / 2.0f - 190.0f, 330.0f, 380.0f, 46.0f }; }
Rectangle pauseResetRect() { return Rectangle{ SCREEN_W / 2.0f - 190.0f, 390.0f, 380.0f, 46.0f }; }
Rectangle pauseMenuRect() { return Rectangle{ SCREEN_W / 2.0f - 190.0f, 450.0f, 380.0f, 46.0f }; }

void drawPauseBox() {
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.55f));
    Rectangle box{ SCREEN_W / 2.0f - 245, 240, 490, 290 };
    DrawRectangleRounded(box, 0.08f, 12, Color{ 9, 17, 42, 248 });
    DrawRectangleRoundedLinesEx(box, 0.08f, 12, 2.5f, SKYBLUE);
    drawCentered("PAUSED", 262, 46, SKYBLUE);

    Rectangle resumeBtn = pauseResumeRect();
    Rectangle resetBtn = pauseResetRect();
    Rectangle menuBtn = pauseMenuRect();

    DrawRectangleRounded(resumeBtn, 0.25f, 8, isRectHeld(resumeBtn) ? Fade(SKYBLUE, 0.45f) : Fade(SKYBLUE, 0.22f));
    DrawRectangleRoundedLinesEx(resumeBtn, 0.25f, 8, 1.5f, SKYBLUE);
    drawPlayIcon(resumeBtn.x + 28.0f, resumeBtn.y + 23.0f, 18, SKYBLUE);
    DrawText("P / TAP: RESUME GAME", (int)(resumeBtn.x + 52.0f), (int)(resumeBtn.y + 13.0f), 19, WHITE);

    DrawRectangleRounded(resetBtn, 0.25f, 8, isRectHeld(resetBtn) ? Fade(ORANGE, 0.45f) : Fade(ORANGE, 0.22f));
    DrawRectangleRoundedLinesEx(resetBtn, 0.25f, 8, 1.5f, ORANGE);
    drawRestartIcon(resetBtn.x + 28.0f, resetBtn.y + 23.0f, 18, ORANGE);
    DrawText("ESC / TAP: RESTART LEVEL", (int)(resetBtn.x + 52.0f), (int)(resetBtn.y + 13.0f), 19, WHITE);

    DrawRectangleRounded(menuBtn, 0.25f, 8, isRectHeld(menuBtn) ? Fade(RED, 0.45f) : Fade(RED, 0.22f));
    DrawRectangleRoundedLinesEx(menuBtn, 0.25f, 8, 1.5f, RED);
    drawExitIcon(menuBtn.x + 28.0f, menuBtn.y + 23.0f, 18, RED);
    DrawText("M / TAP: RETURN TO MENU", (int)(menuBtn.x + 52.0f), (int)(menuBtn.y + 13.0f), 19, WHITE);
}

void drawLevelComplete() {
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.72f));
    char buf[128];
    drawCentered("LEVEL COMPLETE", 220, 52, SKYBLUE);
    snprintf(buf, sizeof(buf), "Level %i cleared!", level);
    drawCentered(buf, 305, 26, WHITE);
    snprintf(buf, sizeof(buf), "Level score: %i", score - levelStartScore);
    drawCentered(buf, 350, 23, WHITE);
    snprintf(buf, sizeof(buf), "Total score: %i", score);
    drawCentered(buf, 385, 23, WHITE);
    snprintf(buf, sizeof(buf), "Lives: %i", player.lives);
    drawCentered(buf, 420, 23, WHITE);

    Rectangle nextBtn{ SCREEN_W / 2.0f - 280.0f, 485.0f, 265.0f, 44.0f };
    Rectangle menuBtn{ SCREEN_W / 2.0f + 15.0f, 485.0f, 265.0f, 44.0f };
    DrawRectangleRounded(nextBtn, 0.25f, 8, Fade(LIME, 0.25f));
    DrawRectangleRoundedLinesEx(nextBtn, 0.25f, 8, 1.5f, LIME);
    drawPlayIcon(nextBtn.x + 24.0f, nextBtn.y + 22.0f, 18, LIME);
    DrawText(level >= MAX_LEVEL ? "SPACE / TAP: FINISH" : "SPACE / TAP: NEXT LVL", (int)(nextBtn.x + 44.0f), (int)(nextBtn.y + 13.0f), 17, WHITE);

    DrawRectangleRounded(menuBtn, 0.25f, 8, Fade(SKYBLUE, 0.25f));
    DrawRectangleRoundedLinesEx(menuBtn, 0.25f, 8, 1.5f, SKYBLUE);
    drawArrowIcon(menuBtn.x + 24.0f, menuBtn.y + 22.0f, 18, -1, SKYBLUE);
    DrawText("M / TAP: MAIN MENU", (int)(menuBtn.x + 46.0f), (int)(menuBtn.y + 13.0f), 17, WHITE);
}

void drawEndScreen(const char* title, int reached) {
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.72f));
    char buf[96];
    drawCentered(title, 200, 52, SKYBLUE);
    snprintf(buf, sizeof(buf), "Final score: %i", score);
    drawCentered(buf, 290, 26, WHITE);
    snprintf(buf, sizeof(buf), "High score: %i", highScore);
    drawCentered(buf, 330, 26, WHITE);
    snprintf(buf, sizeof(buf), "Level reached: %i / %i", reached, MAX_LEVEL);
    drawCentered(buf, 370, 26, WHITE);
    if (newHighScore) drawCentered("NEW HIGH SCORE!", 420, 32, GOLD);

    Rectangle againBtn{ SCREEN_W / 2.0f - 285.0f, 495.0f, 270.0f, 46.0f };
    Rectangle menuBtn{ SCREEN_W / 2.0f + 15.0f, 495.0f, 270.0f, 46.0f };
    DrawRectangleRounded(againBtn, 0.25f, 8, Fade(LIME, 0.25f));
    DrawRectangleRoundedLinesEx(againBtn, 0.25f, 8, 1.5f, LIME);
    drawRestartIcon(againBtn.x + 25.0f, againBtn.y + 23.0f, 18, LIME);
    DrawText("SPACE / TAP: PLAY AGAIN", (int)(againBtn.x + 45.0f), (int)(againBtn.y + 14.0f), 17, WHITE);

    DrawRectangleRounded(menuBtn, 0.25f, 8, Fade(SKYBLUE, 0.25f));
    DrawRectangleRoundedLinesEx(menuBtn, 0.25f, 8, 1.5f, SKYBLUE);
    drawArrowIcon(menuBtn.x + 25.0f, menuBtn.y + 23.0f, 18, -1, SKYBLUE);
    DrawText("M / TAP: MAIN MENU", (int)(menuBtn.x + 47.0f), (int)(menuBtn.y + 14.0f), 17, WHITE);
}

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "Space Shooter - Raylib Edition");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    SetRandomSeed((unsigned int)time(nullptr));
    loadHighScore();
    initStars();
    player.lives = 3;

    int prevTouches = 0;

    while (!WindowShouldClose()) {
        float dt = std::min(GetFrameTime(), 0.05f);
        int touches = GetTouchPointCount();
        bool newTouchOrClick = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || (touches > prevTouches);

        auto tapped = [&](Rectangle r) -> bool {
            if (!newTouchOrClick) return false;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && isPointInRect(GetMousePosition(), r)) return true;
            for (int i = 0; i < touches; ++i) {
                if (isPointInRect(GetTouchPosition(i), r)) return true;
            }
            return false;
            };

        if (screen == MENU) {
            if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) menuChoice = (menuChoice + 1) % 4;
            if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) menuChoice = (menuChoice + 3) % 4;

            int tappedChoice = -1;
            for (int i = 0; i < 4; ++i) {
                if (tapped(menuItemRect(i))) {
                    menuChoice = i;
                    tappedChoice = i;
                    break;
                }
            }

            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || tappedChoice != -1) {
                int choice = (tappedChoice != -1) ? tappedChoice : menuChoice;
                if (choice == 0) startNewGame();
                else if (choice == 1) screen = INSTRUCTIONS;
                else if (choice == 2) screen = HIGH_SCORE;
                else break;
            }
            if (IsKeyPressed(KEY_ESCAPE)) break;
        }
        else if (screen == INSTRUCTIONS) {
            Rectangle startBtn{ SCREEN_W / 2.0f - 290.0f, 625.0f, 260.0f, 44.0f };
            Rectangle backBtn{ SCREEN_W / 2.0f + 30.0f, 625.0f, 260.0f, 44.0f };
            if (IsKeyPressed(KEY_SPACE) || tapped(startBtn)) startNewGame();
            else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || tapped(backBtn)) screen = MENU;
        }
        else if (screen == HIGH_SCORE) {
            Rectangle backBtn{ SCREEN_W / 2.0f - 150.0f, 585.0f, 300.0f, 46.0f };
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE) || tapped(backBtn)) screen = MENU;
        }
        else if (screen == PLAYING) {
            updateGame(dt);
        }
        else if (screen == PAUSED) {
            if (IsKeyPressed(KEY_P) || tapped(pauseResumeRect())) screen = PLAYING;
            else if (IsKeyPressed(KEY_ESCAPE) || tapped(pauseResetRect())) restartCurrentLevel();
            else if (IsKeyPressed(KEY_M) || tapped(pauseMenuRect())) exitToMainMenu();
        }
        else if (screen == LEVEL_COMPLETE) {
            Rectangle nextBtn{ SCREEN_W / 2.0f - 280.0f, 485.0f, 265.0f, 44.0f };
            Rectangle menuBtn{ SCREEN_W / 2.0f + 15.0f, 485.0f, 265.0f, 44.0f };
            if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || tapped(nextBtn)) {
                if (level >= MAX_LEVEL) { screen = YOU_WIN; recordRunScore(); }
                else { level++; levelStartScore = score; levelStartLives = player.lives; setupLevel(); screen = PLAYING; }
            }
            else if (IsKeyPressed(KEY_M) || tapped(menuBtn)) exitToMainMenu();
        }
        else if (screen == GAME_OVER || screen == YOU_WIN) {
            Rectangle againBtn{ SCREEN_W / 2.0f - 285.0f, 495.0f, 270.0f, 46.0f };
            Rectangle menuBtn{ SCREEN_W / 2.0f + 15.0f, 495.0f, 270.0f, 46.0f };
            if (IsKeyPressed(KEY_SPACE) || tapped(againBtn)) startNewGame();
            else if (IsKeyPressed(KEY_M) || tapped(menuBtn)) screen = MENU;
            else if (IsKeyPressed(KEY_ESCAPE)) break;
        }

        BeginDrawing();
        if (screen == MENU) drawMenu();
        else if (screen == INSTRUCTIONS) drawInstructions();
        else if (screen == HIGH_SCORE) drawHighScore();
        else if (screen == PLAYING) drawGameplay(dt);
        else if (screen == PAUSED) { drawGameplay(0.0f); drawPauseBox(); }
        else if (screen == LEVEL_COMPLETE) { drawGameplay(0.0f); drawLevelComplete(); }
        else if (screen == GAME_OVER) { drawBackground(dt, false, level); drawEndScreen("GAME OVER", lastLevelReached); }
        else if (screen == YOU_WIN) { drawBackground(dt, false, level); drawEndScreen("YOU SAVED THE GALAXY!", MAX_LEVEL); }
        EndDrawing();

        prevTouches = touches;
    }

    saveHighScore();
    CloseWindow();
    return 0;
}
