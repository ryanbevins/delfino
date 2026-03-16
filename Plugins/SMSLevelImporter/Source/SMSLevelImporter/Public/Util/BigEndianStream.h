// BigEndianStream.h - Stream reader for big-endian (GameCube) binary data

#pragma once

#include "CoreMinimal.h"

/**
 * A stream reader that wraps a raw byte buffer and reads big-endian values,
 * converting them to native (little-endian) byte order on PC.
 *
 * All GameCube (SMS) binary formats store data in big-endian order.
 * This class is the foundational I/O utility used by every format parser
 * in the SMS Level Importer plugin (ISO, RARC, BMD, COL, BTK, etc.).
 */
class SMSLEVELIMPORTER_API FBigEndianStream
{
public:
	/** Construct from a TArray of bytes (does not copy; caller must keep array alive). */
	explicit FBigEndianStream(const TArray<uint8>& InData);

	/** Construct from a raw pointer and size (does not copy; caller must keep data alive). */
	FBigEndianStream(const uint8* InData, int64 InSize);

	// ---- Primitive reads (big-endian to native) ----

	uint8  ReadU8();
	int8   ReadS8();
	uint16 ReadU16();
	int16  ReadS16();
	uint32 ReadU32();
	int32  ReadS32();
	float  ReadF32();

	// ---- Bulk reads ----

	/** Read a fixed-length ASCII string (Length bytes). Trailing nulls are trimmed. */
	FString ReadString(int32 Length);

	/** Read bytes until a null terminator is found (the null is consumed but not included). */
	FString ReadNullTerminatedString();

	/** Read Count bytes into a caller-provided buffer. */
	void ReadBytes(uint8* Dest, int64 Count);

	/** Read Count bytes and return them as a TArray. */
	TArray<uint8> ReadBytes(int64 Count);

	// ---- Navigation ----

	/** Return the current read position. */
	int64 Tell() const;

	/** Set the read position to an absolute offset. */
	void Seek(int64 InPosition);

	/** Advance the read position by the given number of bytes. */
	void Skip(int64 Bytes);

	/** Return true if the read position is at or past the end of the data. */
	bool IsEOF() const;

	/** Return the total size of the underlying data. */
	int64 Size() const;

	// ---- Sub-stream ----

	/**
	 * Create a sub-stream that is a view into a region of this stream's data.
	 * Does NOT copy data; the original buffer must remain valid.
	 */
	FBigEndianStream SubStream(int64 Offset, int64 Length) const;

private:
	/** Check that at least BytesNeeded bytes remain; logs an error if not. */
	bool CheckBounds(int64 BytesNeeded) const;

	const uint8* Data;
	int64 DataSize;
	int64 Pos;
	mutable bool bOverflowLogged = false;
};
