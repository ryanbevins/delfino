// BTILoader.h - GX texture format decoder for GameCube BTI textures

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"

class FBigEndianStream;

/**
 * GX texture format identifiers.
 * These match the values used in BTI headers and BMD TEX1 sections.
 */
enum class EGXTexFormat : uint8
{
	I4     = 0x00,  // 4-bit grayscale
	I8     = 0x01,  // 8-bit grayscale
	IA4    = 0x02,  // 4-bit intensity + 4-bit alpha
	IA8    = 0x03,  // 8-bit intensity + 8-bit alpha
	RGB565 = 0x04,  // 16-bit color
	RGB5A3 = 0x05,  // 16-bit color with alpha
	RGBA8  = 0x06,  // 32-bit color (tiled in 2-pass AR, GB)
	CI4    = 0x08,  // 4-bit paletted
	CI8    = 0x09,  // 8-bit paletted
	CI14X2 = 0x0A,  // 14-bit paletted
	CMPR   = 0x0E   // S3TC/DXT1 variant
};

/**
 * BTI header structure (0x20 bytes).
 * Matches the ResTIMG struct from the GameCube SDK.
 */
struct FBTIHeader
{
	EGXTexFormat Format;       // 0x00
	uint8 AlphaEnabled;        // 0x01
	uint16 Width;              // 0x02
	uint16 Height;             // 0x04
	uint8 WrapS;               // 0x06
	uint8 WrapT;               // 0x07
	uint8 IsIndexTexture;      // 0x08
	uint8 PaletteFormat;       // 0x09
	uint16 NumPaletteColors;   // 0x0A
	uint32 PaletteOffset;      // 0x0C (relative to header start)
	uint8 MipmapEnabled;       // 0x10
	uint8 DoEdgeLod;           // 0x11
	uint8 BiasClamp;           // 0x12
	uint8 MaxAnisotropy;       // 0x13
	uint8 MinFilter;           // 0x14
	uint8 MagFilter;           // 0x15
	int8 MinLod;               // 0x16
	int8 MaxLod;               // 0x17
	uint8 MipmapCount;         // 0x18
	uint8 Pad19;               // 0x19
	int16 LodBias;             // 0x1A
	uint32 ImageDataOffset;    // 0x1C (relative to header start)
};

/**
 * Decodes GameCube BTI textures to RGBA8 pixel data and UTexture2D assets.
 *
 * Supports all 10 GX texture formats: I4, I8, IA4, IA8, RGB565, RGB5A3,
 * RGBA8, CI4, CI8, and CMPR. Each format uses a tile-based memory layout
 * that must be de-tiled to produce linear scanline-ordered output.
 */
class SMSLEVELIMPORTER_API FBTILoader
{
public:
	/** Parse a BTI header from the current stream position. */
	static FBTIHeader ParseHeader(FBigEndianStream& Stream);

	/**
	 * Decode image pixel data to RGBA8 (4 bytes per pixel, W*H*4 total).
	 * For paletted formats (CI4/CI8), PaletteData must point to the raw palette.
	 */
	static TArray<uint8> DecodePixels(const FBTIHeader& Header,
		const uint8* ImageData, const uint8* PaletteData = nullptr);

	/**
	 * Create a UTexture2D from raw RGBA8 pixel data.
	 * Sets wrap modes and marks as SRGB.
	 */
	static UTexture2D* CreateTexture(UObject* Outer, const FString& Name,
		uint16 Width, uint16 Height, const TArray<uint8>& RGBA8Data,
		uint8 WrapS, uint8 WrapT);

private:
	// Per-format decoders (de-tile and convert to linear RGBA8)
	static void DecodeI4(const uint8* Src, uint8* Dst, uint16 W, uint16 H);
	static void DecodeI8(const uint8* Src, uint8* Dst, uint16 W, uint16 H);
	static void DecodeIA4(const uint8* Src, uint8* Dst, uint16 W, uint16 H);
	static void DecodeIA8(const uint8* Src, uint8* Dst, uint16 W, uint16 H);
	static void DecodeRGB565(const uint8* Src, uint8* Dst, uint16 W, uint16 H);
	static void DecodeRGB5A3(const uint8* Src, uint8* Dst, uint16 W, uint16 H);
	static void DecodeRGBA8(const uint8* Src, uint8* Dst, uint16 W, uint16 H);
	static void DecodeCMPR(const uint8* Src, uint8* Dst, uint16 W, uint16 H);
	static void DecodeCI4(const uint8* Src, const uint8* Pal, uint8 PalFmt, uint8* Dst, uint16 W, uint16 H);
	static void DecodeCI8(const uint8* Src, const uint8* Pal, uint8 PalFmt, uint8* Dst, uint16 W, uint16 H);

	/** Decode one 16-bit palette entry to RGBA8. PalFmt: 0=IA8, 1=RGB565, 2=RGB5A3. */
	static void DecodePaletteColor(uint16 Raw, uint8 PalFmt, uint8& R, uint8& G, uint8& B, uint8& A);
};
