// BTKLoader.h - J3D texture SRT (UV) animation parser (BTK / TTK1)

#pragma once

#include "CoreMinimal.h"

class UCurveFloat;

/**
 * A single keyframe: frame number and value.
 * Used by BTK (UV), BRK (color), and BCK (skeletal) loaders.
 */
struct FSimpleKeyframe
{
	float Time;
	float Value;
};

/**
 * One animated texture matrix entry.
 * Each entry drives the SRT transform of a single texture coordinate generator
 * on a named material.
 */
struct FBTKAnimEntry
{
	FString MaterialName;
	int32 TexGenIndex;
	TArray<FSimpleKeyframe> ScaleS, ScaleT;
	TArray<FSimpleKeyframe> Rotation;
	TArray<FSimpleKeyframe> TranslateS, TranslateT;
};

/** Top-level BTK animation data. */
struct FBTKAnimation
{
	int32 FrameCount;
	uint8 LoopMode;
	TArray<FBTKAnimEntry> Entries;
};

/**
 * Parses .btk (J3D TTK1) UV animation files.
 *
 * These animate texture matrix Scale / Rotation / Translation per material,
 * commonly used for water scrolling, lava flow, cloud movement, etc.
 */
class SMSLEVELIMPORTER_API FBTKLoader
{
public:
	/** Parse a BTK file from raw bytes. Returns false on any structural error. */
	static bool Parse(const TArray<uint8>& Data, FBTKAnimation& OutAnim);

	/**
	 * Create UCurveFloat assets for each animated UV parameter.
	 * Keys in the returned map are formatted as "MaterialName_Channel".
	 */
	static TMap<FString, UCurveFloat*> CreateCurveAssets(UObject* Outer,
		const FBTKAnimation& Anim, const FString& AssetPath);

	/**
	 * Detect if an entry is a simple constant-rate UV scroll (panner).
	 * Returns true and fills OutSpeedS / OutSpeedT if so.
	 */
	static bool IsSimplePanner(const FBTKAnimEntry& Entry,
		float& OutSpeedS, float& OutSpeedT);
};
