// BCKLoader.h - J3D skeletal (joint) transform animation parser (BCK / ANK1)

#pragma once

#include "CoreMinimal.h"
#include "Formats/BTKLoader.h" // FSimpleKeyframe

/**
 * Per-joint animation channels.
 * Each joint has 9 animated components: Scale XYZ, Rotation XYZ, Translation XYZ.
 */
struct FBCKJointAnim
{
	TArray<FSimpleKeyframe> ScaleX, ScaleY, ScaleZ;
	TArray<FSimpleKeyframe> RotationX, RotationY, RotationZ;
	TArray<FSimpleKeyframe> TranslationX, TranslationY, TranslationZ;
};

/** Top-level BCK animation data. */
struct FBCKAnimation
{
	int32 FrameCount;
	uint8 LoopMode;
	uint8 RotationFrac;
	TArray<FBCKJointAnim> JointAnims;
};

/**
 * Parses .bck (J3D ANK1) skeletal animation files.
 *
 * These animate joint transforms (SRT) for character and object animations.
 * Rotation values are stored as fixed-point s16, converted using the
 * rotationFrac field to determine fractional bit count.
 */
class SMSLEVELIMPORTER_API FBCKLoader
{
public:
	/** Parse a BCK file from raw bytes. Returns false on any structural error. */
	static bool Parse(const TArray<uint8>& Data, FBCKAnimation& OutAnim);
};
