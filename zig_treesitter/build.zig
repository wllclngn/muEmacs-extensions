const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const lib = b.addLibrary(.{
        .name = "zig_treesitter",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
        .linkage = .dynamic,
    });

    // Link tree-sitter core
    lib.linkSystemLibrary("tree-sitter");

    // Link language parsers
    lib.linkSystemLibrary("tree-sitter-c");
    lib.linkSystemLibrary("tree-sitter-python");
    lib.linkSystemLibrary("tree-sitter-rust");
    lib.linkSystemLibrary("tree-sitter-bash");
    lib.linkSystemLibrary("tree-sitter-javascript");

    // Install the shared library
    b.installArtifact(lib);
}
