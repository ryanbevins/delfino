// BCKLoader.cpp - J3D skeletal (joint) transform animation parser (BCK / ANK1)

#include "Formats/BCKLoader.h"
#include "Util/BigEndianStream.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSMSImporter, Log, All);

// ----------------------------------------------------------------------------
// Constants
// ----------------------------------------------------------------------------

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
	const float Scale = 180.0f / static_cast<float>(1 << RotFrac);

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
	if (Magic != J3D2Magic)
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
