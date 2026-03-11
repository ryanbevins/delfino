// SMSObjectFactory.h - JDrama scene.bin parser and Blueprint actor generation

#pragma once

#include "CoreMinimal.h"

class FBigEndianStream;
class UBlueprint;
class UStaticMesh;
class UWorld;

/**
 * Represents a single object placement extracted from a JDrama scene.bin file.
 *
 * The scene.bin stores a recursive tree of TNameRef objects. Placement-bearing
 * objects (TPlacement, TActor, TMapObjBase, etc.) carry transform data that
 * this struct captures after coordinate conversion to Unreal Engine space.
 */
struct FSMSObjectPlacement
{
	/** JDrama class name (e.g., "GroupObj", "MapObjGeneral", "SmJ3DAct"). */
	FString ClassName;

	/** Instance name read from the binary stream. */
	FString ObjectName;

	/** Position converted to UE coordinate space. */
	FVector Position = FVector::ZeroVector;

	/** Rotation converted to UE coordinate space. */
	FRotator Rotation = FRotator::ZeroRotator;

	/** Scale (defaults to 1,1,1 when not present in the binary data). */
	FVector Scale = FVector::OneVector;

	/** Additional parameters stored as key-value strings. */
	TMap<FString, FString> Params;

	/** Associated .bmd model name if extractable from the data. */
	FString ModelName;
};

/**
 * Parses JDrama scene.bin files and generates Blueprint actors from the
 * extracted object placements.
 *
 * Scene.bin binary format (from the SMS decompilation):
 *
 *   genObject() reads:
 *     - u32 chunkSize (includes the 4 bytes of this field)
 *     - Within the chunk: u16 classNameLength + className bytes
 *     - The remainder of the chunk is passed to the object's load()
 *
 *   TNameRef::load() reads:
 *     - u16 keyCode
 *     - u16 instanceNameLength + instanceName bytes
 *
 *   Container types (GroupObj, NameRefGrp, Strategy, IdxGroup, etc.):
 *     - TNameRef::load() header
 *     - s32 childCount
 *     - childCount recursive genObject()+load() calls
 *
 *   TPlacement: TNameRef::load() + 3x f32 (position xyz)
 *   TActor:     TPlacement::load() + 3x f32 (rotation xyz) + 3x f32 (scale xyz)
 *               + u16-prefixed model name string + TLightMap data
 */
class SMSLEVELIMPORTER_API FSMSObjectFactory
{
public:
	/**
	 * Parse a scene.bin byte buffer and extract all object placements.
	 * Returns true if parsing completed (even if some branches were skipped).
	 */
	static bool ParseSceneBin(const TArray<uint8>& Data,
		TArray<FSMSObjectPlacement>& OutPlacements);

	/**
	 * Create (or retrieve an existing) Blueprint class for an SMS object type.
	 * The Blueprint will have a BillboardComponent and an empty StaticMeshComponent.
	 */
	static UBlueprint* GetOrCreateObjectBlueprint(const FString& ClassName,
		const FString& AssetPath);

	/**
	 * Spawn actors from a list of placements into a UWorld.
	 * For each placement a Blueprint is created/reused, and if a matching mesh
	 * exists in ObjectMeshes the StaticMeshComponent is assigned.
	 */
	static void SpawnObjectsInLevel(UWorld* World,
		const TArray<FSMSObjectPlacement>& Placements,
		const FString& BlueprintBasePath,
		const TMap<FString, UStaticMesh*>& ObjectMeshes);

private:
	/**
	 * Read the genObject() header: u32 chunk size, then within the chunk
	 * a u16 class name length + class name string. Returns the class name
	 * and sets up a sub-stream for the remainder of the chunk.
	 */
	static bool ReadGenObjectHeader(FBigEndianStream& Stream,
		FString& OutClassName, int64& OutChunkEndOffset);

	/**
	 * Read the TNameRef::load() header: u16 keyCode + u16-prefixed instance name.
	 */
	static bool ReadNameRefHeader(FBigEndianStream& Stream,
		FString& OutClassName, FString& OutInstanceName);

	/**
	 * Recursively walk the scene graph tree, extracting placements from
	 * nodes that carry transform data.
	 */
	static void ParseSceneGraphNode(FBigEndianStream& Stream,
		TArray<FSMSObjectPlacement>& OutPlacements, int32 Depth);

	/**
	 * Read a u16-length-prefixed string from the stream (matches
	 * JSUInputStream::readString from the JSystem library).
	 */
	static FString ReadPrefixedString(FBigEndianStream& Stream);

	/** Returns true if a class name is a known container type with children. */
	static bool IsContainerType(const FString& ClassName);

	/** Returns true if a class name is known to carry TActor-style transforms. */
	static bool IsActorType(const FString& ClassName);

	/** Returns true if a class name is known to carry TPlacement-style position only. */
	static bool IsPlacementType(const FString& ClassName);

	/**
	 * Convert GameCube coordinates to Unreal Engine space.
	 * Position: UE.X = GC.X, UE.Y = GC.Z, UE.Z = GC.Y
	 * Rotation: degrees, with Y-axis negated
	 */
	static FVector ConvertPosition(float GCX, float GCY, float GCZ);
	static FRotator ConvertRotation(float GCRotX, float GCRotY, float GCRotZ);
};
