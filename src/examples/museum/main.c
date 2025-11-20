// Platform detection
#if defined(_WIN32) || defined(_WIN64)
#define PLATFORM_WINDOWS
#else
#define _POSIX_C_SOURCE 200809L
#define PLATFORM_POSIX
#endif

#include "mathlib.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Platform-specific wrappers for popen/pclose
#ifdef PLATFORM_WINDOWS
#define popen _popen
#define pclose _pclose
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Museum dimensions
#define ROOM_SIZE 40.0f
#define ROOM_HEIGHT 15.0f
#define PAINTING_WIDTH 12.0f
#define PAINTING_HEIGHT 9.0f

// Animation parameters
#define NUM_PAINTINGS 4
#define EPICYCLES_PER_PAINTING 150
#define MAX_TRAIL_POINTS 1200  // Increased to handle full animation cycle
#define MAX_DRAWINGS 10

// Movement constants
#define GRAVITY 32.0f
#define MAX_SPEED 20.0f
#define CROUCH_SPEED 5.0f
#define JUMP_FORCE 12.0f
#define MAX_ACCEL 150.0f
#define FRICTION 0.86f
#define AIR_DRAG 0.98f
#define CONTROL 15.0f
#define CROUCH_HEIGHT 0.0f
#define STAND_HEIGHT 1.0f
#define BOTTOM_HEIGHT 0.5f

// Drawing structure
typedef struct {
    char name[64];
    char filepath[256];
    Complex* points;
    size_t n_points;
} Drawing;

// Body structure for player movement
typedef struct {
    Vector3 position;
    Vector3 velocity;
    Vector3 dir;
    bool isGrounded;
} Body;

// Painting structure
typedef struct {
    Vector3 position;
    Vector3 normal;
    RenderTexture2D texture;
    FourierResult fourier;
    float time;
    Vector2* trail;
    int trail_count;
    float line_thickness;
    float pencil_size;
    float speed;
} Painting;

// Generate a square path
static void generate_square_path(Complex* points, size_t n_points, float size)
{
    size_t points_per_side = n_points / 4;
    float half = size / 2.0;

    for (size_t i = 0; i < n_points; i++) {
        size_t side = i / points_per_side;
        float t = (i % points_per_side) / (float)points_per_side;

        switch (side) {
        case 0: // Top
            points[i] = (-half + t * size) + I * half;
            break;
        case 1: // Right
            points[i] = half + I * (half - t * size);
            break;
        case 2: // Bottom
            points[i] = (half - t * size) - I * half;
            break;
        default: // Left
            points[i] = -half + I * (-half + t * size);
            break;
        }
    }
}

// Load all available drawings
static int load_drawings(Drawing* drawings, int max_drawings)
{
    int count = 0;

    // Define available path files
    const char* paths[][2] = {
        { "Square", "generated" },
        { "Colibri", "examples/assets/paths/colibri.bin" },
        { "Monkey", "examples/assets/paths/monkey.bin" },
        { "Spider", "examples/assets/paths/spider.bin" },
    };

    const int n_paths = sizeof(paths) / sizeof(paths[0]);

    for (int i = 0; i < n_paths && count < max_drawings; i++) {
        Drawing* d = &drawings[count];
        strncpy(d->name, paths[i][0], sizeof(d->name) - 1);
        strncpy(d->filepath, paths[i][1], sizeof(d->filepath) - 1);

        // Generate or load path
        if (strcmp(paths[i][1], "generated") == 0) {
            // Generated shapes
            d->n_points = 400;
            d->points = malloc(d->n_points * sizeof(Complex));

            if (strcmp(paths[i][0], "Square") == 0) {
                generate_square_path(d->points, d->n_points, 6.0);
            }
            count++;
        } else {
            // Load from file
            d->points = load_path_binary(d->filepath, &d->n_points);
            if (d->points && d->n_points > 0) {
                printf("Loaded '%s': %zu points from %s\n", d->name, d->n_points, d->filepath);
                count++;
            } else {
                printf("Warning: Could not load '%s' from %s\n", d->name, d->filepath);
                if (d->points) {
                    free(d->points);
                }
                d->points = NULL;
            }
        }
    }

    return count;
}

// Free all drawings
static void free_drawings(Drawing* drawings, int count)
{
    for (int i = 0; i < count; i++) {
        free(drawings[i].points);
    }
}

// Initialize a painting with a drawing
static void init_painting(Painting* p, Vector3 pos, Vector3 normal, const Drawing* drawing)
{
    p->position = pos;
    p->normal = normal;
    p->texture = LoadRenderTexture((int)PAINTING_WIDTH * 80, (int)PAINTING_HEIGHT * 80);
    p->time = 0.0f;
    p->trail = malloc(MAX_TRAIL_POINTS * sizeof(Vector2));
    p->trail_count = 0;
    p->line_thickness = 1.0f;
    p->pencil_size = 6.0f;
    p->speed = 0.5f;

    // Use drawing points for Fourier analysis
    size_t n_epicycles = EPICYCLES_PER_PAINTING;
    size_t max_epicycles = drawing->n_points / 2;
    if (n_epicycles > max_epicycles) {
        n_epicycles = max_epicycles;
    }

    p->fourier = fourier_analyze(drawing->points, drawing->n_points, n_epicycles);
}

// Update painting animation
static void update_painting(Painting* p, float dt)
{
    // Painting display scale - adjusted to fit nicely in the painting frame
    const float PAINTING_SCALE = 60.0f;
    int tex_w = p->texture.texture.width;
    int tex_h = p->texture.texture.height;

    // Update time with adaptive trail interpolation (like simple_example)
    float old_time = p->time;
    float time_step = dt * p->speed;
    p->time += time_step;

    // Wrap around at 2π
    if (p->time > 2.0 * M_PI) {
        p->time = 0.0f;
        p->trail_count = 0; // Reset trail on loop
    } else {
        // Add intermediate trail points for smooth drawing regardless of speed
        // Target: one trail point every ~0.02 time units for smooth curves
        const float trail_time_step = 0.02f;
        int num_trail_points = (int)(time_step / trail_time_step) + 1;

        // Clamp to reasonable number to avoid performance issues
        if (num_trail_points > 20)
            num_trail_points = 20;

        for (int i = 0; i < num_trail_points && p->trail_count < MAX_TRAIL_POINTS; i++) {
            float t = old_time + (time_step * (i + 1)) / num_trail_points;

            // Calculate tip position at this interpolated time
            Complex* temp_positions = malloc((p->fourier.count + 1) * sizeof(Complex));
            Complex tip = epicycles_at_time(&p->fourier, t, temp_positions);
            free(temp_positions);

            p->trail[p->trail_count].x = tex_w / 2 + creal(tip) * PAINTING_SCALE;
            p->trail[p->trail_count].y = tex_h / 2 + cimag(tip) * PAINTING_SCALE;
            p->trail_count++;
        }
    }

    // Calculate epicycle positions for current time (for display)
    Complex* positions = malloc((p->fourier.count + 1) * sizeof(Complex));
    Complex tip = epicycles_at_time(&p->fourier, p->time, positions);

    // Render to texture
    BeginTextureMode(p->texture);
    ClearBackground(BLACK);

    float center_x = tex_w / 2.0f;
    float center_y = tex_h / 2.0f;

    // Draw semi-transparent path guide
    if (p->trail_count > 1) {
        for (int i = 1; i < p->trail_count; i++) {
            float alpha = 0.2f + 0.8f * (float)i / p->trail_count;
            DrawLineEx(p->trail[i - 1], p->trail[i], p->pencil_size / 3.0f, Fade(SKYBLUE, alpha));
        }
    }

    // Draw epicycles (only a few of them to keep it clean)
    for (size_t i = 0; i < p->fourier.count && i < 20; i++) {
        Vector2 center = {
            center_x + creal(positions[i]) * PAINTING_SCALE,
            center_y + cimag(positions[i]) * PAINTING_SCALE
        };
        Vector2 end = {
            center_x + creal(positions[i + 1]) * PAINTING_SCALE,
            center_y + cimag(positions[i + 1]) * PAINTING_SCALE
        };

        float radius = p->fourier.coefficients[i].amplitude * PAINTING_SCALE;

        // Draw circle with adjustable thickness
        int num_lines = (int)(p->line_thickness * 2);
        for (int t = 0; t < num_lines; t++) {
            float r_offset = t * 0.3f;
            DrawCircleLines((int)center.x, (int)center.y, radius + r_offset,
                Fade(GRAY, 0.6f));
        }

        // Draw radius line
        DrawLineEx(center, end, p->line_thickness + 1.0f, Fade(WHITE, 0.4f));
    }

    // Draw tip
    Vector2 tip_pos = { center_x + creal(tip) * PAINTING_SCALE, center_y + cimag(tip) * PAINTING_SCALE };
    DrawCircleV(tip_pos, p->pencil_size, RED);

    EndTextureMode();

    free(positions);
}

// Update body considering current world state
static void update_body(Body* body, float rot, int side, int forward, bool jumpPressed, bool crouchHold)
{
    Vector2 input = (Vector2) { (float)side, (float)-forward };
    float delta = GetFrameTime();

    if (!body->isGrounded)
        body->velocity.y -= GRAVITY * delta;

    if (body->isGrounded && jumpPressed) {
        body->velocity.y = JUMP_FORCE;
        body->isGrounded = false;
    }

    Vector3 front = (Vector3) { sinf(rot), 0.f, cosf(rot) };
    Vector3 right = (Vector3) { cosf(-rot), 0.f, sinf(-rot) };

    Vector3 desiredDir = (Vector3) { input.x * right.x + input.y * front.x, 0.0f, input.x * right.z + input.y * front.z };
    body->dir = Vector3Lerp(body->dir, desiredDir, CONTROL * delta);

    float decel = (body->isGrounded ? FRICTION : AIR_DRAG);
    Vector3 hvel = (Vector3) { body->velocity.x * decel, 0.0f, body->velocity.z * decel };

    float hvelLength = Vector3Length(hvel);
    if (hvelLength < (MAX_SPEED * 0.01f))
        hvel = (Vector3) { 0 };

    float speed = Vector3DotProduct(hvel, body->dir);

    float maxSpeed = (crouchHold ? CROUCH_SPEED : MAX_SPEED);
    float accel = Clamp(maxSpeed - speed, 0.f, MAX_ACCEL * delta);
    hvel.x += body->dir.x * accel;
    hvel.z += body->dir.z * accel;

    body->velocity.x = hvel.x;
    body->velocity.z = hvel.z;

    body->position.x += body->velocity.x * delta;
    body->position.y += body->velocity.y * delta;
    body->position.z += body->velocity.z * delta;

    // Collision with floor
    if (body->position.y <= 0.0f) {
        body->position.y = 0.0f;
        body->velocity.y = 0.0f;
        body->isGrounded = true;
    }

    // Collision with walls (keep player inside museum)
    float wall_limit = ROOM_SIZE / 2.0f - 1.0f;
    if (body->position.x < -wall_limit)
        body->position.x = -wall_limit;
    if (body->position.x > wall_limit)
        body->position.x = wall_limit;
    if (body->position.z < -wall_limit)
        body->position.z = -wall_limit;
    if (body->position.z > wall_limit)
        body->position.z = wall_limit;
}

// Update camera for FPS behaviour
static void update_camera_fps(Camera* camera, Vector2* lookRotation, float walkLerp, float headTimer, Vector2 lean)
{
    const Vector3 up = (Vector3) { 0.0f, 1.0f, 0.0f };
    const Vector3 targetOffset = (Vector3) { 0.0f, 0.0f, -1.0f };

    // Left and right
    Vector3 yaw = Vector3RotateByAxisAngle(targetOffset, up, lookRotation->x);

    // Clamp view up
    float maxAngleUp = Vector3Angle(up, yaw);
    maxAngleUp -= 0.001f;
    if (-(lookRotation->y) > maxAngleUp) {
        lookRotation->y = -maxAngleUp;
    }

    // Clamp view down
    float maxAngleDown = Vector3Angle(Vector3Negate(up), yaw);
    maxAngleDown *= -1.0f;
    maxAngleDown += 0.001f;
    if (-(lookRotation->y) < maxAngleDown) {
        lookRotation->y = -maxAngleDown;
    }

    // Up and down
    Vector3 right = Vector3Normalize(Vector3CrossProduct(yaw, up));

    // Rotate view vector around right axis
    float pitchAngle = -lookRotation->y - lean.y;
    pitchAngle = Clamp(pitchAngle, -PI / 2 + 0.0001f, PI / 2 - 0.0001f);
    Vector3 pitch = Vector3RotateByAxisAngle(yaw, right, pitchAngle);

    // Head animation
    float headSin = sinf(headTimer * PI);
    float headCos = cosf(headTimer * PI);
    const float stepRotation = 0.01f;
    camera->up = Vector3RotateByAxisAngle(up, pitch, headSin * stepRotation + lean.x);

    // Camera BOB
    const float bobSide = 0.1f;
    const float bobUp = 0.15f;
    Vector3 bobbing = Vector3Scale(right, headSin * bobSide);
    bobbing.y = fabsf(headCos * bobUp);

    camera->position = Vector3Add(camera->position, Vector3Scale(bobbing, walkLerp));
    camera->target = Vector3Add(camera->position, pitch);
}

// Draw a painting on the wall
static void draw_painting(const Painting* p)
{
    // Calculate painting corners based on position and normal
    // Offset painting slightly from wall to prevent z-fighting
    Vector3 offset_pos = Vector3Add(p->position, Vector3Scale(p->normal, 0.15f));

    Vector3 up = { 0, 1, 0 };
    Vector3 right = Vector3Normalize(Vector3CrossProduct(up, p->normal));

    Vector3 tl = Vector3Add(offset_pos, Vector3Scale(up, PAINTING_HEIGHT / 2));
    tl = Vector3Add(tl, Vector3Scale(right, -PAINTING_WIDTH / 2));

    Vector3 tr = Vector3Add(offset_pos, Vector3Scale(up, PAINTING_HEIGHT / 2));
    tr = Vector3Add(tr, Vector3Scale(right, PAINTING_WIDTH / 2));

    Vector3 br = Vector3Add(offset_pos, Vector3Scale(up, -PAINTING_HEIGHT / 2));
    br = Vector3Add(br, Vector3Scale(right, PAINTING_WIDTH / 2));

    Vector3 bl = Vector3Add(offset_pos, Vector3Scale(up, -PAINTING_HEIGHT / 2));
    bl = Vector3Add(bl, Vector3Scale(right, -PAINTING_WIDTH / 2));

    // Draw the painting as a textured quad
    rlSetTexture(p->texture.texture.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    rlTexCoord2f(0, 0);
    rlVertex3f(bl.x, bl.y, bl.z);
    rlTexCoord2f(1, 0);
    rlVertex3f(br.x, br.y, br.z);
    rlTexCoord2f(1, 1);
    rlVertex3f(tr.x, tr.y, tr.z);
    rlTexCoord2f(0, 1);
    rlVertex3f(tl.x, tl.y, tl.z);
    rlEnd();
    rlSetTexture(0);

    // Draw frame
    DrawLine3D(tl, tr, GOLD);
    DrawLine3D(tr, br, GOLD);
    DrawLine3D(br, bl, GOLD);
    DrawLine3D(bl, tl, GOLD);
}

int main(void)
{
#ifndef PLATFORM_WEB
    // Check if ffmpeg is available (before InitWindow to avoid stdout issues)
    bool ffmpeg_available = false;
#ifdef PLATFORM_WINDOWS
    FILE* ffmpeg_check = popen("ffmpeg -version 2>nul", "r");
#else
    FILE* ffmpeg_check = popen("ffmpeg -version 2>&1", "r");
#endif

    if (ffmpeg_check) {
        char buffer[256];
        // Try to read some output - if ffmpeg exists, it will output version info
        if (fgets(buffer, sizeof(buffer), ffmpeg_check) != NULL) {
            // Check if output contains "ffmpeg" to verify it's the real deal
            if (strstr(buffer, "ffmpeg") != NULL || strstr(buffer, "FFmpeg") != NULL) {
                ffmpeg_available = true;
            }
        }
        pclose(ffmpeg_check);
    }

    if (ffmpeg_available) {
        printf("ffmpeg detected - video recording enabled (press R)\n");
        fflush(stdout);
    } else {
        printf("ffmpeg not found - video recording disabled\n");
        fflush(stdout);
    }
#endif // PLATFORM_WEB

    // Initialization
    InitWindow(1200, 800, "Fourier Epicycles Museum");
    SetTargetFPS(60);

    // Setup player body
    Body player = { 0 };
    player.position = (Vector3) { 0.0f, 0.0f, 0.0f };
    player.velocity = (Vector3) { 0.0f, 0.0f, 0.0f };
    player.dir = (Vector3) { 0.0f, 0.0f, 0.0f };
    player.isGrounded = true;

    // Camera variables
    Vector2 sensitivity = { 0.001f, 0.001f };
    Vector2 lookRotation = { 0 };
    float headTimer = 0.0f;
    float walkLerp = 0.0f;
    float headLerp = STAND_HEIGHT;
    Vector2 lean = { 0 };
    float targetFov = 60.0f;
    float normalFov = 60.0f;
    float zoomedFov = 30.0f;

#ifndef PLATFORM_WEB
    // Video recording variables
    bool recording = false;
    FILE* ffmpeg_pipe = NULL;
    int frame_count = 0;
    int recording_fps = 20; // Expected FPS for recording (adjustable with [ and ])
#endif

    // Setup camera
    Camera camera = { 0 };
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    camera.position = (Vector3) {
        player.position.x,
        player.position.y + (BOTTOM_HEIGHT + headLerp),
        player.position.z,
    };
    camera.up = (Vector3) { 0.0f, 1.0f, 0.0f };

    DisableCursor();

    // Load all available drawings
    Drawing drawings[MAX_DRAWINGS];
    int n_drawings = load_drawings(drawings, MAX_DRAWINGS);

    printf("Loaded %d drawings for museum\n", n_drawings);

    if (n_drawings == 0) {
        fprintf(stderr, "No drawings available\n");
        CloseWindow();
        return 1;
    }

    // Initialize paintings on each wall
    Painting paintings[NUM_PAINTINGS];
    float wall_offset = ROOM_SIZE / 2.0f - 0.1f;

    // North wall - Square
    init_painting(&paintings[0],
        (Vector3) { 0, ROOM_HEIGHT / 2, -wall_offset },
        (Vector3) { 0, 0, 1 }, &drawings[0]);

    // South wall - Colibri (or second drawing if available)
    init_painting(&paintings[1],
        (Vector3) { 0, ROOM_HEIGHT / 2, wall_offset },
        (Vector3) { 0, 0, -1 }, &drawings[n_drawings > 1 ? 1 : 0]);

    // East wall - Monkey (or third drawing if available)
    init_painting(&paintings[2],
        (Vector3) { wall_offset, ROOM_HEIGHT / 2, 0 },
        (Vector3) { -1, 0, 0 }, &drawings[n_drawings > 2 ? 2 : 0]);

    // West wall - Spider (or fourth drawing if available)
    init_painting(&paintings[3],
        (Vector3) { -wall_offset, ROOM_HEIGHT / 2, 0 },
        (Vector3) { 1, 0, 0 }, &drawings[n_drawings > 3 ? 3 : 0]);

    // Main game loop
    while (!WindowShouldClose()) {
        // Update
        float dt = GetFrameTime();

        // Mouse look
        Vector2 mouseDelta = GetMouseDelta();
        lookRotation.x -= mouseDelta.x * sensitivity.x;
        lookRotation.y += mouseDelta.y * sensitivity.y;

        // Movement input
        int sideway = (IsKeyDown(KEY_D) - IsKeyDown(KEY_A));
        int forward = (IsKeyDown(KEY_W) - IsKeyDown(KEY_S));
        bool crouching = IsKeyDown(KEY_LEFT_CONTROL);

        // Zoom with left mouse button
        bool zooming = IsMouseButtonDown(MOUSE_BUTTON_LEFT);

#ifndef PLATFORM_WEB
        // Adjust recording FPS with [ and ] keys
        if (IsKeyPressed(KEY_LEFT_BRACKET)) {
            recording_fps -= 5;
            if (recording_fps < 5)
                recording_fps = 5;
            printf("Recording FPS: %d\n", recording_fps);
        }
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
            recording_fps += 5;
            if (recording_fps > 60)
                recording_fps = 60;
            printf("Recording FPS: %d\n", recording_fps);
        }
#endif

        // Update player body
        update_body(&player, lookRotation.x, sideway, forward, IsKeyPressed(KEY_SPACE), crouching);

        // Head bob and camera effects
        headLerp = Lerp(headLerp, (crouching ? CROUCH_HEIGHT : STAND_HEIGHT), 20.0f * dt);
        camera.position = (Vector3) {
            player.position.x,
            player.position.y + (BOTTOM_HEIGHT + headLerp),
            player.position.z,
        };

        // Update target FOV based on zoom and movement
        if (zooming) {
            targetFov = zoomedFov;
        } else if (player.isGrounded && ((forward != 0) || (sideway != 0))) {
            targetFov = 55.0f; // Slight zoom while walking
            headTimer += dt * 3.0f;
            walkLerp = Lerp(walkLerp, 1.0f, 10.0f * dt);
        } else {
            targetFov = normalFov;
            walkLerp = Lerp(walkLerp, 0.0f, 10.0f * dt);
        }

        // Smoothly interpolate to target FOV
        camera.fovy = Lerp(camera.fovy, targetFov, 10.0f * dt);

        lean.x = Lerp(lean.x, sideway * 0.02f, 10.0f * dt);
        lean.y = Lerp(lean.y, forward * 0.015f, 10.0f * dt);

        // Update camera orientation
        update_camera_fps(&camera, &lookRotation, walkLerp, headTimer, lean);

#ifndef PLATFORM_WEB
        // Toggle video recording with R key (only if ffmpeg available)
        if (ffmpeg_available && IsKeyPressed(KEY_R)) {
            if (!recording) {
                // Start recording
                printf("Starting video recording...\n");

                // Generate filename with timestamp
                time_t now = time(NULL);
                char filename[256];
                snprintf(filename, sizeof(filename), "museum_recording_%ld.mp4", (long)now);

                // Build ffmpeg command (platform-specific stderr redirect)
                // Use configured recording FPS (adjust with [ and ] keys before recording)
                char ffmpeg_cmd[512];
#ifdef PLATFORM_WINDOWS
                snprintf(ffmpeg_cmd, sizeof(ffmpeg_cmd),
                    "ffmpeg -y -f rawvideo -pixel_format rgba -video_size 1200x800 "
                    "-framerate %d -i pipe:0 -c:v libx264 -preset ultrafast -crf 23 "
                    "-pix_fmt yuv420p \"%s\" 2>nul",
                    recording_fps, filename);
#else
                snprintf(ffmpeg_cmd, sizeof(ffmpeg_cmd),
                    "ffmpeg -y -f rawvideo -pixel_format rgba -video_size 1200x800 "
                    "-framerate %d -i pipe:0 -c:v libx264 -preset ultrafast -crf 23 "
                    "-pix_fmt yuv420p \"%s\" 2>/dev/null",
                    recording_fps, filename);
#endif

                ffmpeg_pipe = popen(ffmpeg_cmd, "w");
                if (ffmpeg_pipe) {
                    recording = true;
                    frame_count = 0;
                    printf("Recording to: %s (assuming %d FPS)\n", filename, recording_fps);
                    printf("Note: If video is too fast/slow, adjust FPS with [ and ] keys before recording\n");
                } else {
                    printf("Error: Failed to start ffmpeg\n");
                }
            } else {
                // Stop recording
                printf("Stopping recording... Captured %d frames\n", frame_count);
                if (ffmpeg_pipe) {
                    pclose(ffmpeg_pipe);
                    ffmpeg_pipe = NULL;
                }
                recording = false;
                printf("Recording saved!\n");
            }
        }
#endif

        // Keyboard controls for all paintings
        if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) { // + key
            for (int i = 0; i < NUM_PAINTINGS; i++) {
                paintings[i].line_thickness += 0.5f;
                paintings[i].pencil_size += 1.0f;
                if (paintings[i].line_thickness > 15.0f)
                    paintings[i].line_thickness = 15.0f;
                if (paintings[i].pencil_size > 20.0f)
                    paintings[i].pencil_size = 20.0f;
            }
        }

        if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) { // - key
            for (int i = 0; i < NUM_PAINTINGS; i++) {
                paintings[i].line_thickness -= 0.5f;
                paintings[i].pencil_size -= 1.0f;
                if (paintings[i].line_thickness < 0.5f)
                    paintings[i].line_thickness = 0.5f;
                if (paintings[i].pencil_size < 2.0f)
                    paintings[i].pencil_size = 2.0f;
            }
        }

        // Update paintings
        for (int i = 0; i < NUM_PAINTINGS; i++) {
            update_painting(&paintings[i], dt);
        }

        // Draw
        BeginDrawing();
        ClearBackground(DARKGRAY);

        BeginMode3D(camera);
        // Draw floor
        DrawPlane((Vector3) { 0, 0, 0 }, (Vector2) { ROOM_SIZE, ROOM_SIZE }, LIGHTGRAY);

        // Draw ceiling
        DrawPlane((Vector3) { 0, ROOM_HEIGHT, 0 }, (Vector2) { ROOM_SIZE, ROOM_SIZE }, LIGHTGRAY);

        // Draw walls
        DrawCube((Vector3) { 0, ROOM_HEIGHT / 2, -ROOM_SIZE / 2 },
            ROOM_SIZE, ROOM_HEIGHT, 0.2f, DARKGRAY);
        DrawCube((Vector3) { 0, ROOM_HEIGHT / 2, ROOM_SIZE / 2 },
            ROOM_SIZE, ROOM_HEIGHT, 0.2f, DARKGRAY);
        DrawCube((Vector3) { -ROOM_SIZE / 2, ROOM_HEIGHT / 2, 0 },
            0.2f, ROOM_HEIGHT, ROOM_SIZE, DARKGRAY);
        DrawCube((Vector3) { ROOM_SIZE / 2, ROOM_HEIGHT / 2, 0 },
            0.2f, ROOM_HEIGHT, ROOM_SIZE, DARKGRAY);

        // Draw paintings
        for (int i = 0; i < NUM_PAINTINGS; i++) {
            draw_painting(&paintings[i]);
        }

        EndMode3D();

        // Draw UI
        DrawText("Fourier Epicycles Museum", 10, 10, 20, RAYWHITE);
        DrawText("Move: WASD | Jump: Space | Crouch: Ctrl | Zoom: Left Click", 10, 35, 14, LIGHTGRAY);

#ifdef PLATFORM_WEB
        DrawText("Thickness: +/- | Explore the paintings!", 10, 55, 14, LIGHTGRAY);
#else
        if (ffmpeg_available) {
            DrawText("Record: R | Recording FPS: [ ] | Thickness: +/-", 10, 55, 14, LIGHTGRAY);
        } else {
            DrawText("Thickness: +/- | Explore the paintings!", 10, 55, 14, LIGHTGRAY);
        }
#endif

        // Display current settings
        char settings[256];
#ifdef PLATFORM_WEB
        snprintf(settings, sizeof(settings), "Epicycles: %d | Thickness: %.1f | Pencil: %.1f | FOV: %.0f",
            EPICYCLES_PER_PAINTING, paintings[0].line_thickness, paintings[0].pencil_size, camera.fovy);
#else
        if (recording) {
            float actual_fps = 1.0f / dt;
            snprintf(settings, sizeof(settings), "Epicycles: %d | Thickness: %.1f | Pencil: %.1f | FOV: %.0f | Recording: %d frames @ %.1f FPS (expecting %d FPS)",
                EPICYCLES_PER_PAINTING, paintings[0].line_thickness, paintings[0].pencil_size, camera.fovy, frame_count, actual_fps, recording_fps);
        } else if (ffmpeg_available) {
            snprintf(settings, sizeof(settings), "Epicycles: %d | Thickness: %.1f | Pencil: %.1f | FOV: %.0f | Recording FPS: %d (adjust with [ ])",
                EPICYCLES_PER_PAINTING, paintings[0].line_thickness, paintings[0].pencil_size, camera.fovy, recording_fps);
        } else {
            snprintf(settings, sizeof(settings), "Epicycles: %d | Thickness: %.1f | Pencil: %.1f | FOV: %.0f",
                EPICYCLES_PER_PAINTING, paintings[0].line_thickness, paintings[0].pencil_size, camera.fovy);
        }
#endif
        DrawText(settings, 10, 75, 12, LIGHTGRAY);

#ifndef PLATFORM_WEB
        // Show recording indicator
        if (recording) {
            DrawCircle(1200 - 30, 30, 10, RED);
            DrawText("REC", 1200 - 90, 22, 20, RED);
        }
#endif

        EndDrawing();

#ifndef PLATFORM_WEB
        // Capture frame for video recording
        if (recording && ffmpeg_pipe) {
            // rlReadScreenPixels returns RGBA data (4 bytes per pixel)
            unsigned char* pixels = rlReadScreenPixels(1200, 800);

            if (pixels) {
                // DIRECT WRITE: No loop, no conversion. FFmpeg handles format conversion.
                // 1200 width * 800 height * 4 bytes (RGBA)
                fwrite(pixels, 1, 1200 * 800 * 4, ffmpeg_pipe);
                frame_count++;
                RL_FREE(pixels);
            }
        }
#endif
    }

    // Cleanup
#ifndef PLATFORM_WEB
    // Stop recording if still active
    if (recording && ffmpeg_pipe) {
        printf("Stopping recording on exit... Captured %d frames\n", frame_count);
        pclose(ffmpeg_pipe);
    }
#endif

    for (int i = 0; i < NUM_PAINTINGS; i++) {
        UnloadRenderTexture(paintings[i].texture);
        fourier_result_free(&paintings[i].fourier);
        free(paintings[i].trail);
    }
    free_drawings(drawings, n_drawings);

    CloseWindow();
    return 0;
}
