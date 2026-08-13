#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "3dsutils.h"
#include "3dslog.h"
#include "3dssettings.h"
#include "3dsfiles.h"

enum class DirCacheStatus {
    Success,
    NotFound,
    Corrupt
};

typedef struct {
    u32 magic;
    u32 version;
    u32 romCount;
    u64 createdAt;
} DirCacheHeader;

void file3dsShowCachingIndicator(std::vector<SMenuTab>& menuTabs);

static std::unordered_map<std::string, std::string> romNameMappings;

static char availableThumbnailTypes[MAX_THUMB_TYPES][32]; // boxart, title, gameplay
static int availableThumbCount = 0;

static char currentDir[PATH_MAX] = "";
static char currentDirCacheDate[32] = "";
static int currentDirRomCount = 0;
static bool currentDirLoadedFromCache = false;

static const char* validExtensions[] = { ".smc", ".sfc", ".fig", ".bs", ".bsx", ".zip" };
static const int validExtensionsCount = sizeof(validExtensions) / sizeof(validExtensions[0]);
static const size_t intialFileCapacity = 1024;

// covers the largest possible UI texture (512x256 RGBA8)
// and should be large enough for snes9x save states
u8* g_fileBuffer = NULL;

// aligned stream buffer for optimized DMA/Cache performance
u8 g_streamBuffer[CACHE_LINE_SIZE * 1024] __attribute__((aligned(CACHE_LINE_SIZE)));
FILE* g_streamBufferOwner = NULL;

bool file3dsInitialize() {
    log3dsWrite("allocate file buffer (%.2fkb)", float(MAX_IO_BUFFER_SIZE) / 1024);
    g_fileBuffer = (u8*)malloc(MAX_IO_BUFFER_SIZE);

    if (!g_fileBuffer) {
        return false;
    }
    
    memset(g_fileBuffer, 0, MAX_IO_BUFFER_SIZE);
    
    log3dsWrite("check for required directories:");

    const char* directories[] = { "", "configs", "saves", "savestates", "savestates/screenshots", "screenshots", ".dir_cache", "stereo3d" };
    char reqDir[128];

    for (const char* dir : directories) {
        if (dir[0] == '\0') {
             snprintf(reqDir, sizeof(reqDir), "%s", settings3DS.RootDir);
        } else {
             snprintf(reqDir, sizeof(reqDir), "%s/%s", settings3DS.RootDir, dir);
        }

        DIR* d = opendir(reqDir);
        if (d) {
            closedir(d);
            log3dsWrite("%s v", reqDir);
        } else {
            mkdir(reqDir, 0777);
            log3dsWrite("%s x (created)", reqDir);
        }
    }
    
    const char* thumbnailTypes[] = { "boxart", "title", "gameplay" };

    log3dsWrite("check for available thumbnail types:");
    char cacheFile[128];

    availableThumbCount = 0; // Reset count

    for (const char* type : thumbnailTypes) {
        snprintf(cacheFile, sizeof(cacheFile), "%s/thumbnails/%s.cache", settings3DS.RootDir, type);

        if (IsFileExists(cacheFile)) {
            if (availableThumbCount < MAX_THUMB_TYPES) {
                snprintf(availableThumbnailTypes[availableThumbCount], 32, "%s", type);
                availableThumbCount++;
            }
            log3dsWrite("%s v", cacheFile);
        } else {
            log3dsWrite("%s x", cacheFile);
        }
    }

    const char* targetDir = NULL;

    // check first, if user has set a default directory
    if (settings3DS.defaultDir[0] != '\0' && strcmp(settings3DS.defaultDir, "/") != 0) {
        targetDir = settings3DS.defaultDir;
    }
    else if (settings3DS.lastSelectedDir[0] != '\0') {
        targetDir = settings3DS.lastSelectedDir;
    }
    
    file3dsSetCurrentDir(targetDir);

    DIR* d = opendir(currentDir);
    if (d) {
        closedir(d);
        log3dsWrite("[file3dsInitialize] start directory set to %s", currentDir);
    } 
    else {
        log3dsWrite("[file3dsInitialize] failed to open %s. Fallback to smdc:/", currentDir);
        file3dsSetCurrentDir(NULL);
    }

    return true;
}

bool IsFileExists(const char * filename) {
    if (filename == nullptr || filename[0] == '\0') {
        return false;
    }

    struct stat buffer;
    
    return (stat(filename, &buffer) == 0);
}

void file3dsSetDefaultDir(bool clear) {
    if (clear) {
        settings3DS.defaultDir[0] = '\0';
    } else {
        snprintf(settings3DS.defaultDir, sizeof(settings3DS.defaultDir), "%s", currentDir);
    }
}

void file3dsSetCurrentDir(const char* targetDir) {
    // default to "sdmc:/"
    if (targetDir == NULL || targetDir[0] == '\0') {
        snprintf(currentDir, sizeof(currentDir), "sdmc:/");

        return;
    }

    // If the path starts with '/' prepend "sdmc:"
    if (targetDir[0] == '/') {
        snprintf(currentDir, sizeof(currentDir), "sdmc:%s", targetDir);
    }
    else {
        snprintf(currentDir, sizeof(currentDir), "%s", targetDir);
    }

    // make sure we only end with a slash
    size_t len = strlen(currentDir);
    if (len > 0 && currentDir[len - 1] != '/') {
        strncat(currentDir, "/", sizeof(currentDir) - len - 1);
    }
}

void file3dsFinalize() 
{
    log3dsWrite("dealloc file buffer, clear romNameMappings");
    free(g_fileBuffer);
    romNameMappings.clear();
}

bool file3dsThumbnailsAvailableByType(const char* type) {
    for (int i = 0; i < availableThumbCount; i++) {
        if (strcmp(availableThumbnailTypes[i], type) == 0) {
            return true;
        }
    }
    return false;
}


bool file3dsThumbnailsAvailable() {
    return availableThumbCount > 0;
}

void file3dsSetRomNameMappings(const char* file) {
    std::ifstream inputFile(file);

    if (!inputFile.is_open()) {
        log3dsWrite("failed to open");


        return;
    }

    romNameMappings.clear();

    std::string line;
    int count = 0;

    while (std::getline(inputFile, line)) {
        if (line.empty() || line[0] == '#') continue;

        size_t delimiterPos = line.find("=");
        if (delimiterPos != std::string::npos) {
            std::string key = line.substr(0, delimiterPos);
            std::string value = line.substr(delimiterPos + 1);
            romNameMappings[key] = value;
            count++;
        }
    }

    log3dsWrite("loaded %d rom name mappings", count);

    inputFile.close();
}

void file3dsGetCurrentDirName(char* output, size_t bufferSize) {
    if (!output || bufferSize == 0) return;
    output[0] = '\0';

    size_t len = strlen(currentDir);
    
    // currentDir implies it always ends in '/'
    if (len < 2) return;

    // start at the character BEFORE the trailing slash
    // e.g. "sdmc:/roms/snes/" (len-1 is slash, len-2 is 's')
    const char* end = currentDir + len - 2;
    const char* start = end;

    // scan backwards to find the slash BEFORE the directory name
    // stop if we hit the beginning of the string
    while (start > currentDir && *start != '/') {
        start--;
    }

    // if we stopped at a slash, move forward one char to get the name start
    if (*start == '/') {
        start++;
    }

    size_t nameLen = (end - start) + 1;
    
    if (nameLen >= bufferSize) nameLen = bufferSize - 1;

    strncpy(output, start, nameLen);
    output[nameLen] = '\0';
}

char *file3dsGetCurrentDir(void)
{
    return currentDir;
}

int file3dsGetCurrentDirRomCount()
{
    return currentDirRomCount;
}

bool file3dsIsCurrentDirLoadedFromCache()
{
    return currentDirLoadedFromCache;
}

static void file3dsGetDirCacheNameFor(const char* dir, char* output, size_t bufferSize) {
    if (!output || bufferSize == 0) return;

    char escapedPath[PATH_MAX];
    utils3dsGetSanitizedPath(dir, escapedPath, sizeof(escapedPath));

    // root path sanitizes to empty string —> no cache file for root
    if (escapedPath[0] == '\0') {
        output[0] = '\0';
        return;
    }

    int written = snprintf(output, bufferSize, "%s/.dir_cache/%s", settings3DS.RootDir, escapedPath);
    if (written < 0 || (size_t)written >= bufferSize) {
        // no cache file for this edge case
        output[0] = '\0';
    }
}

void file3dsGetCurrentDirCacheName(char* output, size_t bufferSize) {
    file3dsGetDirCacheNameFor(currentDir, output, bufferSize);
}

void file3dsGoUpOrDownDirectory(const DirectoryEntry& entry) {
    if (entry.Type == FileEntryType::ParentDirectory) {
        file3dsGoToParentDirectory();
    } else if (entry.Type == FileEntryType::ChildDirectory) {
        file3dsGoToChildDirectory(entry.Filename);
    }
}

void file3dsGoToParentDirectory(void)
{
    int len = strlen(currentDir);

    if (len > 1)
    {
        for (int i = len - 2; i>=0; i--)
        {
            if (currentDir[i] == '/')
            {
                currentDir[i + 1] = 0;
                break;
            }
        }
    }
}

void file3dsGoToChildDirectory(const char* childDir)
{
    size_t len = strlen(currentDir);
    snprintf(currentDir + len, sizeof(currentDir) - len, "%s/", childDir);
}

const char* file3dsGetCurrentDirCacheDate() {
    return currentDirCacheDate;
}

void file3dsSetCurrentDirCacheDate(u64 createdAt) {
    if (!createdAt) {
        currentDirCacheDate[0] = '\0';

        return;
    }

    utils3dsGetFormattedDate((time_t)createdAt, currentDirCacheDate, sizeof(currentDirCacheDate));
}

// raw creation time of the currently loaded cache (0 = none), so the
// staleness check in file3dsGetFiles can compare against the clock
static u64 currentDirCacheCreatedAt = 0;

// --- background cache validation (issue #6) ---------------------------------
// After serving a directory from its cache, a low-priority worker rescans it.
// The worker never touches the UI's vectors: it fills its own list and the UI
// thread swaps it in via file3dsBgRefreshTake at a safe point in the menu
// loop. bgGeneration is bumped on every list rebuild, so results computed for
// a directory the user already left (or rescanned) are dropped.
static LightLock bgLock;
static bool bgLockInitialized = false;
static volatile bool bgScanRunning = false;
static bool bgResultReady = false;
static std::vector<DirectoryEntry> bgResult;
static int bgResultRomCount = 0;
static char bgScanDir[PATH_MAX];
static u32 bgGeneration = 0;
static u32 bgScanGeneration = 0;

static void file3dsBgLockInit() {
    if (!bgLockInitialized) {
        LightLock_Init(&bgLock);
        bgLockInitialized = true;
    }
}

// invalidate any pending/in-flight background result (list is being rebuilt)
static void file3dsBgInvalidate() {
    file3dsBgLockInit();
    LightLock_Lock(&bgLock);
    bgGeneration++;
    bgResultReady = false;
    LightLock_Unlock(&bgLock);
}

// True when the subdirectory contains exactly one ROM plus MSU-1 pack files
// (.msu data/.pcm or compressed audio). romOut receives the ROM's filename.
static bool file3dsProbeMsuPack(const char* parentDir, const char* dirName, char* romOut, size_t romOutSize) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s%s", parentDir, dirName);

    DIR* d = opendir(path);
    if (!d) return false;

    int romCount = 0;
    bool hasMsuFiles = false;
    struct dirent* entry;

    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.' || entry->d_type != DT_REG)
            continue;

        if (file3dsIsValidFilename(entry->d_name)) {
            if (++romCount > 1) break;
            snprintf(romOut, romOutSize, "%s", entry->d_name);
        } else if (!hasMsuFiles) {
            const char* dot = strrchr(entry->d_name, '.');
            if (dot && (strcasecmp(dot, ".msu") == 0 || strcasecmp(dot, ".pcm") == 0 ||
                        strcasecmp(dot, ".flac") == 0 || strcasecmp(dot, ".ogg") == 0)) {
                hasMsuFiles = true;
            }
        }
    }

    closedir(d);
    return dirEntryQualifiesAsMsuPack(romCount, hasMsuFiles);
}

// The full directory sweep shared by the UI scan and the background worker:
// readdir + MSU-pack flattening + sort. menuTabs may be NULL (worker).
static bool file3dsScanDirectoryInto(const char* dirPath, std::vector<DirectoryEntry>& files,
    int& romCount, std::vector<SMenuTab>* menuTabs, bool showCachingIndicator) {

    DIR* d = opendir(dirPath);
    if (!d) {
        return false;
    }

    if (strlen(dirPath) > strlen("sdmc:/")) {
        files.emplace_back(PARENT_DIRECTORY_LABEL, FileEntryType::ParentDirectory);
    }

    struct dirent* entry;

    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        if (entry->d_type == DT_DIR) {
            char packRom[NAME_MAX + 1];
            if (file3dsProbeMsuPack(dirPath, entry->d_name, packRom, sizeof(packRom)) &&
                strlen(entry->d_name) + 1 + strlen(packRom) <= NAME_MAX) {
                char vpath[NAME_MAX * 2 + 2];   // guarded <= NAME_MAX above
                snprintf(vpath, sizeof(vpath), "%s/%s", entry->d_name, packRom);
                files.emplace_back(vpath, FileEntryType::VirtualFile);
                romCount++;
            } else {
                files.emplace_back(entry->d_name, FileEntryType::ChildDirectory);
            }
        } else if (entry->d_type == DT_REG) {
            if (file3dsIsValidFilename(entry->d_name)) {
                files.emplace_back(entry->d_name, FileEntryType::File);
                romCount++;

                if (files.size() == DIRECTORY_CACHE_THRESHOLD + 1 && menuTabs && showCachingIndicator) {
                    file3dsShowCachingIndicator(*menuTabs);
                }
            }
        }
    }

    closedir(d);

    // directories first, then files and MSU packs interleaved alphabetically
    std::sort(files.begin(), files.end(), dirEntrySortLess);

    return true;
}

// header/entry read without any global side effects (also used by the
// background worker, which must not touch the UI-facing state)
static DirCacheStatus file3dsReadDirCacheRaw(std::vector<DirectoryEntry>& files,
    const char* cachePath, DirCacheHeader& header) {
    DirCacheStatus status = DirCacheStatus::Success;

    FILE* fp = file3dsOpen(cachePath, "rb");
    if (!fp) return DirCacheStatus::NotFound;

    if (fread(&header, sizeof(DirCacheHeader), 1, fp) != 1) {
        status = DirCacheStatus::Corrupt;
        goto cleanup;
    }

    if (header.magic != 0x534E3958 || header.version != DIRECTORY_CACHE_VERSION) {
        status = DirCacheStatus::Corrupt;
        goto cleanup;
    }

    files.resize(header.romCount);

    if (header.romCount > 0) {
        size_t itemsRead = fread(files.data(), sizeof(DirectoryEntry), header.romCount, fp);

        if (itemsRead != header.romCount) {
            files.clear();

            status = DirCacheStatus::Corrupt;
            goto cleanup;
        }
    }

cleanup:
    if (fp) file3dsClose(fp);
    return status;
}

DirCacheStatus file3dsLoadDirCache(std::vector<DirectoryEntry>& files, const char* cachePath) {
    DirCacheHeader header;
    DirCacheStatus status = file3dsReadDirCacheRaw(files, cachePath, header);

    if (status == DirCacheStatus::Success) {
        file3dsSetCurrentDirCacheDate(header.createdAt);
        currentDirCacheCreatedAt = header.createdAt;

        for (const auto& entry : files) {
            if (entry.Type == FileEntryType::File || entry.Type == FileEntryType::VirtualFile) {
                currentDirRomCount++;
            }
        }
    }

    if (status == DirCacheStatus::Corrupt) {
        log3dsWrite("[file3dsLoadDirCache] cache invalid (Code %d). Deleting: %s", status, cachePath);
        remove(cachePath);
    }

    return status;
}

// raw write, no UI-facing side effects (worker-safe)
static u64 file3dsWriteDirCacheRaw(const std::vector<DirectoryEntry>& files, const char* cachePath) {
    FILE* fp = fopen(cachePath, "wb");
    if (!fp) return 0;

    // disable buffering (_IONBF)
    // We are writing the vector in one massive shot. 
    // We don't want the overhead of chopping it into 32KB buffer chunks.
    setvbuf(fp, NULL, _IONBF, 0);

    u64 createdAt = (u64)time(NULL);
    DirCacheHeader header = { 0x534E3958, DIRECTORY_CACHE_VERSION, (u32)files.size(), createdAt };
    
    fwrite(&header, sizeof(DirCacheHeader), 1, fp);

    if (!files.empty()) {
        fwrite(files.data(), sizeof(DirectoryEntry), files.size(), fp);
    }
    
    fclose(fp);
    return createdAt;
}

void file3dsSaveDirCache(const std::vector<DirectoryEntry>& files, const char* cachePath) {
    u64 createdAt = file3dsWriteDirCacheRaw(files, cachePath);
    if (createdAt) {
        file3dsSetCurrentDirCacheDate(createdAt);
        currentDirCacheCreatedAt = createdAt;
    }
}

void file3dsDeleteCurrentDirCache() {
    char cachePath[PATH_MAX];
    file3dsGetCurrentDirCacheName(cachePath, sizeof(cachePath));

    if (remove(cachePath) == 0) {
        log3dsWrite("Deleted dir cache: %s", cachePath);
    }
}

void file3dsShowCachingIndicator(std::vector<SMenuTab>& menuTabs) {
    int currentTabIndex = menuTabs.size() - 1;
    SMenuTab& fileMenuTab = menuTabs[currentTabIndex];

    SMenuItem& selectedItem = fileMenuTab.MenuItems[fileMenuTab.SelectedItemIndex];
    selectedItem.Text = selectedItem.Text + " (caching...)";

    menu3dsDrawEverything(currentTabIndex, menuTabs);
    menu3dsSwapBuffersAndWaitForVBlank();
}

static void file3dsBgScanThread(void*) {
    char dir[PATH_MAX];
    u32 gen;

    LightLock_Lock(&bgLock);
    snprintf(dir, sizeof(dir), "%s", bgScanDir);
    gen = bgScanGeneration;
    LightLock_Unlock(&bgLock);

    std::vector<DirectoryEntry> fresh;
    int romCount = 0;

    if (file3dsScanDirectoryInto(dir, fresh, romCount, NULL, false)) {
        char cachePath[PATH_MAX];
        file3dsGetDirCacheNameFor(dir, cachePath, sizeof(cachePath));

        std::vector<DirectoryEntry> cached;
        DirCacheHeader header;
        bool changed = true;
        if (cachePath[0] != '\0' &&
            file3dsReadDirCacheRaw(cached, cachePath, header) == DirCacheStatus::Success) {
            changed = !dirEntryListsEqual(fresh, cached);
        }

        if (changed) {
            log3dsWrite("[file3dsBgScanThread] %s changed on disk, refreshing (%d entries)", dir, fresh.size());
            if (cachePath[0] != '\0' && fresh.size() >= DIRECTORY_CACHE_THRESHOLD) {
                file3dsWriteDirCacheRaw(fresh, cachePath);
            }

            LightLock_Lock(&bgLock);
            if (gen == bgGeneration) {
                bgResult.swap(fresh);
                bgResultRomCount = romCount;
                bgResultReady = true;
            }
            LightLock_Unlock(&bgLock);
        }
    }

    bgScanRunning = false;
}

static void file3dsBgStartValidation() {
    file3dsBgLockInit();

    if (bgScanRunning) return;
    bgScanRunning = true;

    LightLock_Lock(&bgLock);
    snprintf(bgScanDir, sizeof(bgScanDir), "%s", currentDir);
    bgScanGeneration = bgGeneration;
    bgResultReady = false;
    LightLock_Unlock(&bgLock);

    // low priority, current core, detached: one readdir sweep then exits
    Thread t = threadCreate(file3dsBgScanThread, NULL, 0x4000, 0x3F, -2, true);
    if (!t) {
        bgScanRunning = false;
    }
}

bool file3dsBgRefreshTake(std::vector<DirectoryEntry>& files) {
    if (!bgLockInitialized || !bgResultReady) return false;

    bool taken = false;
    LightLock_Lock(&bgLock);
    if (bgResultReady && strcmp(bgScanDir, currentDir) == 0) {
        files.swap(bgResult);
        bgResult.clear();
        currentDirRomCount = bgResultRomCount;
        currentDirLoadedFromCache = false;
        taken = true;
    }
    bgResultReady = false;
    LightLock_Unlock(&bgLock);

    return taken;
}

void file3dsGetVirtualDisplayName(const DirectoryEntry& entry, char* output, size_t bufferSize) {
    dirEntryVirtualDisplayName(entry, output, bufferSize);
}

bool file3dsGetFiles(std::vector<DirectoryEntry>& files, std::vector<SMenuTab>& menuTabs, bool showCachingIndicator) {
    file3dsBgInvalidate();
    currentDirRomCount = 0;
    currentDirLoadedFromCache = false;
    files.clear();

    // only reserve if we are below our "minimum baseline". 
    if (files.capacity() < intialFileCapacity) {
        files.reserve(intialFileCapacity);
    }

    TickCounter timer;
    osTickCounterStart(&timer);
    
    char cachePath[PATH_MAX];
    file3dsGetCurrentDirCacheName(cachePath, sizeof(cachePath));

    if (file3dsLoadDirCache(files, cachePath) == DirCacheStatus::Success) {
        // Self-validating cache (issue #6): a snapshot older than the TTL
        // is re-scanned on this visit (with the caching indicator) and
        // rewritten, so content copied to the SD while the console was
        // off shows up without a manual rescan. Fresh caches still serve
        // instantly, keeping big-directory navigation fast.
        const u64 DIR_CACHE_TTL_SECONDS = 15 * 60;
        u64 now = (u64)time(NULL);
        bool stale = currentDirCacheCreatedAt == 0 ||
            now < currentDirCacheCreatedAt ||
            now - currentDirCacheCreatedAt > DIR_CACHE_TTL_SECONDS;

        if (!stale) {
            currentDirLoadedFromCache = true;
            osTickCounterUpdate(&timer);
            log3dsWrite("[file3dsGetFiles] %d files loaded from cache (%s) in %.3fms", files.size(), cachePath, osTickCounterRead(&timer));

            // serve the snapshot instantly, verify it in the background
            file3dsBgStartValidation();
            return true;
        }

        log3dsWrite("[file3dsGetFiles] cache stale (%llus old), rescanning: %s",
            now - currentDirCacheCreatedAt, cachePath);
        files.clear();
        currentDirRomCount = 0;
    }
    
    // slow path
    osTickCounterStart(&timer);

    file3dsSetCurrentDirCacheDate(0);
    
    if (!file3dsScanDirectoryInto(currentDir, files, currentDirRomCount, &menuTabs, showCachingIndicator)) {
        return false;
    }

    osTickCounterUpdate(&timer);
    log3dsWrite("[file3dsGetFiles] %s: %d files prepared in %.3fms", currentDir, files.size(), osTickCounterRead(&timer));

    if (files.size() >= DIRECTORY_CACHE_THRESHOLD) {
        osTickCounterStart(&timer);
        file3dsSaveDirCache(files, cachePath);
        osTickCounterUpdate(&timer);
        log3dsWrite("[file3dsGetFiles] dir cache created in %.3fms (%s)", osTickCounterRead(&timer), cachePath);
    }

    return true;
}

bool file3dsIsValidFilename(const char* filename) {
    if (!filename || filename[0] == '.') return false;

    const char* dot = strrchr(filename, '.');
    if (!dot || dot[1] == '\0') return false;

    for (int i = 0; i < validExtensionsCount; i++) {
        if (strcasecmp(dot, validExtensions[i]) == 0) {
            return true;
        }
    }

    return false;
}

void file3dsGetRelatedPath(const char* path, char* output, size_t bufferSize, const char* ext, const char* targetDir, bool trimmed) {
    if (!path || !output || bufferSize == 0) {
        if (bufferSize > 0) output[0] = '\0';
        return;
    }

    char basename[NAME_MAX + 1];

    if (trimmed) {
        // e.g. "Kirby Super Star (USA) (V1.2) [!].sfc" -> "Kirby Super Star"
        utils3dsGetTrimmedBasename(path, basename, sizeof(basename), false);

        // if filename is part of romNameMappings use its associated value instead
        // (e.g. "Kirby's Fun Pak" is looking for "Kirby Super Star")
        auto it = romNameMappings.find(std::string(basename));
        if (it != romNameMappings.end()) {
            snprintf(basename, sizeof(basename), "%s", it->second.c_str());
        }
    } else {
        // e.g. "Kirby Super Star (USA) (V1.2) [!].sfc" -> "Kirby Super Star (USA) (V1.2) [!]"
        utils3dsGetBasename(path, basename, sizeof(basename), false);
    }

    const char* extension = (ext) ? ext : "";

    if (targetDir) {
        // (e.g. sdmc:/3ds/snes9x_3ds/backgrounds/game_screen/Super Mario World.png)
        snprintf(output, bufferSize, "%s/%s/%s%s", settings3DS.RootDir, targetDir, basename, extension);
    } 
    else {
        // we need the directory from 'filename'
        const char* slash = strrchr(path, '/');
        
        if (slash) {
            // calculate the length of the directory part
            int dirLen = (int)(slash - path);
            
            // "%.*s" prints exactly 'dirLen' characters from 'filename'
            snprintf(output, bufferSize, "%.*s/%s%s", dirLen, path, basename, extension);
        } 
        else {
            // fallback: No slash found (filename is just "game.sfc")
            snprintf(output, bufferSize, "%s%s", basename, extension);
        }
    }
}
