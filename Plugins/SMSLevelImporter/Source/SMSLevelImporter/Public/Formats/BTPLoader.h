// BTPLoader.h - J3D texture pattern animation parser (BTP / TPT1)

#pragma once

#include "CoreMinimal.h"

/**
 * One animated texture-swap entry.
 * Maps a material + tex-map slot to a list of (frame, textureIndex) pairs.
 */
struct FBTPAnimEntry
{
	FString MaterialName;
	int32 TexMapIndex;
	TArray<TPair<int32, int32>> Frames; // (frame, textureIndex)
};

/** Top-level BTP animation data. */
struct FBTPAnimation
{
	int32 FrameCount;
	uint8 LoopMode;
	TArray<FBTPAnimEntry> Entries;
};

/**
 * Parses .btp (J3D TPT1) texture pattern animation files.
 *
 * These swap texture indices on materials each frame, used for
 * eye-blink animations, sign changes, flipbook effects, etc.
 */
class SMSLEVELIMPORTER_API FBTPLoader
{
public:
	/** Parse a BTP file from raw bytes. Returns false on any structural error. */
	static bool Parse(const TArray<uint8>& Data, FBTPAnimation& OutAnim);
};
