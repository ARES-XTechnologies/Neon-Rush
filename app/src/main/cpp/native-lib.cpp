#include <jni.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>
#include <atomic>

#define LOG_TAG "NEON_RUSH"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define PI 3.14159265358979323846f

namespace {

struct V2 {
    float x{}, y{};
};

struct Enemy {
    V2 p{};
    float r{0.055f};
    float speed{0.18f};
    float hp{1.0f};
    float maxHp{1.0f};
    float phase{0.0f};
    float hitFlash{0.0f};
};

struct Bullet {
    V2 p{};
    V2 v{};
    float life{1.2f};
    float r{0.018f};
    float damage{1.0f};
};

struct Particle {
    V2 p{}, v{};
    float life{0.45f};
    float size{0.02f};
};

struct State {
    bool initialized = false;
    bool touchMove = false;
    bool touchFire = false;
    int movePointerId = -1;
    int firePointerId = -1;

    V2 player{0.0f, 0.0f};
    V2 moveStick{0.0f, 0.0f};
    V2 lastAim{0.0f, 1.0f};

    float playerRadius = 0.065f;
    float playerSpeed = 0.72f;
    float maxHealth = 100.0f;
    float health = 100.0f;
    std::atomic<int> healthDisplay{100};
    float invulnerable = 0.0f;
    float hitFlash = 0.0f;
    float fireCooldown = 0.0f;
    float spawnCooldown = 0.15f;
    float time = 0.0f;
    std::atomic<bool> gameOver{false};

    std::atomic<int> score{0};
    int enemiesDestroyed = 0;
    int totalHits = 0;

    std::vector<Enemy> enemies;
    std::vector<Bullet> bullets;
    std::vector<Particle> particles;

    std::mt19937 rng{std::random_device{}()};
    std::chrono::steady_clock::time_point lastFrame{};

    GLuint program = 0;
    GLint colorLoc = -1;
    GLint posLoc = -1;
    GLint pointSizeLoc = -1;
};

State g;

float frand(float a, float b) {
    std::uniform_real_distribution<float> d(a, b);
    return d(g.rng);
}

float len(V2 v) { return std::sqrt(v.x*v.x + v.y*v.y); }

V2 norm(V2 v) {
    float l = len(v);
    return (l > 0.0001f) ? V2{v.x/l, v.y/l} : V2{0.0f, 0.0f};
}

V2 add(V2 a, V2 b) { return {a.x+b.x, a.y+b.y}; }
V2 sub(V2 a, V2 b) { return {a.x-b.x, a.y-b.y}; }
V2 mul(V2 a, float s) { return {a.x*s, a.y*s}; }
float dot(V2 a, V2 b) { return a.x*b.x + a.y*b.y; }

bool circleHit(V2 a, float ar, V2 b, float br) {
    const V2 d = sub(a, b);
    const float rr = ar + br;
    return dot(d, d) <= rr * rr;
}

float pointSegmentDistanceSq(V2 point, V2 a, V2 b) {
    const V2 ab = sub(b, a);
    const float ab2 = dot(ab, ab);
    if (ab2 <= 0.000001f) {
        const V2 d = sub(point, a);
        return dot(d, d);
    }
    float t = dot(sub(point, a), ab) / ab2;
    t = std::clamp(t, 0.0f, 1.0f);
    const V2 closest = add(a, mul(ab, t));
    const V2 d = sub(point, closest);
    return dot(d, d);
}

bool segmentCircleHit(V2 start, V2 end, float radius, V2 center, float circleRadius) {
    const float rr = radius + circleRadius;
    return pointSegmentDistanceSq(center, start, end) <= rr * rr;
}

GLuint makeShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        LOGI("Shader error: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool makeProgram() {
    const char* vs = R"GLSL(
        attribute vec2 aPos;
        uniform float uPointSize;
        void main() {
            gl_Position = vec4(aPos, 0.0, 1.0);
            gl_PointSize = uPointSize;
        }
    )GLSL";

    const char* fs = R"GLSL(
        precision mediump float;
        uniform vec4 uColor;
        void main() {
            gl_FragColor = uColor;
        }
    )GLSL";

    GLuint v = makeShader(GL_VERTEX_SHADER, vs);
    GLuint f = makeShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return false;
    }

    g.program = glCreateProgram();
    glAttachShader(g.program, v);
    glAttachShader(g.program, f);
    glBindAttribLocation(g.program, 0, "aPos");
    glLinkProgram(g.program);

    GLint ok = GL_FALSE;
    glGetProgramiv(g.program, GL_LINK_STATUS, &ok);
    glDeleteShader(v);
    glDeleteShader(f);

    if (!ok) {
        char log[1024]{};
        glGetProgramInfoLog(g.program, sizeof(log), nullptr, log);
        LOGI("Program link error: %s", log);
        glDeleteProgram(g.program);
        g.program = 0;
        return false;
    }

    g.posLoc = 0;
    g.colorLoc = glGetUniformLocation(g.program, "uColor");
    g.pointSizeLoc = glGetUniformLocation(g.program, "uPointSize");
    return true;
}

void color(float r, float gg, float b, float a = 1.0f) {
    glUniform4f(g.colorLoc, r, gg, b, a);
}

void drawPoints(const std::vector<V2>& pts, float size, float r, float gg, float b, float a=1.0f) {
    if (pts.empty()) return;
    glVertexAttribPointer(g.posLoc, 2, GL_FLOAT, GL_FALSE, sizeof(V2), pts.data());
    glEnableVertexAttribArray(g.posLoc);
    glUniform1f(g.pointSizeLoc, size);
    color(r, gg, b, a);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(pts.size()));
}

void drawLines(const std::vector<V2>& pts, float r, float gg, float b, float a=1.0f) {
    if (pts.size() < 2) return;
    glVertexAttribPointer(g.posLoc, 2, GL_FLOAT, GL_FALSE, sizeof(V2), pts.data());
    glEnableVertexAttribArray(g.posLoc);
    glUniform1f(g.pointSizeLoc, 1.0f);
    color(r, gg, b, a);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(pts.size()));
}

void circle(V2 c, float radius, int segments, float r, float gg, float b, float a=1.0f) {
    std::vector<V2> pts;
    pts.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        float t = (2.0f * PI * i) / segments;
        pts.push_back({c.x + std::cos(t)*radius, c.y + std::sin(t)*radius});
    }
    // GL_LINES needs pairs, so duplicate/bridge around the loop.
    std::vector<V2> lines;
    lines.reserve(static_cast<size_t>(segments) * 2);
    for (int i = 0; i < segments; ++i) {
        lines.push_back(pts[i]);
        lines.push_back(pts[i + 1]);
    }
    drawLines(lines, r, gg, b, a);
}

void filledCircle(V2 c, float radius, float r, float gg, float b, float a=1.0f) {
    std::vector<V2> fan;
    fan.reserve(67);
    fan.push_back(c);
    for (int i = 0; i <= 64; ++i) {
        float t = (2.0f * PI * i) / 64.0f;
        fan.push_back({c.x + std::cos(t)*radius, c.y + std::sin(t)*radius});
    }

    glVertexAttribPointer(g.posLoc, 2, GL_FLOAT, GL_FALSE, sizeof(V2), fan.data());
    glEnableVertexAttribArray(g.posLoc);
    color(r, gg, b, a);
    glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(fan.size()));
}

void triangle(V2 c, float radius, float angle, float r, float gg, float b, float a=1.0f) {
    V2 p[4];
    for (int i = 0; i < 3; ++i) {
        float t = angle + (2.0f * PI * i) / 3.0f;
        p[i] = {c.x + std::cos(t)*radius, c.y + std::sin(t)*radius};
    }
    p[3] = p[0];
    drawLines({p[0],p[1],p[1],p[2],p[2],p[0]}, r, gg, b, a);
}

void rectangleOutline(float left, float bottom, float right, float top,
                      float r, float gg, float b, float a=1.0f) {
    drawLines({
        {left,bottom},{right,bottom},
        {right,bottom},{right,top},
        {right,top},{left,top},
        {left,top},{left,bottom}
    }, r, gg, b, a);
}

void spawnEnemy() {
    if (g.enemies.size() >= 22) return;

    const float side = frand(0.0f, 4.0f);
    Enemy e;
    if (side < 1.0f) e.p = {frand(-0.95f, 0.95f), 1.08f};
    else if (side < 2.0f) e.p = {frand(-0.95f, 0.95f), -1.08f};
    else if (side < 3.0f) e.p = {-1.08f, frand(-0.95f, 0.95f)};
    else e.p = {1.08f, frand(-0.95f, 0.95f)};

    e.speed = frand(0.12f, 0.24f) + std::min(0.12f, g.time / 180.0f);
    e.r = frand(0.045f, 0.065f);

    // A few drones are tougher without introducing a second enemy archetype yet.
    const float roll = frand(0.0f, 1.0f);
    e.maxHp = (roll < 0.78f) ? 2.0f : 3.0f;
    e.hp = e.maxHp;
    e.phase = frand(0.0f, 6.28f);
    g.enemies.push_back(e);
}

void muzzleBurst(V2 at, V2 dir) {
    for (int i = 0; i < 8; ++i) {
        Particle p;
        p.p = at;
        float spread = frand(-0.55f, 0.55f);
        float c = std::cos(spread), s = std::sin(spread);
        V2 v{dir.x*c - dir.y*s, dir.x*s + dir.y*c};
        p.v = mul(v, frand(0.25f, 0.60f));
        p.life = frand(0.12f, 0.26f);
        p.size = frand(0.008f, 0.022f);
        g.particles.push_back(p);
    }
}

void hitBurst(V2 at, V2 dir) {
    for (int i = 0; i < 10; ++i) {
        Particle p;
        p.p = at;
        float spread = frand(-0.95f, 0.95f);
        float c = std::cos(spread), s = std::sin(spread);
        V2 v{dir.x*c - dir.y*s, dir.x*s + dir.y*c};
        p.v = mul(v, frand(0.14f, 0.45f));
        p.life = frand(0.10f, 0.30f);
        p.size = frand(0.007f, 0.018f);
        g.particles.push_back(p);
    }
}

void explode(V2 at) {
    for (int i = 0; i < 22; ++i) {
        Particle p;
        p.p = at;
        float ang = frand(0.0f, 2.0f*PI);
        p.v = {std::cos(ang)*frand(0.18f,0.68f), std::sin(ang)*frand(0.18f,0.68f)};
        p.life = frand(0.20f,0.60f);
        p.size = frand(0.01f,0.036f);
        g.particles.push_back(p);
    }
}

void playerDamageBurst(V2 at, V2 dir) {
    for (int i = 0; i < 16; ++i) {
        Particle p;
        p.p = at;
        float ang = std::atan2(dir.y, dir.x) + frand(-1.2f, 1.2f);
        p.v = {std::cos(ang)*frand(0.18f,0.56f), std::sin(ang)*frand(0.18f,0.56f)};
        p.life = frand(0.16f,0.44f);
        p.size = frand(0.009f,0.026f);
        g.particles.push_back(p);
    }
}

void shootAtNearest() {
    if (g.enemies.empty()) return;

    float best = 9999.0f;
    V2 aim = g.lastAim;
    for (const auto& e : g.enemies) {
        float d = len(sub(e.p, g.player));
        if (d < best) {
            best = d;
            aim = norm(sub(e.p, g.player));
        }
    }
    if (len(aim) < 0.1f) aim = g.lastAim;
    g.lastAim = aim;

    Bullet b;
    b.p = add(g.player, mul(aim, 0.10f));
    b.v = mul(aim, 1.62f);
    b.life = 1.05f;
    b.damage = 1.0f;
    g.bullets.push_back(b);
    muzzleBurst(b.p, aim);
}

void resetGame() {
    g.player = {0.0f, 0.0f};
    g.moveStick = {0.0f, 0.0f};
    g.lastAim = {0.0f, 1.0f};
    g.movePointerId = -1;
    g.firePointerId = -1;
    g.touchMove = false;
    g.touchFire = false;
    g.health = g.maxHealth;
    g.healthDisplay.store(static_cast<int>(g.maxHealth));
    g.invulnerable = 0.0f;
    g.hitFlash = 0.0f;
    g.fireCooldown = 0.0f;
    g.spawnCooldown = 0.25f;
    g.time = 0.0f;
    g.score.store(0);
    g.enemiesDestroyed = 0;
    g.totalHits = 0;
    g.gameOver.store(false);
    g.enemies.clear();
    g.bullets.clear();
    g.particles.clear();
}

void update(float dt) {
    dt = std::min(std::max(dt, 0.0f), 0.04f);
    if (g.gameOver.load()) {
        // Keep death particles animating while the game waits for a restart.
        for (auto& p : g.particles) {
            p.p = add(p.p, mul(p.v, dt));
            p.v = mul(p.v, 0.96f);
            p.life -= dt;
        }
        g.particles.erase(
            std::remove_if(g.particles.begin(), g.particles.end(),
                [](const Particle& p){ return p.life <= 0.0f; }),
            g.particles.end());
        return;
    }

    g.time += dt;
    g.invulnerable = std::max(0.0f, g.invulnerable - dt);
    g.hitFlash = std::max(0.0f, g.hitFlash - dt);

    const float boundary = 0.90f;

    V2 move = g.moveStick;
    float ml = len(move);
    if (ml > 1.0f) move = norm(move);

    g.player = add(g.player, mul(move, g.playerSpeed * dt));
    g.player.x = std::clamp(g.player.x, -boundary, boundary);
    g.player.y = std::clamp(g.player.y, -boundary, boundary);

    g.fireCooldown = std::max(0.0f, g.fireCooldown - dt);
    g.spawnCooldown -= dt;

    if (g.touchFire && g.fireCooldown <= 0.0f) {
        shootAtNearest();
        g.fireCooldown = 0.13f;
    }

    if (g.spawnCooldown <= 0.0f) {
        spawnEnemy();
        g.spawnCooldown = std::max(0.20f, 0.82f - g.time * 0.0025f);
    }

    for (auto& e : g.enemies) {
        V2 toPlayer = sub(g.player, e.p);
        V2 dir = norm(toPlayer);
        float wobble = std::sin(g.time * 3.2f + e.phase) * 0.35f;
        V2 side{-dir.y, dir.x};
        e.p = add(e.p, mul(add(dir, mul(side, wobble)), e.speed * dt));
        e.hitFlash = std::max(0.0f, e.hitFlash - dt);
    }

    for (auto& b : g.bullets) {
        b.p = add(b.p, mul(b.v, dt));
        b.life -= dt;
    }

    // Projectile-vs-enemy collision uses the bullet's travel segment so fast shots
    // don't tunnel through a small drone between frames.
    for (size_t bi = 0; bi < g.bullets.size();) {
        const V2 end = g.bullets[bi].p;
        const V2 start = sub(end, mul(g.bullets[bi].v, dt));
        bool removedBullet = false;

        for (size_t ei = 0; ei < g.enemies.size(); ++ei) {
            Enemy& e = g.enemies[ei];
            if (!segmentCircleHit(start, end, g.bullets[bi].r, e.p, e.r)) continue;

            const V2 hit = g.bullets[bi].p;
            const V2 impactDir = norm(g.bullets[bi].v);
            e.hp -= g.bullets[bi].damage;
            e.hitFlash = 0.13f;
            g.totalHits++;
            hitBurst(hit, impactDir);

            // A bullet is consumed on every confirmed hit in v0.2.
            g.bullets.erase(g.bullets.begin() + static_cast<long>(bi));
            removedBullet = true;

            if (e.hp <= 0.0f) {
                explode(e.p);
                g.score.fetch_add(100);
                g.enemiesDestroyed++;
                g.enemies.erase(g.enemies.begin() + static_cast<long>(ei));
            }
            break;
        }

        if (!removedBullet) {
            ++bi;
        }
    }

    g.bullets.erase(
        std::remove_if(g.bullets.begin(), g.bullets.end(),
            [](const Bullet& b){ return b.life <= 0.0f || std::abs(b.p.x) > 1.2f || std::abs(b.p.y) > 1.2f; }),
        g.bullets.end());

    for (auto& p : g.particles) {
        p.p = add(p.p, mul(p.v, dt));
        p.v = mul(p.v, 0.96f);
        p.life -= dt;
    }
    g.particles.erase(
        std::remove_if(g.particles.begin(), g.particles.end(),
            [](const Particle& p){ return p.life <= 0.0f; }),
        g.particles.end());

    // Player contact damage: an invulnerability window prevents a single drone
    // from deleting the entire health bar in a few consecutive frames.
    if (g.invulnerable <= 0.0f) {
        for (auto& e : g.enemies) {
            if (!circleHit(g.player, g.playerRadius, e.p, e.r * 0.9f)) continue;

            V2 away = norm(sub(g.player, e.p));
            if (len(away) < 0.1f) away = {0.0f, 1.0f};
            g.health -= 15.0f;
            g.health = std::max(0.0f, g.health);
            g.healthDisplay.store(static_cast<int>(std::round(g.health)));
            g.invulnerable = 0.62f;
            g.hitFlash = 0.22f;
            playerDamageBurst(g.player, away);
            g.player = add(g.player, mul(away, 0.07f));
            g.player.x = std::clamp(g.player.x, -boundary, boundary);
            g.player.y = std::clamp(g.player.y, -boundary, boundary);

            if (g.health <= 0.0f) {
                g.gameOver.store(true);
                explode(g.player);
                break;
            }
            break;
        }
    }
}

void render(int width, int height) {
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.008f, 0.012f, 0.025f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!g.program) return;

    glUseProgram(g.program);

    // Procedural arena grid.
    std::vector<V2> grid;
    grid.reserve(84);
    for (int i = -10; i <= 10; ++i) {
        float p = i / 10.0f;
        grid.push_back({p, -1.0f});
        grid.push_back({p, 1.0f});
        grid.push_back({-1.0f, p});
        grid.push_back({1.0f, p});
    }
    drawLines(grid, 0.02f, 0.26f, 0.33f, 0.28f);

    circle({0.0f,0.0f}, 0.91f, 96, 0.05f, 0.75f, 0.95f, 0.45f);
    circle({0.0f,0.0f}, 0.87f, 96, 0.02f, 0.15f, 0.22f, 0.75f);

    // Damage vignette-like procedural rings around the player.
    if (g.invulnerable > 0.0f) {
        float pulse = 0.5f + 0.5f * std::sin(g.time * 26.0f);
        circle(g.player, g.playerRadius * (1.8f + 0.22f*pulse), 28, 1.0f, 0.2f, 0.36f, 0.35f);
    }

    // Particles.
    for (const auto& p : g.particles) {
        float alpha = std::clamp(p.life * 2.4f, 0.0f, 1.0f);
        drawPoints({p.p}, 8.0f + p.size*100.0f, 0.10f, 0.85f, 1.0f, alpha);
    }

    // Bullets.
    for (const auto& b : g.bullets) {
        drawPoints({b.p}, 13.0f, 0.15f, 0.95f, 1.0f, 1.0f);
        circle(b.p, 0.026f, 16, 0.05f, 0.40f, 0.55f, 0.75f);
    }

    // Enemies + compact health bars.
    for (const auto& e : g.enemies) {
        float pulse = 0.85f + 0.15f * std::sin(g.time*6.0f + e.phase);
        const bool flashing = e.hitFlash > 0.0f;
        if (flashing) {
            filledCircle(e.p, e.r*0.86f, 1.0f, 0.85f, 0.85f, 1.0f);
        } else {
            filledCircle(e.p, e.r*0.72f, 1.0f, 0.18f, 0.45f, 0.95f);
        }
        circle(e.p, e.r, 20, 1.0f, 0.05f, 0.40f, 0.90f);
        circle(e.p, e.r*(0.55f + 0.10f*pulse), 16, 1.0f, 0.35f, 0.70f, 0.40f);

        const float barW = e.r * 2.2f;
        const float barY = e.p.y + e.r + 0.025f;
        const float left = e.p.x - barW * 0.5f;
        const float right = e.p.x + barW * 0.5f;
        rectangleOutline(left, barY, right, barY + 0.012f, 0.15f, 0.72f, 0.80f, 0.55f);
        const float hpRatio = std::clamp(e.hp / e.maxHp, 0.0f, 1.0f);
        if (hpRatio > 0.0f) {
            drawLines({
                {left, barY + 0.006f}, {left + (right-left)*hpRatio, barY + 0.006f}
            }, 1.0f, 0.15f, 0.42f, 0.92f);
        }
    }

    // Player.
    float a = std::atan2(g.lastAim.y, g.lastAim.x);
    if (len(g.moveStick) > 0.1f) a = std::atan2(g.moveStick.y, g.moveStick.x);
    const bool playerFlashing = g.hitFlash > 0.0f && (static_cast<int>(g.time * 24.0f) % 2 == 0);
    if (playerFlashing) {
        filledCircle(g.player, g.playerRadius*0.76f, 1.0f, 0.25f, 0.35f, 1.0f);
        triangle(g.player, g.playerRadius*1.32f, a, 1.0f, 0.35f, 0.42f, 1.0f);
    } else {
        filledCircle(g.player, g.playerRadius*0.72f, 0.05f, 0.80f, 0.98f, 1.0f);
        triangle(g.player, g.playerRadius*1.28f, a, 0.08f, 0.92f, 1.0f, 1.0f);
    }
    circle(g.player, g.playerRadius*1.5f, 24, 0.02f, 0.65f, 0.85f, 0.55f);

    // Touch controls.
    const V2 moveBase{-0.68f, -0.67f};
    circle(moveBase, 0.17f, 48, 0.10f, 0.65f, 0.78f, g.touchMove ? 0.28f : 0.12f);
    circle(add(moveBase, mul(g.moveStick, 0.11f)), 0.07f, 32, 0.30f, 0.95f, 1.0f, g.touchMove ? 0.55f : 0.12f);

    circle({0.72f, -0.67f}, 0.17f, 48, 0.90f, 0.20f, 0.60f, g.touchFire ? 0.40f : 0.14f);
    circle({0.72f, -0.67f}, 0.10f, 32, 1.0f, 0.50f, 0.80f, g.touchFire ? 0.35f : 0.12f);

    // Procedural top-center health meter.
    const float healthRatio = std::clamp(g.health / g.maxHealth, 0.0f, 1.0f);
    const float hLeft = -0.42f, hRight = 0.42f, hBottom = 0.91f, hTop = 0.945f;
    rectangleOutline(hLeft, hBottom, hRight, hTop, 0.10f, 0.55f, 0.68f, 0.55f);
    if (healthRatio > 0.0f) {
        const float fillRight = hLeft + (hRight - hLeft) * healthRatio;
        drawLines({{hLeft, (hBottom+hTop)*0.5f}, {fillRight, (hBottom+hTop)*0.5f}},
                  0.15f, 0.95f, 0.62f, 0.90f);
    }

    // Score energy ticks.
    const int ticks = std::min(12, g.score.load() / 100);
    for (int i=0;i<ticks;++i) {
        float x = -0.85f + i*0.09f;
        rectangleOutline(x, 0.82f, x+0.065f, 0.85f,
                         0.15f,0.85f,1.0f,0.65f);
    }

    if (g.gameOver.load()) {
        // Full-screen procedural border communicates the end state; Java text
        // overlays provide readable text without importing a font asset.
        rectangleOutline(-0.92f, -0.92f, 0.92f, 0.92f, 1.0f, 0.16f, 0.32f, 0.85f);
        circle(g.player, 0.15f + 0.02f*std::sin(g.time*3.0f), 36, 1.0f, 0.10f, 0.26f, 0.65f);
    }
}

void applyTouch(int action, float x, float y, int width, int height, int pointerId) {
    if (width <= 0 || height <= 0 || !g.initialized) return;

    float nx = (2.0f * x / width) - 1.0f;
    float ny = -((2.0f * y / height) - 1.0f);

    const bool inMoveZone = (nx < -0.35f && ny < -0.35f);
    const bool inFireZone = (nx > 0.35f && ny < -0.35f);

    if (g.gameOver.load() && action == 0) {
        resetGame();
        return;
    }

    switch (action) {
        case 0:   // ACTION_DOWN
        case 5: { // ACTION_POINTER_DOWN
            if (inMoveZone && !g.touchMove) {
                g.touchMove = true;
                g.movePointerId = pointerId;
                V2 base{-0.68f,-0.67f};
                V2 stick = sub({nx,ny}, base);
                if (len(stick) > 0.22f) stick = mul(norm(stick),0.22f);
                g.moveStick = mul(stick, 1.0f/0.22f);
            } else if (inFireZone && !g.touchFire) {
                g.touchFire = true;
                g.firePointerId = pointerId;
            }
            break;
        }
        case 2: { // ACTION_MOVE
            if (pointerId == g.movePointerId && g.touchMove) {
                V2 base{-0.68f,-0.67f};
                V2 stick = sub({nx,ny}, base);
                if (len(stick) > 0.22f) stick = mul(norm(stick),0.22f);
                g.moveStick = mul(stick, 1.0f/0.22f);
            }
            if (pointerId == g.firePointerId) {
                g.touchFire = inFireZone;
            }
            break;
        }
        case 1:   // ACTION_UP
        case 6: { // ACTION_POINTER_UP
            if (pointerId == g.movePointerId) {
                g.movePointerId = -1;
                g.touchMove = false;
                g.moveStick = {0.0f,0.0f};
            }
            if (pointerId == g.firePointerId) {
                g.firePointerId = -1;
                g.touchFire = false;
            }
            break;
        }
        case 3: { // ACTION_CANCEL
            if (pointerId == g.movePointerId || pointerId < 0) {
                g.movePointerId = -1;
                g.touchMove = false;
                g.moveStick = {0.0f,0.0f};
            }
            if (pointerId == g.firePointerId || pointerId < 0) {
                g.firePointerId = -1;
                g.touchFire = false;
            }
            break;
        }
        default:
            break;
    }
}

} // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_aresx_neonrush_MainActivity_00024NeonSurface_nativeInit(JNIEnv*, jclass) {
    if (g.initialized) return;
    if (!makeProgram()) {
        LOGI("Failed to create shader program");
        return;
    }
    resetGame();
    g.initialized = true;
    g.lastFrame = std::chrono::steady_clock::now();
    LOGI("NEON RUSH v0.2 initialized");
}

extern "C" JNIEXPORT void JNICALL
Java_com_aresx_neonrush_MainActivity_00024NeonSurface_nativeShutdown(JNIEnv*, jclass) {
    if (g.program) {
        glDeleteProgram(g.program);
        g.program = 0;
    }
    g.enemies.clear();
    g.bullets.clear();
    g.particles.clear();
    g.initialized = false;
}

extern "C" JNIEXPORT void JNICALL
Java_com_aresx_neonrush_MainActivity_00024NeonSurface_nativeResize(
        JNIEnv*, jclass, jint width, jint height) {
    if (width > 0 && height > 0) {
        glViewport(0, 0, width, height);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_aresx_neonrush_MainActivity_00024NeonSurface_nativeStep(
        JNIEnv*, jclass) {
    if (!g.initialized) return;

    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - g.lastFrame).count();
    g.lastFrame = now;

    update(dt);

    GLint viewport[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    render(viewport[2], viewport[3]);
}

extern "C" JNIEXPORT void JNICALL
Java_com_aresx_neonrush_MainActivity_00024NeonSurface_nativeTouch(
        JNIEnv*, jclass, jint action, jfloat x, jfloat y, jint width, jint height, jint pointerId) {
    applyTouch(action, x, y, width, height, pointerId);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_aresx_neonrush_MainActivity_00024NeonSurface_nativeGetScore(
        JNIEnv*, jclass) {
    return static_cast<jint>(g.score.load());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_aresx_neonrush_MainActivity_00024NeonSurface_nativeGetHealth(
        JNIEnv*, jclass) {
    return static_cast<jint>(g.healthDisplay.load());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_aresx_neonrush_MainActivity_00024NeonSurface_nativeIsGameOver(
        JNIEnv*, jclass) {
    return g.gameOver.load() ? JNI_TRUE : JNI_FALSE;
}

