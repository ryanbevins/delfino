// BMDLoader.h - J3D BMD/BDL model format parser
//
// Parses GameCube BMD (J3D v2) model files into intermediate structures
// that can be converted to UE5 static meshes. Handles VTX1 (vertices),
// SHP1 (shapes/display lists), MAT3 (materials), TEX1 (textures),
// and INF1 (scene graph) blocks.

#pragma once

#include "CoreMinimal.h"
#include "Formats/BTILoader.h"

class FBigEndianStream;

// ---- Block magic values (big-endian) ----

namespace BMDMagic
{
	static constexpr uint32 J3D2 = 0x4A334432;
	static constexpr uint32 INF1 = 0x494E4631;
	static constexpr uint32 VTX1 = 0x56545831;
	static constexpr uint32 EVP1 = 0x45565031;
	static constexpr uint32 DRW1 = 0x44525731;
	static constexpr uint32 JNT1 = 0x4A4E5431;
	static constexpr uint32 SHP1 = 0x53485031;
	static constexpr uint32 MAT3 = 0x4D415433;
	static constexpr uint32 MAT2 = 0x4D415432;
	static constexpr uint32 TEX1 = 0x54455831;
}

// ---- GX draw opcodes ----

namespace GXOpcode
{
	static constexpr uint8 NOP              = 0x00;
	static constexpr uint8 DRAW_QUADS       = 0x80;
	static constexpr uint8 DRAW_LINES       = 0xA8;
	static constexpr uint8 DRAW_TRIANGLES   = 0x90;
	static constexpr uint8 DRAW_TRISTRIP    = 0x98;
	static constexpr uint8 DRAW_TRIFAN      = 0xA0;
	static constexpr uint8 DRAW_LINE_STRIP  = 0xB0;
	static constexpr uint8 DRAW_POINTS      = 0xB8;
}

// ---- GX vertex attribute enums ----

namespace GXAttr
{
	static constexpr uint32 PositionMatIdx = 0;
	static constexpr uint32 Tex0MatIdx     = 1;
	static constexpr uint32 Tex1MatIdx     = 2;
	static constexpr uint32 Tex2MatIdx     = 3;
	static constexpr uint32 Tex3MatIdx     = 4;
	static constexpr uint32 Tex4MatIdx     = 5;
	static constexpr uint32 Tex5MatIdx     = 6;
	static constexpr uint32 Tex6MatIdx     = 7;
	static constexpr uint32 Tex7MatIdx     = 8;
	static constexpr uint32 Position       = 9;
	static constexpr uint32 Normal         = 10;
	static constexpr uint32 Color0         = 11;
	static constexpr uint32 Color1         = 12;
	static constexpr uint32 TexCoord0      = 13;
	static constexpr uint32 TexCoord7      = 20;
	static constexpr uint32 NullAttr       = 0xFF;
}

// ---- GX component type for vertex descriptor ----

namespace GXCompAccess
{
	static constexpr uint8 None    = 0;
	static constexpr uint8 Direct  = 1;
	static constexpr uint8 Index8  = 2;
	static constexpr uint8 Index16 = 3;
}

// ---- Data structures ----

struct FBMDBlock
{
	uint32 Magic;
	uint32 Size;
	int64 Offset;   // Absolute offset to start of block (magic field)
};

struct FBMDVertexData
{
	TArray<FVector3f> Positions;
	TArray<FVector3f> Normals;
	TArray<FColor> Colors0;
	TArray<FColor> Colors1;
	TArray<FVector2f> TexCoords[8];
};

/** A single triangle vertex with fully resolved attribute data. */
struct FBMDVertex
{
	FVector3f Position = FVector3f::ZeroVector;
	FVector3f Normal = FVector3f::ZeroVector;
	FColor Color0 = FColor::White;
	FColor Color1 = FColor::White;
	FVector2f TexCoords[8];
	bool bHasNormal = false;
	bool bHasColor0 = false;
	bool bHasColor1 = false;
	uint8 NumTexCoords = 0;
};

/** A primitive is a triangle list (every 3 vertices = one triangle). */
struct FBMDPrimitive
{
	TArray<FBMDVertex> Vertices;
};

struct FBMDShape
{
	TArray<FBMDPrimitive> Primitives;
	int32 MaterialIndex = -1;
};

struct FBMDMaterial
{
	FString Name;
	uint8 CullMode = 2;                // 0=none, 1=front, 2=back
	TArray<int16> TextureIndices;       // Indices into TEX1 textures (-1 = none)
	FLinearColor MatColor = FLinearColor::White;
	FLinearColor AmbientColor = FLinearColor(0.2f, 0.2f, 0.2f, 1.0f);
	uint8 BlendSrcFactor = 1;          // GX_BL_ONE
	uint8 BlendDstFactor = 0;          // GX_BL_ZERO
	uint8 AlphaCompFunc = 7;           // GX_ALWAYS
	uint8 AlphaCompRef = 0;
	bool bZCompEnable = true;
	bool bZWrite = true;
	int32 TevStageCount = 1;
};

struct FBMDTextureEntry
{
	FBTIHeader Header;
	TArray<uint8> RGBA8Pixels;          // Decoded pixel data (W*H*4)
};

/** Complete parsed BMD model. */
struct FBMDModel
{
	FBMDVertexData Vertices;
	TArray<FBMDShape> Shapes;
	TArray<FBMDMaterial> Materials;
	TArray<FBMDTextureEntry> Textures;

	/** INF1 scene graph mapping: shape index -> material index */
	TMap<int32, int32> ShapeToMaterial;
};

// Forward declarations for UE5 asset types
class UStaticMesh;
class UMaterial;
class UMaterialInstanceConstant;

// ---- Parser / Asset Creator ----

class SMSLEVELIMPORTER_API FBMDLoader
{
public:
	/**
	 * Parse a complete BMD/BDL file into intermediate structures.
	 * @param Data      Raw file bytes (may be YAZ0-compressed externally before calling).
	 * @param OutModel  Receives the parsed model data.
	 * @return true on success.
	 */
	static bool Parse(const TArray<uint8>& Data, FBMDModel& OutModel);

	/**
	 * Create UStaticMesh from parsed BMD model. Also creates materials and textures.
	 * @param Outer     Outer object for transient ownership (can be GetTransientPackage()).
	 * @param Name      Base name for the mesh asset (e.g. "DolpicPlaza").
	 * @param Model     Parsed BMD model data from Parse().
	 * @param AssetPath Base content path like "/Game/SMS/DolpicPlaza/Episode0".
	 * @return The created UStaticMesh, or nullptr on failure.
	 */
	static UStaticMesh* CreateStaticMesh(UObject* Outer, const FString& Name,
		const FBMDModel& Model, const FString& AssetPath);

private:
	/** Create a base material asset (parent for all material instances). */
	static UMaterial* GetOrCreateBaseMaterial(const FString& AssetPath);

	/** Create a material instance for one BMD material entry. */
	static UMaterialInstanceConstant* CreateMaterialInstance(UObject* Outer,
		const FBMDMaterial& Mat, const TArray<FBMDTextureEntry>& Textures,
		UMaterial* BaseMaterial, const FString& AssetPath, int32 MatIndex);

private:
	// Phase 1: Scan all blocks in the file
	static bool ScanBlocks(FBigEndianStream& Stream, uint32 BlockCount, TArray<FBMDBlock>& OutBlocks);

	// Phase 2: Parse individual blocks
	static bool ParseVTX1(FBigEndianStream& Stream, const FBMDBlock& Block, FBMDVertexData& OutVerts);
	static bool ParseSHP1(FBigEndianStream& Stream, const FBMDBlock& Block,
		const FBMDVertexData& Verts, TArray<FBMDShape>& OutShapes);
	static bool ParseMAT3(FBigEndianStream& Stream, const FBMDBlock& Block,
		TArray<FBMDMaterial>& OutMaterials);
	static bool ParseTEX1(FBigEndianStream& Stream, const FBMDBlock& Block,
		TArray<FBMDTextureEntry>& OutTextures);
	static bool ParseINF1(FBigEndianStream& Stream, const FBMDBlock& Block,
		TMap<int32, int32>& OutShapeToMaterial);

	// GX display list decoding
	struct FVtxAttrDesc
	{
		uint32 Attr;       // GX attribute enum (Position, Normal, Color0, etc.)
		uint8 CompType;    // GX_NONE, GX_DIRECT, GX_INDEX8, GX_INDEX16
	};

	static TArray<FVtxAttrDesc> ParseVtxDescList(FBigEndianStream& Stream, int64 Offset);
	static void DecodeDisplayList(const uint8* DLData, int64 DLSize,
		const TArray<FVtxAttrDesc>& Descs, const FBMDVertexData& Verts,
		FBMDPrimitive& OutPrim);

	// Name table reader (used by MAT3 and TEX1)
	static TArray<FString> ReadNameTable(FBigEndianStream& Stream, int64 TableOffset);

	// Helpers
	static const FBMDBlock* FindBlock(const TArray<FBMDBlock>& Blocks, uint32 Magic);
};
