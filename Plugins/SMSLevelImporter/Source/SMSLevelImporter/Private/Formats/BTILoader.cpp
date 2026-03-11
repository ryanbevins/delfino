// BTILoader.cpp - GX texture format decoder for GameCube BTI textures

#include "Formats/BTILoader.h"
#include "Util/BigEndianStream.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"

// ----------------------------------------------------------------------------
// Header parsing
// ----------------------------------------------------------------------------

FBTIHeader FBTILoader::ParseHeader(FBigEndianStream& Stream)
{
	FBTIHeader H;
	H.Format           = static_cast<EGXTexFormat>(Stream.ReadU8());
	H.AlphaEnabled     = Stream.ReadU8();
	H.Width            = Stream.ReadU16();
	H.Height           = Stream.ReadU16();
	H.WrapS            = Stream.ReadU8();
	H.WrapT            = Stream.ReadU8();
	H.IsIndexTexture   = Stream.ReadU8();
	H.PaletteFormat    = Stream.ReadU8();
	H.NumPaletteColors = Stream.ReadU16();
	H.PaletteOffset    = Stream.ReadU32();
	H.MipmapEnabled    = Stream.ReadU8();
	H.DoEdgeLod        = Stream.ReadU8();
	H.BiasClamp        = Stream.ReadU8();
	H.MaxAnisotropy    = Stream.ReadU8();
	H.MinFilter        = Stream.ReadU8();
	H.MagFilter        = Stream.ReadU8();
	H.MinLod           = Stream.ReadS8();
	H.MaxLod           = Stream.ReadS8();
	H.MipmapCount      = Stream.ReadU8();
	H.Pad19            = Stream.ReadU8();
	H.LodBias          = Stream.ReadS16();
	H.ImageDataOffset  = Stream.ReadU32();
	return H;
}

// ----------------------------------------------------------------------------
// Pixel decoding dispatcher
// ----------------------------------------------------------------------------

TArray<uint8> FBTILoader::DecodePixels(const FBTIHeader& Header,
	const uint8* ImageData, const uint8* PaletteData)
{
	const uint16 W = Header.Width;
	const uint16 H = Header.Height;
	TArray<uint8> RGBA;
	RGBA.SetNumZeroed(W * H * 4);

	uint8* Dst = RGBA.GetData();

	switch (Header.Format)
	{
	case EGXTexFormat::I4:     DecodeI4(ImageData, Dst, W, H);     break;
	case EGXTexFormat::I8:     DecodeI8(ImageData, Dst, W, H);     break;
	case EGXTexFormat::IA4:    DecodeIA4(ImageData, Dst, W, H);    break;
	case EGXTexFormat::IA8:    DecodeIA8(ImageData, Dst, W, H);    break;
	case EGXTexFormat::RGB565: DecodeRGB565(ImageData, Dst, W, H); break;
	case EGXTexFormat::RGB5A3: DecodeRGB5A3(ImageData, Dst, W, H); break;
	case EGXTexFormat::RGBA8:  DecodeRGBA8(ImageData, Dst, W, H);  break;
	case EGXTexFormat::CMPR:   DecodeCMPR(ImageData, Dst, W, H);   break;
	case EGXTexFormat::CI4:
		DecodeCI4(ImageData, PaletteData, Header.PaletteFormat, Dst, W, H);
		break;
	case EGXTexFormat::CI8:
		DecodeCI8(ImageData, PaletteData, Header.PaletteFormat, Dst, W, H);
		break;
	case EGXTexFormat::CI14X2:
		UE_LOG(LogTemp, Warning, TEXT("FBTILoader: CI14X2 format not supported"));
		break;
	default:
		UE_LOG(LogTemp, Error, TEXT("FBTILoader: Unknown texture format 0x%02X"),
			static_cast<uint8>(Header.Format));
		break;
	}

	return RGBA;
}

// ----------------------------------------------------------------------------
// I4 — 4-bit grayscale, tile 8x8
// ----------------------------------------------------------------------------

void FBTILoader::DecodeI4(const uint8* Src, uint8* Dst, uint16 W, uint16 H)
{
	constexpr int32 TileW = 8;
	constexpr int32 TileH = 8;
	int32 SrcPos = 0;

	for (int32 TileY = 0; TileY < H; TileY += TileH)
	{
		for (int32 TileX = 0; TileX < W; TileX += TileW)
		{
			for (int32 PixY = 0; PixY < TileH; PixY++)
			{
				for (int32 PixX = 0; PixX < TileW; PixX += 2)
				{
					uint8 Byte = Src[SrcPos++];
					// Two pixels per byte, high nibble first
					for (int32 Nib = 0; Nib < 2; Nib++)
					{
						int32 PX = TileX + PixX + Nib;
						int32 PY = TileY + PixY;
						uint8 Val = (Nib == 0) ? (Byte >> 4) : (Byte & 0x0F);
						uint8 Intensity = (Val << 4) | Val;

						if (PX < W && PY < H)
						{
							int32 Idx = (PY * W + PX) * 4;
							Dst[Idx + 0] = Intensity;
							Dst[Idx + 1] = Intensity;
							Dst[Idx + 2] = Intensity;
							Dst[Idx + 3] = 255;
						}
					}
				}
			}
		}
	}
}

// ----------------------------------------------------------------------------
// I8 — 8-bit grayscale, tile 8x4
// ----------------------------------------------------------------------------

void FBTILoader::DecodeI8(const uint8* Src, uint8* Dst, uint16 W, uint16 H)
{
	constexpr int32 TileW = 8;
	constexpr int32 TileH = 4;
	int32 SrcPos = 0;

	for (int32 TileY = 0; TileY < H; TileY += TileH)
	{
		for (int32 TileX = 0; TileX < W; TileX += TileW)
		{
			for (int32 PixY = 0; PixY < TileH; PixY++)
			{
				for (int32 PixX = 0; PixX < TileW; PixX++)
				{
					uint8 Val = Src[SrcPos++];
					int32 PX = TileX + PixX;
					int32 PY = TileY + PixY;

					if (PX < W && PY < H)
					{
						int32 Idx = (PY * W + PX) * 4;
						Dst[Idx + 0] = Val;
						Dst[Idx + 1] = Val;
						Dst[Idx + 2] = Val;
						Dst[Idx + 3] = 255;
					}
				}
			}
		}
	}
}

// ----------------------------------------------------------------------------
// IA4 — 4-bit intensity + 4-bit alpha, tile 8x4
// ----------------------------------------------------------------------------

void FBTILoader::DecodeIA4(const uint8* Src, uint8* Dst, uint16 W, uint16 H)
{
	constexpr int32 TileW = 8;
	constexpr int32 TileH = 4;
	int32 SrcPos = 0;

	for (int32 TileY = 0; TileY < H; TileY += TileH)
	{
		for (int32 TileX = 0; TileX < W; TileX += TileW)
		{
			for (int32 PixY = 0; PixY < TileH; PixY++)
			{
				for (int32 PixX = 0; PixX < TileW; PixX++)
				{
					uint8 Byte = Src[SrcPos++];
					uint8 A4 = (Byte >> 4) & 0x0F;
					uint8 I4 = Byte & 0x0F;
					uint8 Alpha = (A4 << 4) | A4;
					uint8 Intensity = (I4 << 4) | I4;

					int32 PX = TileX + PixX;
					int32 PY = TileY + PixY;

					if (PX < W && PY < H)
					{
						int32 Idx = (PY * W + PX) * 4;
						Dst[Idx + 0] = Intensity;
						Dst[Idx + 1] = Intensity;
						Dst[Idx + 2] = Intensity;
						Dst[Idx + 3] = Alpha;
					}
				}
			}
		}
	}
}

// ----------------------------------------------------------------------------
// IA8 — 8-bit intensity + 8-bit alpha, tile 4x4
// ----------------------------------------------------------------------------

void FBTILoader::DecodeIA8(const uint8* Src, uint8* Dst, uint16 W, uint16 H)
{
	constexpr int32 TileW = 4;
	constexpr int32 TileH = 4;
	int32 SrcPos = 0;

	for (int32 TileY = 0; TileY < H; TileY += TileH)
	{
		for (int32 TileX = 0; TileX < W; TileX += TileW)
		{
			for (int32 PixY = 0; PixY < TileH; PixY++)
			{
				for (int32 PixX = 0; PixX < TileW; PixX++)
				{
					uint8 Alpha     = Src[SrcPos++];
					uint8 Intensity = Src[SrcPos++];

					int32 PX = TileX + PixX;
					int32 PY = TileY + PixY;

					if (PX < W && PY < H)
					{
						int32 Idx = (PY * W + PX) * 4;
						Dst[Idx + 0] = Intensity;
						Dst[Idx + 1] = Intensity;
						Dst[Idx + 2] = Intensity;
						Dst[Idx + 3] = Alpha;
					}
				}
			}
		}
	}
}

// ----------------------------------------------------------------------------
// RGB565 — 16-bit color, tile 4x4
// ----------------------------------------------------------------------------

void FBTILoader::DecodeRGB565(const uint8* Src, uint8* Dst, uint16 W, uint16 H)
{
	constexpr int32 TileW = 4;
	constexpr int32 TileH = 4;
	int32 SrcPos = 0;

	for (int32 TileY = 0; TileY < H; TileY += TileH)
	{
		for (int32 TileX = 0; TileX < W; TileX += TileW)
		{
			for (int32 PixY = 0; PixY < TileH; PixY++)
			{
				for (int32 PixX = 0; PixX < TileW; PixX++)
				{
					uint16 Val = (static_cast<uint16>(Src[SrcPos]) << 8)
					           |  static_cast<uint16>(Src[SrcPos + 1]);
					SrcPos += 2;

					uint8 R5 = (Val >> 11) & 0x1F;
					uint8 G6 = (Val >> 5)  & 0x3F;
					uint8 B5 =  Val        & 0x1F;

					uint8 R = (R5 << 3) | (R5 >> 2);
					uint8 G = (G6 << 2) | (G6 >> 4);
					uint8 B = (B5 << 3) | (B5 >> 2);

					int32 PX = TileX + PixX;
					int32 PY = TileY + PixY;

					if (PX < W && PY < H)
					{
						int32 Idx = (PY * W + PX) * 4;
						Dst[Idx + 0] = R;
						Dst[Idx + 1] = G;
						Dst[Idx + 2] = B;
						Dst[Idx + 3] = 255;
					}
				}
			}
		}
	}
}

// ----------------------------------------------------------------------------
// RGB5A3 — 16-bit color with optional alpha, tile 4x4
// ----------------------------------------------------------------------------

void FBTILoader::DecodeRGB5A3(const uint8* Src, uint8* Dst, uint16 W, uint16 H)
{
	constexpr int32 TileW = 4;
	constexpr int32 TileH = 4;
	int32 SrcPos = 0;

	for (int32 TileY = 0; TileY < H; TileY += TileH)
	{
		for (int32 TileX = 0; TileX < W; TileX += TileW)
		{
			for (int32 PixY = 0; PixY < TileH; PixY++)
			{
				for (int32 PixX = 0; PixX < TileW; PixX++)
				{
					uint16 Val = (static_cast<uint16>(Src[SrcPos]) << 8)
					           |  static_cast<uint16>(Src[SrcPos + 1]);
					SrcPos += 2;

					uint8 R, G, B, A;

					if (Val & 0x8000)
					{
						// Opaque: RGB555
						uint8 R5 = (Val >> 10) & 0x1F;
						uint8 G5 = (Val >> 5)  & 0x1F;
						uint8 B5 =  Val        & 0x1F;
						R = (R5 << 3) | (R5 >> 2);
						G = (G5 << 3) | (G5 >> 2);
						B = (B5 << 3) | (B5 >> 2);
						A = 255;
					}
					else
					{
						// Translucent: A3RGB444
						uint8 A3 = (Val >> 12) & 0x07;
						uint8 R4 = (Val >> 8)  & 0x0F;
						uint8 G4 = (Val >> 4)  & 0x0F;
						uint8 B4 =  Val        & 0x0F;
						A = (A3 << 5) | (A3 << 2) | (A3 >> 1);
						R = (R4 << 4) | R4;
						G = (G4 << 4) | G4;
						B = (B4 << 4) | B4;
					}

					int32 PX = TileX + PixX;
					int32 PY = TileY + PixY;

					if (PX < W && PY < H)
					{
						int32 Idx = (PY * W + PX) * 4;
						Dst[Idx + 0] = R;
						Dst[Idx + 1] = G;
						Dst[Idx + 2] = B;
						Dst[Idx + 3] = A;
					}
				}
			}
		}
	}
}

// ----------------------------------------------------------------------------
// RGBA8 — 32-bit color, tile 4x4, two-pass AR+GB
// ----------------------------------------------------------------------------

void FBTILoader::DecodeRGBA8(const uint8* Src, uint8* Dst, uint16 W, uint16 H)
{
	constexpr int32 TileW = 4;
	constexpr int32 TileH = 4;
	int32 SrcPos = 0;

	for (int32 TileY = 0; TileY < H; TileY += TileH)
	{
		for (int32 TileX = 0; TileX < W; TileX += TileW)
		{
			// Each tile is 64 bytes: 32 bytes AR, then 32 bytes GB
			// AR pass: for each of 16 pixels (row by row): A, R
			// GB pass: for each of 16 pixels (row by row): G, B

			uint8 AR[32];
			uint8 GB[32];
			FMemory::Memcpy(AR, Src + SrcPos, 32);
			FMemory::Memcpy(GB, Src + SrcPos + 32, 32);
			SrcPos += 64;

			for (int32 PixY = 0; PixY < TileH; PixY++)
			{
				for (int32 PixX = 0; PixX < TileW; PixX++)
				{
					int32 PixIdx = PixY * TileW + PixX;
					uint8 A = AR[PixIdx * 2 + 0];
					uint8 R = AR[PixIdx * 2 + 1];
					uint8 G = GB[PixIdx * 2 + 0];
					uint8 B = GB[PixIdx * 2 + 1];

					int32 PX = TileX + PixX;
					int32 PY = TileY + PixY;

					if (PX < W && PY < H)
					{
						int32 Idx = (PY * W + PX) * 4;
						Dst[Idx + 0] = R;
						Dst[Idx + 1] = G;
						Dst[Idx + 2] = B;
						Dst[Idx + 3] = A;
					}
				}
			}
		}
	}
}

// ----------------------------------------------------------------------------
// CMPR — S3TC/DXT1, tile 8x8 (4 sub-blocks of 4x4)
// ----------------------------------------------------------------------------

/** Decode a single RGB565 value to 8-bit RGB (helper for CMPR). */
static void DecodeRGB565Color(uint16 Val, uint8& R, uint8& G, uint8& B)
{
	uint8 R5 = (Val >> 11) & 0x1F;
	uint8 G6 = (Val >> 5)  & 0x3F;
	uint8 B5 =  Val        & 0x1F;
	R = (R5 << 3) | (R5 >> 2);
	G = (G6 << 2) | (G6 >> 4);
	B = (B5 << 3) | (B5 >> 2);
}

void FBTILoader::DecodeCMPR(const uint8* Src, uint8* Dst, uint16 W, uint16 H)
{
	constexpr int32 TileW = 8;
	constexpr int32 TileH = 8;
	int32 SrcPos = 0;

	for (int32 TileY = 0; TileY < H; TileY += TileH)
	{
		for (int32 TileX = 0; TileX < W; TileX += TileW)
		{
			// 4 sub-blocks in 2x2 arrangement: TL, TR, BL, BR
			for (int32 SubBlock = 0; SubBlock < 4; SubBlock++)
			{
				int32 SubX = TileX + (SubBlock & 1) * 4;
				int32 SubY = TileY + (SubBlock >> 1) * 4;

				// Read DXT1 sub-block (8 bytes)
				uint16 Color0 = (static_cast<uint16>(Src[SrcPos]) << 8)
				              |  static_cast<uint16>(Src[SrcPos + 1]);
				uint16 Color1 = (static_cast<uint16>(Src[SrcPos + 2]) << 8)
				              |  static_cast<uint16>(Src[SrcPos + 3]);

				uint8 R0, G0, B0, R1, G1, B1;
				DecodeRGB565Color(Color0, R0, G0, B0);
				DecodeRGB565Color(Color1, R1, G1, B1);

				// Build color palette
				uint8 Palette[4][4]; // [index][RGBA]
				Palette[0][0] = R0; Palette[0][1] = G0; Palette[0][2] = B0; Palette[0][3] = 255;
				Palette[1][0] = R1; Palette[1][1] = G1; Palette[1][2] = B1; Palette[1][3] = 255;

				if (Color0 > Color1)
				{
					Palette[2][0] = (2 * R0 + R1) / 3;
					Palette[2][1] = (2 * G0 + G1) / 3;
					Palette[2][2] = (2 * B0 + B1) / 3;
					Palette[2][3] = 255;
					Palette[3][0] = (R0 + 2 * R1) / 3;
					Palette[3][1] = (G0 + 2 * G1) / 3;
					Palette[3][2] = (B0 + 2 * B1) / 3;
					Palette[3][3] = 255;
				}
				else
				{
					Palette[2][0] = (R0 + R1) / 2;
					Palette[2][1] = (G0 + G1) / 2;
					Palette[2][2] = (B0 + B1) / 2;
					Palette[2][3] = 255;
					Palette[3][0] = 0;
					Palette[3][1] = 0;
					Palette[3][2] = 0;
					Palette[3][3] = 0; // transparent black
				}

				// Decode 4x4 pixel indices (bytes 4-7)
				for (int32 Row = 0; Row < 4; Row++)
				{
					uint8 Bits = Src[SrcPos + 4 + Row];
					for (int32 Col = 0; Col < 4; Col++)
					{
						// MSB first: bits 7-6 = pixel 0, 5-4 = pixel 1, etc.
						int32 Shift = (3 - Col) * 2;
						uint8 Index = (Bits >> Shift) & 0x03;

						int32 PX = SubX + Col;
						int32 PY = SubY + Row;

						if (PX < W && PY < H)
						{
							int32 Idx = (PY * W + PX) * 4;
							Dst[Idx + 0] = Palette[Index][0];
							Dst[Idx + 1] = Palette[Index][1];
							Dst[Idx + 2] = Palette[Index][2];
							Dst[Idx + 3] = Palette[Index][3];
						}
					}
				}

				SrcPos += 8;
			}
		}
	}
}

// ----------------------------------------------------------------------------
// Palette color decoder
// ----------------------------------------------------------------------------

void FBTILoader::DecodePaletteColor(uint16 Raw, uint8 PalFmt,
	uint8& R, uint8& G, uint8& B, uint8& A)
{
	switch (PalFmt)
	{
	case 0: // IA8
	{
		A = (Raw >> 8) & 0xFF;
		uint8 I = Raw & 0xFF;
		R = G = B = I;
		break;
	}
	case 1: // RGB565
	{
		uint8 R5 = (Raw >> 11) & 0x1F;
		uint8 G6 = (Raw >> 5)  & 0x3F;
		uint8 B5 =  Raw        & 0x1F;
		R = (R5 << 3) | (R5 >> 2);
		G = (G6 << 2) | (G6 >> 4);
		B = (B5 << 3) | (B5 >> 2);
		A = 255;
		break;
	}
	case 2: // RGB5A3
	{
		if (Raw & 0x8000)
		{
			uint8 R5 = (Raw >> 10) & 0x1F;
			uint8 G5 = (Raw >> 5)  & 0x1F;
			uint8 B5 =  Raw        & 0x1F;
			R = (R5 << 3) | (R5 >> 2);
			G = (G5 << 3) | (G5 >> 2);
			B = (B5 << 3) | (B5 >> 2);
			A = 255;
		}
		else
		{
			uint8 A3 = (Raw >> 12) & 0x07;
			uint8 R4 = (Raw >> 8)  & 0x0F;
			uint8 G4 = (Raw >> 4)  & 0x0F;
			uint8 B4 =  Raw        & 0x0F;
			A = (A3 << 5) | (A3 << 2) | (A3 >> 1);
			R = (R4 << 4) | R4;
			G = (G4 << 4) | G4;
			B = (B4 << 4) | B4;
		}
		break;
	}
	default:
		R = G = B = 0;
		A = 255;
		break;
	}
}

// ----------------------------------------------------------------------------
// CI4 — 4-bit paletted, tile 8x8
// ----------------------------------------------------------------------------

void FBTILoader::DecodeCI4(const uint8* Src, const uint8* Pal, uint8 PalFmt,
	uint8* Dst, uint16 W, uint16 H)
{
	constexpr int32 TileW = 8;
	constexpr int32 TileH = 8;
	int32 SrcPos = 0;

	for (int32 TileY = 0; TileY < H; TileY += TileH)
	{
		for (int32 TileX = 0; TileX < W; TileX += TileW)
		{
			for (int32 PixY = 0; PixY < TileH; PixY++)
			{
				for (int32 PixX = 0; PixX < TileW; PixX += 2)
				{
					uint8 Byte = Src[SrcPos++];
					// Two pixels per byte, high nibble first
					for (int32 Nib = 0; Nib < 2; Nib++)
					{
						int32 PX = TileX + PixX + Nib;
						int32 PY = TileY + PixY;
						uint8 Index = (Nib == 0) ? (Byte >> 4) : (Byte & 0x0F);

						if (PX < W && PY < H && Pal)
						{
							// Each palette entry is 2 bytes (big-endian)
							uint16 PalEntry = (static_cast<uint16>(Pal[Index * 2]) << 8)
							                |  static_cast<uint16>(Pal[Index * 2 + 1]);
							uint8 R, G, B, A;
							DecodePaletteColor(PalEntry, PalFmt, R, G, B, A);

							int32 Idx = (PY * W + PX) * 4;
							Dst[Idx + 0] = R;
							Dst[Idx + 1] = G;
							Dst[Idx + 2] = B;
							Dst[Idx + 3] = A;
						}
					}
				}
			}
		}
	}
}

// ----------------------------------------------------------------------------
// CI8 — 8-bit paletted, tile 8x4
// ----------------------------------------------------------------------------

void FBTILoader::DecodeCI8(const uint8* Src, const uint8* Pal, uint8 PalFmt,
	uint8* Dst, uint16 W, uint16 H)
{
	constexpr int32 TileW = 8;
	constexpr int32 TileH = 4;
	int32 SrcPos = 0;

	for (int32 TileY = 0; TileY < H; TileY += TileH)
	{
		for (int32 TileX = 0; TileX < W; TileX += TileW)
		{
			for (int32 PixY = 0; PixY < TileH; PixY++)
			{
				for (int32 PixX = 0; PixX < TileW; PixX++)
				{
					uint8 Index = Src[SrcPos++];

					int32 PX = TileX + PixX;
					int32 PY = TileY + PixY;

					if (PX < W && PY < H && Pal)
					{
						uint16 PalEntry = (static_cast<uint16>(Pal[Index * 2]) << 8)
						                |  static_cast<uint16>(Pal[Index * 2 + 1]);
						uint8 R, G, B, A;
						DecodePaletteColor(PalEntry, PalFmt, R, G, B, A);

						int32 Idx = (PY * W + PX) * 4;
						Dst[Idx + 0] = R;
						Dst[Idx + 1] = G;
						Dst[Idx + 2] = B;
						Dst[Idx + 3] = A;
					}
				}
			}
		}
	}
}

// ----------------------------------------------------------------------------
// CreateTexture — build UTexture2D from RGBA8 data
// ----------------------------------------------------------------------------

UTexture2D* FBTILoader::CreateTexture(UObject* Outer, const FString& Name,
	uint16 Width, uint16 Height, const TArray<uint8>& RGBA8Data,
	uint8 WrapS, uint8 WrapT)
{
	if (!Outer || Width == 0 || Height == 0 || RGBA8Data.Num() < Width * Height * 4)
	{
		UE_LOG(LogTemp, Error, TEXT("FBTILoader::CreateTexture: Invalid parameters (W=%d H=%d DataSize=%d)"),
			Width, Height, RGBA8Data.Num());
		return nullptr;
	}

	UPackage* Package = Outer->GetOutermost();
	UTexture2D* Texture = NewObject<UTexture2D>(Package, *Name, RF_Public | RF_Standalone);

	FTexturePlatformData* PlatformData = new FTexturePlatformData();
	PlatformData->SizeX = Width;
	PlatformData->SizeY = Height;
	PlatformData->PixelFormat = PF_R8G8B8A8;

	FTexture2DMipMap* Mip = new FTexture2DMipMap();
	Mip->SizeX = Width;
	Mip->SizeY = Height;
	Mip->BulkData.Lock(LOCK_READ_WRITE);
	void* MipData = Mip->BulkData.Realloc(Width * Height * 4);
	FMemory::Memcpy(MipData, RGBA8Data.GetData(), Width * Height * 4);
	Mip->BulkData.Unlock();
	PlatformData->Mips.Add(Mip);

	Texture->SetPlatformData(PlatformData);

	// Wrap modes: 0=Clamp, 1=Repeat, 2=Mirror
	Texture->AddressX = (WrapS == 1) ? TA_Wrap : ((WrapS == 2) ? TA_Mirror : TA_Clamp);
	Texture->AddressY = (WrapT == 1) ? TA_Wrap : ((WrapT == 2) ? TA_Mirror : TA_Clamp);
	Texture->SRGB = true;
	Texture->UpdateResource();

	return Texture;
}
