// Platform detection
#if defined(_WIN32) || defined(_WIN64)
#define PLATFORM_WINDOWS
#else
#define _POSIX_C_SOURCE 200809L
#define PLATFORM_POSIX
#endif

#include "mathlib.h"
#include "raylib.h"
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

// Screen dimensions
#define SCREEN_WIDTH 1200
#define SCREEN_HEIGHT 800
#define CENTER_X (SCREEN_WIDTH / 2.0f)
#define CENTER_Y (SCREEN_HEIGHT / 2.0f)

// Animation parameters
#define MAX_TRAIL_POINTS 2000
#define DEFAULT_EPICYCLES 150
#define MAX_DRAWINGS 10
#define DISPLAY_SCALE 50.0f // Scale factor for displaying paths on screen

// Drawing structure
typedef struct {
    char name[64];
    char filepath[256];
    Complex* points;
    size_t n_points;
} Drawing;

// Generate a test shape (circle with points)
static void generate_circle_path(Complex* points, size_t n_points, float radius)
{
    for (size_t i = 0; i < n_points; i++) {
        float angle = 2.0 * M_PI * i / n_points;
        points[i] = radius * (cos(angle) + I * sin(angle));
    }
}

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
        printf("ffmpeg detected - video recording enabled (press O)\n");
        fflush(stdout);
    } else {
        printf("ffmpeg not found - video recording disabled\n");
        fflush(stdout);
    }
#endif // PLATFORM_WEB

    // Initialization
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Fourier Series Epicycles - Simple Example");
    SetTargetFPS(60);

    // Load all available drawings
    Drawing drawings[MAX_DRAWINGS];
    int n_drawings = load_drawings(drawings, MAX_DRAWINGS);
    int current_drawing = 0;

    printf("Loaded %d drawings\n", n_drawings);

    if (n_drawings == 0) {
        fprintf(stderr, "No drawings available\n");
        CloseWindow();
        return 1;
    }

    // Perform Fourier analysis on the first drawing
    size_t n_epicycles = DEFAULT_EPICYCLES;

    // Limit epicycles to half the point count (Nyquist limit) for best results
    size_t max_epicycles = drawings[current_drawing].n_points / 2;
    if (n_epicycles > max_epicycles) {
        n_epicycles = max_epicycles;
    }

    printf("Analyzing drawing '%s' with %zu epicycles (from %zu points)...\n",
        drawings[current_drawing].name, n_epicycles, drawings[current_drawing].n_points);

    FourierResult fourier = fourier_analyze(
        drawings[current_drawing].points,
        drawings[current_drawing].n_points,
        n_epicycles);

    if (!fourier.coefficients) {
        fprintf(stderr, "Failed to perform Fourier analysis\n");
        free_drawings(drawings, n_drawings);
        CloseWindow();
        return 1;
    }

    printf("Analysis complete. Using %zu epicycles\n", fourier.count);

    // Animation state
    float time = 0.0f;
    float speed = 0.5f; // Lower speed for better visibility
    bool paused = false;
    bool show_path_preview = false;
    bool follow_mode = false; // Camera follow mode
    float line_thickness = 1.0f; // Epicycle border thickness
    float pencil_size = 6.0f; // Red pencil tip size

    // Camera for zoom/follow
    Camera2D camera = { 0 };
    camera.offset = (Vector2) { SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f };
    camera.target = (Vector2) { CENTER_X, CENTER_Y };
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;

#ifndef PLATFORM_WEB
    // Video recording variables
    bool recording = false;
    FILE* ffmpeg_pipe = NULL;
    int frame_count = 0;
    int recording_fps = 20; // Expected FPS for recording (adjustable with [ and ])
#endif

    // Trail points
    Vector2* trail = malloc(MAX_TRAIL_POINTS * sizeof(Vector2));
    int trail_count = 0;

    // Epicycle positions buffer
    Complex* epicycle_positions = malloc((fourier.count + 1) * sizeof(Complex));

    // Main game loop
    while (!WindowShouldClose()) {
        // Update
        //----------------------------------------------------------------------------------
        float dt = GetFrameTime();

        // Keyboard controls
        if (IsKeyPressed(KEY_SPACE)) {
            paused = !paused;
        }

        if (IsKeyPressed(KEY_UP)) {
            speed *= 1.2f;
        }

        if (IsKeyPressed(KEY_DOWN)) {
            speed /= 1.2f;
        }

        // Switch to next/previous drawing
        if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_N)) {
            current_drawing = (current_drawing + 1) % n_drawings;

            // Adjust epicycles for new drawing (use up to half the point count)
            size_t max_allowed = drawings[current_drawing].n_points / 2;

            size_t temp_epicycles = n_epicycles;
            if (temp_epicycles > max_allowed) {
                temp_epicycles = max_allowed;
            }

            fourier_result_free(&fourier);
            printf("Switching to '%s'...\n", drawings[current_drawing].name);
            fourier = fourier_analyze(
                drawings[current_drawing].points,
                drawings[current_drawing].n_points,
                temp_epicycles);
            n_epicycles = fourier.count;
            free(epicycle_positions);
            epicycle_positions = malloc((fourier.count + 1) * sizeof(Complex));
            time = 0.0f;
            trail_count = 0;
        }

        if (IsKeyPressed(KEY_P)) {
            current_drawing = (current_drawing - 1 + n_drawings) % n_drawings;

            // Adjust epicycles for new drawing (use up to half the point count)
            size_t max_allowed = drawings[current_drawing].n_points / 2;

            size_t temp_epicycles = n_epicycles;
            if (temp_epicycles > max_allowed) {
                temp_epicycles = max_allowed;
            }

            fourier_result_free(&fourier);
            printf("Switching to '%s'...\n", drawings[current_drawing].name);
            fourier = fourier_analyze(
                drawings[current_drawing].points,
                drawings[current_drawing].n_points,
                temp_epicycles);
            n_epicycles = fourier.count;
            free(epicycle_positions);
            epicycle_positions = malloc((fourier.count + 1) * sizeof(Complex));
            time = 0.0f;
            trail_count = 0;
        }

        // Number keys to select drawing directly
        for (int i = 0; i < n_drawings && i < 9; i++) {
            if (IsKeyPressed(KEY_ONE + i)) {
                current_drawing = i;

                // Adjust epicycles for new drawing (use up to half the point count)
                size_t max_allowed = drawings[current_drawing].n_points / 2;

                size_t temp_epicycles = n_epicycles;
                if (temp_epicycles > max_allowed) {
                    temp_epicycles = max_allowed;
                }

                fourier_result_free(&fourier);
                printf("Switching to '%s'...\n", drawings[current_drawing].name);
                fourier = fourier_analyze(
                    drawings[current_drawing].points,
                    drawings[current_drawing].n_points,
                    temp_epicycles);
                n_epicycles = fourier.count;
                free(epicycle_positions);
                epicycle_positions = malloc((fourier.count + 1) * sizeof(Complex));
                time = 0.0f;
                trail_count = 0;
            }
        }

        if (IsKeyPressed(KEY_RIGHT)) {
            size_t new_epicycles = n_epicycles + 10;
            // Limit to half the point count (Nyquist limit)
            size_t max_allowed = drawings[current_drawing].n_points / 2;

            if (new_epicycles <= max_allowed && new_epicycles != n_epicycles) {
                fourier_result_free(&fourier);
                n_epicycles = new_epicycles;
                fourier = fourier_analyze(
                    drawings[current_drawing].points,
                    drawings[current_drawing].n_points,
                    n_epicycles);
                n_epicycles = fourier.count;
                free(epicycle_positions);
                epicycle_positions = malloc((fourier.count + 1) * sizeof(Complex));
                trail_count = 0; // Reset trail when changing epicycles
            }
        }

        if (IsKeyPressed(KEY_LEFT) && n_epicycles > 1) {
            fourier_result_free(&fourier);
            n_epicycles = (n_epicycles > 10) ? n_epicycles - 10 : 1;
            fourier = fourier_analyze(
                drawings[current_drawing].points,
                drawings[current_drawing].n_points,
                n_epicycles);
            n_epicycles = fourier.count;
            free(epicycle_positions);
            epicycle_positions = malloc((fourier.count + 1) * sizeof(Complex));
            trail_count = 0; // Reset trail when changing epicycles
        }

        if (IsKeyPressed(KEY_R)) {
            time = 0.0f;
            trail_count = 0;
        }

        if (IsKeyPressed(KEY_V)) {
            show_path_preview = !show_path_preview;
        }

        if (IsKeyPressed(KEY_F)) {
            follow_mode = !follow_mode;
            if (!follow_mode) {
                // Reset camera when exiting follow mode
                camera.target = (Vector2) { CENTER_X, CENTER_Y };
                camera.zoom = 1.0f;
            }
        }

        // Adjust line thickness and pencil size
        if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) { // + key
            line_thickness += 0.5f; // Increase blue circles
            pencil_size += 1.0f;
            if (line_thickness > 15.0f)
                line_thickness = 15.0f;
            if (pencil_size > 20.0f)
                pencil_size = 20.0f;
        }

        if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) { // - key
            line_thickness -= 0.5f; // Decrease blue circles
            pencil_size -= 1.0f;
            if (line_thickness < 0.5f)
                line_thickness = 0.5f;
            if (pencil_size < 2.0f)
                pencil_size = 2.0f;
        }

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

        // Toggle video recording with O key (only if ffmpeg available)
        if (ffmpeg_available && IsKeyPressed(KEY_O)) {
            if (!recording) {
                // Start recording
                printf("Starting video recording...\n");

                // Generate filename with timestamp
                time_t now_time;
                {
                    // Use extern to avoid collision with local 'time' variable
                    extern time_t time(time_t*);
                    now_time = time(NULL);
                }
                char filename[256];
                snprintf(filename, sizeof(filename), "simple_recording_%ld.mp4", (long)now_time);

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

        // Update time and trail
        if (!paused) {
            float old_time = time;
            float time_step = dt * speed;
            time += time_step;

            // Wrap around at 2π
            if (time > 2.0 * M_PI) {
                time = 0.0f;
                trail_count = 0; // Reset trail on loop
            } else {
                // Add intermediate trail points for smooth drawing regardless of speed
                // Target: one trail point every ~0.02 time units for smooth curves
                const float trail_time_step = 0.02f;
                int num_trail_points = (int)(time_step / trail_time_step) + 1;

                // Clamp to reasonable number to avoid performance issues
                if (num_trail_points > 20)
                    num_trail_points = 20;

                for (int i = 0; i < num_trail_points && trail_count < MAX_TRAIL_POINTS; i++) {
                    float t = old_time + (time_step * (i + 1)) / num_trail_points;
                    Complex tip = epicycles_at_time(&fourier, t, epicycle_positions);
                    trail[trail_count].x = CENTER_X + creal(tip) * DISPLAY_SCALE;
                    trail[trail_count].y = CENTER_Y + cimag(tip) * DISPLAY_SCALE;
                    trail_count++;
                }
            }
        }

        // Calculate epicycle positions for current time (for display)
        Complex tip = epicycles_at_time(&fourier, time, epicycle_positions);

        // Update camera for follow mode
        if (follow_mode) {
            Vector2 tip_pos = {
                CENTER_X + creal(tip) * DISPLAY_SCALE,
                CENTER_Y + cimag(tip) * DISPLAY_SCALE
            };
            camera.target = tip_pos;
            camera.zoom = 3.0f;
        }

        //----------------------------------------------------------------------------------

        // Draw
        //----------------------------------------------------------------------------------
        BeginDrawing();

        ClearBackground(BLACK);

        // Draw title and instructions
        DrawText("Fourier Series Epicycles", 10, 10, 20, RAYWHITE);
        DrawText("SPACE: Pause | UP/DOWN: Speed | LEFT/RIGHT: Epicycles | +/-: Thickness | TAB/N: Next | P: Prev", 10, 35, 14, GRAY);

#ifdef PLATFORM_WEB
        DrawText("1-9: Select | V: Preview | F: Follow | R: Reset", 10, 50, 14, GRAY);
#else
        if (ffmpeg_available) {
            DrawText("1-9: Select | V: Preview | F: Follow | R: Reset | O: Record | [ ]: FPS", 10, 50, 14, GRAY);
        } else {
            DrawText("1-9: Select | V: Preview | F: Follow | R: Reset", 10, 50, 14, GRAY);
        }
#endif

        // Draw current drawing name
        char drawing_info[256];
        snprintf(drawing_info, sizeof(drawing_info), "Drawing: [%d/%d] %s",
            current_drawing + 1, n_drawings, drawings[current_drawing].name);
        DrawText(drawing_info, 10, 65, 16, YELLOW);

        // Draw status
        size_t max_allowed = drawings[current_drawing].n_points;
        if (max_allowed > 150)
            max_allowed = 150;

        char status[256];
#ifdef PLATFORM_WEB
        snprintf(status, sizeof(status), "Epicycles: %zu/%zu | Speed: %.2fx | Time: %.2f/%.2f | Follow: %s | Thickness: %.1f | Pencil: %.1f",
            fourier.count, max_allowed, speed, time, 2.0 * M_PI, follow_mode ? "ON" : "OFF", line_thickness, pencil_size);
#else
        if (recording) {
            float actual_fps = 1.0f / dt;
            snprintf(status, sizeof(status), "Epicycles: %zu/%zu | Speed: %.2fx | Follow: %s | Thickness: %.1f | Recording: %d frames @ %.1f FPS (expecting %d FPS)",
                fourier.count, max_allowed, speed, follow_mode ? "ON" : "OFF", line_thickness, frame_count, actual_fps, recording_fps);
        } else if (ffmpeg_available) {
            snprintf(status, sizeof(status), "Epicycles: %zu/%zu | Speed: %.2fx | Time: %.2f/%.2f | Follow: %s | Thickness: %.1f | Recording FPS: %d",
                fourier.count, max_allowed, speed, time, 2.0 * M_PI, follow_mode ? "ON" : "OFF", line_thickness, recording_fps);
        } else {
            snprintf(status, sizeof(status), "Epicycles: %zu/%zu | Speed: %.2fx | Time: %.2f/%.2f | Follow: %s | Thickness: %.1f | Pencil: %.1f",
                fourier.count, max_allowed, speed, time, 2.0 * M_PI, follow_mode ? "ON" : "OFF", line_thickness, pencil_size);
        }
#endif
        DrawText(status, 10, 85, 14, GRAY);

        if (paused) {
            DrawText("PAUSED", 10, 105, 16, RED);
        }

#ifndef PLATFORM_WEB
        // Show recording indicator
        if (recording) {
            DrawCircle(SCREEN_WIDTH - 30, 30, 10, RED);
            DrawText("REC", SCREEN_WIDTH - 90, 22, 20, RED);
        }
#endif

        // Begin camera mode for world rendering
        BeginMode2D(camera);

        // Draw path preview (original target path from file)
        if (show_path_preview) {
            // Draw the original path points to show the target
            for (size_t i = 1; i < drawings[current_drawing].n_points; i++) {
                Complex point = drawings[current_drawing].points[i];
                Complex prev_point = drawings[current_drawing].points[i - 1];

                float x = CENTER_X + creal(point) * DISPLAY_SCALE;
                float y = CENTER_Y + cimag(point) * DISPLAY_SCALE;
                float prev_x = CENTER_X + creal(prev_point) * DISPLAY_SCALE;
                float prev_y = CENTER_Y + cimag(prev_point) * DISPLAY_SCALE;

                DrawLineEx((Vector2) { prev_x, prev_y }, (Vector2) { x, y }, 1.0f, Fade(WHITE, 0.25f));
            }
        }

        // Draw epicycles (circles and connecting lines)
        for (size_t i = 0; i < fourier.count; i++) {
            Vector2 center = {
                CENTER_X + creal(epicycle_positions[i]) * DISPLAY_SCALE,
                CENTER_Y + cimag(epicycle_positions[i]) * DISPLAY_SCALE
            };
            Vector2 end = {
                CENTER_X + creal(epicycle_positions[i + 1]) * DISPLAY_SCALE,
                CENTER_Y + cimag(epicycle_positions[i + 1]) * DISPLAY_SCALE
            };

            float radius = fourier.coefficients[i].amplitude * DISPLAY_SCALE;

            // Draw circle with adjustable thickness
            // Draw multiple concentric circles to create thickness effect
            int num_lines = (int)(line_thickness * 2);
            for (int t = 0; t < num_lines; t++) {
                float r_offset = t * 0.3f;
                DrawCircleLines((int)center.x, (int)center.y, radius + r_offset,
                    Fade(SKYBLUE, 0.6f));
            }

            // Draw radius line
            DrawLineEx(center, end, line_thickness + 1.0f, Fade(WHITE, 0.8f));

            // Draw arrow at the end to show rotation direction
            if (radius > 5.0f) { // Only draw arrows for visible circles
                // Calculate arrow direction (perpendicular to radius, in rotation direction)
                Vector2 dir = { end.x - center.x, end.y - center.y };
                float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
                if (len > 0.1f) {
                    dir.x /= len;
                    dir.y /= len;

                    // Perpendicular direction (rotation direction)
                    Vector2 perp = { -dir.y, dir.x };

                    // Arrow size based on circle size
                    float arrow_size = fminf(8.0f, radius * 0.15f);

                    // Arrow tip
                    Vector2 arrow_base = {
                        end.x - dir.x * arrow_size * 1.5f,
                        end.y - dir.y * arrow_size * 1.5f
                    };

                    Vector2 arrow_left = {
                        arrow_base.x - perp.x * arrow_size * 0.7f,
                        arrow_base.y - perp.y * arrow_size * 0.7f
                    };

                    Vector2 arrow_right = {
                        arrow_base.x + perp.x * arrow_size * 0.7f,
                        arrow_base.y + perp.y * arrow_size * 0.7f
                    };

                    // Draw arrow triangle
                    DrawTriangle(end, arrow_left, arrow_right, Fade(YELLOW, 0.9f));
                }
            }
        }

        // Draw trail
        if (trail_count > 1) {
            for (int i = 1; i < trail_count; i++) {
                float alpha = 0.3f + 0.7f * (float)i / trail_count;
                DrawLineEx(trail[i - 1], trail[i], pencil_size / 3.0f, Fade(RED, alpha));
            }
        }

        // Draw tip point
        if (fourier.count > 0) {
            Vector2 tip_pos = {
                CENTER_X + creal(tip) * DISPLAY_SCALE,
                CENTER_Y + cimag(tip) * DISPLAY_SCALE
            };
            DrawCircleV(tip_pos, pencil_size, RED);
        }

        // End camera mode
        EndMode2D();

        EndDrawing();

#ifndef PLATFORM_WEB
        // Capture frame for video recording
        if (recording && ffmpeg_pipe) {
            // rlReadScreenPixels returns RGBA data (4 bytes per pixel)
            unsigned char* pixels = rlReadScreenPixels(SCREEN_WIDTH, SCREEN_HEIGHT);

            if (pixels) {
                // DIRECT WRITE: No loop, no conversion. FFmpeg handles format conversion.
                // 1200 width * 800 height * 4 bytes (RGBA)
                fwrite(pixels, 1, SCREEN_WIDTH * SCREEN_HEIGHT * 4, ffmpeg_pipe);
                frame_count++;
                RL_FREE(pixels);
            }
        }
#endif
        //----------------------------------------------------------------------------------
    }

    // De-Initialization
    //----------------------------------------------------------------------------------
#ifndef PLATFORM_WEB
    // Stop recording if still active
    if (recording && ffmpeg_pipe) {
        printf("Stopping recording on exit... Captured %d frames\n", frame_count);
        pclose(ffmpeg_pipe);
    }
#endif

    free(trail);
    free(epicycle_positions);
    free_drawings(drawings, n_drawings);
    fourier_result_free(&fourier);
    CloseWindow();

    return 0;
}
