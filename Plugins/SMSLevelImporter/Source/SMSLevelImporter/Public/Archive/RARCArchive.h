// Copyright ryana. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Represents a single file or directory entry within a RARC archive.
 */
struct FRARCFileEntry
{
	FString Name;
	FString FullPath;
	uint16 FileID;
	uint16 Hash;
	uint8 Flags;        // 0x02=dir, 0x04=compressed, 0x80=YAZ0 compressed within archive
	uint32 DataOffset;  // Relative to file data section start
	uint32 Size;
	bool bIsDirectory;
};

/**
 * Parser for RARC archives — the container format found inside decompressed .szs files.
 *
 * After the ISO reader fetches a .szs file and YAZ0 decompresses it, the result
 * is RARC data containing all the scene's assets (models, textures, collision, etc.).
 * This class parses that data and provides random access to individual files.
 *
 * Binary layout:
 *   0x00: SArcHeader  (0x20 bytes) — signature 'RARC', sizes, offsets
 *   0x20: SArcDataInfo (0x20 bytes) — node/entry counts and offsets (relative to 0x20)
 *   Directory nodes at (0x20 + node_offset), each 0x10 bytes
 *   File entries at (0x20 + file_entry_offset), each 0x14 bytes
 *   String table at (0x20 + string_table_offset)
 *   File data region at (0x20 + file_data_offset)
 */
class SMSLEVELIMPORTER_API FRARCArchive
{
public:
	/** Parse from decompressed RARC data. Returns false if invalid. */
	bool Parse(const TArray<uint8>& Data);

	/**
	 * Get file data by internal path (e.g., "/scene/map/map/map.bmd").
	 * If the file is YAZ0 compressed within the archive (flag 0x80), auto-decompresses.
	 */
	TArray<uint8> GetFile(const FString& Path) const;

	/** Check if a file exists at the given internal path. */
	bool FileExists(const FString& Path) const;

	/** List all files (full paths, excluding directories). */
	TArray<FString> ListFiles() const;

	/**
	 * Find files matching an extension filter (e.g., "*.bmd" or ".bmd").
	 * Matches files whose names end with the given extension (case-insensitive).
	 */
	TArray<FString> FindFiles(const FString& Extension) const;

private:
	/** Full archive data (kept alive so GetFile can extract bytes on demand). */
	TArray<uint8> RawData;

	/** All parsed file entries (files only, no directories). */
	TArray<FRARCFileEntry> Entries;

	/** Map from lowercase full path to index in Entries. */
	TMap<FString, int32> PathToIndex;

	/** Absolute offset in RawData where the file data region begins. */
	uint32 FileDataStart = 0;
};
