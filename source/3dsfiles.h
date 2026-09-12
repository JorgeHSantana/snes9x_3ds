
#ifndef _3DSFILES_H
#define _3DSFILES_H

#include <3ds.h>
#include <limits.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "3dsmenu.h"
#include "3dsdirentry.h"
#include "file_stream.h"

#define PARENT_DIRECTORY_LABEL "  ... Parent Directory"

#define DIRECTORY_CACHE_THRESHOLD 50
#define DIRECTORY_CACHE_VERSION 2

#define MAX_THUMB_TYPES 3

bool file3dsInitialize();
void file3dsFinalize();
// stop the background directory sweep and wait for it (called by
// file3dsFinalize before the process returns and sdmc: is unmounted)
void file3dsBgScanShutdown();
// true when parentDir/dirName is an MSU-1 pack folder (one ROM + audio/data
// files) - the browser shows those as one virtual entry and never enters them
bool file3dsIsMsuPackDir(const char* parentDir, const char* dirName);

void file3dsGoUpOrDownDirectory(const DirectoryEntry& entry);
void file3dsGoToParentDirectory(void);
void file3dsGoToChildDirectory(const char* childDir);

void file3dsSetDefaultDir(bool clear);
void file3dsSetCurrentDir(const char* targetDir = NULL);
char *file3dsGetCurrentDir(void);
void file3dsGetCurrentDirName(char* output, size_t bufferSize);
int file3dsGetCurrentDirRomCount(void);
void file3dsGetCurrentDirCacheName(char* output, size_t bufferSize);
const char* file3dsGetCurrentDirCacheDate();
bool file3dsIsCurrentDirLoadedFromCache();
void file3dsDeleteCurrentDirCache();

bool file3dsGetFiles(std::vector<DirectoryEntry>& files, std::vector<SMenuTab>& menuTabs, bool showCachingIndicator = false);
bool file3dsThumbnailsAvailable();
bool file3dsThumbnailsAvailableByType(const char* type);
void file3dsSetRomNameMappings(const char* file);

bool IsFileExists(const char * filename);
bool file3dsIsValidFilename(const char* filename);

// Background cache validation (issue #6): after a directory is served from
// its cache, a worker thread rescans it; when the contents differ, the UI
// thread picks the fresh list up here (returns false if nothing pending or
// the user already navigated elsewhere).
bool file3dsBgRefreshTake(std::vector<DirectoryEntry>& files);

// display name for a VirtualFile entry (the pack folder name)
void file3dsGetVirtualDisplayName(const DirectoryEntry& entry, char* output, size_t bufferSize);

// full path for related files (saves, configs, etc.)
void file3dsGetRelatedPath(const char* path, char* output, size_t bufferSize, const char* ext, const char* targetDir, bool trimmed = false);


#endif
