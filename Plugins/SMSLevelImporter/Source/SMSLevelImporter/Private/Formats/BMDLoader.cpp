// BMDLoader.cpp - J3D BMD/BDL model format parser implementation

#include "Formats/BMDLoader.h"
#include "Formats/BTILoader.h"
#include "Util/BigEndianStream.h"
#include "SMSLevelImporterModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"

// ============================================================================
// Parse — main entry point
// ============================================================================

bool FBMDLoader::Parse(const TArray<uint8>& Data, FBMDModel& OutModel)
{
	if (Data.Num() < 0x20)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("BMD: File too small (%d bytes)"), Data.Num());
		return false;
	}

	FBigEndianStream Stream(Data);

	// ---- Validate file header ----
	const uint32 Magic = Stream.ReadU32();
	if (Magic != BMDMagic::J3D2)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("BMD: Invalid magic 0x%08X (expected J3D2)"), Magic);
		return false;
	}

	const uint32 Type = Stream.ReadU32();
	// Accept bmd3 (v26), bmd2 (v21), bdl4 (BDL variant)
	const uint32 BMD3 = 0x626D6433;
	const uint32 BMD2 = 0x626D6432;
	const uint32 BDL4 = 0x62646C34;
	if (Type != BMD3 && Type != BMD2 && Type != BDL4)
	{
		UE_LOG(LogSMSImporter, Warning, TEXT("BMD: Unknown sub-type 0x%08X, attempting parse anyway"), Type);
	}

	const uint32 FileSize = Stream.ReadU32();
	const uint32 BlockCount = Stream.ReadU32();

	UE_LOG(LogSMSImporter, Log, TEXT("BMD: type=0x%08X size=%u blocks=%u"), Type, FileSize, BlockCount);

	// ---- Phase 1: Scan all blocks ----
	TArray<FBMDBlock> Blocks;
	if (!ScanBlocks(Stream, BlockCount, Blocks))
	{
		return false;
	}

	// ---- Phase 2: Parse VTX1 ----
	const FBMDBlock* VTX1Block = FindBlock(Blocks, BMDMagic::VTX1);
	if (!VTX1Block)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("BMD: Missing VTX1 block"));
		return false;
	}
	if (!ParseVTX1(Stream, *VTX1Block, OutModel.Vertices))
	{
		return false;
	}

	// ---- Phase 3: Parse SHP1 ----
	const FBMDBlock* SHP1Block = FindBlock(Blocks, BMDMagic::SHP1);
	if (!SHP1Block)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("BMD: Missing SHP1 block"));
		return false;
	}
	if (!ParseSHP1(Stream, *SHP1Block, OutModel.Vertices, OutModel.Shapes))
	{
		return false;
	}

	// ---- Phase 4: Parse MAT3 (or MAT2) ----
	const FBMDBlock* MAT3Block = FindBlock(Blocks, BMDMagic::MAT3);
	if (!MAT3Block)
	{
		MAT3Block = FindBlock(Blocks, BMDMagic::MAT2);
	}
	if (MAT3Block)
	{
		if (!ParseMAT3(Stream, *MAT3Block, OutModel.Materials))
		{
			UE_LOG(LogSMSImporter, Warning, TEXT("BMD: MAT3 parse failed, continuing with defaults"));
		}
	}
	else
	{
		UE_LOG(LogSMSImporter, Warning, TEXT("BMD: No MAT3/MAT2 block found"));
	}

	// ---- Phase 5: Parse TEX1 ----
	const FBMDBlock* TEX1Block = FindBlock(Blocks, BMDMagic::TEX1);
	if (TEX1Block)
	{
		if (!ParseTEX1(Stream, *TEX1Block, OutModel.Textures))
		{
			UE_LOG(LogSMSImporter, Warning, TEXT("BMD: TEX1 parse failed, continuing without textures"));
		}
	}

	// ---- Phase 6: Parse INF1 ----
	const FBMDBlock* INF1Block = FindBlock(Blocks, BMDMagic::INF1);
	if (INF1Block)
	{
		if (!ParseINF1(Stream, *INF1Block, OutModel.ShapeToMaterial))
		{
			UE_LOG(LogSMSImporter, Warning, TEXT("BMD: INF1 parse failed, shape-material mapping unavailable"));
		}
	}

	// ---- Phase 7: Apply ShapeToMaterial mapping ----
	for (auto& Pair : OutModel.ShapeToMaterial)
	{
		const int32 ShapeIdx = Pair.Key;
		const int32 MatIdx = Pair.Value;
		if (ShapeIdx >= 0 && ShapeIdx < OutModel.Shapes.Num())
		{
			OutModel.Shapes[ShapeIdx].MaterialIndex = MatIdx;
		}
	}

	UE_LOG(LogSMSImporter, Log, TEXT("BMD: Parsed %d shapes, %d materials, %d textures"),
		OutModel.Shapes.Num(), OutModel.Materials.Num(), OutModel.Textures.Num());

	return true;
}

// ============================================================================
// ScanBlocks
// ============================================================================

// Known BMD/BDL block magic values for validation
static bool IsKnownBlockMagic(uint32 Magic)
{
	return Magic == BMDMagic::INF1
		|| Magic == BMDMagic::VTX1
		|| Magic == BMDMagic::EVP1
		|| Magic == BMDMagic::DRW1
		|| Magic == BMDMagic::JNT1
		|| Magic == BMDMagic::SHP1
		|| Magic == BMDMagic::MAT3
		|| Magic == BMDMagic::MAT2
		|| Magic == BMDMagic::TEX1;
}

bool FBMDLoader::ScanBlocks(FBigEndianStream& Stream, uint32 BlockCount, TArray<FBMDBlock>& OutBlocks)
{
	// First block starts at offset 0x20
	int64 Offset = 0x20;

	for (uint32 i = 0; i < BlockCount; i++)
	{
		if (Offset + 8 > Stream.Size())
		{
			UE_LOG(LogSMSImporter, Error, TEXT("BMD: Block %u extends past end of file at offset %lld"), i, Offset);
			return false;
		}

		Stream.Seek(Offset);
		FBMDBlock Block;
		Block.Offset = Offset;
		Block.Magic = Stream.ReadU32();
		Block.Size = Stream.ReadU32();

		if (Block.Size < 8)
		{
			UE_LOG(LogSMSImporter, Error, TEXT("BMD: Block %u has invalid size %u"), i, Block.Size);
			return false;
		}

		if (!IsKnownBlockMagic(Block.Magic))
		{
			UE_LOG(LogSMSImporter, Warning,
				TEXT("BMD: Block %u has unknown magic 0x%08X at offset %lld, skipping"),
				i, Block.Magic, Offset);
			Offset += Block.Size;
			continue;
		}

		OutBlocks.Add(Block);
		Offset += Block.Size;
	}

	return true;
}

// ============================================================================
// FindBlock
// ============================================================================

const FBMDBlock* FBMDLoader::FindBlock(const TArray<FBMDBlock>& Blocks, uint32 Magic)
{
	for (const FBMDBlock& B : Blocks)
	{
		if (B.Magic == Magic)
		{
			return &B;
		}
	}
	return nullptr;
}

// ============================================================================
// ReadNameTable
// ============================================================================

TArray<FString> FBMDLoader::ReadNameTable(FBigEndianStream& Stream, int64 TableOffset)
{
	TArray<FString> Names;

	Stream.Seek(TableOffset);
	const uint16 EntryCount = Stream.ReadU16();
	Stream.Skip(2); // padding

	// Each entry: u16 keyCode, u16 stringOffset (relative to start of string pool)
	TArray<uint16> StringOffsets;
	StringOffsets.SetNum(EntryCount);

	for (uint16 i = 0; i < EntryCount; i++)
	{
		Stream.ReadU16(); // keyCode — not needed
		StringOffsets[i] = Stream.ReadU16();
	}

	// String pool starts right after the entry table
	// The offsets in ResNTAB are relative to the entries array start
	// From the decompilation: getName returns ((char*)mEntries) + mEntries[index].mOffs - 4
	// The entries array starts at TableOffset + 4 (after count + pad)
	const int64 EntriesStart = TableOffset + 4;

	for (uint16 i = 0; i < EntryCount; i++)
	{
		// String offset is relative to the entries array, minus 4 adjustment removed
		// Actually from decompilation: ((const char*)mEntries) + mEntries[index].mOffs - 4
		// But mOffs is stored as an offset. The -4 compensates for something.
		// In practice, the name table stores offsets relative to the name table start.
		// The entries start at +4 from table start. Each entry is 4 bytes.
		// String data starts after all entries: TableOffset + 4 + EntryCount * 4
		// The mOffs value in the file is offset from the entries base.
		// Let's use the decompilation formula: string at (EntriesStart + StringOffsets[i] - 4)
		// Which simplifies to: TableOffset + StringOffsets[i]
		const int64 StringAddr = TableOffset + StringOffsets[i];
		if (StringAddr >= 0 && StringAddr < Stream.Size())
		{
			Stream.Seek(StringAddr);
			Names.Add(Stream.ReadNullTerminatedString());
		}
		else
		{
			Names.Add(FString::Printf(TEXT("unnamed_%d"), i));
		}
	}

	return Names;
}

// ============================================================================
// ParseVTX1
// ============================================================================

bool FBMDLoader::ParseVTX1(FBigEndianStream& Stream, const FBMDBlock& Block, FBMDVertexData& OutVerts)
{
	const int64 Base = Block.Offset;

	// Read data offset table
	Stream.Seek(Base + 0x08);
	const uint32 FmtListOffset = Stream.ReadU32();

	// Data array offsets (positions, normals, NBT, color0, color1, texcoord0..7)
	// Offsets at +0x0C through +0x3C, each a u32 relative to block start
	uint32 DataOffsets[13]; // pos, nrm, nbt, clr0, clr1, tex0..tex7
	for (int32 i = 0; i < 13; i++)
	{
		DataOffsets[i] = Stream.ReadU32();
	}

	// Parse attribute format list to know component types
	struct FAttrFormat
	{
		uint32 Attr;
		uint32 CompCount;
		uint32 CompType; // 0=u8, 1=s8, 2=u16, 3=s16, 4=f32
		uint8 FracBits;
	};

	TArray<FAttrFormat> Formats;
	Stream.Seek(Base + FmtListOffset);
	while (true)
	{
		FAttrFormat Fmt;
		Fmt.Attr = Stream.ReadU32();
		if (Fmt.Attr == 0xFF || Fmt.Attr == 0xFFFFFFFF)
		{
			break;
		}
		Fmt.CompCount = Stream.ReadU32();
		Fmt.CompType = Stream.ReadU32();
		Fmt.FracBits = Stream.ReadU8();
		Stream.Skip(3); // padding to 16 bytes
		Formats.Add(Fmt);
	}

	// Helper lambda to find format for an attribute
	auto FindFormat = [&Formats](uint32 Attr) -> const FAttrFormat*
	{
		for (const FAttrFormat& F : Formats)
		{
			if (F.Attr == Attr)
			{
				return &F;
			}
		}
		return nullptr;
	};

	// ---- Read positions (attr 9, data offset index 0) ----
	if (DataOffsets[0] != 0)
	{
		const FAttrFormat* Fmt = FindFormat(GXAttr::Position);
		const int64 DataStart = Base + DataOffsets[0];

		// Determine end of position data by finding next non-zero data offset
		int64 DataEnd = Base + Block.Size;
		for (int32 i = 1; i < 13; i++)
		{
			if (DataOffsets[i] != 0)
			{
				DataEnd = Base + DataOffsets[i];
				break;
			}
		}

		Stream.Seek(DataStart);
		if (Fmt && Fmt->CompType == 4) // f32
		{
			const int64 Count = (DataEnd - DataStart) / 12;
			OutVerts.Positions.Reserve(Count);
			for (int64 i = 0; i < Count; i++)
			{
				const float X = Stream.ReadF32();
				const float Y = Stream.ReadF32();
				const float Z = Stream.ReadF32();
				OutVerts.Positions.Add(FVector3f(X, Y, Z));
			}
		}
		else if (Fmt && Fmt->CompType == 3) // s16
		{
			const float Scale = 1.0f / static_cast<float>(1 << Fmt->FracBits);
			const int64 Count = (DataEnd - DataStart) / 6;
			OutVerts.Positions.Reserve(Count);
			for (int64 i = 0; i < Count; i++)
			{
				const float X = Stream.ReadS16() * Scale;
				const float Y = Stream.ReadS16() * Scale;
				const float Z = Stream.ReadS16() * Scale;
				OutVerts.Positions.Add(FVector3f(X, Y, Z));
			}
		}
		else
		{
			// Default: assume f32
			const int64 Count = (DataEnd - DataStart) / 12;
			OutVerts.Positions.Reserve(Count);
			for (int64 i = 0; i < Count; i++)
			{
				const float X = Stream.ReadF32();
				const float Y = Stream.ReadF32();
				const float Z = Stream.ReadF32();
				OutVerts.Positions.Add(FVector3f(X, Y, Z));
			}
		}
		UE_LOG(LogSMSImporter, Verbose, TEXT("BMD VTX1: %d positions"), OutVerts.Positions.Num());
	}

	// ---- Read normals (attr 10, data offset index 1) ----
	if (DataOffsets[1] != 0)
	{
		const FAttrFormat* Fmt = FindFormat(GXAttr::Normal);
		const int64 DataStart = Base + DataOffsets[1];

		// Find end
		int64 DataEnd = Base + Block.Size;
		for (int32 i = 2; i < 13; i++)
		{
			if (DataOffsets[i] != 0)
			{
				DataEnd = Base + DataOffsets[i];
				break;
			}
		}

		Stream.Seek(DataStart);
		if (Fmt && Fmt->CompType == 3) // s16
		{
			const float Scale = (Fmt->FracBits > 0) ? (1.0f / static_cast<float>(1 << Fmt->FracBits)) : (1.0f / 16384.0f);
			const int64 Count = (DataEnd - DataStart) / 6;
			OutVerts.Normals.Reserve(Count);
			for (int64 i = 0; i < Count; i++)
			{
				const float X = Stream.ReadS16() * Scale;
				const float Y = Stream.ReadS16() * Scale;
				const float Z = Stream.ReadS16() * Scale;
				OutVerts.Normals.Add(FVector3f(X, Y, Z));
			}
		}
		else // f32 (default)
		{
			const int64 Count = (DataEnd - DataStart) / 12;
			OutVerts.Normals.Reserve(Count);
			for (int64 i = 0; i < Count; i++)
			{
				const float X = Stream.ReadF32();
				const float Y = Stream.ReadF32();
				const float Z = Stream.ReadF32();
				OutVerts.Normals.Add(FVector3f(X, Y, Z));
			}
		}
		UE_LOG(LogSMSImporter, Verbose, TEXT("BMD VTX1: %d normals"), OutVerts.Normals.Num());
	}

	// ---- Read colors (attr 11=Color0, 12=Color1; data offset index 3=clr0, 4=clr1) ----
	auto ReadColors = [&](int32 OffsetIdx, TArray<FColor>& OutColors)
	{
		if (DataOffsets[OffsetIdx] == 0)
		{
			return;
		}
		const int64 DataStart = Base + DataOffsets[OffsetIdx];

		int64 DataEnd = Base + Block.Size;
		for (int32 i = OffsetIdx + 1; i < 13; i++)
		{
			if (DataOffsets[i] != 0)
			{
				DataEnd = Base + DataOffsets[i];
				break;
			}
		}

		const int64 Count = (DataEnd - DataStart) / 4; // RGBA8 = 4 bytes per color
		Stream.Seek(DataStart);
		OutColors.Reserve(Count);
		for (int64 i = 0; i < Count; i++)
		{
			const uint8 R = Stream.ReadU8();
			const uint8 G = Stream.ReadU8();
			const uint8 B = Stream.ReadU8();
			const uint8 A = Stream.ReadU8();
			OutColors.Add(FColor(R, G, B, A));
		}
	};

	ReadColors(3, OutVerts.Colors0);
	ReadColors(4, OutVerts.Colors1);

	if (OutVerts.Colors0.Num() > 0)
	{
		UE_LOG(LogSMSImporter, Verbose, TEXT("BMD VTX1: %d color0 entries"), OutVerts.Colors0.Num());
	}

	// ---- Read texcoords (attr 13-20, data offset index 5-12) ----
	for (int32 TexIdx = 0; TexIdx < 8; TexIdx++)
	{
		const int32 OffsetIdx = 5 + TexIdx;
		if (DataOffsets[OffsetIdx] == 0)
		{
			continue;
		}

		const FAttrFormat* Fmt = FindFormat(GXAttr::TexCoord0 + TexIdx);
		const int64 DataStart = Base + DataOffsets[OffsetIdx];

		int64 DataEnd = Base + Block.Size;
		for (int32 i = OffsetIdx + 1; i < 13; i++)
		{
			if (DataOffsets[i] != 0)
			{
				DataEnd = Base + DataOffsets[i];
				break;
			}
		}

		Stream.Seek(DataStart);
		if (Fmt && Fmt->CompType == 3) // s16
		{
			const float Scale = (Fmt->FracBits > 0) ? (1.0f / static_cast<float>(1 << Fmt->FracBits)) : (1.0f / 256.0f);
			const int64 Count = (DataEnd - DataStart) / 4; // 2 x s16
			OutVerts.TexCoords[TexIdx].Reserve(Count);
			for (int64 i = 0; i < Count; i++)
			{
				const float S = Stream.ReadS16() * Scale;
				const float T = Stream.ReadS16() * Scale;
				OutVerts.TexCoords[TexIdx].Add(FVector2f(S, T));
			}
		}
		else // f32 (default)
		{
			const int64 Count = (DataEnd - DataStart) / 8; // 2 x f32
			OutVerts.TexCoords[TexIdx].Reserve(Count);
			for (int64 i = 0; i < Count; i++)
			{
				const float S = Stream.ReadF32();
				const float T = Stream.ReadF32();
				OutVerts.TexCoords[TexIdx].Add(FVector2f(S, T));
			}
		}

		UE_LOG(LogSMSImporter, Verbose, TEXT("BMD VTX1: %d texcoord%d entries"), OutVerts.TexCoords[TexIdx].Num(), TexIdx);
	}

	return true;
}

// ============================================================================
// ParseVtxDescList
// ============================================================================

TArray<FBMDLoader::FVtxAttrDesc> FBMDLoader::ParseVtxDescList(FBigEndianStream& Stream, int64 Offset)
{
	TArray<FVtxAttrDesc> Descs;
	Stream.Seek(Offset);

	while (true)
	{
		FVtxAttrDesc Desc;
		Desc.Attr = Stream.ReadU32();
		Desc.CompType = static_cast<uint8>(Stream.ReadU32());

		if (Desc.Attr == GXAttr::NullAttr || Desc.Attr == 0xFFFFFFFF)
		{
			break;
		}

		Descs.Add(Desc);
	}

	return Descs;
}

// ============================================================================
// DecodeDisplayList
// ============================================================================

void FBMDLoader::DecodeDisplayList(const uint8* DLData, int64 DLSize,
	const TArray<FVtxAttrDesc>& Descs, const FBMDVertexData& Verts,
	FBMDPrimitive& OutPrim)
{
	if (!DLData || DLSize <= 0)
	{
		return;
	}

	// Create a stream over the display list data
	FBigEndianStream DLStream(DLData, DLSize);

	while (!DLStream.IsEOF())
	{
		const uint8 Opcode = DLStream.ReadU8();

		if (Opcode == GXOpcode::NOP)
		{
			continue;
		}

		// Check if this is a draw command
		const bool bIsDraw = (Opcode == GXOpcode::DRAW_TRIANGLES ||
		                      Opcode == GXOpcode::DRAW_TRISTRIP ||
		                      Opcode == GXOpcode::DRAW_TRIFAN ||
		                      Opcode == GXOpcode::DRAW_QUADS ||
		                      Opcode == GXOpcode::DRAW_LINES ||
		                      Opcode == GXOpcode::DRAW_LINE_STRIP ||
		                      Opcode == GXOpcode::DRAW_POINTS);

		if (!bIsDraw)
		{
			// Unrecognized opcode — skip the rest of this display list with a warning.
			// In BMD display lists, only draw commands and NOPs are typically present.
			UE_LOG(LogSMSImporter, Warning,
				TEXT("BMD SHP1: Unrecognized display list opcode 0x%02X at offset %lld, skipping rest of display list"),
				Opcode, DLStream.Tell() - 1);
			break;
		}

		if (DLStream.Tell() + 2 > DLSize)
		{
			break;
		}

		const uint16 VertexCount = DLStream.ReadU16();

		// Read raw vertex indices
		TArray<FBMDVertex> RawVerts;
		RawVerts.Reserve(VertexCount);

		for (uint16 v = 0; v < VertexCount; v++)
		{
			FBMDVertex Vert;

			for (const FVtxAttrDesc& Desc : Descs)
			{
				if (Desc.CompType == GXCompAccess::None)
				{
					continue;
				}

				// Read index
				int32 Index = 0;
				if (Desc.CompType == GXCompAccess::Index8)
				{
					if (DLStream.IsEOF()) break;
					Index = DLStream.ReadU8();
				}
				else if (Desc.CompType == GXCompAccess::Index16)
				{
					if (DLStream.Tell() + 2 > DLSize) break;
					Index = DLStream.ReadU16();
				}
				else if (Desc.CompType == GXCompAccess::Direct)
				{
					// Direct data: for matrix indices this is a single byte
					if (Desc.Attr <= GXAttr::Tex7MatIdx)
					{
						if (!DLStream.IsEOF()) DLStream.ReadU8();
					}
					else
					{
						// For other direct attributes, skip (rare in BMD)
						if (!DLStream.IsEOF()) DLStream.ReadU8();
					}
					continue;
				}

				// Look up actual vertex data by index
				if (Desc.Attr == GXAttr::Position)
				{
					if (Index >= 0 && Index < Verts.Positions.Num())
					{
						Vert.Position = Verts.Positions[Index];
					}
				}
				else if (Desc.Attr == GXAttr::Normal)
				{
					if (Index >= 0 && Index < Verts.Normals.Num())
					{
						Vert.Normal = Verts.Normals[Index];
						Vert.bHasNormal = true;
					}
				}
				else if (Desc.Attr == GXAttr::Color0)
				{
					if (Index >= 0 && Index < Verts.Colors0.Num())
					{
						Vert.Color0 = Verts.Colors0[Index];
						Vert.bHasColor0 = true;
					}
				}
				else if (Desc.Attr == GXAttr::Color1)
				{
					if (Index >= 0 && Index < Verts.Colors1.Num())
					{
						Vert.Color1 = Verts.Colors1[Index];
						Vert.bHasColor1 = true;
					}
				}
				else if (Desc.Attr >= GXAttr::TexCoord0 && Desc.Attr <= GXAttr::TexCoord7)
				{
					const int32 TexIdx = Desc.Attr - GXAttr::TexCoord0;
					if (Index >= 0 && Index < Verts.TexCoords[TexIdx].Num())
					{
						Vert.TexCoords[TexIdx] = Verts.TexCoords[TexIdx][Index];
						if (TexIdx + 1 > Vert.NumTexCoords)
						{
							Vert.NumTexCoords = static_cast<uint8>(TexIdx + 1);
						}
					}
				}
				// PositionMatIdx and TexNMatIdx: skip (matrix index, not vertex data)
				// Already handled by Direct case above, but Index8/16 also possible
			}

			RawVerts.Add(Vert);
		}

		// ---- Convert primitive mode to triangle list ----
		if (Opcode == GXOpcode::DRAW_TRIANGLES)
		{
			// Already a triangle list
			for (int32 i = 0; i + 2 < RawVerts.Num(); i += 3)
			{
				OutPrim.Vertices.Add(RawVerts[i]);
				OutPrim.Vertices.Add(RawVerts[i + 1]);
				OutPrim.Vertices.Add(RawVerts[i + 2]);
			}
		}
		else if (Opcode == GXOpcode::DRAW_TRISTRIP)
		{
			for (int32 i = 2; i < RawVerts.Num(); i++)
			{
				if ((i & 1) == 0)
				{
					// Even: i-2, i-1, i
					OutPrim.Vertices.Add(RawVerts[i - 2]);
					OutPrim.Vertices.Add(RawVerts[i - 1]);
					OutPrim.Vertices.Add(RawVerts[i]);
				}
				else
				{
					// Odd: i-1, i-2, i (swap winding)
					OutPrim.Vertices.Add(RawVerts[i - 1]);
					OutPrim.Vertices.Add(RawVerts[i - 2]);
					OutPrim.Vertices.Add(RawVerts[i]);
				}
			}
		}
		else if (Opcode == GXOpcode::DRAW_TRIFAN)
		{
			for (int32 i = 2; i < RawVerts.Num(); i++)
			{
				OutPrim.Vertices.Add(RawVerts[0]);
				OutPrim.Vertices.Add(RawVerts[i - 1]);
				OutPrim.Vertices.Add(RawVerts[i]);
			}
		}
		else if (Opcode == GXOpcode::DRAW_QUADS)
		{
			for (int32 i = 0; i + 3 < RawVerts.Num(); i += 4)
			{
				// Quad -> 2 triangles: (0,1,2) and (0,2,3)
				OutPrim.Vertices.Add(RawVerts[i]);
				OutPrim.Vertices.Add(RawVerts[i + 1]);
				OutPrim.Vertices.Add(RawVerts[i + 2]);

				OutPrim.Vertices.Add(RawVerts[i]);
				OutPrim.Vertices.Add(RawVerts[i + 2]);
				OutPrim.Vertices.Add(RawVerts[i + 3]);
			}
		}
		// Lines/points: skip (rare, not useful for mesh import)
	}
}

// ============================================================================
// ParseSHP1
// ============================================================================

bool FBMDLoader::ParseSHP1(FBigEndianStream& Stream, const FBMDBlock& Block,
	const FBMDVertexData& Verts, TArray<FBMDShape>& OutShapes)
{
	const int64 Base = Block.Offset;

	Stream.Seek(Base + 0x08);
	const uint16 ShapeCount = Stream.ReadU16();
	Stream.Skip(2); // padding

	const uint32 ShapeInitOffset = Stream.ReadU32();
	const uint32 IndexRemapOffset = Stream.ReadU32();
	Stream.ReadU32(); // name table offset (often 0)
	const uint32 VtxDescListOffset = Stream.ReadU32();
	Stream.ReadU32(); // matrix table offset
	const uint32 DisplayListDataOffset = Stream.ReadU32();
	Stream.ReadU32(); // matrix init data offset
	const uint32 DrawInitDataOffset = Stream.ReadU32();

	UE_LOG(LogSMSImporter, Verbose, TEXT("BMD SHP1: %d shapes"), ShapeCount);

	OutShapes.SetNum(ShapeCount);

	for (uint16 ShapeIdx = 0; ShapeIdx < ShapeCount; ShapeIdx++)
	{
		// Read index remap
		Stream.Seek(Base + IndexRemapOffset + ShapeIdx * 2);
		const uint16 RemappedIdx = Stream.ReadU16();

		// Read shape init data (0x28 bytes per shape)
		const int64 InitAddr = Base + ShapeInitOffset + RemappedIdx * 0x28;
		Stream.Seek(InitAddr);

		Stream.ReadU8();  // matrixType
		Stream.Skip(1);   // padding
		const uint16 MtxGroupCount = Stream.ReadU16();
		const uint16 VtxDescListIndex = Stream.ReadU16();
		Stream.ReadU16(); // mtxInitDataIndex
		const uint16 DrawInitDataIndex = Stream.ReadU16();
		// Skip rest (radius, bbox)

		// Parse vertex descriptor list for this shape
		// VtxDescListIndex is a byte offset into the vtx descriptor list area
		TArray<FVtxAttrDesc> VtxDescs = ParseVtxDescList(Stream, Base + VtxDescListOffset + VtxDescListIndex);

		// Process each draw batch (MtxGroupCount batches)
		FBMDShape& Shape = OutShapes[ShapeIdx];

		for (uint16 BatchIdx = 0; BatchIdx < MtxGroupCount; BatchIdx++)
		{
			// Read draw init data (0x08 bytes per batch)
			const int64 DrawInitAddr = Base + DrawInitDataOffset + (DrawInitDataIndex + BatchIdx) * 0x08;
			Stream.Seek(DrawInitAddr);

			const uint32 DLSize = Stream.ReadU32();
			const uint32 DLOffset = Stream.ReadU32();

			if (DLSize == 0)
			{
				continue;
			}

			// Display list data is at block base + DisplayListDataOffset + DLOffset
			const int64 DLAbsOffset = Base + DisplayListDataOffset + DLOffset;
			if (DLAbsOffset + DLSize > Stream.Size())
			{
				UE_LOG(LogSMSImporter, Warning,
					TEXT("BMD SHP1: Display list for shape %d batch %d extends past file end"),
					ShapeIdx, BatchIdx);
				continue;
			}

			// Get pointer to DL data
			Stream.Seek(DLAbsOffset);
			TArray<uint8> DLData = Stream.ReadBytes(DLSize);

			FBMDPrimitive Prim;
			DecodeDisplayList(DLData.GetData(), DLSize, VtxDescs, Verts, Prim);

			if (Prim.Vertices.Num() > 0)
			{
				Shape.Primitives.Add(MoveTemp(Prim));
			}
		}
	}

	return true;
}

// ============================================================================
// ParseMAT3
// ============================================================================

bool FBMDLoader::ParseMAT3(FBigEndianStream& Stream, const FBMDBlock& Block,
	TArray<FBMDMaterial>& OutMaterials)
{
	const int64 Base = Block.Offset;

	Stream.Seek(Base + 0x08);
	const uint16 MaterialCount = Stream.ReadU16();
	Stream.Skip(2); // padding

	// Read offsets to sub-tables
	// MAT3 block has a large number of offset fields
	const uint32 MatInitDataOffset = Stream.ReadU32();  // 0x0C
	const uint32 MatIDOffset = Stream.ReadU32();        // 0x10
	const uint32 NameTableOffset = Stream.ReadU32();    // 0x14
	Stream.ReadU32(); // 0x18: ind init data
	const uint32 CullModeOffset = Stream.ReadU32();     // 0x1C
	const uint32 MatColorOffset = Stream.ReadU32();     // 0x20
	Stream.ReadU32(); // 0x24: color chan num
	Stream.ReadU32(); // 0x28: color chan info
	const uint32 AmbColorOffset = Stream.ReadU32();     // 0x2C
	Stream.ReadU32(); // 0x30: light info
	Stream.ReadU32(); // 0x34: tex gen num
	Stream.ReadU32(); // 0x38: tex coord info
	Stream.ReadU32(); // 0x3C: tex coord2 info
	Stream.ReadU32(); // 0x40: tex mtx info
	Stream.ReadU32(); // 0x44: field_0x44
	const uint32 TexNoOffset = Stream.ReadU32();        // 0x48
	Stream.ReadU32(); // 0x4C: tev order info
	Stream.ReadU32(); // 0x50: tev color
	Stream.ReadU32(); // 0x54: tev k color
	const uint32 TevStageNumOffset = Stream.ReadU32();  // 0x58
	Stream.ReadU32(); // 0x5C: tev stage info
	Stream.ReadU32(); // 0x60: tev swap mode info
	Stream.ReadU32(); // 0x64: tev swap mode table info
	Stream.ReadU32(); // 0x68: fog info
	const uint32 AlphaCompOffset = Stream.ReadU32();    // 0x6C
	const uint32 BlendOffset = Stream.ReadU32();        // 0x70
	const uint32 ZModeOffset = Stream.ReadU32();        // 0x74

	// Read name table
	TArray<FString> Names;
	if (NameTableOffset != 0)
	{
		Names = ReadNameTable(Stream, Base + NameTableOffset);
	}

	UE_LOG(LogSMSImporter, Verbose, TEXT("BMD MAT3: %d materials"), MaterialCount);

	OutMaterials.SetNum(MaterialCount);

	for (uint16 i = 0; i < MaterialCount; i++)
	{
		FBMDMaterial& Mat = OutMaterials[i];

		// Read material ID remap
		Stream.Seek(Base + MatIDOffset + i * 2);
		const uint16 MatID = Stream.ReadU16();

		// Name
		if (i < Names.Num())
		{
			Mat.Name = Names[i];
		}
		else
		{
			Mat.Name = FString::Printf(TEXT("Material_%d"), i);
		}

		// Read material init data for this material
		// J3DMaterialInitData is 0x14C bytes (332 bytes) per entry
		const int64 InitBase = Base + MatInitDataOffset + MatID * 0x14C;

		// Validate offset
		if (InitBase + 0x14C > Stream.Size())
		{
			UE_LOG(LogSMSImporter, Warning, TEXT("BMD MAT3: Material %d init data out of bounds"), i);
			continue;
		}

		Stream.Seek(InitBase);
		Stream.ReadU8();                          // 0x00: materialMode
		const uint8 CullModeIdx = Stream.ReadU8();  // 0x01
		Stream.ReadU8();                          // 0x02: colorChanNumIdx
		Stream.ReadU8();                          // 0x03: texGenNumIdx
		const uint8 TevStageNumIdx = Stream.ReadU8(); // 0x04
		Stream.ReadU8();                          // 0x05: zCompLocIdx
		const uint8 ZModeIdx = Stream.ReadU8();     // 0x06
		Stream.ReadU8();                          // 0x07: ditherIdx

		// 0x08: matColorIdx[2] (u16 each)
		const uint16 MatColorIdx0 = Stream.ReadU16();
		Stream.ReadU16(); // matColorIdx[1]

		// Skip to texNoIdx at offset 0x84
		Stream.Seek(InitBase + 0x84);
		int16 TexNos[8];
		for (int32 t = 0; t < 8; t++)
		{
			TexNos[t] = static_cast<int16>(Stream.ReadU16());
		}

		// Skip to alphaCompIdx at offset 0x146
		Stream.Seek(InitBase + 0x146);
		const uint16 AlphaCompIdx = Stream.ReadU16();

		// BlendIdx at offset 0x148
		const uint16 BlendIdx = Stream.ReadU16();

		// ---- Look up sub-table values ----

		// Cull mode (u32 per entry in cull mode table)
		if (CullModeOffset != 0)
		{
			Stream.Seek(Base + CullModeOffset + CullModeIdx * 4);
			const uint32 CullVal = Stream.ReadU32();
			// GX cull: 0=GX_CULL_NONE, 1=GX_CULL_FRONT, 2=GX_CULL_BACK, 3=GX_CULL_ALL
			Mat.CullMode = static_cast<uint8>(FMath::Min(CullVal, 3u));
		}

		// Material color (GXColor = 4 bytes: RGBA)
		if (MatColorOffset != 0 && MatColorIdx0 != 0xFFFF)
		{
			Stream.Seek(Base + MatColorOffset + MatColorIdx0 * 4);
			const uint8 R = Stream.ReadU8();
			const uint8 G = Stream.ReadU8();
			const uint8 B = Stream.ReadU8();
			const uint8 A = Stream.ReadU8();
			Mat.MatColor = FLinearColor(R / 255.0f, G / 255.0f, B / 255.0f, A / 255.0f);
		}

		// Ambient color
		if (AmbColorOffset != 0)
		{
			// AmbColorIdx is at InitBase + 0x14 (u16[2])
			Stream.Seek(InitBase + 0x14);
			const uint16 AmbColorIdx0 = Stream.ReadU16();
			if (AmbColorIdx0 != 0xFFFF)
			{
				Stream.Seek(Base + AmbColorOffset + AmbColorIdx0 * 4);
				const uint8 R = Stream.ReadU8();
				const uint8 G = Stream.ReadU8();
				const uint8 B = Stream.ReadU8();
				const uint8 A = Stream.ReadU8();
				Mat.AmbientColor = FLinearColor(R / 255.0f, G / 255.0f, B / 255.0f, A / 255.0f);
			}
		}

		// Texture indices: look up from texNo table
		if (TexNoOffset != 0)
		{
			for (int32 t = 0; t < 8; t++)
			{
				if (TexNos[t] != static_cast<int16>(0xFFFF) && TexNos[t] >= 0)
				{
					Stream.Seek(Base + TexNoOffset + TexNos[t] * 2);
					const int16 TexIndex = static_cast<int16>(Stream.ReadU16());
					Mat.TextureIndices.Add(TexIndex);
				}
				else
				{
					Mat.TextureIndices.Add(-1);
				}
			}
		}

		// TEV stage count
		if (TevStageNumOffset != 0)
		{
			Stream.Seek(Base + TevStageNumOffset + TevStageNumIdx);
			Mat.TevStageCount = Stream.ReadU8();
		}

		// Alpha compare (J3DAlphaCompInfo: 8 bytes)
		if (AlphaCompOffset != 0 && AlphaCompIdx != 0xFFFF)
		{
			Stream.Seek(Base + AlphaCompOffset + AlphaCompIdx * 8);
			Mat.AlphaCompFunc = Stream.ReadU8(); // comp0
			Mat.AlphaCompRef = Stream.ReadU8();  // ref0
			// op, comp1, ref1 skipped for simplicity
		}

		// Blend mode (J3DBlendInfo: 4 bytes)
		if (BlendOffset != 0 && BlendIdx != 0xFFFF)
		{
			Stream.Seek(Base + BlendOffset + BlendIdx * 4);
			Stream.ReadU8(); // blendMode (GX_BM_NONE, GX_BM_BLEND, etc.)
			Mat.BlendSrcFactor = Stream.ReadU8();
			Mat.BlendDstFactor = Stream.ReadU8();
			// logicOp skipped
		}

		// Z mode (J3DZModeInfo: 4 bytes)
		if (ZModeOffset != 0 && ZModeIdx != 0xFF)
		{
			Stream.Seek(Base + ZModeOffset + ZModeIdx * 4);
			Mat.bZCompEnable = Stream.ReadU8() != 0;
			Stream.ReadU8(); // func
			Mat.bZWrite = Stream.ReadU8() != 0;
		}
	}

	return true;
}

// ============================================================================
// CreatePlaceholderTexture — 8x8 magenta/black checkerboard
// ============================================================================

TArray<uint8> FBMDLoader::CreatePlaceholderTexture()
{
	TArray<uint8> Pixels;
	Pixels.SetNumUninitialized(8 * 8 * 4);

	for (int32 y = 0; y < 8; ++y)
	{
		for (int32 x = 0; x < 8; ++x)
		{
			const int32 Idx = (y * 8 + x) * 4;
			const bool bMagenta = ((x + y) % 2 == 0);
			Pixels[Idx + 0] = bMagenta ? 255 : 0;   // R
			Pixels[Idx + 1] = 0;                      // G
			Pixels[Idx + 2] = bMagenta ? 255 : 0;   // B
			Pixels[Idx + 3] = 255;                    // A
		}
	}

	return Pixels;
}

// ============================================================================
// ParseTEX1
// ============================================================================

bool FBMDLoader::ParseTEX1(FBigEndianStream& Stream, const FBMDBlock& Block,
	TArray<FBMDTextureEntry>& OutTextures)
{
	const int64 Base = Block.Offset;

	Stream.Seek(Base + 0x08);
	const uint16 TextureCount = Stream.ReadU16();
	Stream.Skip(2); // padding

	const uint32 HeaderArrayOffset = Stream.ReadU32(); // 0x0C
	const uint32 NameTableOffset = Stream.ReadU32();   // 0x10

	// Read name table
	TArray<FString> Names;
	if (NameTableOffset != 0)
	{
		Names = ReadNameTable(Stream, Base + NameTableOffset);
	}

	UE_LOG(LogSMSImporter, Verbose, TEXT("BMD TEX1: %d textures"), TextureCount);

	OutTextures.SetNum(TextureCount);

	for (uint16 i = 0; i < TextureCount; i++)
	{
		// Each texture header is 0x20 bytes (FBTIHeader)
		const int64 TexHeaderAddr = Base + HeaderArrayOffset + i * 0x20;

		Stream.Seek(TexHeaderAddr);
		FBTIHeader Header = FBTILoader::ParseHeader(Stream);

		OutTextures[i].Header = Header;

		// Image data: offset is relative to this texture's header start
		const int64 ImageAddr = TexHeaderAddr + Header.ImageDataOffset;

		// Palette data (for CI4/CI8/CI14X2)
		const uint8* PalettePtr = nullptr;
		if (Header.IsIndexTexture && Header.PaletteOffset != 0)
		{
			const int64 PaletteAddr = TexHeaderAddr + Header.PaletteOffset;
			if (PaletteAddr >= 0 && PaletteAddr < Stream.Size())
			{
				// Get raw pointer into stream data
				Stream.Seek(PaletteAddr);
				// We need the raw pointer — read palette bytes
				const int64 PalSize = Header.NumPaletteColors * 2;
				// Use a temporary for palette data
				TArray<uint8> PaletteData = Stream.ReadBytes(FMath::Min(PalSize, Stream.Size() - PaletteAddr));

				if (ImageAddr >= 0 && ImageAddr < Stream.Size())
				{
					Stream.Seek(ImageAddr);
					// Calculate image data size (approximate: read until end of block or next texture)
					int64 ImageSize = Block.Size - (ImageAddr - Base);
					// Clamp to a reasonable max
					ImageSize = FMath::Min(ImageSize, Stream.Size() - ImageAddr);
					TArray<uint8> ImageData = Stream.ReadBytes(ImageSize);

					OutTextures[i].RGBA8Pixels = FBTILoader::DecodePixels(Header,
						ImageData.GetData(), PaletteData.GetData());
				}

				// If decode failed, use placeholder
				if (OutTextures[i].RGBA8Pixels.Num() == 0)
				{
					UE_LOG(LogSMSImporter, Warning,
						TEXT("BMD TEX1: Failed to decode paletted texture %d (%dx%d fmt=%d), using placeholder"),
						i, Header.Width, Header.Height, static_cast<uint8>(Header.Format));
					OutTextures[i].RGBA8Pixels = CreatePlaceholderTexture();
					OutTextures[i].Header.Width = 8;
					OutTextures[i].Header.Height = 8;
				}
				continue;
			}
		}

		// Non-paletted path
		if (ImageAddr >= 0 && ImageAddr < Stream.Size())
		{
			Stream.Seek(ImageAddr);
			int64 ImageSize = Block.Size - (ImageAddr - Base);
			ImageSize = FMath::Min(ImageSize, Stream.Size() - ImageAddr);
			TArray<uint8> ImageData = Stream.ReadBytes(ImageSize);

			OutTextures[i].RGBA8Pixels = FBTILoader::DecodePixels(Header, ImageData.GetData());
		}

		// If decode failed, use placeholder
		if (OutTextures[i].RGBA8Pixels.Num() == 0)
		{
			UE_LOG(LogSMSImporter, Warning,
				TEXT("BMD TEX1: Failed to decode texture %d (%dx%d fmt=%d), using placeholder"),
				i, Header.Width, Header.Height, static_cast<uint8>(Header.Format));
			OutTextures[i].RGBA8Pixels = CreatePlaceholderTexture();
			OutTextures[i].Header.Width = 8;
			OutTextures[i].Header.Height = 8;
		}

		if (i < Names.Num())
		{
			UE_LOG(LogSMSImporter, Verbose, TEXT("BMD TEX1: [%d] %s (%dx%d fmt=%d)"),
				i, *Names[i], Header.Width, Header.Height, static_cast<uint8>(Header.Format));
		}
	}

	return true;
}

// ============================================================================
// ParseINF1
// ============================================================================

bool FBMDLoader::ParseINF1(FBigEndianStream& Stream, const FBMDBlock& Block,
	TMap<int32, int32>& OutShapeToMaterial)
{
	const int64 Base = Block.Offset;

	// J3DModelInfoBlock layout:
	//   +0x08: u16 flags
	//   +0x0A: padding
	//   +0x0C: u32 packetNum
	//   +0x10: u32 vtxNum
	//   +0x14: u32 hierarchyOffset (relative to block start)
	Stream.Seek(Base + 0x14);
	const uint32 HierarchyOffset = Stream.ReadU32();

	if (HierarchyOffset == 0)
	{
		UE_LOG(LogSMSImporter, Warning, TEXT("BMD INF1: No hierarchy data"));
		return false;
	}

	// Walk the scene graph
	int32 CurrentMaterial = -1;
	Stream.Seek(Base + HierarchyOffset);

	// Safety limit to prevent infinite loops
	const int64 MaxEntries = (Block.Size - HierarchyOffset) / 4;
	int64 EntryCount = 0;

	while (EntryCount < MaxEntries)
	{
		const uint16 Type = Stream.ReadU16();
		const uint16 Index = Stream.ReadU16();
		EntryCount++;

		if (Type == 0x00) // End
		{
			break;
		}
		else if (Type == 0x01) // Open child scope
		{
			// Push — we don't need a full stack for the simple material-shape mapping
		}
		else if (Type == 0x02) // Close scope
		{
			// Pop
		}
		else if (Type == 0x10) // Joint
		{
			// Joint reference — skip
		}
		else if (Type == 0x11) // Material
		{
			CurrentMaterial = static_cast<int32>(Index);
		}
		else if (Type == 0x12) // Shape
		{
			if (CurrentMaterial >= 0)
			{
				OutShapeToMaterial.Add(static_cast<int32>(Index), CurrentMaterial);
			}
		}
	}

	UE_LOG(LogSMSImporter, Verbose, TEXT("BMD INF1: %d shape-material mappings"), OutShapeToMaterial.Num());
	return true;
}

// ============================================================================
// CreateStaticMesh — Convert parsed FBMDModel into UE5 UStaticMesh asset
// ============================================================================

UStaticMesh* FBMDLoader::CreateStaticMesh(UObject* Outer, const FString& Name,
	const FBMDModel& Model, const FString& AssetPath)
{
	// 1. Create package for the mesh
	FString MeshPackagePath = FString::Printf(TEXT("%s/Meshes/SM_%s"), *AssetPath, *Name);
	UPackage* MeshPackage = CreatePackage(*MeshPackagePath);
	MeshPackage->FullyLoad();

	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(MeshPackage,
		*FString::Printf(TEXT("SM_%s"), *Name), RF_Public | RF_Standalone);

	// 2. Build FMeshDescription
	FMeshDescription MeshDesc;
	FStaticMeshAttributes Attributes(MeshDesc);
	Attributes.Register();

	// Get attribute accessors
	TVertexAttributesRef<FVector3f> VertexPositions =
		MeshDesc.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> VertexNormals =
		MeshDesc.VertexInstanceAttributes().GetAttributesRef<FVector3f>(
			MeshAttribute::VertexInstance::Normal);
	TVertexInstanceAttributesRef<FVector2f> VertexUVs =
		MeshDesc.VertexInstanceAttributes().GetAttributesRef<FVector2f>(
			MeshAttribute::VertexInstance::TextureCoordinate);
	TVertexInstanceAttributesRef<FVector4f> VertexColors =
		MeshDesc.VertexInstanceAttributes().GetAttributesRef<FVector4f>(
			MeshAttribute::VertexInstance::Color);

	// 3. Create base material and material instances
	UMaterial* BaseMaterial = GetOrCreateBaseMaterial(AssetPath);
	TArray<UMaterialInstanceConstant*> MaterialInstances;

	for (int32 i = 0; i < Model.Materials.Num(); i++)
	{
		MaterialInstances.Add(CreateMaterialInstance(MeshPackage, Model.Materials[i],
			Model.Textures, BaseMaterial, AssetPath, i));
	}

	// 4. For each shape, create a polygon group (one per material slot)
	TMap<int32, FPolygonGroupID> MatToGroup;

	for (const FBMDShape& Shape : Model.Shapes)
	{
		int32 MatIdx = FMath::Max(0, Shape.MaterialIndex);
		if (!MatToGroup.Contains(MatIdx))
		{
			FPolygonGroupID GroupID = MeshDesc.CreatePolygonGroup();
			MatToGroup.Add(MatIdx, GroupID);
		}
	}

	// If no shapes have materials, create at least one default group
	if (MatToGroup.Num() == 0)
	{
		MatToGroup.Add(0, MeshDesc.CreatePolygonGroup());
	}

	// 5. Add vertices and triangles
	// Coordinate conversion: UE.X = GC.X, UE.Y = GC.Z, UE.Z = GC.Y
	// Winding order reversed (2,1,0 instead of 0,1,2) due to handedness change

	int32 TotalTriangles = 0;

	for (const FBMDShape& Shape : Model.Shapes)
	{
		int32 MatIdx = FMath::Max(0, Shape.MaterialIndex);
		FPolygonGroupID GroupID = MatToGroup[MatIdx];

		for (const FBMDPrimitive& Prim : Shape.Primitives)
		{
			// Every 3 vertices = 1 triangle
			for (int32 i = 0; i + 2 < Prim.Vertices.Num(); i += 3)
			{
				TArray<FVertexInstanceID> TriVerts;
				TriVerts.SetNum(3);

				for (int32 j = 0; j < 3; j++)
				{
					const FBMDVertex& V = Prim.Vertices[i + j];

					// Create vertex (position only)
					FVertexID VertID = MeshDesc.CreateVertex();
					FVector3f ConvertedPos(V.Position.X, V.Position.Z, V.Position.Y);
					VertexPositions[VertID] = ConvertedPos;

					// Create vertex instance (per-corner attributes)
					FVertexInstanceID InstID = MeshDesc.CreateVertexInstance(VertID);

					if (V.bHasNormal)
					{
						VertexNormals[InstID] = FVector3f(V.Normal.X, V.Normal.Z, V.Normal.Y);
					}

					if (V.NumTexCoords > 0)
					{
						VertexUVs.Set(InstID, 0, V.TexCoords[0]);
					}

					if (V.bHasColor0)
					{
						VertexColors[InstID] = FVector4f(
							V.Color0.R / 255.f, V.Color0.G / 255.f,
							V.Color0.B / 255.f, V.Color0.A / 255.f);
					}

					// Reverse winding for handedness change
					TriVerts[2 - j] = InstID;
				}

				MeshDesc.CreatePolygon(GroupID, TriVerts);
				TotalTriangles++;
			}
		}
	}

	if (TotalTriangles == 0)
	{
		UE_LOG(LogSMSImporter, Warning, TEXT("BMD CreateStaticMesh: No triangles for '%s'"), *Name);
		return nullptr;
	}

	// 6. Assign material slots
	TArray<FStaticMaterial> StaticMaterials;
	for (auto& Pair : MatToGroup)
	{
		int32 MatIdx = Pair.Key;
		UMaterialInterface* Mat = (MatIdx < MaterialInstances.Num()) ?
			static_cast<UMaterialInterface*>(MaterialInstances[MatIdx]) : nullptr;
		StaticMaterials.Add(FStaticMaterial(Mat));
	}
	StaticMesh->SetStaticMaterials(StaticMaterials);

	// 7. Build mesh from description
	TArray<const FMeshDescription*> MeshDescs;
	MeshDescs.Add(&MeshDesc);
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	StaticMesh->BuildFromMeshDescriptions(MeshDescs, Params);

	// 8. Register asset with Content Browser
	FAssetRegistryModule::AssetCreated(StaticMesh);
	MeshPackage->MarkPackageDirty();

	UE_LOG(LogSMSImporter, Log, TEXT("BMD: Created static mesh '%s' with %d triangles, %d material slots"),
		*Name, TotalTriangles, StaticMaterials.Num());

	return StaticMesh;
}

// ============================================================================
// GetOrCreateBaseMaterial — Shared parent material for all BMD material instances
// ============================================================================

UMaterial* FBMDLoader::GetOrCreateBaseMaterial(const FString& AssetPath)
{
	FString MatPath = FString::Printf(TEXT("%s/Materials/M_SMS_Base"), *AssetPath);

	// Check if already exists
	UMaterial* Existing = LoadObject<UMaterial>(nullptr, *(MatPath + TEXT(".M_SMS_Base")));
	if (Existing)
	{
		return Existing;
	}

	UPackage* Package = CreatePackage(*MatPath);
	Package->FullyLoad();

	UMaterial* Material = NewObject<UMaterial>(Package, TEXT("M_SMS_Base"),
		RF_Public | RF_Standalone);

	// Create texture sample parameter connected to Base Color
	UMaterialExpressionTextureSampleParameter2D* TexParam =
		NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
	TexParam->ParameterName = TEXT("BaseColorTexture");
	Material->GetExpressionCollection().AddExpression(TexParam);

#if WITH_EDITORONLY_DATA
	Material->GetEditorOnlyData()->BaseColor.Expression = TexParam;
#endif

	// Two-sided by default (SMS models often need it)
	Material->TwoSided = true;

	Material->PreEditChange(nullptr);
	Material->PostEditChange();

	FAssetRegistryModule::AssetCreated(Material);
	Package->MarkPackageDirty();

	UE_LOG(LogSMSImporter, Log, TEXT("BMD: Created base material at %s"), *MatPath);

	return Material;
}

// ============================================================================
// CreateMaterialInstance — Per-BMD-material instance with texture assignment
// ============================================================================

UMaterialInstanceConstant* FBMDLoader::CreateMaterialInstance(UObject* Outer,
	const FBMDMaterial& Mat, const TArray<FBMDTextureEntry>& Textures,
	UMaterial* BaseMaterial, const FString& AssetPath, int32 MatIndex)
{
	FString CleanName = Mat.Name.IsEmpty()
		? FString::Printf(TEXT("Mat_%d"), MatIndex)
		: Mat.Name;

	FString MatPackagePath = FString::Printf(TEXT("%s/Materials/MI_%s"), *AssetPath, *CleanName);
	UPackage* Package = CreatePackage(*MatPackagePath);
	Package->FullyLoad();

	UMaterialInstanceConstant* MIC = NewObject<UMaterialInstanceConstant>(Package,
		*FString::Printf(TEXT("MI_%s"), *CleanName), RF_Public | RF_Standalone);
	MIC->SetParentEditorOnly(BaseMaterial);

	// Set texture parameter if this material references a valid texture
	if (Mat.TextureIndices.Num() > 0 && Mat.TextureIndices[0] >= 0
		&& Mat.TextureIndices[0] < Textures.Num())
	{
		const FBMDTextureEntry& TexEntry = Textures[Mat.TextureIndices[0]];

		// Create UTexture2D from decoded pixels via BTI loader
		if (TexEntry.RGBA8Pixels.Num() > 0)
		{
			UTexture2D* Tex = FBTILoader::CreateTexture(Package,
				FString::Printf(TEXT("T_%s_%d"), *CleanName, Mat.TextureIndices[0]),
				TexEntry.Header.Width, TexEntry.Header.Height,
				TexEntry.RGBA8Pixels, TexEntry.Header.WrapS, TexEntry.Header.WrapT);

			if (Tex)
			{
				MIC->SetTextureParameterValueEditorOnly(
					FMaterialParameterInfo(TEXT("BaseColorTexture")), Tex);
			}
		}
	}

	MIC->InitStaticPermutation();
	FAssetRegistryModule::AssetCreated(MIC);
	Package->MarkPackageDirty();

	return MIC;
}
