// Copyright ryana. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformFileManager.h"

/**
 * Represents a single entry (file or directory) in the GameCube ISO filesystem table (FST).
 */
struct FGCFileEntry
{
	FString Name;
	uint32 Offset;      // Byte offset in ISO (for files)
	uint32 Size;        // File size (for files)
	bool bIsDirectory;
	int32 ParentIndex;  // For directories: parent dir index
	TArray<int32> Children; // For directories: child indices
};

/**
 * Reads a raw GameCube ISO file and provides a virtual filesystem interface.
 *
 * This is the entry point to the SMS data pipeline — it parses the disc header
 * and filesystem table (FST) to provide path-based access to all files on disc.
 *
 * GC ISO layout:
 *   0x0000: DVDDiskID — bytes 0-3 are game code (e.g., "GMSJ")
 *   0x0420: DVDBB2 struct — contains FST position and length
 *   FST: array of 12-byte entries followed by a string table
 */
class SMSLEVELIMPORTER_API FGCISOReader
{
public:
	~FGCISOReader();

	/** Open ISO file, parse disc header + FST. Returns false if not a valid GC disc. */
	bool Open(const FString& ISOPath);

	/** Close file handle and reset state. */
	void Close();

	/** Validate this is Super Mario Sunshine (game code starts with "GMS"). */
	bool IsSMS() const;

	/** Get region string from 4th char of game code: "JP"/"US"/"EU"/"KR". */
	FString GetRegion() const;

	/** List all files as full paths (e.g., "/scene/dolpic0.szs"). */
	TArray<FString> ListFiles() const;

	/** Read file by path — returns file data, empty array if not found. */
	TArray<uint8> ReadFile(const FString& Path) const;

	/** Check if file exists at the given path. */
	bool FileExists(const FString& Path) const;

private:
	IFileHandle* FileHandle = nullptr;
	FString GameCode;       // e.g., "GMSJ"
	TArray<FGCFileEntry> FST;
	TMap<FString, int32> PathToIndex;  // Full path -> FST index

	/** Parse the filesystem table from raw FST data. */
	bool ParseFST();

	/** Recursively build full paths for all entries starting from EntryIndex. */
	void BuildPaths(int32 EntryIndex, const FString& ParentPath);

	/** Read raw bytes from the ISO at the given offset. */
	TArray<uint8> ReadRawBytes(uint32 Offset, uint32 Size) const;

	/** FST raw data cached in memory after Open(). */
	TArray<uint8> FSTData;

	/** Position of FST on disc (from DVDBB2). */
	uint32 FSTPosition = 0;

	/** Length of FST on disc (from DVDBB2). */
	uint32 FSTLength = 0;
};
