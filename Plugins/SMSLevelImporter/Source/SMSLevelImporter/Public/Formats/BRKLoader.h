// BRKLoader.h - J3D TEV register color animation parser (BRK / TRK1)

#pragma once

#include "CoreMinimal.h"
#include "Formats/BTKLoader.h" // FSimpleKeyframe

class UCurveLinearColor;

/**
 * One animated TEV color register entry.
 * Drives RGBA values on a named material's TEV constant-color register.
 */
struct FBRKAnimEntry
{
	FString MaterialName;
	uint8 ColorId; // which TEV register (0-3 for CReg, 0-3 for KReg)
	TArray<FSimpleKeyframe> R, G, B, A;
};

/** Top-level BRK animation data. */
struct FBRKAnimation
{
	int32 FrameCount;
	uint8 LoopMode;
	TArray<FBRKAnimEntry> CRegEntries; // color registers (signed s10)
	TArray<FBRKAnimEntry> KRegEntries; // konst registers (unsigned u8)
};

/**
 * Parses .brk (J3D TRK1) TEV register color animation files.
 *
 * These animate TEV color and konst registers per material, used for
 * color pulsing, fading, tinting effects, etc.
 */
class SMSLEVELIMPORTER_API FBRKLoader
{
public:
	/** Parse a BRK file from raw bytes. Returns false on any structural error. */
	static bool Parse(const TArray<uint8>& Data, FBRKAnimation& OutAnim);

	/**
	 * Create UCurveLinearColor assets for each animated color register.
	 * Keys in the returned map are formatted as "MaterialName_CReg0" etc.
	 */
	static TMap<FString, UCurveLinearColor*> CreateColorCurves(UObject* Outer,
		const FBRKAnimation& Anim, const FString& AssetPath);
};
