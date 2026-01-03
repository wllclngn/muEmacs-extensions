{-# LANGUAGE ForeignFunctionInterface #-}

-- Project Management Extension for μEmacs
-- Written in Haskell - demonstrating functional programming integration

module Project where

import Foreign.C.Types
import Foreign.C.String
import Foreign.Ptr
import System.Directory
import System.FilePath
import Control.Monad
import Control.Exception (catch, IOException)
import Data.List

-- C bridge imports
foreign import ccall "bridge_message"
    c_message :: CString -> IO ()

foreign import ccall "bridge_current_buffer"
    c_currentBuffer :: IO (Ptr ())

foreign import ccall "bridge_buffer_filename"
    c_bufferFilename :: Ptr () -> IO CString

foreign import ccall "bridge_buffer_create"
    c_bufferCreate :: CString -> IO (Ptr ())

foreign import ccall "bridge_buffer_switch"
    c_bufferSwitch :: Ptr () -> IO CInt

foreign import ccall "bridge_buffer_clear"
    c_bufferClear :: Ptr () -> IO CInt

foreign import ccall "bridge_buffer_insert"
    c_bufferInsert :: CString -> CSize -> IO CInt

-- Helper: send message to μEmacs
message :: String -> IO ()
message msg = withCString msg c_message

-- Helper: insert text into current buffer
insertText :: String -> IO ()
insertText txt = withCString txt $ \cs ->
    void $ c_bufferInsert cs (fromIntegral $ length txt)

-- Helper: show content in a named buffer
showInBuffer :: String -> String -> IO ()
showInBuffer bufName content = do
    bp <- withCString bufName c_bufferCreate
    when (bp /= nullPtr) $ do
        void $ c_bufferSwitch bp
        void $ c_bufferClear bp
        forM_ (lines content) $ \line ->
            insertText (line ++ "\n")

-- Get current file's directory
getCurrentFileDir :: IO (Maybe FilePath)
getCurrentFileDir = do
    bp <- c_currentBuffer
    if bp == nullPtr
        then return Nothing
        else do
            fnPtr <- c_bufferFilename bp
            if fnPtr == nullPtr
                then return Nothing
                else do
                    fn <- peekCString fnPtr
                    if null fn
                        then return Nothing
                        else return $ Just $ takeDirectory fn

-- Project root markers
projectMarkers :: [String]
projectMarkers =
    [ ".git", ".hg", ".svn"
    , "Makefile", "CMakeLists.txt"
    , "package.json", "Cargo.toml", "go.mod"
    , "build.zig", "stack.yaml", "dune-project"
    , "setup.py", "pyproject.toml"
    ]

-- Check if directory has any project marker
hasProjectMarker :: FilePath -> IO Bool
hasProjectMarker dir = do
    contents <- listDirectory dir `catch` handler
    return $ any (`elem` contents) projectMarkers
  where
    handler :: IOException -> IO [FilePath]
    handler _ = return []

-- Find project root by walking up
findProjectRoot :: FilePath -> IO (Maybe FilePath)
findProjectRoot startDir = go (splitDirectories $ normalise startDir)
  where
    go [] = return Nothing
    go parts = do
        let dir = joinPath parts
        isRoot <- hasProjectMarker dir
        if isRoot
            then return $ Just dir
            else if length parts <= 1
                then return Nothing
                else go (init parts)

-- Source file extensions
sourceExts :: [String]
sourceExts = [".c", ".h", ".hs", ".py", ".rs", ".go", ".zig", ".js", ".ts",
              ".cpp", ".hpp", ".java", ".rb", ".ml", ".f90", ".adb", ".ads"]

-- Walk directory tree
walkDir :: FilePath -> IO [FilePath]
walkDir dir = do
    contents <- listDirectory dir `catch` handler
    let validContents = filter (not . isHidden) contents
    paths <- forM validContents $ \name -> do
        let path = dir </> name
        isDir <- doesDirectoryExist path
        if isDir && not (isIgnoredDir name)
            then walkDir path
            else return [path | not isDir && isSourceFile name]
    return $ concat paths
  where
    handler :: IOException -> IO [FilePath]
    handler _ = return []
    isHidden ('.':_) = True
    isHidden _ = False
    isIgnoredDir name = name `elem`
        ["node_modules", "target", "build", "dist", "__pycache__", ".git"]
    isSourceFile f = any (`isSuffixOf` f) sourceExts

-- Command: project-root
hsProjectRoot :: CInt -> CInt -> IO CInt
hsProjectRoot _ _ = do
    mDir <- getCurrentFileDir
    case mDir of
        Nothing -> message "project-root: No file" >> return 0
        Just dir -> do
            mRoot <- findProjectRoot dir
            case mRoot of
                Nothing -> message ("project-root: No project in " ++ dir) >> return 0
                Just root -> message ("Project: " ++ root) >> return 1

-- Command: project-files
hsProjectFiles :: CInt -> CInt -> IO CInt
hsProjectFiles _ _ = do
    mDir <- getCurrentFileDir
    case mDir of
        Nothing -> message "project-files: No file" >> return 0
        Just dir -> do
            mRoot <- findProjectRoot dir
            case mRoot of
                Nothing -> message "project-files: No project" >> return 0
                Just root -> do
                    files <- walkDir root
                    let content = unlines $ take 200 $ sort files
                    showInBuffer "*project-files*" content
                    message $ "project-files: " ++ show (length files) ++ " files"
                    return 1

-- Command: project-find (shows file list)
hsProjectFind :: CInt -> CInt -> IO CInt
hsProjectFind _ _ = do
    mDir <- getCurrentFileDir
    case mDir of
        Nothing -> message "project-find: No file" >> return 0
        Just dir -> do
            mRoot <- findProjectRoot dir
            case mRoot of
                Nothing -> message "project-find: No project" >> return 0
                Just root -> do
                    files <- walkDir root
                    let content = unlines $ take 100 $ sort files
                    showInBuffer "*project-find*" content
                    message $ show (length files) ++ " project files"
                    return 1

-- FFI exports
foreign export ccall hs_project_root :: CInt -> CInt -> IO CInt
foreign export ccall hs_project_files :: CInt -> CInt -> IO CInt
foreign export ccall hs_project_find :: CInt -> CInt -> IO CInt

hs_project_root :: CInt -> CInt -> IO CInt
hs_project_root = hsProjectRoot

hs_project_files :: CInt -> CInt -> IO CInt
hs_project_files = hsProjectFiles

hs_project_find :: CInt -> CInt -> IO CInt
hs_project_find = hsProjectFind
