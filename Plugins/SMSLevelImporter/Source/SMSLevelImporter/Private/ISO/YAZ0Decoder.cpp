// Copyright ryana. All Rights Reserved.

#include "ISO/YAZ0Decoder.h"
#include "SMSLevelImporterModule.h"

bool FYAZ0Decoder::IsYAZ0(const TArray<uint8>& Data)
{
	if (Data.Num() < HeaderSize)
	{
		return false;
	}

	return Data[0] == 'Y'
		&& Data[1] == 'a'
		&& Data[2] == 'z'
		&& Data[3] == '0';
}

uint32 FYAZ0Decoder::GetDecompressedSize(const TArray<uint8>& Data)
{
	if (Data.Num() < HeaderSize)
	{
		return 0;
	}

	// Big-endian u32 at offset 0x04
	return (static_cast<uint32>(Data[4]) << 24)
		 | (static_cast<uint32>(Data[5]) << 16)
		 | (static_cast<uint32>(Data[6]) << 8)
		 | (static_cast<uint32>(Data[7]));
}

TArray<uint8> FYAZ0Decoder::Decode(const TArray<uint8>& Data)
{
	if (!IsYAZ0(Data))
	{
		UE_LOG(LogSMSImporter, Error, TEXT("YAZ0Decoder: Data does not have valid YAZ0 header."));
		return TArray<uint8>();
	}

	const uint32 DecompressedSize = GetDecompressedSize(Data);
	if (DecompressedSize == 0)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("YAZ0Decoder: Decompressed size is 0."));
		return TArray<uint8>();
	}

	if (DecompressedSize > MaxDecompressedSize)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("YAZ0Decoder: Decompressed size %u exceeds maximum allowed size."), DecompressedSize);
		return TArray<uint8>();
	}

	const int32 SrcSize = Data.Num();
	const uint8* Src = Data.GetData();

	TArray<uint8> Dst;
	Dst.SetNumZeroed(DecompressedSize);

	int32 SrcPos = HeaderSize;
	int32 DstPos = 0;
	int32 ChunkBitsLeft = 0;
	uint8 ChunkBits = 0;

	while (DstPos < static_cast<int32>(DecompressedSize))
	{
		// Read a new flag byte when we've exhausted the current one
		if (ChunkBitsLeft == 0)
		{
			if (SrcPos >= SrcSize)
			{
				UE_LOG(LogSMSImporter, Error, TEXT("YAZ0Decoder: Source position %d out of bounds (size %d) while reading flag byte."), SrcPos, SrcSize);
				return TArray<uint8>();
			}
			ChunkBits = Src[SrcPos++];
			ChunkBitsLeft = 8;
		}

		if ((ChunkBits & 0x80) != 0)
		{
			// Bit is 1: literal copy — copy 1 byte from src to dst
			if (SrcPos >= SrcSize)
			{
				UE_LOG(LogSMSImporter, Error, TEXT("YAZ0Decoder: Source position %d out of bounds (size %d) during literal copy."), SrcPos, SrcSize);
				return TArray<uint8>();
			}
			if (DstPos >= static_cast<int32>(DecompressedSize))
			{
				UE_LOG(LogSMSImporter, Error, TEXT("YAZ0Decoder: Destination position %d out of bounds (size %u) during literal copy."), DstPos, DecompressedSize);
				return TArray<uint8>();
			}
			Dst[DstPos++] = Src[SrcPos++];
		}
		else
		{
			// Bit is 0: back-reference
			if (SrcPos + 1 >= SrcSize)
			{
				UE_LOG(LogSMSImporter, Error, TEXT("YAZ0Decoder: Source position %d out of bounds (size %d) during back-reference read."), SrcPos, SrcSize);
				return TArray<uint8>();
			}

			const uint8 Byte0 = Src[SrcPos++];
			const uint8 Byte1 = Src[SrcPos++];

			const int32 Distance = ((Byte0 & 0x0F) << 8) | Byte1;
			int32 Length = Byte0 >> 4;

			if (Length == 0)
			{
				// Extended length: read one more byte
				if (SrcPos >= SrcSize)
				{
					UE_LOG(LogSMSImporter, Error, TEXT("YAZ0Decoder: Source position %d out of bounds (size %d) during extended length read."), SrcPos, SrcSize);
					return TArray<uint8>();
				}
				Length = Src[SrcPos++] + 0x12;
			}
			else
			{
				Length += 2;
			}

			const int32 CopyFrom = DstPos - Distance - 1;
			if (CopyFrom < 0)
			{
				UE_LOG(LogSMSImporter, Error, TEXT("YAZ0Decoder: Back-reference points before start of output (copyFrom=%d)."), CopyFrom);
				return TArray<uint8>();
			}

			if (DstPos + Length > static_cast<int32>(DecompressedSize))
			{
				UE_LOG(LogSMSImporter, Error, TEXT("YAZ0Decoder: Back-reference copy would exceed output buffer (dstPos=%d, length=%d, decompSize=%u)."), DstPos, Length, DecompressedSize);
				return TArray<uint8>();
			}

			// Byte-by-byte copy to handle overlapping references (RLE-like behavior)
			for (int32 i = 0; i < Length; ++i)
			{
				Dst[DstPos] = Dst[CopyFrom + i];
				DstPos++;
			}
		}

		ChunkBits <<= 1;
		ChunkBitsLeft--;
	}

	return Dst;
}
