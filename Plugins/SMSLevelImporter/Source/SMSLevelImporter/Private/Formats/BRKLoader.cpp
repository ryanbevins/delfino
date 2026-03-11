// BRKLoader.cpp - J3D TEV register color animation parser (BRK / TRK1)

#include "Formats/BRKLoader.h"
#include "Util/BigEndianStream.h"
#include "Curves/CurveLinearColor.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSMSImporter, Log, All);

// ----------------------------------------------------------------------------
// Constants
// ----------------------------------------------------------------------------

static constexpr uint32 J3D2Magic = 0x4A334432; // "J3D2"
static constexpr uint32 TRK1Magic = 0x54524B31; // "TRK1"

// J3DAnmCRegKeyTable / J3DAnmKRegKeyTable: 4 channels x 6 bytes + colorId(1) + pad(3) = 0x1C
static constexpr int32 BRKRegEntrySize = 0x1C;

// ----------------------------------------------------------------------------
// Name table parsing (same pattern as BTK/BTP)
// ----------------------------------------------------------------------------

static TArray<FString> ParseNameTable(FBigEndianStream& Stream, int64 TableOffset)
{
	TArray<FString> Names;
	Stream.Seek(TableOffset);

	const uint16 EntryCount = Stream.ReadU16();
	Stream.Skip(2); // padding

	TArray<uint16> StringOffsets;
	StringOffsets.SetNum(EntryCount);
	for (uint16 i = 0; i < EntryCount; ++i)
	{
		Stream.ReadU16(); // hash
		StringOffsets[i] = Stream.ReadU16();
	}

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
// Keyframe reading for s16 color values
// ----------------------------------------------------------------------------

static TArray<FSimpleKeyframe> ReadColorKeyframes(
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
		Stream.Seek(ValuesBaseOffset + FirstIndex * sizeof(int16));
		FSimpleKeyframe KF;
		KF.Time = 0.0f;
		KF.Value = static_cast<float>(Stream.ReadS16());
		Result.Add(KF);
		return Result;
	}

	// tangentType 0 = stride 3 (time, value, tangent)
	// tangentType 1 = stride 4 (time, value, inTangent, outTangent)
	const int32 Stride = (TangentType == 0) ? 3 : 4;

	Stream.Seek(ValuesBaseOffset + FirstIndex * sizeof(int16));
	Result.Reserve(Count);
	for (uint16 i = 0; i < Count; ++i)
	{
		FSimpleKeyframe KF;
		KF.Time = static_cast<float>(Stream.ReadS16());
		KF.Value = static_cast<float>(Stream.ReadS16());
		Stream.Skip((Stride - 2) * sizeof(int16));
		Result.Add(KF);
	}

	return Result;
}

// ----------------------------------------------------------------------------
// Parse a set of register entries (CReg or KReg)
// ----------------------------------------------------------------------------

static bool ParseRegEntries(
	FBigEndianStream& Stream,
	int64 TableAbsOffset,
	int64 NameTableAbsOffset,
	int64 AbsR, int64 AbsG, int64 AbsB, int64 AbsA,
	uint16 EntryCount,
	TArray<FBRKAnimEntry>& OutEntries)
{
	TArray<FString> Names;
	if (NameTableAbsOffset > 0)
	{
		Names = ParseNameTable(Stream, NameTableAbsOffset);
	}

	OutEntries.SetNum(EntryCount);
	for (uint16 i = 0; i < EntryCount; ++i)
	{
		Stream.Seek(TableAbsOffset + i * BRKRegEntrySize);

		// R channel
		const uint16 RCount = Stream.ReadU16();
		const uint16 RFirst = Stream.ReadU16();
		const uint16 RTang  = Stream.ReadU16();

		// G channel
		const uint16 GCount = Stream.ReadU16();
		const uint16 GFirst = Stream.ReadU16();
		const uint16 GTang  = Stream.ReadU16();

		// B channel
		const uint16 BCount = Stream.ReadU16();
		const uint16 BFirst = Stream.ReadU16();
		const uint16 BTang  = Stream.ReadU16();

		// A channel
		const uint16 ACount = Stream.ReadU16();
		const uint16 AFirst = Stream.ReadU16();
		const uint16 ATang  = Stream.ReadU16();

		// Color ID
		const uint8 ColorId = Stream.ReadU8();
		Stream.Skip(3); // padding

		FBRKAnimEntry& Entry = OutEntries[i];
		Entry.MaterialName = (i < Names.Num()) ? Names[i] : FString::Printf(TEXT("Material_%d"), i);
		Entry.ColorId = ColorId;

		Entry.R = ReadColorKeyframes(Stream, AbsR, RCount, RFirst, RTang);
		Entry.G = ReadColorKeyframes(Stream, AbsG, GCount, GFirst, GTang);
		Entry.B = ReadColorKeyframes(Stream, AbsB, BCount, BFirst, BTang);
		Entry.A = ReadColorKeyframes(Stream, AbsA, ACount, AFirst, ATang);
	}

	return true;
}

// ----------------------------------------------------------------------------
// Parse
// ----------------------------------------------------------------------------

bool FBRKLoader::Parse(const TArray<uint8>& Data, FBRKAnimation& OutAnim)
{
	if (Data.Num() < 0x20)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("BRKLoader: Data too small for J3D header (%d bytes)."), Data.Num());
		return false;
	}

	FBigEndianStream Stream(Data);

	// --- J3D file header ---
	const uint32 Magic = Stream.ReadU32();
	if (Magic != J3D2Magic)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("BRKLoader: Invalid J3D magic 0x%08X."), Magic);
		return false;
	}

	Stream.Skip(4); // file type tag
	const uint32 FileSize = Stream.ReadU32();
	if (static_cast<int64>(FileSize) > Data.Num())
	{
		UE_LOG(LogSMSImporter, Warning, TEXT("BRKLoader: Declared file size %u exceeds data (%d bytes)."), FileSize, Data.Num());
	}

	Stream.Skip(4);  // block count
	Stream.Skip(16); // reserved

	// --- TRK1 block header ---
	// Matches J3DAnmTevRegKeyData layout
	const int64 BlockStart = Stream.Tell(); // 0x20
	const uint32 BlockMagic = Stream.ReadU32();
	if (BlockMagic != TRK1Magic)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("BRKLoader: Expected TRK1 block, got 0x%08X."), BlockMagic);
		return false;
	}

	Stream.ReadU32(); // block size
	OutAnim.LoopMode = Stream.ReadU8();    // +0x08
	Stream.Skip(1);                         // +0x09 padding
	OutAnim.FrameCount = Stream.ReadS16(); // +0x0A

	const uint16 CRegCount = Stream.ReadU16(); // +0x0C mCRegUpdateMaterialNum
	const uint16 KRegCount = Stream.ReadU16(); // +0x0E mKRegUpdateMaterialNum

	// Data counts (unused for parsing, we read from tables)
	Stream.Skip(2 * 8); // +0x10..+0x1F: 8 u16 count fields

	const uint32 CRegTableOffset     = Stream.ReadU32(); // +0x20
	const uint32 KRegTableOffset     = Stream.ReadU32(); // +0x24
	const uint32 CRegMatIDOffset     = Stream.ReadU32(); // +0x28
	const uint32 KRegMatIDOffset     = Stream.ReadU32(); // +0x2C
	const uint32 CRegNameTabOffset   = Stream.ReadU32(); // +0x30
	const uint32 KRegNameTabOffset   = Stream.ReadU32(); // +0x34
	const uint32 CRValuesOffset      = Stream.ReadU32(); // +0x38
	const uint32 CGValuesOffset      = Stream.ReadU32(); // +0x3C
	const uint32 CBValuesOffset      = Stream.ReadU32(); // +0x40
	const uint32 CAValuesOffset      = Stream.ReadU32(); // +0x44
	const uint32 KRValuesOffset      = Stream.ReadU32(); // +0x48
	const uint32 KGValuesOffset      = Stream.ReadU32(); // +0x4C
	const uint32 KBValuesOffset      = Stream.ReadU32(); // +0x50
	const uint32 KAValuesOffset      = Stream.ReadU32(); // +0x54

	// Parse CReg entries
	if (CRegCount > 0)
	{
		ParseRegEntries(Stream,
			BlockStart + CRegTableOffset,
			BlockStart + CRegNameTabOffset,
			BlockStart + CRValuesOffset,
			BlockStart + CGValuesOffset,
			BlockStart + CBValuesOffset,
			BlockStart + CAValuesOffset,
			CRegCount, OutAnim.CRegEntries);
	}

	// Parse KReg entries
	if (KRegCount > 0)
	{
		ParseRegEntries(Stream,
			BlockStart + KRegTableOffset,
			BlockStart + KRegNameTabOffset,
			BlockStart + KRValuesOffset,
			BlockStart + KGValuesOffset,
			BlockStart + KBValuesOffset,
			BlockStart + KAValuesOffset,
			KRegCount, OutAnim.KRegEntries);
	}

	UE_LOG(LogSMSImporter, Log, TEXT("BRKLoader: Parsed %d CReg + %d KReg entries, %d frames."),
		OutAnim.CRegEntries.Num(), OutAnim.KRegEntries.Num(), OutAnim.FrameCount);
	return true;
}

// ----------------------------------------------------------------------------
// CreateColorCurves
// ----------------------------------------------------------------------------

static void PopulateColorCurve(UCurveLinearColor* Curve, const FBRKAnimEntry& Entry, float Scale)
{
	if (!Curve)
	{
		return;
	}

	auto AddKeys = [&](int32 ChannelIndex, const TArray<FSimpleKeyframe>& Keys)
	{
		for (const FSimpleKeyframe& KF : Keys)
		{
			Curve->FloatCurves[ChannelIndex].AddKey(KF.Time, KF.Value * Scale);
		}
	};

	AddKeys(0, Entry.R);
	AddKeys(1, Entry.G);
	AddKeys(2, Entry.B);
	AddKeys(3, Entry.A);
}

TMap<FString, UCurveLinearColor*> FBRKLoader::CreateColorCurves(UObject* Outer,
	const FBRKAnimation& Anim, const FString& AssetPath)
{
	TMap<FString, UCurveLinearColor*> Result;

	// CReg entries: signed s10 values, normalize to [0,1] by dividing by 255
	for (const FBRKAnimEntry& Entry : Anim.CRegEntries)
	{
		const FString CurveName = FString::Printf(TEXT("%s_CReg%d"), *Entry.MaterialName, Entry.ColorId);
		UCurveLinearColor* Curve = NewObject<UCurveLinearColor>(Outer, *CurveName);
		if (Curve)
		{
			PopulateColorCurve(Curve, Entry, 1.0f / 255.0f);
			Result.Add(CurveName, Curve);
		}
	}

	// KReg entries: unsigned u8 values, normalize to [0,1] by dividing by 255
	for (const FBRKAnimEntry& Entry : Anim.KRegEntries)
	{
		const FString CurveName = FString::Printf(TEXT("%s_KReg%d"), *Entry.MaterialName, Entry.ColorId);
		UCurveLinearColor* Curve = NewObject<UCurveLinearColor>(Outer, *CurveName);
		if (Curve)
		{
			PopulateColorCurve(Curve, Entry, 1.0f / 255.0f);
			Result.Add(CurveName, Curve);
		}
	}

	return Result;
}
