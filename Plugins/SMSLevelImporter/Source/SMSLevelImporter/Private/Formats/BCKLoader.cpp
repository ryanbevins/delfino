// BCKLoader.cpp - J3D skeletal (joint) transform animation parser (BCK / ANK1)

#include "Formats/BCKLoader.h"
#include "Formats/BMDLoader.h"
#include "Util/BigEndianStream.h"
#include "SMSLevelImporterModule.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

// ----------------------------------------------------------------------------
// Constants
// ----------------------------------------------------------------------------

static constexpr uint32 J3D1Magic = 0x4A334431; // "J3D1"
static constexpr uint32 J3D2Magic = 0x4A334432; // "J3D2"
static constexpr uint32 ANK1Magic = 0x414E4B31; // "ANK1"

// J3DAnmTransformKeyTable: 3 components x J3DAnmKeyTableBase (6 bytes) = 0x12
// Per joint: ScaleX/Y/Z, RotX/Y/Z, TransX/Y/Z = 9 channels x 6 bytes = 0x36
static constexpr int32 BCKJointEntrySize = 0x36;

// ----------------------------------------------------------------------------
// Keyframe reading helpers
// ----------------------------------------------------------------------------

/** Read f32 keyframes (scale / translation). */
static TArray<FSimpleKeyframe> ReadKeyframesF32(
	FBigEndianStream& Stream,
	int64 ValuesBaseOffset,
	uint16 Count,
	uint16 FirstIndex,
	uint16 TangentType)
{
	TArray<FSimpleKeyframe> Result;

	if (Count == 0)
	{
		return Result;
	}

	if (Count == 1)
	{
		Stream.Seek(ValuesBaseOffset + FirstIndex * sizeof(float));
		FSimpleKeyframe KF;
		KF.Time = 0.0f;
		KF.Value = Stream.ReadF32();
		Result.Add(KF);
		return Result;
	}

	const int32 Stride = (TangentType == 0) ? 3 : 4;

	Stream.Seek(ValuesBaseOffset + FirstIndex * sizeof(float));
	Result.Reserve(Count);
	for (uint16 i = 0; i < Count; ++i)
	{
		FSimpleKeyframe KF;
		KF.Time = Stream.ReadF32();
		KF.Value = Stream.ReadF32();
		Stream.Skip((Stride - 2) * sizeof(float));
		Result.Add(KF);
	}

	return Result;
}

/** Read s16 rotation keyframes, converting to degrees. */
static TArray<FSimpleKeyframe> ReadKeyframesS16(
	FBigEndianStream& Stream,
	int64 ValuesBaseOffset,
	uint16 Count,
	uint16 FirstIndex,
	uint16 TangentType,
	uint8 RotFrac)
{
	TArray<FSimpleKeyframe> Result;
	// J3D rotation: degrees = s16_value * (2^RotFrac) * (180/32768)
	const float Scale = static_cast<float>(1 << RotFrac) * (180.0f / 32768.0f);

	if (Count == 0)
	{
		return Result;
	}

	if (Count == 1)
	{
		Stream.Seek(ValuesBaseOffset + FirstIndex * sizeof(int16));
		FSimpleKeyframe KF;
		KF.Time = 0.0f;
		KF.Value = static_cast<float>(Stream.ReadS16()) * Scale;
		Result.Add(KF);
		return Result;
	}

	const int32 Stride = (TangentType == 0) ? 3 : 4;

	Stream.Seek(ValuesBaseOffset + FirstIndex * sizeof(int16));
	Result.Reserve(Count);
	for (uint16 i = 0; i < Count; ++i)
	{
		FSimpleKeyframe KF;
		KF.Time = static_cast<float>(Stream.ReadS16());
		KF.Value = static_cast<float>(Stream.ReadS16()) * Scale;
		Stream.Skip((Stride - 2) * sizeof(int16));
		Result.Add(KF);
	}

	return Result;
}

// ----------------------------------------------------------------------------
// Parse
// ----------------------------------------------------------------------------

bool FBCKLoader::Parse(const TArray<uint8>& Data, FBCKAnimation& OutAnim)
{
	if (Data.Num() < 0x20)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("BCKLoader: Data too small for J3D header (%d bytes)."), Data.Num());
		return false;
	}

	FBigEndianStream Stream(Data);

	// --- J3D file header ---
	const uint32 Magic = Stream.ReadU32();
	if (Magic != J3D1Magic && Magic != J3D2Magic)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("BCKLoader: Invalid J3D magic 0x%08X."), Magic);
		return false;
	}

	Stream.Skip(4); // file type tag ("bck1")
	const uint32 FileSize = Stream.ReadU32();
	if (static_cast<int64>(FileSize) > Data.Num())
	{
		UE_LOG(LogSMSImporter, Warning, TEXT("BCKLoader: Declared file size %u exceeds data (%d bytes)."), FileSize, Data.Num());
	}

	Stream.Skip(4);  // block count
	Stream.Skip(16); // reserved

	// --- ANK1 block header ---
	// Matches J3DAnmTransformKeyData layout
	const int64 BlockStart = Stream.Tell(); // 0x20
	const uint32 BlockMagic = Stream.ReadU32();
	if (BlockMagic != ANK1Magic)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("BCKLoader: Expected ANK1 block, got 0x%08X."), BlockMagic);
		return false;
	}

	Stream.ReadU32(); // block size
	OutAnim.LoopMode = Stream.ReadU8();       // +0x08
	OutAnim.RotationFrac = Stream.ReadU8();   // +0x09
	OutAnim.FrameCount = Stream.ReadU16();    // +0x0A

	const uint16 JointAnimCount = Stream.ReadU16(); // +0x0C
	Stream.Skip(6); // +0x0E: u16 scaleCount, u16 rotCount, u16 transCount

	const uint32 JointAnimOffset    = Stream.ReadU32(); // +0x14
	const uint32 ScaleValuesOffset  = Stream.ReadU32(); // +0x18
	const uint32 RotValuesOffset    = Stream.ReadU32(); // +0x1C
	const uint32 TransValuesOffset  = Stream.ReadU32(); // +0x20

	const int64 AbsJointAnim = BlockStart + JointAnimOffset;
	const int64 AbsScale     = BlockStart + ScaleValuesOffset;
	const int64 AbsRot       = BlockStart + RotValuesOffset;
	const int64 AbsTrans     = BlockStart + TransValuesOffset;

	// --- Parse joint animation entries ---
	OutAnim.JointAnims.SetNum(JointAnimCount);
	for (uint16 i = 0; i < JointAnimCount; ++i)
	{
		Stream.Seek(AbsJointAnim + i * BCKJointEntrySize);

		// 9 channels, each: u16 count, u16 firstIdx, u16 tangentType
		// Order: ScaleX, RotX, TransX, ScaleY, RotY, TransY, ScaleZ, RotZ, TransZ
		struct ChannelDesc { uint16 Count, First, Tang; };
		ChannelDesc Channels[9];
		for (int32 c = 0; c < 9; ++c)
		{
			Channels[c].Count = Stream.ReadU16();
			Channels[c].First = Stream.ReadU16();
			Channels[c].Tang  = Stream.ReadU16();
		}

		FBCKJointAnim& Joint = OutAnim.JointAnims[i];

		// ScaleX/Y/Z (f32 values)
		Joint.ScaleX = ReadKeyframesF32(Stream, AbsScale, Channels[0].Count, Channels[0].First, Channels[0].Tang);
		Joint.ScaleY = ReadKeyframesF32(Stream, AbsScale, Channels[3].Count, Channels[3].First, Channels[3].Tang);
		Joint.ScaleZ = ReadKeyframesF32(Stream, AbsScale, Channels[6].Count, Channels[6].First, Channels[6].Tang);

		// RotationX/Y/Z (s16 values -> degrees)
		Joint.RotationX = ReadKeyframesS16(Stream, AbsRot, Channels[1].Count, Channels[1].First, Channels[1].Tang, OutAnim.RotationFrac);
		Joint.RotationY = ReadKeyframesS16(Stream, AbsRot, Channels[4].Count, Channels[4].First, Channels[4].Tang, OutAnim.RotationFrac);
		Joint.RotationZ = ReadKeyframesS16(Stream, AbsRot, Channels[7].Count, Channels[7].First, Channels[7].Tang, OutAnim.RotationFrac);

		// TranslationX/Y/Z (f32 values)
		Joint.TranslationX = ReadKeyframesF32(Stream, AbsTrans, Channels[2].Count, Channels[2].First, Channels[2].Tang);
		Joint.TranslationY = ReadKeyframesF32(Stream, AbsTrans, Channels[5].Count, Channels[5].First, Channels[5].Tang);
		Joint.TranslationZ = ReadKeyframesF32(Stream, AbsTrans, Channels[8].Count, Channels[8].First, Channels[8].Tang);
	}

	UE_LOG(LogSMSImporter, Log, TEXT("BCKLoader: Parsed %d joint anims, %d frames, rotFrac=%d."),
		OutAnim.JointAnims.Num(), OutAnim.FrameCount, OutAnim.RotationFrac);
	return true;
}

// ----------------------------------------------------------------------------
// SampleKeyframes — Linear interpolation between bracketing keyframes
// ----------------------------------------------------------------------------

float FBCKLoader::SampleKeyframes(const TArray<FSimpleKeyframe>& Keys, float Frame)
{
	if (Keys.Num() == 0) return 0.0f;
	if (Keys.Num() == 1) return Keys[0].Value;

	// Before first key
	if (Frame <= Keys[0].Time) return Keys[0].Value;

	// After last key
	if (Frame >= Keys.Last().Time) return Keys.Last().Value;

	// Find bracketing keyframes
	for (int32 i = 0; i + 1 < Keys.Num(); i++)
	{
		if (Frame >= Keys[i].Time && Frame <= Keys[i + 1].Time)
		{
			const float Span = Keys[i + 1].Time - Keys[i].Time;
			if (Span <= 0.0f) return Keys[i].Value;
			const float Alpha = (Frame - Keys[i].Time) / Span;
			return FMath::Lerp(Keys[i].Value, Keys[i + 1].Value, Alpha);
		}
	}

	return Keys.Last().Value;
}

// ----------------------------------------------------------------------------
// CreateAnimSequence — Build UE5 UAnimSequence from parsed BCK animation
// ----------------------------------------------------------------------------

UAnimSequence* FBCKLoader::CreateAnimSequence(USkeleton* Skeleton,
	const FBCKAnimation& Anim, const TArray<FBMDJoint>& Joints,
	const TArray<FString>& JointNames,
	const FString& AnimName, const FString& AssetPath)
{
	if (!Skeleton)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("BCKLoader: Cannot create AnimSequence without skeleton"));
		return nullptr;
	}

	FString AnimPackagePath = FString::Printf(TEXT("%s/Animations/ANIM_%s"), *AssetPath, *AnimName);
	UPackage* AnimPackage = CreatePackage(*AnimPackagePath);
	AnimPackage->FullyLoad();

	UAnimSequence* AnimSeq = NewObject<UAnimSequence>(AnimPackage,
		*FString::Printf(TEXT("ANIM_%s"), *AnimName), RF_Public | RF_Standalone);

	AnimSeq->SetSkeleton(Skeleton);

	const float FrameRate = 30.0f;
	const int32 NumFrames = FMath::Max(1, Anim.FrameCount);
	const float SequenceLength = static_cast<float>(NumFrames) / FrameRate;

	IAnimationDataController& Controller = AnimSeq->GetController();
	Controller.InitializeModel();
	Controller.OpenBracket(FText::FromString(TEXT("BCK Import")), false);
	Controller.SetFrameRate(FFrameRate(static_cast<uint32>(FrameRate), 1), false);
	Controller.SetNumberOfFrames(FFrameNumber(NumFrames), false);

	// For each joint that has animation data
	const int32 NumJoints = FMath::Min(Anim.JointAnims.Num(),
		FMath::Min(Joints.Num(), JointNames.Num()));

	for (int32 i = 0; i < NumJoints; i++)
	{
		const FBCKJointAnim& JointAnim = Anim.JointAnims[i];
		FName BoneName(*JointNames[i]);

		// Check if bone exists in skeleton
		const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
		if (RefSkel.FindBoneIndex(BoneName) == INDEX_NONE)
		{
			continue;
		}

		// Sample keyframes per frame
		TArray<FVector3f> PosKeys;
		TArray<FQuat4f> RotKeys;
		TArray<FVector3f> ScaleKeys;

		PosKeys.SetNum(NumFrames);
		RotKeys.SetNum(NumFrames);
		ScaleKeys.SetNum(NumFrames);

		for (int32 Frame = 0; Frame < NumFrames; Frame++)
		{
			const float F = static_cast<float>(Frame);

			// Sample SRT in GC space
			float TX = SampleKeyframes(JointAnim.TranslationX, F);
			float TY = SampleKeyframes(JointAnim.TranslationY, F);
			float TZ = SampleKeyframes(JointAnim.TranslationZ, F);

			float RX = SampleKeyframes(JointAnim.RotationX, F);
			float RY = SampleKeyframes(JointAnim.RotationY, F);
			float RZ = SampleKeyframes(JointAnim.RotationZ, F);

			float SX = SampleKeyframes(JointAnim.ScaleX, F);
			float SY = SampleKeyframes(JointAnim.ScaleY, F);
			float SZ = SampleKeyframes(JointAnim.ScaleZ, F);

			// Build GC-space local transform (extrinsic ZYX = Rz * Ry * Rx)
			const float RXr = FMath::DegreesToRadians(RX);
			const float RYr = FMath::DegreesToRadians(RY);
			const float RZr = FMath::DegreesToRadians(RZ);

			FQuat QX(FVector::XAxisVector, RXr);
			FQuat QY(FVector::YAxisVector, RYr);
			FQuat QZ(FVector::ZAxisVector, RZr);
			FQuat GCQuat = QZ * QY * QX;

			FTransform GCTransform(GCQuat, FVector(TX, TY, TZ), FVector(SX, SY, SZ));
			FMatrix44f GCMat = FMatrix44f(GCTransform.ToMatrixWithScale());

			// Convert GC→UE via matrix sandwich (swap rows 1↔2, cols 1↔2)
			for (int32 c = 0; c < 4; c++)
			{
				float Tmp = GCMat.M[1][c];
				GCMat.M[1][c] = GCMat.M[2][c];
				GCMat.M[2][c] = Tmp;
			}
			for (int32 r = 0; r < 4; r++)
			{
				float Tmp = GCMat.M[r][1];
				GCMat.M[r][1] = GCMat.M[r][2];
				GCMat.M[r][2] = Tmp;
			}

			FTransform UETransform;
			UETransform.SetFromMatrix(FMatrix(GCMat));

			PosKeys[Frame] = FVector3f(UETransform.GetTranslation());
			RotKeys[Frame] = FQuat4f(UETransform.GetRotation());
			ScaleKeys[Frame] = FVector3f(UETransform.GetScale3D());
		}

		Controller.AddBoneCurve(BoneName, false);
		Controller.SetBoneTrackKeys(BoneName, PosKeys, RotKeys, ScaleKeys, false);
	}

	Controller.CloseBracket(false);
	Controller.NotifyPopulated();

	// Register and save
	FAssetRegistryModule::AssetCreated(AnimSeq);
	AnimPackage->MarkPackageDirty();

	FString Filename = FPackageName::LongPackageNameToFilename(AnimPackagePath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	UPackage::SavePackage(AnimPackage, AnimSeq, *Filename, SaveArgs);

	UE_LOG(LogSMSImporter, Log, TEXT("BCKLoader: Created AnimSequence '%s' (%d frames, %d bone tracks)"),
		*AnimName, NumFrames, NumJoints);

	return AnimSeq;
}
