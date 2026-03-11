// SMSObjectFactory.cpp - JDrama scene.bin parser and Blueprint actor generation

#include "Scene/SMSObjectFactory.h"
#include "Util/BigEndianStream.h"
#include "SMSLevelImporterModule.h"

#include "Engine/Blueprint.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/BillboardComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphSchema_K2.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

// Maximum recursion depth guard to prevent infinite loops on malformed data
static constexpr int32 MaxParseDepth = 64;

// ============================================================================
// Coordinate conversion
// ============================================================================

FVector FSMSObjectFactory::ConvertPosition(float GCX, float GCY, float GCZ)
{
	// GameCube Y-up to Unreal Z-up: UE.X = GC.X, UE.Y = GC.Z, UE.Z = GC.Y
	return FVector(GCX, GCZ, GCY);
}

FRotator FSMSObjectFactory::ConvertRotation(float GCRotX, float GCRotY, float GCRotZ)
{
	// GC rotation stored as degrees (f32).
	// UE: Pitch=Y, Yaw=Z, Roll=X
	// Axis swap mirrors the position swap, and Y rotation is negated for handedness.
	return FRotator(GCRotX, -GCRotY, GCRotZ);
}

// ============================================================================
// String reader matching JSUInputStream::readString()
// ============================================================================

FString FSMSObjectFactory::ReadPrefixedString(FBigEndianStream& Stream)
{
	if (Stream.IsEOF())
	{
		return FString();
	}

	const uint16 Length = Stream.ReadU16();
	if (Length == 0)
	{
		return FString();
	}

	TArray<uint8> Bytes = Stream.ReadBytes(static_cast<int64>(Length));
	// Convert from ASCII/Shift-JIS to FString (treating as Latin-1 for safety)
	FString Result;
	Result.Reserve(Bytes.Num());
	for (int32 i = 0; i < Bytes.Num(); ++i)
	{
		if (Bytes[i] == 0)
		{
			break; // stop at embedded null
		}
		Result.AppendChar(static_cast<TCHAR>(Bytes[i]));
	}
	return Result;
}

// ============================================================================
// Type classification
// ============================================================================

bool FSMSObjectFactory::IsContainerType(const FString& ClassName)
{
	// Container types that store a child count (s32) followed by child nodes.
	// From JDRNameRefGen.cpp and MarNameRefGen.cpp:
	//   "GroupObj" -> TViewObjPtrListT (has children)
	//   "NameRefGrp" -> TNameRefPtrListT (has children)
	//   "Strategy" -> TStrategy (has children with custom load)
	//   "IdxGroup" -> TIdxGroupObj (has children, plus extra u32)
	//   "SmJ3DScn" -> TSmJ3DScn (extends TViewObjPtrListT, has children)
	return ClassName == TEXT("GroupObj")
		|| ClassName == TEXT("NameRefGrp")
		|| ClassName == TEXT("SmJ3DScn");
}

bool FSMSObjectFactory::IsActorType(const FString& ClassName)
{
	// Types that use TActor::load() (position + rotation + scale + model name + lightmap)
	// or derivatives that call TActor::load() first:
	//   SmJ3DAct, SmplChara, LiveActor, Mario, plus all enemies, map objects, NPCs, etc.
	//
	// From MarNameRefGen.cpp, these all eventually subclass TActor:
	static const TSet<FString> ActorTypes = {
		// JDrama core types
		TEXT("SmJ3DAct"),
		TEXT("SmplChara"),

		// Game actor types from MarNameRefGen
		TEXT("ObjChara"),
		TEXT("LiveActor"),
		TEXT("Mario"),

		// Enemies (from getNameRef_Enemy / getNameRef_BossEnemy)
		TEXT("TStroller"),
		TEXT("TGesso"),
		TEXT("TNameKuri"),
		TEXT("THamukuri"),
		TEXT("TElecNokonoko"),
		TEXT("TBossManta"),
		TEXT("TBossGesso"),
		TEXT("TBossHanachan"),

		// NPCs
		TEXT("TPiantas"),
		TEXT("TNoki"),

		// Map objects (from getNameRef_MapObj)
		TEXT("MapObjGeneral"),
		TEXT("MapObjBase"),
		TEXT("TCoin"),
		TEXT("TCoinRed"),
		TEXT("TCoinBlue"),
		TEXT("TShine"),
		TEXT("TFruit"),
		TEXT("TNozzleBox"),
		TEXT("TWoodBarrel"),
		TEXT("TMapStaticObj"),
		TEXT("TMapObjWaterSpray"),
		TEXT("TPool"),

		// Effect objects
		TEXT("EffectObjManager"),
		TEXT("EffectFire"),

		// Generators
		TEXT("Generator"),
		TEXT("OneShotGenerator"),
	};

	return ActorTypes.Contains(ClassName);
}

bool FSMSObjectFactory::IsPlacementType(const FString& ClassName)
{
	// Types that extend TPlacement (position only, no rotation/scale).
	// PolarCamera reads TPlacement::load() + 2 extra floats.
	static const TSet<FString> PlacementTypes = {
		TEXT("PolarCamera"),
		TEXT("MirrorCamera"),
		TEXT("PolarSubCamera"),
	};

	return PlacementTypes.Contains(ClassName);
}

// ============================================================================
// Binary parsing
// ============================================================================

bool FSMSObjectFactory::ReadGenObjectHeader(FBigEndianStream& Stream,
	FString& OutClassName, int64& OutChunkEndOffset)
{
	// From the SMS decompilation, TNameRef::getType() does:
	//   u32 x = param_1.readU32();                          // chunk size (includes this u32)
	//   param_2.setBuffer(param_1.pos, x - 4);              // sub-stream for remaining chunk
	//   param_1.skip(x - 4);                                // advance past chunk in main stream
	//   u32 len = param_2.readU16();                         // discarded u16 (unknown purpose)
	//   return param_2.readString();                         // u16 length + class name bytes
	//
	// Then genObject() calls getNameRef(className) to create the object,
	// and the object's load() is called with param_2 (sub-stream), which has
	// its position past the class name, so load() reads from there onward.
	//
	// All children of container types are read from the SAME sub-stream,
	// so a single-stream approach works: all child chunks are nested within
	// the parent chunk.

	if (Stream.IsEOF() || Stream.Size() - Stream.Tell() < 4)
	{
		return false;
	}

	const int64 ChunkStart = Stream.Tell();
	const uint32 ChunkSize = Stream.ReadU32();

	if (ChunkSize < 8) // Minimum: 4 (u32 size) + 2 (discarded u16) + 2 (string u16 for len=0)
	{
		UE_LOG(LogSMSImporter, Warning,
			TEXT("scene.bin: Invalid chunk size %u at offset %lld"), ChunkSize, ChunkStart);
		return false;
	}

	// Total consumed = 4 (u32) + (ChunkSize - 4) = ChunkSize bytes
	OutChunkEndOffset = ChunkStart + static_cast<int64>(ChunkSize);

	// Clamp to stream bounds
	if (OutChunkEndOffset > Stream.Size())
	{
		UE_LOG(LogSMSImporter, Warning,
			TEXT("scene.bin: Chunk extends past end of stream (size=%u, offset=%lld, streamSize=%lld)"),
			ChunkSize, ChunkStart, Stream.Size());
		OutChunkEndOffset = Stream.Size();
	}

	// Read the discarded u16 (purpose unknown from decompilation)
	if (Stream.Tell() + 2 > OutChunkEndOffset)
	{
		return false;
	}
	/*uint16 Discarded =*/ Stream.ReadU16();

	// Read class name via readString() pattern: u16 length + string bytes
	OutClassName = ReadPrefixedString(Stream);

	if (OutClassName.IsEmpty())
	{
		UE_LOG(LogSMSImporter, Warning,
			TEXT("scene.bin: Empty class name at offset %lld"), ChunkStart);
		return false;
	}

	return true;
}

bool FSMSObjectFactory::ReadNameRefHeader(FBigEndianStream& Stream,
	FString& OutClassName, FString& OutInstanceName)
{
	// TNameRef::load() reads:
	//   u16 keyCode (hash of the instance name, we just skip it)
	//   u16-prefixed instance name string

	if (Stream.IsEOF() || Stream.Size() - Stream.Tell() < 2)
	{
		return false;
	}

	/*uint16 KeyCode =*/ Stream.ReadU16();
	OutInstanceName = ReadPrefixedString(Stream);

	return true;
}

void FSMSObjectFactory::ParseSceneGraphNode(FBigEndianStream& Stream,
	TArray<FSMSObjectPlacement>& OutPlacements, int32 Depth)
{
	if (Depth > MaxParseDepth)
	{
		UE_LOG(LogSMSImporter, Warning,
			TEXT("scene.bin: Maximum recursion depth (%d) exceeded, stopping"), MaxParseDepth);
		return;
	}

	if (Stream.IsEOF())
	{
		return;
	}

	// Step 1: Read the genObject header (chunk size + class name)
	FString ClassName;
	int64 ChunkEnd = 0;
	if (!ReadGenObjectHeader(Stream, ClassName, ChunkEnd))
	{
		return;
	}

	UE_LOG(LogSMSImporter, Verbose,
		TEXT("scene.bin: [depth=%d] genObject class='%s' chunkEnd=%lld"),
		Depth, *ClassName, ChunkEnd);

	// Step 2: The remaining bytes in the chunk are passed to load().
	// We create a sub-stream view for safe bounds-checked parsing.
	const int64 LoadStart = Stream.Tell();
	const int64 LoadSize = ChunkEnd - LoadStart;

	if (LoadSize <= 0)
	{
		UE_LOG(LogSMSImporter, Verbose,
			TEXT("scene.bin: Empty load data for class '%s'"), *ClassName);
		Stream.Seek(ChunkEnd);
		return;
	}

	// Step 3: Read the TNameRef header (keyCode + instance name) from the load stream
	FString InstanceName;
	if (!ReadNameRefHeader(Stream, ClassName, InstanceName))
	{
		UE_LOG(LogSMSImporter, Warning,
			TEXT("scene.bin: Failed to read NameRef header for class '%s'"), *ClassName);
		Stream.Seek(ChunkEnd);
		return;
	}

	UE_LOG(LogSMSImporter, Verbose,
		TEXT("scene.bin:   instance='%s'"), *InstanceName);

	// Step 4: Dispatch based on type
	if (IsContainerType(ClassName))
	{
		// Container types: read s32 child count, then recurse for each child.
		// Note: some containers like TIdxGroupObj read extra data before the count,
		// but we handle the common case here.

		if (Stream.Tell() + 4 > ChunkEnd)
		{
			Stream.Seek(ChunkEnd);
			return;
		}

		const int32 ChildCount = Stream.ReadS32();

		UE_LOG(LogSMSImporter, Verbose,
			TEXT("scene.bin:   container '%s' with %d children"), *ClassName, ChildCount);

		if (ChildCount < 0 || ChildCount > 10000)
		{
			UE_LOG(LogSMSImporter, Warning,
				TEXT("scene.bin: Suspicious child count %d for '%s', skipping"),
				ChildCount, *ClassName);
			Stream.Seek(ChunkEnd);
			return;
		}

		for (int32 i = 0; i < ChildCount; ++i)
		{
			if (Stream.IsEOF())
			{
				break;
			}
			ParseSceneGraphNode(Stream, OutPlacements, Depth + 1);
		}
	}
	else if (ClassName == TEXT("Strategy"))
	{
		// TStrategy::load() calls TViewObj::load() (which is TNameRef::load(),
		// already read above), then reads u32 childCount and recursively
		// creates TIdxGroupObj children via genObject()+load().

		if (Stream.Tell() + 4 > ChunkEnd)
		{
			Stream.Seek(ChunkEnd);
			return;
		}

		const int32 ChildCount = static_cast<int32>(Stream.ReadU32());

		UE_LOG(LogSMSImporter, Verbose,
			TEXT("scene.bin:   Strategy with %d children"), ChildCount);

		if (ChildCount < 0 || ChildCount > 10000)
		{
			Stream.Seek(ChunkEnd);
			return;
		}

		for (int32 i = 0; i < ChildCount; ++i)
		{
			if (Stream.IsEOF())
			{
				break;
			}
			ParseSceneGraphNode(Stream, OutPlacements, Depth + 1);
		}
	}
	else if (ClassName == TEXT("IdxGroup"))
	{
		// TIdxGroupObj::loadSuper() calls TViewObjPtrListT::loadSuper()
		// (which is TNameRef::load(), already done), then reads an extra u32 (group index),
		// THEN the standard container s32 childCount + children.

		if (Stream.Tell() + 8 > ChunkEnd)
		{
			Stream.Seek(ChunkEnd);
			return;
		}

		/*uint32 GroupIndex =*/ Stream.ReadU32();
		const int32 ChildCount = Stream.ReadS32();

		UE_LOG(LogSMSImporter, Verbose,
			TEXT("scene.bin:   IdxGroup with %d children"), ChildCount);

		if (ChildCount < 0 || ChildCount > 10000)
		{
			Stream.Seek(ChunkEnd);
			return;
		}

		for (int32 i = 0; i < ChildCount; ++i)
		{
			if (Stream.IsEOF())
			{
				break;
			}
			ParseSceneGraphNode(Stream, OutPlacements, Depth + 1);
		}
	}
	else if (IsActorType(ClassName))
	{
		// TActor::load() after TNameRef::load():
		//   3x f32 position (from TPlacement::load)
		//   3x f32 rotation
		//   3x f32 scale
		//   u16-prefixed model/character name string
		//   TLightMap data (variable size, we skip it)

		const int64 MinActorData = 9 * sizeof(float); // 36 bytes for transforms
		if (Stream.Tell() + MinActorData > ChunkEnd)
		{
			UE_LOG(LogSMSImporter, Warning,
				TEXT("scene.bin: Not enough data for actor '%s' ('%s')"),
				*ClassName, *InstanceName);
			Stream.Seek(ChunkEnd);
			return;
		}

		// Read position
		const float PosX = Stream.ReadF32();
		const float PosY = Stream.ReadF32();
		const float PosZ = Stream.ReadF32();

		// Read rotation (stored as f32 degrees in the decompilation)
		const float RotX = Stream.ReadF32();
		const float RotY = Stream.ReadF32();
		const float RotZ = Stream.ReadF32();

		// Read scale
		const float ScaleX = Stream.ReadF32();
		const float ScaleY = Stream.ReadF32();
		const float ScaleZ = Stream.ReadF32();

		// Read model/character name (u16-prefixed string)
		FString ModelName;
		if (Stream.Tell() + 2 <= ChunkEnd)
		{
			ModelName = ReadPrefixedString(Stream);
		}

		// Skip TLightMap data and any remaining class-specific data
		// (we cannot reliably parse variable-length lightmap/manager strings)

		// Build the placement entry
		FSMSObjectPlacement Placement;
		Placement.ClassName = ClassName;
		Placement.ObjectName = InstanceName;
		Placement.Position = ConvertPosition(PosX, PosY, PosZ);
		Placement.Rotation = ConvertRotation(RotX, RotY, RotZ);
		Placement.Scale = FVector(ScaleX, ScaleY, ScaleZ);
		Placement.ModelName = ModelName;

		Placement.Params.Add(TEXT("GC_PosX"), FString::SanitizeFloat(PosX));
		Placement.Params.Add(TEXT("GC_PosY"), FString::SanitizeFloat(PosY));
		Placement.Params.Add(TEXT("GC_PosZ"), FString::SanitizeFloat(PosZ));
		Placement.Params.Add(TEXT("GC_RotX"), FString::SanitizeFloat(RotX));
		Placement.Params.Add(TEXT("GC_RotY"), FString::SanitizeFloat(RotY));
		Placement.Params.Add(TEXT("GC_RotZ"), FString::SanitizeFloat(RotZ));

		OutPlacements.Add(MoveTemp(Placement));

		UE_LOG(LogSMSImporter, Log,
			TEXT("scene.bin:   Actor '%s' ('%s') pos=(%.1f,%.1f,%.1f) rot=(%.1f,%.1f,%.1f) scale=(%.1f,%.1f,%.1f) model='%s'"),
			*ClassName, *InstanceName,
			PosX, PosY, PosZ, RotX, RotY, RotZ, ScaleX, ScaleY, ScaleZ,
			*ModelName);

		// After reading the actor fields, there may be additional class-specific
		// data (lightmap, manager references, etc.). We skip to the chunk end
		// since we cannot reliably parse those without knowing every subclass.
		// NOTE: For actors that are also containers of children spawned via
		// genObject within their load(), those children are read from the OUTER
		// stream (not the chunk sub-stream), so this skip is safe.
		Stream.Seek(ChunkEnd);
	}
	else if (IsPlacementType(ClassName))
	{
		// TPlacement::load(): 3x f32 position only
		const int64 MinPlacementData = 3 * sizeof(float);
		if (Stream.Tell() + MinPlacementData > ChunkEnd)
		{
			Stream.Seek(ChunkEnd);
			return;
		}

		const float PosX = Stream.ReadF32();
		const float PosY = Stream.ReadF32();
		const float PosZ = Stream.ReadF32();

		FSMSObjectPlacement Placement;
		Placement.ClassName = ClassName;
		Placement.ObjectName = InstanceName;
		Placement.Position = ConvertPosition(PosX, PosY, PosZ);

		OutPlacements.Add(MoveTemp(Placement));

		UE_LOG(LogSMSImporter, Log,
			TEXT("scene.bin:   Placement '%s' ('%s') pos=(%.1f,%.1f,%.1f)"),
			*ClassName, *InstanceName, PosX, PosY, PosZ);

		Stream.Seek(ChunkEnd);
	}
	else if (ClassName == TEXT("CubeGeneralInfo") || ClassName == TEXT("CameraCubeInfo")
		|| ClassName == TEXT("CubeStreamInfo"))
	{
		// Cube info types carry transform-like data. TCubeGeneralInfo is a TPlacement
		// subclass that reads position + additional cube parameters.
		// Extract position as a placement for visibility in the editor.

		const int64 MinData = 3 * sizeof(float);
		if (Stream.Tell() + MinData > ChunkEnd)
		{
			Stream.Seek(ChunkEnd);
			return;
		}

		const float PosX = Stream.ReadF32();
		const float PosY = Stream.ReadF32();
		const float PosZ = Stream.ReadF32();

		FSMSObjectPlacement Placement;
		Placement.ClassName = ClassName;
		Placement.ObjectName = InstanceName;
		Placement.Position = ConvertPosition(PosX, PosY, PosZ);

		OutPlacements.Add(MoveTemp(Placement));

		Stream.Seek(ChunkEnd);
	}
	else if (ClassName == TEXT("CubeGeneralInfoTable")
		|| ClassName == TEXT("StreamGeneralInfoTable")
		|| ClassName == TEXT("EventTable")
		|| ClassName == TEXT("ScenarioArchiveNameTable")
		|| ClassName == TEXT("ScenarioArchiveNamesInStage")
		|| ClassName == TEXT("PositionHolder")
		|| ClassName == TEXT("CameraMapToolTable")
		|| ClassName == TEXT("StageEnemyInfoHeader"))
	{
		// These are TNameRefAryT or TNameRefPtrAryT types that have
		// a similar container structure: TNameRef::load() + s32 count + children.
		// TNameRefAryT stores items inline, TNameRefPtrAryT uses genObject.
		// Both use the same pattern of count + child objects.

		if (Stream.Tell() + 4 > ChunkEnd)
		{
			Stream.Seek(ChunkEnd);
			return;
		}

		const int32 ChildCount = Stream.ReadS32();

		UE_LOG(LogSMSImporter, Verbose,
			TEXT("scene.bin:   Table '%s' with %d entries"), *ClassName, ChildCount);

		if (ChildCount < 0 || ChildCount > 10000)
		{
			Stream.Seek(ChunkEnd);
			return;
		}

		for (int32 i = 0; i < ChildCount; ++i)
		{
			if (Stream.IsEOF())
			{
				break;
			}
			ParseSceneGraphNode(Stream, OutPlacements, Depth + 1);
		}
	}
	else
	{
		// Unrecognized type: we cannot reliably determine how much data
		// belongs to this node's load() since each class reads different
		// amounts. However, we know the chunk boundary from genObject().
		//
		// As a heuristic, if there are at least 12 bytes left in the chunk
		// and they look like valid float coordinates, try extracting a position.

		UE_LOG(LogSMSImporter, Warning,
			TEXT("scene.bin:   Unknown type '%s' ('%s') encountered, attempting heuristic parse"),
			*ClassName, *InstanceName);

		const int64 Remaining = ChunkEnd - Stream.Tell();

		if (Remaining >= 12)
		{
			const int64 SavedPos = Stream.Tell();
			const float MaybeX = Stream.ReadF32();
			const float MaybeY = Stream.ReadF32();
			const float MaybeZ = Stream.ReadF32();

			// Heuristic: valid SMS world coords are roughly -100000 to +100000
			const float Limit = 100000.0f;
			const bool LooksLikePosition =
				FMath::IsFinite(MaybeX) && FMath::Abs(MaybeX) < Limit &&
				FMath::IsFinite(MaybeY) && FMath::Abs(MaybeY) < Limit &&
				FMath::IsFinite(MaybeZ) && FMath::Abs(MaybeZ) < Limit &&
				// At least one nonzero coordinate (filter out all-zeros from padding)
				(FMath::Abs(MaybeX) > 0.001f || FMath::Abs(MaybeY) > 0.001f || FMath::Abs(MaybeZ) > 0.001f);

			if (LooksLikePosition)
			{
				FSMSObjectPlacement Placement;
				Placement.ClassName = ClassName;
				Placement.ObjectName = InstanceName;
				Placement.Position = ConvertPosition(MaybeX, MaybeY, MaybeZ);

				OutPlacements.Add(MoveTemp(Placement));

				UE_LOG(LogSMSImporter, Log,
					TEXT("scene.bin:   Unknown type '%s' ('%s') - extracted heuristic pos=(%.1f,%.1f,%.1f)"),
					*ClassName, *InstanceName, MaybeX, MaybeY, MaybeZ);
			}
			else
			{
				UE_LOG(LogSMSImporter, Verbose,
					TEXT("scene.bin:   Unknown type '%s' ('%s') - no valid position found, skipping %lld bytes"),
					*ClassName, *InstanceName, Remaining);
			}
		}
		else
		{
			UE_LOG(LogSMSImporter, Verbose,
				TEXT("scene.bin:   Unknown type '%s' ('%s') - only %lld bytes remaining, skipping"),
				*ClassName, *InstanceName, Remaining);
		}

		Stream.Seek(ChunkEnd);
	}
}

// ============================================================================
// Public API: ParseSceneBin
// ============================================================================

bool FSMSObjectFactory::ParseSceneBin(const TArray<uint8>& Data,
	TArray<FSMSObjectPlacement>& OutPlacements)
{
	if (Data.Num() < 8)
	{
		UE_LOG(LogSMSImporter, Error,
			TEXT("scene.bin: Data too small (%d bytes)"), Data.Num());
		return false;
	}

	UE_LOG(LogSMSImporter, Log,
		TEXT("scene.bin: Parsing %d bytes"), Data.Num());

	FBigEndianStream Stream(Data);
	OutPlacements.Reset();

	// The scene.bin is loaded via:
	//   TNameRef::genObject(stream, stream2) -> reads chunk header
	//   obj->load(stream2) -> loads from sub-stream
	// This is exactly what ParseSceneGraphNode does.
	const int32 PlacementsBefore = OutPlacements.Num();
	ParseSceneGraphNode(Stream, OutPlacements, 0);

	// Count how many placements came from unknown types (heuristic extraction)
	int32 UnknownTypeCount = 0;
	for (const FSMSObjectPlacement& P : OutPlacements)
	{
		if (!IsContainerType(P.ClassName) && !IsActorType(P.ClassName)
			&& !IsPlacementType(P.ClassName))
		{
			++UnknownTypeCount;
		}
	}

	const int32 TotalParsed = OutPlacements.Num();
	UE_LOG(LogSMSImporter, Log,
		TEXT("scene.bin: Parsed %d objects, %d skipped due to unknown types"),
		TotalParsed, UnknownTypeCount);

	return true;
}

// ============================================================================
// Blueprint generation
// ============================================================================

UBlueprint* FSMSObjectFactory::GetOrCreateObjectBlueprint(
	const FString& ClassName, const FString& AssetPath)
{
	const FString BPName = FString::Printf(TEXT("BP_SMS_%s"), *ClassName);
	const FString BPPath = FString::Printf(TEXT("%s/Blueprints/%s"), *AssetPath, *BPName);

	// Check if already exists
	UBlueprint* Existing = LoadObject<UBlueprint>(nullptr,
		*FString::Printf(TEXT("%s.%s"), *BPPath, *BPName));
	if (Existing)
	{
		return Existing;
	}

	UPackage* Package = CreatePackage(*BPPath);
	if (!Package)
	{
		UE_LOG(LogSMSImporter, Error,
			TEXT("Failed to create package for Blueprint '%s'"), *BPPath);
		return nullptr;
	}

	UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(),
		Package,
		*BPName,
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass());

	if (!BP)
	{
		UE_LOG(LogSMSImporter, Error,
			TEXT("Failed to create Blueprint for '%s'"), *ClassName);
		return nullptr;
	}

	// Add a BillboardComponent for editor visibility
	if (BP->SimpleConstructionScript)
	{
		USCS_Node* BillboardNode = BP->SimpleConstructionScript->CreateNode(
			UBillboardComponent::StaticClass(), TEXT("TypeIcon"));
		if (BillboardNode)
		{
			BP->SimpleConstructionScript->AddNode(BillboardNode);
		}

		// Add a StaticMeshComponent (empty by default, can be assigned later)
		USCS_Node* MeshNode = BP->SimpleConstructionScript->CreateNode(
			UStaticMeshComponent::StaticClass(), TEXT("ObjectMesh"));
		if (MeshNode)
		{
			BP->SimpleConstructionScript->AddNode(MeshNode);
		}
	}

	// Add a member variable: SMSClassName (FString) to store the original class name
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;

		const bool bAdded = FBlueprintEditorUtils::AddMemberVariable(
			BP, TEXT("SMSClassName"), PinType, FString::Printf(TEXT("\"%s\""), *ClassName));

		if (!bAdded)
		{
			UE_LOG(LogSMSImporter, Warning,
				TEXT("Failed to add SMSClassName variable to Blueprint '%s'"), *BPName);
		}
	}

	FKismetEditorUtilities::CompileBlueprint(BP);
	FAssetRegistryModule::AssetCreated(BP);
	Package->MarkPackageDirty();

	UE_LOG(LogSMSImporter, Log,
		TEXT("Created Blueprint: %s"), *BPPath);

	return BP;
}

// ============================================================================
// Spawning
// ============================================================================

void FSMSObjectFactory::SpawnObjectsInLevel(UWorld* World,
	const TArray<FSMSObjectPlacement>& Placements,
	const FString& BlueprintBasePath,
	const TMap<FString, UStaticMesh*>& ObjectMeshes)
{
	if (!World)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("SpawnObjectsInLevel: World is null"));
		return;
	}

	UE_LOG(LogSMSImporter, Log,
		TEXT("Spawning %d objects into level"), Placements.Num());

	// Cache created Blueprints to avoid redundant lookups
	TMap<FString, UBlueprint*> BlueprintCache;

	int32 SpawnedCount = 0;

	for (const FSMSObjectPlacement& Placement : Placements)
	{
		// Get or create the Blueprint for this class
		UBlueprint** CachedBP = BlueprintCache.Find(Placement.ClassName);
		UBlueprint* BP = nullptr;

		if (CachedBP)
		{
			BP = *CachedBP;
		}
		else
		{
			BP = GetOrCreateObjectBlueprint(Placement.ClassName, BlueprintBasePath);
			BlueprintCache.Add(Placement.ClassName, BP);
		}

		if (!BP || !BP->GeneratedClass)
		{
			UE_LOG(LogSMSImporter, Warning,
				TEXT("Failed to get Blueprint for class '%s', skipping '%s'"),
				*Placement.ClassName, *Placement.ObjectName);
			continue;
		}

		// Spawn the actor
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = *FString::Printf(TEXT("SMS_%s_%s"),
			*Placement.ClassName, *Placement.ObjectName);
		SpawnParams.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		FTransform SpawnTransform;
		SpawnTransform.SetLocation(Placement.Position);
		SpawnTransform.SetRotation(FQuat(Placement.Rotation));
		SpawnTransform.SetScale3D(Placement.Scale);

		AActor* SpawnedActor = World->SpawnActor<AActor>(
			BP->GeneratedClass, &SpawnTransform, SpawnParams);

		if (!SpawnedActor)
		{
			UE_LOG(LogSMSImporter, Warning,
				TEXT("Failed to spawn actor '%s' of class '%s'"),
				*Placement.ObjectName, *Placement.ClassName);
			continue;
		}

		// Set the actor label for editor display
		SpawnedActor->SetActorLabel(
			FString::Printf(TEXT("%s (%s)"), *Placement.ObjectName, *Placement.ClassName));

		// If we have a mesh for this object type, assign it to the StaticMeshComponent
		if (const UStaticMesh* const* FoundMesh = ObjectMeshes.Find(Placement.ClassName))
		{
			if (*FoundMesh)
			{
				TArray<UStaticMeshComponent*> MeshComps;
				SpawnedActor->GetComponents<UStaticMeshComponent>(MeshComps);
				if (MeshComps.Num() > 0)
				{
					MeshComps[0]->SetStaticMesh(const_cast<UStaticMesh*>(*FoundMesh));
				}
			}
		}
		// Also try matching by model name
		else if (!Placement.ModelName.IsEmpty())
		{
			if (const UStaticMesh* const* FoundMesh = ObjectMeshes.Find(Placement.ModelName))
			{
				if (*FoundMesh)
				{
					TArray<UStaticMeshComponent*> MeshComps;
					SpawnedActor->GetComponents<UStaticMeshComponent>(MeshComps);
					if (MeshComps.Num() > 0)
					{
						MeshComps[0]->SetStaticMesh(const_cast<UStaticMesh*>(*FoundMesh));
					}
				}
			}
		}

		++SpawnedCount;
	}

	UE_LOG(LogSMSImporter, Log,
		TEXT("Successfully spawned %d / %d objects"), SpawnedCount, Placements.Num());
}

// ============================================================================
// Remap utility
// ============================================================================

int32 FSMSObjectFactory::RemapObjectType(UWorld* World, const FString& OldClassName, UClass* NewClass)
{
	if (!World)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("RemapObjectType: World is null"));
		return 0;
	}

	if (!NewClass)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("RemapObjectType: NewClass is null"));
		return 0;
	}

	const FString SearchPattern = FString::Printf(TEXT("BP_SMS_%s"), *OldClassName);

	// Phase 1: Collect actors to replace (don't modify while iterating)
	struct FActorRecord
	{
		FTransform Transform;
		FString Label;
	};
	TArray<FActorRecord> Records;
	TArray<AActor*> ActorsToDestroy;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		const FString ActorClassName = Actor->GetClass()->GetName();
		if (ActorClassName.Contains(SearchPattern))
		{
			FActorRecord Record;
			Record.Transform = Actor->GetActorTransform();
			Record.Label = Actor->GetActorLabel();
			Records.Add(MoveTemp(Record));
			ActorsToDestroy.Add(Actor);
		}
	}

	// Phase 2: Destroy old actors
	for (AActor* Actor : ActorsToDestroy)
	{
		Actor->Destroy();
	}

	// Phase 3: Spawn replacements
	int32 ReplacedCount = 0;
	for (const FActorRecord& Record : Records)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AActor* NewActor = World->SpawnActor<AActor>(
			NewClass, &Record.Transform, SpawnParams);

		if (NewActor)
		{
			if (!Record.Label.IsEmpty())
			{
				NewActor->SetActorLabel(Record.Label);
			}
			++ReplacedCount;
		}
	}

	UE_LOG(LogSMSImporter, Log,
		TEXT("RemapObjectType: Replaced %d actors of '%s' with '%s'"),
		ReplacedCount, *OldClassName, *NewClass->GetName());

	return ReplacedCount;
}

TArray<FString> FSMSObjectFactory::GetSMSObjectTypesInWorld(UWorld* World)
{
	TSet<FString> UniqueTypes;

	if (!World)
	{
		return TArray<FString>();
	}

	const FString Prefix = TEXT("BP_SMS_");

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		const FString ActorClassName = Actor->GetClass()->GetName();
		const int32 PrefixIdx = ActorClassName.Find(Prefix);
		if (PrefixIdx != INDEX_NONE)
		{
			// Extract the type name after "BP_SMS_"
			FString TypeName = ActorClassName.Mid(PrefixIdx + Prefix.Len());
			// Strip any _C suffix from generated class names
			if (TypeName.EndsWith(TEXT("_C")))
			{
				TypeName.LeftChopInline(2);
			}
			if (!TypeName.IsEmpty())
			{
				UniqueTypes.Add(TypeName);
			}
		}
	}

	TArray<FString> Result = UniqueTypes.Array();
	Result.Sort();
	return Result;
}
