// BigEndianStream.cpp - Stream reader for big-endian (GameCube) binary data

#include "Util/BigEndianStream.h"

FBigEndianStream::FBigEndianStream(const TArray<uint8>& InData)
	: Data(InData.GetData())
	, DataSize(InData.Num())
	, Pos(0)
{
}

FBigEndianStream::FBigEndianStream(const uint8* InData, int64 InSize)
	: Data(InData)
	, DataSize(InSize)
	, Pos(0)
{
}

// ----------------------------------------------------------------------------
// Bounds checking
// ----------------------------------------------------------------------------

bool FBigEndianStream::CheckBounds(int64 BytesNeeded) const
{
	if (Pos + BytesNeeded > DataSize)
	{
		if (!bOverflowLogged)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("FBigEndianStream: Out-of-bounds read at offset %lld (need %lld bytes, size %lld)"),
				Pos, BytesNeeded, DataSize);
			bOverflowLogged = true;
		}
		return false;
	}
	return true;
}

// ----------------------------------------------------------------------------
// Primitive reads
// ----------------------------------------------------------------------------

uint8 FBigEndianStream::ReadU8()
{
	if (!CheckBounds(1)) return 0;
	return Data[Pos++];
}

int8 FBigEndianStream::ReadS8()
{
	return static_cast<int8>(ReadU8());
}

uint16 FBigEndianStream::ReadU16()
{
	if (!CheckBounds(2)) return 0;
	uint16 Value = (static_cast<uint16>(Data[Pos]) << 8)
	             |  static_cast<uint16>(Data[Pos + 1]);
	Pos += 2;
	return Value;
}

int16 FBigEndianStream::ReadS16()
{
	return static_cast<int16>(ReadU16());
}

uint32 FBigEndianStream::ReadU32()
{
	if (!CheckBounds(4)) return 0;
	uint32 Value = (static_cast<uint32>(Data[Pos])     << 24)
	             | (static_cast<uint32>(Data[Pos + 1]) << 16)
	             | (static_cast<uint32>(Data[Pos + 2]) << 8)
	             |  static_cast<uint32>(Data[Pos + 3]);
	Pos += 4;
	return Value;
}

int32 FBigEndianStream::ReadS32()
{
	return static_cast<int32>(ReadU32());
}

float FBigEndianStream::ReadF32()
{
	uint32 Bits = ReadU32();
	float Value;
	FMemory::Memcpy(&Value, &Bits, 4);
	return Value;
}

// ----------------------------------------------------------------------------
// Bulk reads
// ----------------------------------------------------------------------------

FString FBigEndianStream::ReadString(int32 Length)
{
	if (!CheckBounds(Length)) return FString();

	// Build an ANSI string, then convert
	TArray<char> Buf;
	Buf.SetNumUninitialized(Length + 1);
	FMemory::Memcpy(Buf.GetData(), Data + Pos, Length);
	Buf[Length] = '\0';
	Pos += Length;

	// Trim trailing nulls
	int32 ActualLen = Length;
	while (ActualLen > 0 && Buf[ActualLen - 1] == '\0')
	{
		--ActualLen;
	}
	Buf[ActualLen] = '\0';

	return FString(ANSI_TO_TCHAR(Buf.GetData()));
}

FString FBigEndianStream::ReadNullTerminatedString()
{
	TArray<char> Buf;
	while (Pos < DataSize)
	{
		uint8 Byte = Data[Pos++];
		if (Byte == 0)
		{
			break;
		}
		Buf.Add(static_cast<char>(Byte));
	}
	Buf.Add('\0');
	return FString(ANSI_TO_TCHAR(Buf.GetData()));
}

void FBigEndianStream::ReadBytes(uint8* Dest, int64 Count)
{
	if (!CheckBounds(Count))
	{
		if (Dest)
		{
			FMemory::Memzero(Dest, Count);
		}
		return;
	}
	FMemory::Memcpy(Dest, Data + Pos, Count);
	Pos += Count;
}

TArray<uint8> FBigEndianStream::ReadBytes(int64 Count)
{
	TArray<uint8> Result;
	if (!CheckBounds(Count)) return Result;

	Result.SetNumUninitialized(Count);
	FMemory::Memcpy(Result.GetData(), Data + Pos, Count);
	Pos += Count;
	return Result;
}

// ----------------------------------------------------------------------------
// Navigation
// ----------------------------------------------------------------------------

int64 FBigEndianStream::Tell() const
{
	return Pos;
}

void FBigEndianStream::Seek(int64 InPosition)
{
	if (InPosition < 0 || InPosition > DataSize)
	{
		UE_LOG(LogTemp, Error,
			TEXT("FBigEndianStream: Seek to invalid position %lld (size %lld)"),
			InPosition, DataSize);
		return;
	}
	Pos = InPosition;
}

void FBigEndianStream::Skip(int64 Bytes)
{
	Seek(Pos + Bytes);
}

bool FBigEndianStream::IsEOF() const
{
	return Pos >= DataSize;
}

int64 FBigEndianStream::Size() const
{
	return DataSize;
}

// ----------------------------------------------------------------------------
// Sub-stream
// ----------------------------------------------------------------------------

FBigEndianStream FBigEndianStream::SubStream(int64 Offset, int64 Length) const
{
	if (Offset < 0 || Length < 0 || Offset + Length > DataSize)
	{
		UE_LOG(LogTemp, Error,
			TEXT("FBigEndianStream: SubStream out of range (offset %lld, length %lld, size %lld)"),
			Offset, Length, DataSize);
		return FBigEndianStream(nullptr, 0);
	}
	return FBigEndianStream(Data + Offset, Length);
}
