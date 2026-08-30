const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});

    // RELEASESAFE IS THE DEFAULT DELIBERATELY, and the default matters more
    // here than in most projects because Zig's own default is Debug and a
    // status line is only ever run as a built artefact.
    //
    // Measured 2026-08-30, 200 runs interleaved against the Go binary on a real
    // event: Debug 17.7ms and a 17.1MB binary, ReleaseSafe 4.9ms and 4.4MB.
    // Debug is not a slower build of the same program, it is a different one,
    // and shipping it would have cost three times what this program exists to
    // be cheap at.
    //
    // SAFE rather than Fast, and it costs nothing measurable: ReleaseFast came
    // out at 5.4ms, inside the run-to-run spread. So the checks are free here,
    // and a bounds check that panics beats one that does not exist in a program
    // whose input is a payload somebody else's release note can change.
    // Declared rather than taken from `standardOptimizeOption`, which given a
    // `preferred_optimize_mode` REPLACES `-Doptimize` with a `-Drelease` bool
    // instead of changing the default. That silently left `zig build` on Debug
    // while `-Doptimize=ReleaseSafe` became an invalid option, and the failed
    // build left the previous binary in place to be measured.
    const optimize = b.option(
        std.builtin.OptimizeMode,
        "optimize",
        "Optimize mode; ReleaseSafe unless asked otherwise",
    ) orelse .ReleaseSafe;

    // Two binaries, matching the Go tree: the status line and the SessionEnd
    // cleanup. Both are thin entry points over src/, so a test can reach the
    // work and a checker can read it.
    const exes = .{
        .{ "statusline", "src/main.zig" },
        .{ "forget", "src/forget_main.zig" },
    };

    inline for (exes) |pair| {
        const exe = b.addExecutable(.{
            .name = pair[0],
            .root_module = b.createModule(.{
                .root_source_file = b.path(pair[1]),
                .target = target,
                .optimize = optimize,
            }),
        });
        b.installArtifact(exe);
    }

    const test_step = b.step("test", "Run the suite");
    inline for (.{
        "src/num.zig",
        "src/payload.zig",
        "src/eaw.zig",
        "src/width.zig",
        "src/palette.zig",
        "src/pricing.zig",
        "src/state.zig",
        "src/segments.zig",
        "src/rows.zig",
        "src/usage.zig",
        "src/forget.zig",
    }) |path| {
        const unit = b.addTest(.{
            .root_module = b.createModule(.{
                .root_source_file = b.path(path),
                .target = target,
                .optimize = optimize,
            }),
        });
        test_step.dependOn(&b.addRunArtifact(unit).step);
    }
}
