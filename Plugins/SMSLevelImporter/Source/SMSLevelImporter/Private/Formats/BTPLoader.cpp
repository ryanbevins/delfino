// BTPLoader.cpp - J3D texture pattern animation parser (BTP / TPT1)

#include "Formats/BTPLoader.h"
#include "Util/BigEndianStream.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSMSImporter, Log, All);

// ----------------------------------------------------------------------------
// Constants
// ----------------------------------------------------------------------------

static constexpr uint32 J3D2Magic = 0x4A334432; // "J3D2"
static constexpr uint32 TPT1Magic = 0x54505431; // "TPT1"

// J3DAnmTexPatternFullTable: u16 maxFrame, u16 offset, u8 texNo, u8 pad, u16 pad2 = 8 bytes
static constexpr int32 BTPEntrySize = 0x08;

// ----------------------------------------------------------------------------
// Name table parsing (same as BTKLoader)
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
// Parse
// ----------------------------------------------------------------------------

bool FBTPLoader::Parse(const TArray<uint8>& Data, FBTPAnimation& OutAnim)
{
	if (Data.Num() < 0x20)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("BTPLoader: Data too small for J3D header (%d bytes)."), Data.Num());
		return false;
	}

	FBigEndianStream Stream(Data);

	// --- J3D file header ---
	const uint32 Magic = Stream.ReadU32();
	if (Magic != J3D2Magic)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("BTPLoader: Invalid J3D magic 0x%08X."), Magic);
		return false;
	}

	Stream.Skip(4); // file type tag
	const uint32 FileSize = Stream.ReadU32();
	if (static_cast<int64>(FileSize) > Data.Num())
	{
		UE_LOG(LogSMSImporter, Warning, TEXT("BTPLoader: Declared file size %u exceeds data (%d bytes)."), FileSize, Data.Num());
	}

	Stream.Skip(4);  // block count
	Stream.Skip(16); // reserved

	// --- TPT1 block header ---
	const int64 BlockStart = Stream.Tell(); // 0x20
	const uint32 BlockMagic = Stream.ReadU32();
	if (BlockMagic != TPT1Magic)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("BTPLoader: Expected TPT1 block, got 0x%08X."), BlockMagic);
		return false;
	}

	Stream.ReadU32(); // block size
	OutAnim.LoopMode = Stream.ReadU8();    // +0x08
	Stream.Skip(1);                         // +0x09 padding
	OutAnim.FrameCount = Stream.ReadS16(); // +0x0A

	const uint16 AnimEntryCount = Stream.ReadU16();      // +0x0C
	const uint16 TotalKeyframeCount = Stream.ReadU16();  // +0x0E

	const uint32 AnimEntryOffset    = Stream.ReadU32();  // +0x10
	const uint32 KeyframeDataOffset = Stream.ReadU32();  // +0x14
	const uint32 UpdateMatIDOffset  = Stream.ReadU32();  // +0x18
	const uint32 NameTableOffset    = Stream.ReadU32();  // +0x1C

	const int64 AbsAnimEntry  = BlockStart + AnimEntryOffset;
	const int64 AbsKeyframes  = BlockStart + KeyframeDataOffset;
	const int64 AbsMatID      = BlockStart + UpdateMatIDOffset;
	const int64 AbsNameTable  = BlockStart + NameTableOffset;

	// --- Parse name table ---
	TArray<FString> Names = ParseNameTable(Stream, AbsNameTable);

	// --- Parse animation entries ---
	// J3DAnmTexPatternFullTable: u16 maxFrame, u16 offset, u8 texNo, padding
	OutAnim.Entries.SetNum(AnimEntryCount);
	for (uint16 i = 0; i < AnimEntryCount; ++i)
	{
		Stream.Seek(AbsAnimEntry + i * BTPEntrySize);

		const uint16 MaxFrame   = Stream.ReadU16(); // keyframe count for this entry
		const uint16 Offset     = Stream.ReadU16(); // first index into keyframe array
		const uint8 TexMapIndex = Stream.ReadU8();   // which tex map slot
		Stream.Skip(1); // padding
		Stream.Skip(2); // _6

		FBTPAnimEntry& Entry = OutAnim.Entries[i];
		Entry.MaterialName = (i < Names.Num()) ? Names[i] : FString::Printf(TEXT("Material_%d"), i);
		Entry.TexMapIndex = TexMapIndex;

		// Read keyframe data: each keyframe is u16 textureIndex, u16 pad
		// The frame number is implicit from the array position when MaxFrame == FrameCount,
		// or explicit when using indexed lookup.
		// In TPT1, values are u16 texture indices, one per frame from Offset.
		Entry.Frames.Reserve(MaxFrame);
		Stream.Seek(AbsKeyframes + Offset * sizeof(uint16));
		for (uint16 f = 0; f < MaxFrame; ++f)
		{
			const uint16 TexIndex = Stream.ReadU16();
			Entry.Frames.Add(TPair<int32, int32>(f, TexIndex));
		}
	}

	UE_LOG(LogSMSImporter, Log, TEXT("BTPLoader: Parsed %d anim entries, %d frames."),
		OutAnim.Entries.Num(), OutAnim.FrameCount);
	return true;
}
