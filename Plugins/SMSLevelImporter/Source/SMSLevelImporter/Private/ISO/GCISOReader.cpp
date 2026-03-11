// Copyright ryana. All Rights Reserved.

#include "ISO/GCISOReader.h"

DEFINE_LOG_CATEGORY_STATIC(LogGCISO, Log, All);

/** Read a big-endian uint32 from a byte buffer at the given offset. */
static uint32 ReadBE32(const uint8* Data, int32 Offset)
{
	return (static_cast<uint32>(Data[Offset]) << 24)
		 | (static_cast<uint32>(Data[Offset + 1]) << 16)
		 | (static_cast<uint32>(Data[Offset + 2]) << 8)
		 | (static_cast<uint32>(Data[Offset + 3]));
}

/** Read a big-endian uint24 from bytes 1-3 of a 4-byte group (masking out byte 0). */
static uint32 ReadBE24(const uint8* Data, int32 Offset)
{
	return (static_cast<uint32>(Data[Offset]) << 16)
		 | (static_cast<uint32>(Data[Offset + 1]) << 8)
		 | (static_cast<uint32>(Data[Offset + 2]));
}

FGCISOReader::~FGCISOReader()
{
	Close();
}

bool FGCISOReader::Open(const FString& ISOPath)
{
	Close();

	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();
	FileHandle = PlatformFile.OpenRead(*ISOPath);
	if (!FileHandle)
	{
		UE_LOG(LogGCISO, Error, TEXT("Failed to open ISO file: %s"), *ISOPath);
		return false;
	}

	// Read game code (4 bytes at offset 0x0000)
	uint8 GameCodeBytes[4];
	if (!FileHandle->Seek(0x0000))
	{
		UE_LOG(LogGCISO, Error, TEXT("Failed to seek to disc header"));
		Close();
		return false;
	}
	if (!FileHandle->Read(GameCodeBytes, 4))
	{
		UE_LOG(LogGCISO, Error, TEXT("Failed to read game code"));
		Close();
		return false;
	}
	GameCode = FString(4, reinterpret_cast<const char*>(GameCodeBytes));

	// Read FST position and length from DVDBB2 (offset 0x0420)
	// DVDBB2 starts at 0x0420. FSTPosition is at offset 0x04 within it = 0x0424
	// FSTLength is at offset 0x08 within it = 0x0428
	uint8 BB2Bytes[8];
	if (!FileHandle->Seek(0x0424))
	{
		UE_LOG(LogGCISO, Error, TEXT("Failed to seek to DVDBB2"));
		Close();
		return false;
	}
	if (!FileHandle->Read(BB2Bytes, 8))
	{
		UE_LOG(LogGCISO, Error, TEXT("Failed to read DVDBB2"));
		Close();
		return false;
	}
	FSTPosition = ReadBE32(BB2Bytes, 0);
	FSTLength = ReadBE32(BB2Bytes, 4);

	if (FSTPosition == 0 || FSTLength == 0)
	{
		UE_LOG(LogGCISO, Error, TEXT("Invalid FST position (%u) or length (%u)"), FSTPosition, FSTLength);
		Close();
		return false;
	}

	// Read entire FST into memory
	FSTData.SetNumUninitialized(FSTLength);
	if (!FileHandle->Seek(FSTPosition))
	{
		UE_LOG(LogGCISO, Error, TEXT("Failed to seek to FST at offset 0x%08X"), FSTPosition);
		Close();
		return false;
	}
	if (!FileHandle->Read(FSTData.GetData(), FSTLength))
	{
		UE_LOG(LogGCISO, Error, TEXT("Failed to read FST (%u bytes)"), FSTLength);
		Close();
		return false;
	}

	// Parse FST entries and build paths
	if (!ParseFST())
	{
		UE_LOG(LogGCISO, Error, TEXT("Failed to parse FST"));
		Close();
		return false;
	}

	UE_LOG(LogGCISO, Log, TEXT("Opened GC ISO: GameCode=%s, FST at 0x%08X (%u bytes), %d entries"),
		*GameCode, FSTPosition, FSTLength, FST.Num());

	return true;
}

void FGCISOReader::Close()
{
	if (FileHandle)
	{
		delete FileHandle;
		FileHandle = nullptr;
	}
	GameCode.Empty();
	FST.Empty();
	PathToIndex.Empty();
	FSTData.Empty();
	FSTPosition = 0;
	FSTLength = 0;
}

bool FGCISOReader::IsSMS() const
{
	return GameCode.StartsWith(TEXT("GMS"));
}

FString FGCISOReader::GetRegion() const
{
	if (GameCode.Len() < 4)
	{
		return TEXT("Unknown");
	}

	const TCHAR RegionChar = GameCode[3];
	switch (RegionChar)
	{
	case TEXT('J'): return TEXT("JP");
	case TEXT('E'): return TEXT("US");
	case TEXT('P'): return TEXT("EU");
	case TEXT('K'): return TEXT("KR");
	default:        return TEXT("Unknown");
	}
}

TArray<FString> FGCISOReader::ListFiles() const
{
	TArray<FString> Files;
	for (const auto& Pair : PathToIndex)
	{
		const FGCFileEntry& Entry = FST[Pair.Value];
		if (!Entry.bIsDirectory)
		{
			Files.Add(Pair.Key);
		}
	}
	Files.Sort();
	return Files;
}

TArray<uint8> FGCISOReader::ReadFile(const FString& Path) const
{
	const int32* IndexPtr = PathToIndex.Find(Path);
	if (!IndexPtr)
	{
		UE_LOG(LogGCISO, Warning, TEXT("File not found in ISO: %s"), *Path);
		return TArray<uint8>();
	}

	const FGCFileEntry& Entry = FST[*IndexPtr];
	if (Entry.bIsDirectory)
	{
		UE_LOG(LogGCISO, Warning, TEXT("Path is a directory, not a file: %s"), *Path);
		return TArray<uint8>();
	}

	return ReadRawBytes(Entry.Offset, Entry.Size);
}

bool FGCISOReader::FileExists(const FString& Path) const
{
	const int32* IndexPtr = PathToIndex.Find(Path);
	if (!IndexPtr)
	{
		return false;
	}
	return !FST[*IndexPtr].bIsDirectory;
}

bool FGCISOReader::ParseFST()
{
	if (FSTData.Num() < 12)
	{
		UE_LOG(LogGCISO, Error, TEXT("FST data too small (%d bytes)"), FSTData.Num());
		return false;
	}

	const uint8* Data = FSTData.GetData();

	// Root entry (index 0) is always a directory.
	// Its bytes 8-11 give the total entry count.
	const uint8 RootFlags = Data[0];
	if (RootFlags != 0x01)
	{
		UE_LOG(LogGCISO, Error, TEXT("Root FST entry is not a directory (flags=0x%02X)"), RootFlags);
		return false;
	}

	const uint32 TotalEntries = ReadBE32(Data, 8);
	const uint32 ExpectedMinSize = TotalEntries * 12;
	if (ExpectedMinSize > static_cast<uint32>(FSTData.Num()))
	{
		UE_LOG(LogGCISO, Error, TEXT("FST claims %u entries but data is only %d bytes"), TotalEntries, FSTData.Num());
		return false;
	}

	// String table starts right after the entry array
	const uint32 StringTableOffset = TotalEntries * 12;
	const uint8* StringTable = Data + StringTableOffset;
	const uint32 StringTableSize = FSTData.Num() - StringTableOffset;

	FST.SetNum(TotalEntries);

	for (uint32 i = 0; i < TotalEntries; ++i)
	{
		const uint32 EntryOffset = i * 12;
		const uint8 Flags = Data[EntryOffset];
		const uint32 NameOffset = ReadBE24(Data + EntryOffset, 1);

		FGCFileEntry& Entry = FST[i];
		Entry.bIsDirectory = (Flags == 0x01);

		// Read name from string table (null-terminated)
		if (i == 0)
		{
			Entry.Name = TEXT("");  // Root has no name
		}
		else if (NameOffset < StringTableSize)
		{
			// Find null terminator
			const char* NameStart = reinterpret_cast<const char*>(StringTable + NameOffset);
			uint32 MaxLen = StringTableSize - NameOffset;
			uint32 NameLen = 0;
			while (NameLen < MaxLen && NameStart[NameLen] != '\0')
			{
				++NameLen;
			}
			Entry.Name = FString(NameLen, NameStart);
		}
		else
		{
			UE_LOG(LogGCISO, Warning, TEXT("FST entry %u has invalid name offset %u (string table size %u)"), i, NameOffset, StringTableSize);
			Entry.Name = FString::Printf(TEXT("_invalid_%u"), i);
		}

		if (Entry.bIsDirectory)
		{
			Entry.ParentIndex = static_cast<int32>(ReadBE32(Data + EntryOffset, 4));
			Entry.Offset = 0;
			Entry.Size = ReadBE32(Data + EntryOffset, 8); // "next" index (one past last child)
		}
		else
		{
			Entry.Offset = ReadBE32(Data + EntryOffset, 4);
			Entry.Size = ReadBE32(Data + EntryOffset, 8);
			Entry.ParentIndex = -1;
		}
	}

	// Build parent-child relationships for directories
	// For each entry i (non-root), find its parent directory.
	// A directory entry's "Size" field stores the index of the next entry after its last child.
	// So entry i belongs to a directory D if D.index < i < D.Size and D is the innermost such directory.
	// We can determine this by scanning: for each non-root entry, its parent is the innermost
	// enclosing directory. We use a stack approach.
	TArray<int32> DirStack;
	DirStack.Add(0); // Root

	for (uint32 i = 1; i < TotalEntries; ++i)
	{
		// Pop directories whose range we've exited
		while (DirStack.Num() > 0)
		{
			const int32 TopIdx = DirStack.Last();
			if (i >= FST[TopIdx].Size && FST[TopIdx].bIsDirectory)
			{
				DirStack.Pop();
			}
			else
			{
				break;
			}
		}

		if (DirStack.Num() == 0)
		{
			UE_LOG(LogGCISO, Error, TEXT("FST entry %u has no parent directory"), i);
			return false;
		}

		const int32 ParentIdx = DirStack.Last();
		FST[ParentIdx].Children.Add(static_cast<int32>(i));

		if (!FST[i].bIsDirectory)
		{
			FST[i].ParentIndex = ParentIdx;
		}

		if (FST[i].bIsDirectory)
		{
			DirStack.Add(static_cast<int32>(i));
		}
	}

	// Build full paths
	BuildPaths(0, TEXT(""));

	return true;
}

void FGCISOReader::BuildPaths(int32 EntryIndex, const FString& ParentPath)
{
	if (EntryIndex < 0 || EntryIndex >= FST.Num())
	{
		return;
	}

	const FGCFileEntry& Entry = FST[EntryIndex];
	FString FullPath;

	if (EntryIndex == 0)
	{
		// Root directory — path is "/"
		FullPath = TEXT("/");
	}
	else
	{
		// Build path with forward slashes explicitly (avoid operator/ which uses platform separator)
		if (ParentPath == TEXT("/"))
		{
			FullPath = TEXT("/") + Entry.Name;
		}
		else
		{
			FullPath = ParentPath + TEXT("/") + Entry.Name;
		}
	}

	PathToIndex.Add(FullPath, EntryIndex);

	// Recurse into children
	if (Entry.bIsDirectory)
	{
		for (int32 ChildIndex : Entry.Children)
		{
			BuildPaths(ChildIndex, FullPath);
		}
	}
}

TArray<uint8> FGCISOReader::ReadRawBytes(uint32 Offset, uint32 Size) const
{
	TArray<uint8> Result;

	if (!FileHandle)
	{
		UE_LOG(LogGCISO, Error, TEXT("Cannot read: file handle is null"));
		return Result;
	}

	if (Size == 0)
	{
		return Result;
	}

	if (!FileHandle->Seek(Offset))
	{
		UE_LOG(LogGCISO, Error, TEXT("Failed to seek to offset 0x%08X"), Offset);
		return Result;
	}

	Result.SetNumUninitialized(Size);
	if (!FileHandle->Read(Result.GetData(), Size))
	{
		UE_LOG(LogGCISO, Error, TEXT("Failed to read %u bytes at offset 0x%08X"), Size, Offset);
		Result.Empty();
		return Result;
	}

	return Result;
}
