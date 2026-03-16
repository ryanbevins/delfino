// BCKLoader.h - J3D skeletal (joint) transform animation parser (BCK / ANK1)

#pragma once

#include "CoreMinimal.h"
#include "Formats/BTKLoader.h" // FSimpleKeyframe

class USkeleton;
class UAnimSequence;
struct FBMDJoint;

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

	/**
	 * Create a UAnimSequence from parsed BCK animation data.
	 * @param Skeleton     Target skeleton (must match joint layout).
	 * @param Anim         Parsed BCK animation.
	 * @param Joints       Joint hierarchy from BMD (for transforms).
	 * @param JointNames   Joint names from BMD.
	 * @param AnimName     Name for the animation asset.
	 * @param AssetPath    Base content path for saving.
	 * @return Created UAnimSequence, or nullptr on failure.
	 */
	static UAnimSequence* CreateAnimSequence(USkeleton* Skeleton,
		const FBCKAnimation& Anim, const TArray<FBMDJoint>& Joints,
		const TArray<FString>& JointNames,
		const FString& AnimName, const FString& AssetPath);

private:
	/** Sample keyframes at a given frame, with linear interpolation. */
	static float SampleKeyframes(const TArray<FSimpleKeyframe>& Keys, float Frame);
};
