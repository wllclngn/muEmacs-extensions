const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const lib = b.addSharedLibrary(.{
        .name = "treesitter_hl",
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
    });

    // Link tree-sitter core
    lib.linkSystemLibrary("tree-sitter");

    // Link language parsers
    lib.linkSystemLibrary("tree-sitter-c");
    lib.linkSystemLibrary("tree-sitter-python");
    lib.linkSystemLibrary("tree-sitter-rust");
    lib.linkSystemLibrary("tree-sitter-bash");
    lib.linkSystemLibrary("tree-sitter-javascript");

    // Link libc for C interop
    lib.linkLibC();

    // Install the shared library
    b.installArtifact(lib);

    // Copy to extension directory
    const install_step = b.addInstallArtifact(lib, .{});
    const copy_step = b.addInstallFile(
        install_step.emitted_files.items[0].src,
        "../treesitter_hl.so",
    );
    copy_step.step.dependOn(&install_step.step);
    b.default_step.dependOn(&copy_step.step);
}
