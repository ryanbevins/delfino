// BTKLoader.cpp - J3D texture SRT (UV) animation parser (BTK / TTK1)

#include "Formats/BTKLoader.h"
#include "Util/BigEndianStream.h"
#include "Curves/CurveFloat.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSMSImporter, Log, All);

// ----------------------------------------------------------------------------
// Constants
// ----------------------------------------------------------------------------

static constexpr uint32 J3D2Magic = 0x4A334432; // "J3D2"
static constexpr uint32 TTK1Magic = 0x54544B31; // "TTK1"

// BTK anim entry size: 5 channels x 6 bytes + texMatIndex(2) + centerType(2) + pad(2) = 0x24
static constexpr int32 BTKEntrySize = 0x24;

// ----------------------------------------------------------------------------
// Name table parsing (shared J3D pattern)
// ----------------------------------------------------------------------------

static TArray<FString> ParseNameTable(FBigEndianStream& Stream, int64 TableOffset)
{
	TArray<FString> Names;
	Stream.Seek(TableOffset);

	const uint16 EntryCount = Stream.ReadU16();
	Stream.Skip(2); // padding

	// Read hash + offset pairs
	TArray<uint16> StringOffsets;
	StringOffsets.SetNum(EntryCount);
	for (uint16 i = 0; i < EntryCount; ++i)
	{
		Stream.ReadU16(); // hash (unused for our purposes)
		StringOffsets[i] = Stream.ReadU16();
	}

	// Read null-terminated strings
	const int64 StringBase = TableOffset + 4 + EntryCount * 4;
	Names.SetNum(EntryCount);
	for (uint16 i = 0; i < EntryCount; ++i)
	{
		Stream.Seek(StringBase + StringOffsets[i]);
		Names[i] = Stream.ReadNullTerminatedString();
	}

	return Names;
}

// ----------------------------------------------------------------------------
// Keyframe reading helpers
// ----------------------------------------------------------------------------

/** Read keyframes from a channel descriptor (count, firstIdx, tangentType). */
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
		// Constant value: single entry at the first index
		Stream.Seek(ValuesBaseOffset + FirstIndex * sizeof(float));
		FSimpleKeyframe KF;
		KF.Time = 0.0f;
		KF.Value = Stream.ReadF32();
		Result.Add(KF);
		return Result;
	}

	// Multiple keyframes
	// tangentType 0 = pairs of 3 (time, value, tangent) -> stride 3
	// tangentType 1 = pairs of 4 (time, value, inTangent, outTangent) -> stride 4
	const int32 Stride = (TangentType == 0) ? 3 : 4;

	Stream.Seek(ValuesBaseOffset + FirstIndex * sizeof(float));
	Result.Reserve(Count);
	for (uint16 i = 0; i < Count; ++i)
	{
		FSimpleKeyframe KF;
		KF.Time = Stream.ReadF32();
		KF.Value = Stream.ReadF32();
		// Skip tangent(s)
		Stream.Skip((Stride - 2) * sizeof(float));
		Result.Add(KF);
	}

	return Result;
}

/** Read rotation keyframes (s16 values, converted to degrees). */
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

bool FBTKLoader::Parse(const TArray<uint8>& Data, FBTKAnimation& OutAnim)
{
	if (Data.Num() < 0x20)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("BTKLoader: Data too small for J3D header (%d bytes)."), Data.Num());
		return false;
	}

	FBigEndianStream Stream(Data);

	// --- J3D file header ---
	const uint32 Magic = Stream.ReadU32();
	if (Magic != J3D2Magic)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("BTKLoader: Invalid J3D magic 0x%08X."), Magic);
		return false;
	}

	Stream.Skip(4); // file type tag ("btk1")
	const uint32 FileSize = Stream.ReadU32();
	if (static_cast<int64>(FileSize) > Data.Num())
	{
		UE_LOG(LogSMSImporter, Warning, TEXT("BTKLoader: Declared file size %u exceeds data (%d bytes)."), FileSize, Data.Num());
	}

	Stream.Skip(4); // block count
	Stream.Skip(16); // reserved

	// --- TTK1 block header ---
	const int64 BlockStart = Stream.Tell(); // 0x20
	const uint32 BlockMagic = Stream.ReadU32();
	if (BlockMagic != TTK1Magic)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("BTKLoader: Expected TTK1 block, got 0x%08X."), BlockMagic);
		return false;
	}

	const uint32 BlockSize = Stream.ReadU32();
	OutAnim.LoopMode = Stream.ReadU8();  // +0x08
	const uint8 RotFrac = Stream.ReadU8();  // +0x09
	OutAnim.FrameCount = Stream.ReadU16(); // +0x0A

	const uint16 AnimEntryCount = Stream.ReadU16(); // +0x0C
	Stream.Skip(2 * 3); // scaleCount, rotCount, transCount (+0x0E..+0x13)

	const uint32 AnimEntryOffset       = Stream.ReadU32(); // +0x14
	Stream.Skip(4); // indexRemapOffset                      +0x18
	const uint32 NameTableOffset       = Stream.ReadU32(); // +0x1C
	const uint32 TexMatIndexTableOffset = Stream.ReadU32(); // +0x20
	const uint32 CenterPointOffset     = Stream.ReadU32(); // +0x24
	const uint32 ScaleValuesOffset     = Stream.ReadU32(); // +0x28
	const uint32 RotValuesOffset       = Stream.ReadU32(); // +0x2C
	const uint32 TransValuesOffset     = Stream.ReadU32(); // +0x30

	// All offsets are relative to block start
	const int64 AbsAnimEntry   = BlockStart + AnimEntryOffset;
	const int64 AbsNameTable   = BlockStart + NameTableOffset;
	const int64 AbsTexMatIndex = BlockStart + TexMatIndexTableOffset;
	const int64 AbsScale       = BlockStart + ScaleValuesOffset;
	const int64 AbsRot         = BlockStart + RotValuesOffset;
	const int64 AbsTrans       = BlockStart + TransValuesOffset;

	// --- Parse name table ---
	TArray<FString> Names = ParseNameTable(Stream, AbsNameTable);

	// --- Parse tex matrix index table ---
	TArray<uint8> TexMatIndices;
	TexMatIndices.SetNum(AnimEntryCount);
	Stream.Seek(AbsTexMatIndex);
	for (uint16 i = 0; i < AnimEntryCount; ++i)
	{
		TexMatIndices[i] = Stream.ReadU8();
	}

	// --- Parse animation entries ---
	OutAnim.Entries.SetNum(AnimEntryCount);
	for (uint16 i = 0; i < AnimEntryCount; ++i)
	{
		Stream.Seek(AbsAnimEntry + i * BTKEntrySize);

		// ScaleS
		const uint16 ScaleSCount = Stream.ReadU16();
		const uint16 ScaleSFirst = Stream.ReadU16();
		const uint16 ScaleSTang  = Stream.ReadU16();

		// Rotation
		const uint16 RotCount = Stream.ReadU16();
		const uint16 RotFirst = Stream.ReadU16();
		const uint16 RotTang  = Stream.ReadU16();

		// ScaleT
		const uint16 ScaleTCount = Stream.ReadU16();
		const uint16 ScaleTFirst = Stream.ReadU16();
		const uint16 ScaleTTang  = Stream.ReadU16();

		// TranslateS
		const uint16 TransSCount = Stream.ReadU16();
		const uint16 TransSFirst = Stream.ReadU16();
		const uint16 TransSTang  = Stream.ReadU16();

		// TranslateT
		const uint16 TransTCount = Stream.ReadU16();
		const uint16 TransTFirst = Stream.ReadU16();
		const uint16 TransTTang  = Stream.ReadU16();

		Stream.ReadU16(); // texMatIndex (from entry, but we use the table instead)
		Stream.ReadU16(); // centerType
		Stream.ReadU16(); // pad

		FBTKAnimEntry& Entry = OutAnim.Entries[i];
		Entry.MaterialName = (i < Names.Num()) ? Names[i] : FString::Printf(TEXT("Material_%d"), i);
		Entry.TexGenIndex = (i < TexMatIndices.Num()) ? TexMatIndices[i] : 0;

		Entry.ScaleS     = ReadKeyframesF32(Stream, AbsScale, ScaleSCount, ScaleSFirst, ScaleSTang);
		Entry.ScaleT     = ReadKeyframesF32(Stream, AbsScale, ScaleTCount, ScaleTFirst, ScaleTTang);
		Entry.Rotation   = ReadKeyframesS16(Stream, AbsRot, RotCount, RotFirst, RotTang, RotFrac);
		Entry.TranslateS = ReadKeyframesF32(Stream, AbsTrans, TransSCount, TransSFirst, TransSTang);
		Entry.TranslateT = ReadKeyframesF32(Stream, AbsTrans, TransTCount, TransTFirst, TransTTang);
	}

	UE_LOG(LogSMSImporter, Log, TEXT("BTKLoader: Parsed %d anim entries, %d frames."),
		OutAnim.Entries.Num(), OutAnim.FrameCount);
	return true;
}

// ----------------------------------------------------------------------------
// IsSimplePanner
// ----------------------------------------------------------------------------

bool FBTKLoader::IsSimplePanner(const FBTKAnimEntry& Entry,
	float& OutSpeedS, float& OutSpeedT)
{
	OutSpeedS = 0.0f;
	OutSpeedT = 0.0f;

	auto IsLinearRamp = [](const TArray<FSimpleKeyframe>& Keys, float& OutSpeed)
	{
		if (Keys.Num() == 1)
		{
			// Constant — speed is zero.
			OutSpeed = 0.0f;
			return true;
		}
		if (Keys.Num() == 2)
		{
			const float DeltaTime = Keys[1].Time - Keys[0].Time;
			if (DeltaTime > 0.0f)
			{
				OutSpeed = (Keys[1].Value - Keys[0].Value) / DeltaTime;
				return true;
			}
		}
		return false;
	};

	// Scale must be constant 1.0 and rotation must be constant 0.0
	if (Entry.ScaleS.Num() == 1 && FMath::IsNearlyEqual(Entry.ScaleS[0].Value, 1.0f) &&
		Entry.ScaleT.Num() == 1 && FMath::IsNearlyEqual(Entry.ScaleT[0].Value, 1.0f) &&
		Entry.Rotation.Num() == 1 && FMath::IsNearlyEqual(Entry.Rotation[0].Value, 0.0f))
	{
		float SpeedS, SpeedT;
		if (IsLinearRamp(Entry.TranslateS, SpeedS) && IsLinearRamp(Entry.TranslateT, SpeedT))
		{
			OutSpeedS = SpeedS;
			OutSpeedT = SpeedT;
			return true;
		}
	}

	return false;
}

// ----------------------------------------------------------------------------
// CreateCurveAssets
// ----------------------------------------------------------------------------

static void AddKeysToCurve(UCurveFloat* Curve, const TArray<FSimpleKeyframe>& Keys)
{
	if (!Curve)
	{
		return;
	}

	for (const FSimpleKeyframe& KF : Keys)
	{
		Curve->FloatCurve.AddKey(KF.Time, KF.Value);
	}
}

TMap<FString, UCurveFloat*> FBTKLoader::CreateCurveAssets(UObject* Outer,
	const FBTKAnimation& Anim, const FString& AssetPath)
{
	TMap<FString, UCurveFloat*> Result;

	for (const FBTKAnimEntry& Entry : Anim.Entries)
	{
		auto MakeCurve = [&](const FString& ChannelName, const TArray<FSimpleKeyframe>& Keys)
		{
			if (Keys.Num() <= 1)
			{
				return; // constant — no curve needed
			}

			const FString CurveName = FString::Printf(TEXT("%s_%s"), *Entry.MaterialName, *ChannelName);
			UCurveFloat* Curve = NewObject<UCurveFloat>(Outer, *CurveName);
			if (Curve)
			{
				AddKeysToCurve(Curve, Keys);
				Result.Add(CurveName, Curve);
			}
		};

		MakeCurve(TEXT("ScaleS"), Entry.ScaleS);
		MakeCurve(TEXT("ScaleT"), Entry.ScaleT);
		MakeCurve(TEXT("Rotation"), Entry.Rotation);
		MakeCurve(TEXT("TranslateS"), Entry.TranslateS);
		MakeCurve(TEXT("TranslateT"), Entry.TranslateT);
	}

	return Result;
}
