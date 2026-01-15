const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // API version option - passed by uep_build.py via -Dapi_version=N
    const api_version = b.option(i32, "api_version", "μEmacs API version") orelse 4;

    // Create options module to pass api_version to main.zig
    const options = b.addOptions();
    options.addOption(i32, "api_version", api_version);

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

    // Add options module so main.zig can import it
    lib.root_module.addImport("config", options.createModule());

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
