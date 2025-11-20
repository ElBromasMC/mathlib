const std = @import("std");

pub fn build(b: *std.Build) !void {
    // Standard target and optimization options
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // Linux display backend option (Wayland or X11)
    const linux_display_backend = b.option(
        enum { Wayland, X11 },
        "linux_display_backend",
        "Linux display backend to use (Wayland or X11)",
    ) orelse .Wayland;

    // OpenGL version option
    const opengl_version = b.option(
        enum { auto, gl_1_1, gl_2_1, gl_3_3, gl_4_3, gles_2, gles_3 },
        "opengl_version",
        "OpenGL version to use (auto, gl_1_1, gl_2_1, gl_3_3, gl_4_3, gles_2, gles_3)",
    ) orelse .auto;

    // ===== Math Library =====
    const mathlib = b.addLibrary(.{
        .name = "mathlib",
        .linkage = .static,
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });

    // Add C source files for math library
    mathlib.root_module.addCSourceFiles(.{
        .files = &.{
            "src/fft.c",
            "src/fourier.c",
            "src/path_loader.c",
        },
        .flags = &.{
            "-std=c99",
            "-Wall",
            "-Wextra",
            "-pedantic",
        },
    });

    // Add include path
    mathlib.root_module.addIncludePath(b.path("src"));

    // Install the library
    b.installArtifact(mathlib);

    // Install header
    mathlib.installHeader(b.path("src/mathlib.h"), "mathlib.h");

    // ===== Raylib Dependency (Lazy) =====
    // Use lazyDependency to avoid fetching raylib unless actually needed
    const raylib_dep = b.lazyDependency("raylib", .{
        .target = target,
        .optimize = optimize,
        .linux_display_backend = linux_display_backend,
        .opengl_version = opengl_version,
    });

    // ===== Simple Example (2D) =====
    const simple_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });

    simple_mod.addIncludePath(b.path("src"));
    simple_mod.addCSourceFile(.{
        .file = b.path("examples/simple_example/main.c"),
        .flags = &.{
            "-std=c99",
            "-Wall",
            "-Wextra",
        },
    });
    simple_mod.linkLibrary(mathlib);
    if (raylib_dep) |dep| {
        simple_mod.linkLibrary(dep.artifact("raylib"));
    }

    const simple_example = b.addExecutable(.{
        .name = "simple_example",
        .root_module = simple_mod,
    });

    const install_simple = b.addInstallArtifact(simple_example, .{});
    const simple_step = b.step("simple", "Build the simple 2D example");
    simple_step.dependOn(&install_simple.step);

    // Add run step for simple example
    const run_simple_cmd = b.addRunArtifact(simple_example);
    run_simple_cmd.step.dependOn(&install_simple.step);
    if (b.args) |args| {
        run_simple_cmd.addArgs(args);
    }

    const run_simple_step = b.step("run-simple", "Run the simple 2D example");
    run_simple_step.dependOn(&run_simple_cmd.step);

    // ===== Museum Example (3D) =====
    const museum_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });

    museum_mod.addIncludePath(b.path("src"));
    museum_mod.addCSourceFile(.{
        .file = b.path("examples/museum/main.c"),
        .flags = &.{
            "-std=c99",
            "-Wall",
            "-Wextra",
        },
    });
    museum_mod.linkLibrary(mathlib);
    if (raylib_dep) |dep| {
        museum_mod.linkLibrary(dep.artifact("raylib"));
    }

    const museum = b.addExecutable(.{
        .name = "museum",
        .root_module = museum_mod,
    });

    const install_museum = b.addInstallArtifact(museum, .{});
    const museum_step = b.step("museum", "Build the museum 3D example");
    museum_step.dependOn(&install_museum.step);

    // Add run step for museum
    const run_museum_cmd = b.addRunArtifact(museum);
    run_museum_cmd.step.dependOn(&install_museum.step);
    if (b.args) |args| {
        run_museum_cmd.addArgs(args);
    }

    const run_museum_step = b.step("run-museum", "Run the museum 3D example");
    run_museum_step.dependOn(&run_museum_cmd.step);

    // ===== Examples Step =====
    const examples_step = b.step("examples", "Build all examples");
    examples_step.dependOn(simple_step);
    examples_step.dependOn(museum_step);

    // ===== Default Build =====
    // By default, build mathlib and install it
    b.default_step.dependOn(&mathlib.step);
}
