// build.rs - Generate API version constant from environment
//
// The μEmacs build script (uep_build.py) sets UEMACS_API_VERSION env var.
// This build script generates a const that lib.rs can use.

use std::env;
use std::fs;
use std::path::Path;

fn main() {
    // Read API version from env var, default to 4 if not set
    let api_version = env::var("UEMACS_API_VERSION")
        .unwrap_or_else(|_| "4".to_string());

    // Generate the const file
    let out_dir = env::var("OUT_DIR").unwrap();
    let dest_path = Path::new(&out_dir).join("api_version.rs");

    fs::write(
        &dest_path,
        format!("pub const UEMACS_API_VERSION: i32 = {};\n", api_version)
    ).expect("Failed to write api_version.rs");

    // Tell Cargo to rerun if the env var changes
    println!("cargo:rerun-if-env-changed=UEMACS_API_VERSION");
}
