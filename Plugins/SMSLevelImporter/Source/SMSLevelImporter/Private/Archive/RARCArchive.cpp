// Copyright ryana. All Rights Reserved.

#include "Archive/RARCArchive.h"
#include "ISO/YAZ0Decoder.h"
#include "Util/BigEndianStream.h"
#include "SMSLevelImporterModule.h"

// ---- Internal structures matching the on-disk RARC layout ----

namespace
{
	/** RARC magic signature: 'RARC' = 0x52415243 */
	static constexpr uint32 RARCMagic = 0x52415243;

	/** Minimum size for a valid RARC archive (SArcHeader + SArcDataInfo). */
	static constexpr int32 MinArchiveSize = 0x40;

	struct FRARCHeader
	{
		uint32 Signature;       // 0x00: 'RARC'
		uint32 FileLength;      // 0x04: total archive size
		uint32 HeaderLength;    // 0x08: usually 0x20
		uint32 FileDataOffset;  // 0x0C: from end of SArcHeader to file data region
		uint32 FileDataLength;  // 0x10
		// 0x14-0x1F: reserved
	};

	struct FRARCDataInfo
	{
		uint32 NumNodes;           // 0x00
		uint32 NodeOffset;         // 0x04 (relative to 0x20)
		uint32 NumFileEntries;     // 0x08
		uint32 FileEntryOffset;    // 0x0C (relative to 0x20)
		uint32 StringTableLength;  // 0x10
		uint32 StringTableOffset;  // 0x14 (relative to 0x20)
		// 0x18-0x1F: padding/misc
	};

	struct FRARCDirNode
	{
		uint32 Type;             // 0x00: 4-char code (e.g., 'ROOT')
		uint32 NameOffset;       // 0x04: into string table
		uint16 Hash;             // 0x08
		uint16 NumEntries;       // 0x0A: files + subdirs in this directory
		uint32 FirstEntryIndex;  // 0x0C
	};

	struct FRARCRawFileEntry
	{
		uint16 FileID;           // 0x00
		uint16 Hash;             // 0x02
		uint32 FlagsAndName;     // 0x04: upper 8 bits = flags, lower 24 = name offset
		uint32 DataOffset;       // 0x08
		uint32 Size;             // 0x0C
		// 0x10: 4 bytes padding
	};

	/** Read a null-terminated string from the string table at the given offset. */
	FString ReadStringFromTable(const uint8* StringTable, int32 TableLength, int32 Offset)
	{
		if (Offset < 0 || Offset >= TableLength)
		{
			return FString();
		}

		const char* Start = reinterpret_cast<const char*>(StringTable + Offset);
		int32 MaxLen = TableLength - Offset;
		int32 Len = 0;
		while (Len < MaxLen && Start[Len] != '\0')
		{
			Len++;
		}

		return FString(Len, Start);
	}
}

// ---- FRARCArchive implementation ----

bool FRARCArchive::Parse(const TArray<uint8>& Data)
{
	Entries.Empty();
	PathToIndex.Empty();
	FileDataStart = 0;

	if (Data.Num() < MinArchiveSize)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("RARCArchive: Data too small (%d bytes, need at least %d)."), Data.Num(), MinArchiveSize);
		return false;
	}

	// Keep a copy of the raw data so GetFile can extract bytes later.
	RawData = Data;

	FBigEndianStream Stream(RawData);

	// ---- 1. Read and validate SArcHeader (0x00-0x1F) ----

	FRARCHeader Header;
	Header.Signature      = Stream.ReadU32();
	Header.FileLength     = Stream.ReadU32();
	Header.HeaderLength   = Stream.ReadU32();
	Header.FileDataOffset = Stream.ReadU32();
	Header.FileDataLength = Stream.ReadU32();
	Stream.Skip(12); // reserved bytes 0x14-0x1F

	if (Header.Signature != RARCMagic)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("RARCArchive: Invalid magic 0x%08X (expected 0x%08X)."), Header.Signature, RARCMagic);
		return false;
	}

	// File data region starts at header_length + file_data_offset (= 0x20 + file_data_offset)
	FileDataStart = 0x20 + Header.FileDataOffset;

	// ---- 2. Read SArcDataInfo (0x20-0x3F) ----

	Stream.Seek(0x20);
	FRARCDataInfo DataInfo;
	DataInfo.NumNodes          = Stream.ReadU32();
	DataInfo.NodeOffset        = Stream.ReadU32();
	DataInfo.NumFileEntries    = Stream.ReadU32();
	DataInfo.FileEntryOffset   = Stream.ReadU32();
	DataInfo.StringTableLength = Stream.ReadU32();
	DataInfo.StringTableOffset = Stream.ReadU32();
	Stream.Skip(8); // 0x38-0x3F

	// Compute absolute offsets (all relative to 0x20)
	const uint32 NodeAbsOffset        = 0x20 + DataInfo.NodeOffset;
	const uint32 FileEntryAbsOffset   = 0x20 + DataInfo.FileEntryOffset;
	const uint32 StringTableAbsOffset = 0x20 + DataInfo.StringTableOffset;

	// Bounds checks
	if (static_cast<int64>(NodeAbsOffset) + static_cast<int64>(DataInfo.NumNodes) * 0x10 > RawData.Num())
	{
		UE_LOG(LogSMSImporter, Error, TEXT("RARCArchive: Directory nodes extend beyond archive data."));
		return false;
	}
	if (static_cast<int64>(FileEntryAbsOffset) + static_cast<int64>(DataInfo.NumFileEntries) * 0x14 > RawData.Num())
	{
		UE_LOG(LogSMSImporter, Error, TEXT("RARCArchive: File entries extend beyond archive data."));
		return false;
	}
	if (static_cast<int64>(StringTableAbsOffset) + DataInfo.StringTableLength > RawData.Num())
	{
		UE_LOG(LogSMSImporter, Error, TEXT("RARCArchive: String table extends beyond archive data."));
		return false;
	}

	const uint8* StringTable = RawData.GetData() + StringTableAbsOffset;
	const int32 StringTableLen = static_cast<int32>(DataInfo.StringTableLength);

	// ---- 3. Read directory nodes ----

	TArray<FRARCDirNode> Nodes;
	Nodes.SetNum(DataInfo.NumNodes);

	Stream.Seek(NodeAbsOffset);
	for (uint32 i = 0; i < DataInfo.NumNodes; ++i)
	{
		Nodes[i].Type            = Stream.ReadU32();
		Nodes[i].NameOffset      = Stream.ReadU32();
		Nodes[i].Hash            = Stream.ReadU16();
		Nodes[i].NumEntries      = Stream.ReadU16();
		Nodes[i].FirstEntryIndex = Stream.ReadU32();
	}

	// ---- 4. Read all raw file entries ----

	TArray<FRARCRawFileEntry> RawEntries;
	RawEntries.SetNum(DataInfo.NumFileEntries);

	Stream.Seek(FileEntryAbsOffset);
	for (uint32 i = 0; i < DataInfo.NumFileEntries; ++i)
	{
		RawEntries[i].FileID       = Stream.ReadU16();
		RawEntries[i].Hash         = Stream.ReadU16();
		RawEntries[i].FlagsAndName = Stream.ReadU32();
		RawEntries[i].DataOffset   = Stream.ReadU32();
		RawEntries[i].Size         = Stream.ReadU32();
		Stream.Skip(4); // runtime pointer padding
	}

	// ---- 5. Build full paths by traversing directory tree ----

	// For each directory node, resolve its name and build paths for contained entries.
	// We use a stack-based DFS starting from node 0 (ROOT).

	struct FDirStackEntry
	{
		uint32 NodeIndex;
		FString ParentPath;
	};

	TArray<FDirStackEntry> Stack;
	if (DataInfo.NumNodes > 0)
	{
		FString RootName = ReadStringFromTable(StringTable, StringTableLen, Nodes[0].NameOffset);
		Stack.Add({0, TEXT("/") + RootName});
	}

	while (Stack.Num() > 0)
	{
		FDirStackEntry Current = Stack.Pop();
		const FRARCDirNode& Node = Nodes[Current.NodeIndex];

		for (uint32 i = 0; i < Node.NumEntries; ++i)
		{
			const uint32 EntryIdx = Node.FirstEntryIndex + i;
			if (EntryIdx >= DataInfo.NumFileEntries)
			{
				UE_LOG(LogSMSImporter, Warning, TEXT("RARCArchive: File entry index %u out of bounds."), EntryIdx);
				continue;
			}

			const FRARCRawFileEntry& Raw = RawEntries[EntryIdx];

			const uint32 NameOffset = Raw.FlagsAndName & 0x00FFFFFF;
			const uint8 Flags = static_cast<uint8>(Raw.FlagsAndName >> 24);
			const bool bIsDir = (Flags & 0x02) != 0;

			FString EntryName = ReadStringFromTable(StringTable, StringTableLen, NameOffset);

			// Skip "." and ".." entries
			if (EntryName == TEXT(".") || EntryName == TEXT(".."))
			{
				continue;
			}

			FString FullPath = Current.ParentPath / EntryName;

			if (bIsDir)
			{
				// DataOffset for directories is the node index
				const uint32 ChildNodeIdx = Raw.DataOffset;
				if (ChildNodeIdx < DataInfo.NumNodes)
				{
					Stack.Add({ChildNodeIdx, FullPath});
				}
				else
				{
					UE_LOG(LogSMSImporter, Warning, TEXT("RARCArchive: Directory '%s' points to invalid node index %u."), *FullPath, ChildNodeIdx);
				}
			}
			else
			{
				// It's a file — add to our entries list
				FRARCFileEntry Entry;
				Entry.Name         = EntryName;
				Entry.FullPath     = FullPath;
				Entry.FileID       = Raw.FileID;
				Entry.Hash         = Raw.Hash;
				Entry.Flags        = Flags;
				Entry.DataOffset   = Raw.DataOffset;
				Entry.Size         = Raw.Size;
				Entry.bIsDirectory = false;

				const int32 Idx = Entries.Num();
				Entries.Add(MoveTemp(Entry));

				// Store in PathToIndex map (case-insensitive: lowercase key)
				PathToIndex.Add(FullPath.ToLower(), Idx);
			}
		}
	}

	UE_LOG(LogSMSImporter, Log, TEXT("RARCArchive: Parsed %d files from %d directory nodes."), Entries.Num(), DataInfo.NumNodes);
	return true;
}

TArray<uint8> FRARCArchive::GetFile(const FString& Path) const
{
	const FString Key = Path.ToLower();
	const int32* IdxPtr = PathToIndex.Find(Key);

	if (!IdxPtr)
	{
		UE_LOG(LogSMSImporter, Warning, TEXT("RARCArchive: File not found: '%s'"), *Path);
		return TArray<uint8>();
	}

	const FRARCFileEntry& Entry = Entries[*IdxPtr];

	const int64 AbsOffset = static_cast<int64>(FileDataStart) + Entry.DataOffset;
	const int64 EndOffset = AbsOffset + Entry.Size;

	if (EndOffset > RawData.Num())
	{
		UE_LOG(LogSMSImporter, Error, TEXT("RARCArchive: File '%s' data (offset %lld, size %u) extends beyond archive."), *Path, AbsOffset, Entry.Size);
		return TArray<uint8>();
	}

	TArray<uint8> FileData;
	FileData.SetNumUninitialized(Entry.Size);
	FMemory::Memcpy(FileData.GetData(), RawData.GetData() + AbsOffset, Entry.Size);

	// If flagged as YAZ0 compressed within the archive, auto-decompress
	if ((Entry.Flags & 0x80) != 0 && FYAZ0Decoder::IsYAZ0(FileData))
	{
		TArray<uint8> Decompressed = FYAZ0Decoder::Decode(FileData);
		if (Decompressed.Num() == 0)
		{
			UE_LOG(LogSMSImporter, Error, TEXT("RARCArchive: Failed to YAZ0-decompress file '%s'."), *Path);
			return TArray<uint8>();
		}
		return Decompressed;
	}

	return FileData;
}

bool FRARCArchive::FileExists(const FString& Path) const
{
	return PathToIndex.Contains(Path.ToLower());
}

TArray<FString> FRARCArchive::ListFiles() const
{
	TArray<FString> Result;
	Result.Reserve(Entries.Num());

	for (const FRARCFileEntry& Entry : Entries)
	{
		Result.Add(Entry.FullPath);
	}

	return Result;
}

TArray<FString> FRARCArchive::FindFiles(const FString& Extension) const
{
	// Normalize: ensure the pattern starts with a dot for comparison
	FString Ext = Extension;
	if (Ext.StartsWith(TEXT("*")))
	{
		Ext = Ext.Mid(1); // Strip leading '*' so "*.bmd" becomes ".bmd"
	}
	if (!Ext.StartsWith(TEXT(".")))
	{
		Ext = TEXT(".") + Ext; // Ensure ".bmd" form
	}

	TArray<FString> Result;

	for (const FRARCFileEntry& Entry : Entries)
	{
		if (Entry.FullPath.EndsWith(Ext, ESearchCase::IgnoreCase))
		{
			Result.Add(Entry.FullPath);
		}
	}

	return Result;
}
