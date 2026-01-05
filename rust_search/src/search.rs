//! Search implementation using the grep crate (ripgrep's library)
//!
//! This searches in-process without fork/exec overhead.

use grep_regex::RegexMatcher;
use grep_searcher::{sinks::UTF8, Searcher};
use ignore::WalkBuilder;
use std::path::Path;

/// A single search match
#[derive(Debug, Clone)]
pub struct Match {
    pub file: String,
    pub line: u64,
    pub col: u64,
    pub text: String,
}

/// Search a directory recursively for a pattern
pub fn search_directory(pattern: &str, path: &str) -> Result<Vec<Match>, String> {
    let matcher = RegexMatcher::new(pattern).map_err(|e| format!("Invalid pattern: {}", e))?;

    let mut matches = Vec::new();

    // Use ignore crate's WalkBuilder - respects .gitignore
    let walker = WalkBuilder::new(path)
        .hidden(false) // Don't skip hidden files by default
        .git_ignore(true) // Respect .gitignore
        .git_global(true)
        .git_exclude(true)
        .build();

    for entry in walker.filter_map(|e| e.ok()) {
        let entry_path = entry.path();

        // Skip directories
        if entry_path.is_dir() {
            continue;
        }

        // Skip binary files (simple heuristic)
        if is_likely_binary(entry_path) {
            continue;
        }

        // Search this file
        if let Ok(file_matches) = search_file(&matcher, entry_path) {
            matches.extend(file_matches);
        }
    }

    Ok(matches)
}

/// Search a single file for matches
fn search_file(matcher: &RegexMatcher, path: &Path) -> Result<Vec<Match>, String> {
    let mut matches = Vec::new();
    let path_str = path.to_string_lossy().to_string();

    let mut searcher = Searcher::new();

    let result = searcher.search_path(
        matcher,
        path,
        UTF8(|line_num, line| {
            // Find column within line
            let col = find_match_column(matcher, line).unwrap_or(0);

            matches.push(Match {
                file: path_str.clone(),
                line: line_num,
                col,
                text: line.trim_end().to_string(),
            });
            Ok(true)
        }),
    );

    match result {
        Ok(_) => Ok(matches),
        Err(_) => Ok(vec![]), // Skip files that can't be read
    }
}

/// Find the column of the first match in a line
fn find_match_column(matcher: &RegexMatcher, line: &str) -> Option<u64> {
    use grep_matcher::Matcher;

    // Find where the match starts in this line
    if let Ok(Some(mat)) = matcher.find(line.as_bytes()) {
        return Some(mat.start() as u64);
    }

    None
}

/// Heuristic: is this file likely binary?
fn is_likely_binary(path: &Path) -> bool {
    let binary_extensions = [
        "exe", "dll", "so", "dylib", "a", "o", "obj", "bin", "dat", "db", "sqlite", "png", "jpg",
        "jpeg", "gif", "bmp", "ico", "pdf", "zip", "tar", "gz", "bz2", "xz", "7z", "rar", "class",
        "pyc", "pyo", "wasm", "ttf", "otf", "woff", "woff2", "mp3", "mp4", "avi", "mov", "mkv",
    ];

    if let Some(ext) = path.extension() {
        let ext_lower = ext.to_string_lossy().to_lowercase();
        return binary_extensions.contains(&ext_lower.as_str());
    }

    false
}

/// Format matches for display in the results buffer
pub fn format_results(matches: &[Match]) -> String {
    let mut result = String::new();

    result.push_str(&format!("=== {} matches ===\n", matches.len()));

    for m in matches {
        // Format: file:line:col: text
        result.push_str(&format!("{}:{}:{}: {}\n", m.file, m.line, m.col, m.text));
    }

    result
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_format_results() {
        let matches = vec![
            Match {
                file: "src/main.rs".to_string(),
                line: 10,
                col: 4,
                text: "fn main() {".to_string(),
            },
            Match {
                file: "src/lib.rs".to_string(),
                line: 5,
                col: 0,
                text: "pub fn test() {".to_string(),
            },
        ];

        let result = format_results(&matches);
        assert!(result.contains("2 matches"));
        assert!(result.contains("src/main.rs:10:4:"));
    }
}
