// Copyright ryana. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Static utility class for decompressing YAZ0/SZS compressed data.
 *
 * YAZ0 is the compression format used by Nintendo GameCube games (including
 * Super Mario Sunshine). Scene archive files (.szs) are YAZ0-compressed RARC
 * archives that must be decoded before parsing.
 *
 * Format:
 *   Header (16 bytes):
 *     0x00-0x03: Magic "Yaz0"
 *     0x04-0x07: Decompressed size (big-endian u32)
 *     0x08-0x0F: Reserved/padding
 *   Compressed data stream (offset 0x10):
 *     Groups of 8 operations controlled by a flag byte.
 */
class SMSLEVELIMPORTER_API FYAZ0Decoder
{
public:
	FYAZ0Decoder() = delete;

	/** Returns true if data starts with "Yaz0" magic (bytes 0-3). */
	static bool IsYAZ0(const TArray<uint8>& Data);

	/** Returns decompressed size from header (offset 0x04, big-endian u32). */
	static uint32 GetDecompressedSize(const TArray<uint8>& Data);

	/** Decompress YAZ0 data. Returns empty array on failure. */
	static TArray<uint8> Decode(const TArray<uint8>& Data);

private:
	/** YAZ0 header size in bytes. */
	static constexpr int32 HeaderSize = 0x10;

	/** Maximum allowed decompressed size (256 MB) to prevent allocation bombs. */
	static constexpr uint32 MaxDecompressedSize = 256 * 1024 * 1024;
};
