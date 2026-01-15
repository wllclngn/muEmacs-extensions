//! Full ripgrep-powered search implementation for μEmacs
//!
//! This module provides comprehensive regex search functionality using
//! ripgrep's core libraries (grep-regex, grep-searcher, ignore).
//!
//! Features:
//! - Parallel multi-threaded search
//! - .gitignore and .ignore file support
//! - Memory-mapped file reading for large files
//! - Binary file detection and skipping
//! - Case insensitive searching
//! - Word boundary matching
//! - Context lines (before/after)
//! - Inverted matching
//! - File type filtering
//! - Glob patterns for include/exclude

use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};

use crossbeam_channel as channel;
use grep_matcher::Matcher;
use grep_regex::RegexMatcherBuilder;
use grep_searcher::sinks::UTF8;
use grep_searcher::{BinaryDetection, MmapChoice, Searcher, SearcherBuilder};
use ignore::overrides::OverrideBuilder;
use ignore::types::TypesBuilder;
use ignore::{WalkBuilder, WalkState};

/// Search options - mirrors ripgrep's full option set
#[derive(Clone, Debug)]
pub struct SearchOptions {
    /// Case insensitive search (-i)
    pub case_insensitive: bool,
    /// Smart case (case insensitive if pattern is all lowercase)
    pub smart_case: bool,
    /// Match whole words only (-w)
    pub word_boundary: bool,
    /// Lines of context before match (-B)
    pub context_before: usize,
    /// Lines of context after match (-A)
    pub context_after: usize,
    /// Invert match - show non-matching lines (-v)
    pub invert_match: bool,
    /// Include hidden files
    pub hidden: bool,
    /// Follow symlinks
    pub follow_symlinks: bool,
    /// Respect .gitignore files
    pub git_ignore: bool,
    /// Maximum depth to search (0 = unlimited)
    pub max_depth: Option<usize>,
    /// Number of threads (0 = auto)
    pub threads: usize,
    /// File type filters (e.g., "rust", "py")
    pub file_types: Vec<String>,
    /// Glob patterns to include
    pub glob_include: Vec<String>,
    /// Glob patterns to exclude
    pub glob_exclude: Vec<String>,
    /// Maximum file size to search (bytes, 0 = unlimited)
    pub max_filesize: Option<u64>,
    /// Use memory mapping for large files
    pub mmap: bool,
    /// Fixed string search (not regex)
    pub fixed_strings: bool,
    /// Multiline mode
    pub multiline: bool,
    /// Maximum matches per file (0 = unlimited)
    pub max_count: Option<u64>,
}

impl Default for SearchOptions {
    fn default() -> Self {
        SearchOptions {
            case_insensitive: false,
            smart_case: true,
            word_boundary: false,
            context_before: 0,
            context_after: 0,
            invert_match: false,
            hidden: false,
            follow_symlinks: false,
            git_ignore: true,
            max_depth: None,
            threads: 0, // auto-detect
            file_types: Vec::new(),
            glob_include: Vec::new(),
            glob_exclude: Vec::new(),
            max_filesize: None,
            mmap: true,
            fixed_strings: false,
            multiline: false,
            max_count: None,
        }
    }
}

/// A single search match
#[derive(Debug, Clone)]
pub struct Match {
    pub file: PathBuf,
    pub line_number: u64,
    pub column: usize,
    pub text: String,
}

/// Search statistics
#[derive(Debug, Clone, Default)]
pub struct SearchStats {
    pub matches: usize,
    pub files_searched: usize,
    pub files_matched: usize,
    pub elapsed_ms: u64,
}

/// Search result containing matches and statistics
#[derive(Debug)]
pub struct SearchResult {
    pub matches: Vec<Match>,
    pub stats: SearchStats,
    pub errors: Vec<String>,
}

/// Build a regex matcher with the given options
fn build_matcher(
    pattern: &str,
    opts: &SearchOptions,
) -> Result<grep_regex::RegexMatcher, String> {
    let mut builder = RegexMatcherBuilder::new();

    builder
        .case_insensitive(opts.case_insensitive)
        .case_smart(opts.smart_case && !opts.case_insensitive)
        .word(opts.word_boundary)
        .multi_line(opts.multiline);

    if opts.fixed_strings {
        builder.fixed_strings(true);
    }

    builder.build(pattern).map_err(|e| format!("Invalid pattern: {}", e))
}

/// Build a searcher with the given options
fn build_searcher(opts: &SearchOptions) -> Searcher {
    let mut builder = SearcherBuilder::new();

    builder
        .binary_detection(BinaryDetection::quit(b'\x00'))
        .before_context(opts.context_before)
        .after_context(opts.context_after)
        .invert_match(opts.invert_match);

    if opts.mmap {
        // Use memory mapping for files > 1MB
        unsafe {
            builder.memory_map(MmapChoice::auto());
        }
    }

    builder.build()
}

/// Build a directory walker with the given options
fn build_walker(path: &Path, opts: &SearchOptions) -> Result<WalkBuilder, String> {
    let mut builder = WalkBuilder::new(path);

    builder
        .hidden(!opts.hidden)
        .git_ignore(opts.git_ignore)
        .git_global(opts.git_ignore)
        .git_exclude(opts.git_ignore)
        .follow_links(opts.follow_symlinks)
        .same_file_system(false);

    if let Some(depth) = opts.max_depth {
        builder.max_depth(Some(depth));
    }

    let threads = if opts.threads == 0 {
        num_cpus::get()
    } else {
        opts.threads
    };
    builder.threads(threads);

    // Add file type filters
    if !opts.file_types.is_empty() {
        let mut types_builder = TypesBuilder::new();
        types_builder.add_defaults();
        for file_type in &opts.file_types {
            types_builder.select(file_type);
        }
        let types = types_builder
            .build()
            .map_err(|e| format!("Failed to build type matcher: {}", e))?;
        builder.types(types);
    }

    // Add glob overrides
    if !opts.glob_include.is_empty() || !opts.glob_exclude.is_empty() {
        let mut override_builder = OverrideBuilder::new(path);
        for glob in &opts.glob_include {
            override_builder
                .add(glob)
                .map_err(|e| format!("Invalid glob '{}': {}", glob, e))?;
        }
        for glob in &opts.glob_exclude {
            override_builder
                .add(&format!("!{}", glob))
                .map_err(|e| format!("Invalid glob '{}': {}", glob, e))?;
        }
        let overrides = override_builder
            .build()
            .map_err(|e| format!("Failed to build glob matcher: {}", e))?;
        builder.overrides(overrides);
    }

    Ok(builder)
}

/// Search a single file and collect matches
fn search_file(
    matcher: &grep_regex::RegexMatcher,
    searcher: &mut Searcher,
    path: &Path,
    max_count: Option<u64>,
) -> Result<Vec<Match>, std::io::Error> {
    let mut matches = Vec::new();
    let path_str = path.to_path_buf();
    let match_count = AtomicUsize::new(0);

    searcher.search_path(
        matcher,
        path,
        UTF8(|line_num, line| {
            // Check max count
            if let Some(max) = max_count {
                if match_count.load(Ordering::Relaxed) as u64 >= max {
                    return Ok(false); // Stop searching this file
                }
            }

            // Find column of match
            let col = if let Ok(Some(m)) = matcher.find(line.as_bytes()) {
                m.start()
            } else {
                0
            };

            matches.push(Match {
                file: path_str.clone(),
                line_number: line_num,
                column: col,
                text: line.trim_end_matches(&['\r', '\n'][..]).to_string(),
            });

            match_count.fetch_add(1, Ordering::Relaxed);
            Ok(true)
        }),
    )?;

    Ok(matches)
}

/// Perform a parallel search across a directory
pub fn search_parallel(
    pattern: &str,
    path: &str,
    opts: &SearchOptions,
) -> Result<SearchResult, String> {
    let start = std::time::Instant::now();
    let search_path = Path::new(path);

    // Build components
    let matcher = Arc::new(build_matcher(pattern, opts)?);
    let walker = build_walker(search_path, opts)?;

    // Shared state
    let matches: Arc<Mutex<Vec<Match>>> = Arc::new(Mutex::new(Vec::new()));
    let errors: Arc<Mutex<Vec<String>>> = Arc::new(Mutex::new(Vec::new()));
    let files_searched = Arc::new(AtomicUsize::new(0));
    let files_matched = Arc::new(AtomicUsize::new(0));
    let quit_flag = Arc::new(AtomicBool::new(false));

    // Channel for sending matches from workers to collector
    let (tx, rx) = channel::unbounded::<Vec<Match>>();

    // Spawn collector thread
    let matches_clone = Arc::clone(&matches);
    let collector = std::thread::spawn(move || {
        for file_matches in rx {
            let mut all_matches = matches_clone.lock().unwrap();
            all_matches.extend(file_matches);
        }
    });

    // Run parallel walk
    let max_count = opts.max_count;
    let max_filesize = opts.max_filesize;

    walker.build_parallel().run(|| {
        let matcher = Arc::clone(&matcher);
        let tx = tx.clone();
        let errors = Arc::clone(&errors);
        let files_searched = Arc::clone(&files_searched);
        let files_matched = Arc::clone(&files_matched);
        let quit_flag = Arc::clone(&quit_flag);
        let mut searcher = build_searcher(opts);

        Box::new(move |entry| {
            // Check if we should quit
            if quit_flag.load(Ordering::Relaxed) {
                return WalkState::Quit;
            }

            let entry = match entry {
                Ok(e) => e,
                Err(err) => {
                    errors.lock().unwrap().push(format!("{}", err));
                    return WalkState::Continue;
                }
            };

            // Skip directories
            if entry.file_type().map(|t| t.is_dir()).unwrap_or(false) {
                return WalkState::Continue;
            }

            let path = entry.path();

            // Check file size limit
            if let Some(max_size) = max_filesize {
                if let Ok(meta) = path.metadata() {
                    if meta.len() > max_size {
                        return WalkState::Continue;
                    }
                }
            }

            files_searched.fetch_add(1, Ordering::Relaxed);

            // Search the file
            match search_file(&matcher, &mut searcher, path, max_count) {
                Ok(file_matches) => {
                    if !file_matches.is_empty() {
                        files_matched.fetch_add(1, Ordering::Relaxed);
                        let _ = tx.send(file_matches);
                    }
                }
                Err(err) => {
                    // Silently skip files that can't be read (binary, permission denied, etc.)
                    if err.kind() != std::io::ErrorKind::InvalidData {
                        errors.lock().unwrap().push(format!("{}: {}", path.display(), err));
                    }
                }
            }

            WalkState::Continue
        })
    });

    // Close sender and wait for collector
    drop(tx);
    collector.join().unwrap();

    let elapsed = start.elapsed();
    let all_matches = Arc::try_unwrap(matches).unwrap().into_inner().unwrap();
    let all_errors = Arc::try_unwrap(errors).unwrap().into_inner().unwrap();

    Ok(SearchResult {
        stats: SearchStats {
            matches: all_matches.len(),
            files_searched: files_searched.load(Ordering::Relaxed),
            files_matched: files_matched.load(Ordering::Relaxed),
            elapsed_ms: elapsed.as_millis() as u64,
        },
        matches: all_matches,
        errors: all_errors,
    })
}

/// Format elapsed time in human-readable form
fn format_duration(ms: u64) -> String {
    if ms < 1000 {
        format!("{} ms", ms)
    } else if ms < 60_000 {
        let secs = ms as f64 / 1000.0;
        if secs < 10.0 {
            format!("{:.1} seconds", secs)
        } else {
            format!("{} seconds", secs as u64)
        }
    } else if ms < 3_600_000 {
        let mins = ms / 60_000;
        let secs = (ms % 60_000) / 1000;
        if secs > 0 {
            format!("{} minutes {} seconds", mins, secs)
        } else {
            format!("{} minutes", mins)
        }
    } else {
        let hours = ms / 3_600_000;
        let mins = (ms % 3_600_000) / 60_000;
        format!("{} hours {} minutes", hours, mins)
    }
}

/// Format results with statistics
pub fn format_results_with_stats(result: &SearchResult) -> String {
    let mut output = String::new();

    let time_str = format_duration(result.stats.elapsed_ms);
    let result_word = if result.stats.matches == 1 { "RESULT" } else { "RESULTS" };
    let file_word = if result.stats.files_searched == 1 { "FILE" } else { "FILES" };
    output.push_str(&format!(
        "{} {} ACROSS {} {}. Search completed in {}.\n\n",
        result.stats.matches,
        result_word,
        result.stats.files_searched,
        file_word,
        time_str
    ));

    for m in &result.matches {
        output.push_str(&format!(
            "{}:{}:{}: {}\n",
            m.file.display(),
            m.line_number,
            m.column,
            m.text
        ));
    }

    if !result.errors.is_empty() {
        output.push_str(&format!("\n{} errors encountered:\n", result.errors.len()));
        for err in &result.errors {
            output.push_str(&format!("  {}\n", err));
        }
    }

    output
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_default_options() {
        let opts = SearchOptions::default();
        assert!(!opts.case_insensitive);
        assert!(opts.smart_case);
        assert!(opts.git_ignore);
    }

    #[test]
    fn test_build_matcher() {
        let opts = SearchOptions::default();
        let matcher = build_matcher("test", &opts);
        assert!(matcher.is_ok());
    }

    #[test]
    fn test_build_matcher_invalid() {
        let opts = SearchOptions::default();
        let matcher = build_matcher("[invalid", &opts);
        assert!(matcher.is_err());
    }
}
