// COLLoader.cpp - GameCube SMS collision data (.col) parser implementation
//
// On-disk .col format (per-object collision, big-endian):
//
// Header (16 bytes):
//   u32 vertexCount
//   u32 vertexDataOffset    (absolute offset from file start)
//   u32 groupCount
//   u32 groupDataOffset     (absolute offset from file start)
//
// Vertex array (at vertexDataOffset): vertexCount x Vec3f (3 x f32 = 12 bytes)
//
// Group array (at groupDataOffset): groupCount x GroupEntry (0x18 bytes each):
//   0x00: u16 bgType          (surface type)
//   0x02: s16 triCount        (number of triangles in this group)
//   0x04: u16 flags           (bit 0 = has per-triangle custom data)
//   0x06: 2 bytes padding
//   0x08: u32 indicesOffset   (-> s16[triCount*3], vertex indices)
//   0x0C: u32 attrib1Offset   (-> u8[triCount], unk6 per triangle)
//   0x10: u32 attrib2Offset   (-> u8[triCount], unk7 per triangle)
//   0x14: u32 perTriDataOff   (-> s16[triCount], custom data; 0 if !flags&1)
//
// Coordinate conversion: UE.X = GC.X * Scale, UE.Y = GC.Z * Scale, UE.Z = GC.Y * Scale

#include "Formats/COLLoader.h"
#include "Util/BigEndianStream.h"
#include "SMSLevelImporterModule.h"

#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "PhysicsEngine/BodySetup.h"
#include "AssetRegistry/AssetRegistryModule.h"

// GameCube -> UE5 coordinate conversion scale
static constexpr float GCToUEScale = 1.0f;

/** Convert a GameCube coordinate (Y-up, right-handed) to UE5 (Z-up, left-handed). */
static FVector GCToUE(float X, float Y, float Z)
{
	return FVector(
		X * GCToUEScale,
		Z * GCToUEScale,   // GC Z -> UE Y
		Y * GCToUEScale    // GC Y -> UE Z
	);
}

// ============================================================================
// Parse
// ============================================================================

bool FCOLLoader::Parse(const TArray<uint8>& Data, FSMSCollisionData& OutCollision)
{
	if (Data.Num() < 16)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("COL: File too small (%d bytes)"), Data.Num());
		return false;
	}

	FBigEndianStream Stream(Data);

	// ---- Read header ----
	const uint32 VertexCount = Stream.ReadU32();
	const uint32 VertexDataOffset = Stream.ReadU32();
	const uint32 GroupCount = Stream.ReadU32();
	const uint32 GroupDataOffset = Stream.ReadU32();

	UE_LOG(LogSMSImporter, Log,
		TEXT("COL: vertices=%u vertOff=0x%X groups=%u groupOff=0x%X"),
		VertexCount, VertexDataOffset, GroupCount, GroupDataOffset);

	// Validate offsets
	const int64 FileSize = Data.Num();
	if (VertexDataOffset + static_cast<int64>(VertexCount) * 12 > FileSize)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("COL: Vertex data extends past end of file"));
		return false;
	}

	if (GroupDataOffset + static_cast<int64>(GroupCount) * 0x18 > FileSize)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("COL: Group data extends past end of file"));
		return false;
	}

	// ---- Read vertex array ----
	TArray<FVector> Vertices;
	Vertices.SetNum(VertexCount);
	Stream.Seek(VertexDataOffset);

	for (uint32 i = 0; i < VertexCount; i++)
	{
		const float X = Stream.ReadF32();
		const float Y = Stream.ReadF32();
		const float Z = Stream.ReadF32();
		Vertices[i] = GCToUE(X, Y, Z);
	}

	// ---- Read groups and build triangles ----
	OutCollision.Triangles.Empty();
	OutCollision.TrianglesByType.Empty();

	for (uint32 g = 0; g < GroupCount; g++)
	{
		const int64 GroupBase = GroupDataOffset + static_cast<int64>(g) * 0x18;
		Stream.Seek(GroupBase);

		const uint16 BGType = Stream.ReadU16();
		const int16 TriCount = Stream.ReadS16();
		const uint16 Flags = Stream.ReadU16();
		Stream.Skip(2); // padding

		const uint32 IndicesOffset = Stream.ReadU32();
		const uint32 Attrib1Offset = Stream.ReadU32();
		const uint32 Attrib2Offset = Stream.ReadU32();
		const uint32 PerTriDataOffset = Stream.ReadU32();

		const bool bHasPerTriData = (Flags & 1) != 0 && PerTriDataOffset != 0;

		if (TriCount <= 0)
		{
			continue;
		}

		// Validate index data fits
		if (IndicesOffset + static_cast<int64>(TriCount) * 6 > FileSize)
		{
			UE_LOG(LogSMSImporter, Warning,
				TEXT("COL: Group %u index data extends past EOF, skipping"), g);
			continue;
		}

		for (int16 t = 0; t < TriCount; t++)
		{
			FSMSCollisionTriangle Tri;
			Tri.SurfaceType = BGType;
			Tri.Flags = 0;

			// Read 3 vertex indices (s16 each)
			Stream.Seek(IndicesOffset + static_cast<int64>(t) * 6);
			const int16 Idx0 = Stream.ReadS16();
			const int16 Idx1 = Stream.ReadS16();
			const int16 Idx2 = Stream.ReadS16();

			if (Idx0 < 0 || Idx0 >= static_cast<int32>(VertexCount) ||
				Idx1 < 0 || Idx1 >= static_cast<int32>(VertexCount) ||
				Idx2 < 0 || Idx2 >= static_cast<int32>(VertexCount))
			{
				UE_LOG(LogSMSImporter, Warning,
					TEXT("COL: Group %u tri %d has out-of-range index (%d,%d,%d), skipping"),
					g, t, Idx0, Idx1, Idx2);
				continue;
			}

			Tri.Vertices[0] = Vertices[Idx0];
			Tri.Vertices[1] = Vertices[Idx1];
			Tri.Vertices[2] = Vertices[Idx2];

			// Compute normal from cross product (in UE space)
			const FVector Edge1 = Tri.Vertices[1] - Tri.Vertices[0];
			const FVector Edge2 = Tri.Vertices[2] - Tri.Vertices[0];
			Tri.Normal = FVector::CrossProduct(Edge1, Edge2).GetSafeNormal();

			// Read per-triangle custom data if available
			if (bHasPerTriData)
			{
				Stream.Seek(PerTriDataOffset + static_cast<int64>(t) * 2);
				Tri.CustomData = Stream.ReadS16();
			}
			else
			{
				Tri.CustomData = 0;
			}

			const int32 TriIndex = OutCollision.Triangles.Num();
			OutCollision.Triangles.Add(Tri);

			// Group by surface type
			OutCollision.TrianglesByType.FindOrAdd(BGType).Add(TriIndex);
		}
	}

	UE_LOG(LogSMSImporter, Log, TEXT("COL: Parsed %d triangles across %d surface types"),
		OutCollision.Triangles.Num(), OutCollision.TrianglesByType.Num());

	return OutCollision.Triangles.Num() > 0;
}

// ============================================================================
// ApplyCollision
// ============================================================================

void FCOLLoader::ApplyCollision(UStaticMesh* Mesh, const FSMSCollisionData& Collision)
{
	if (!Mesh || Collision.Triangles.Num() == 0)
	{
		return;
	}

	UBodySetup* BodySetup = Mesh->GetBodySetup();
	if (!BodySetup)
	{
		Mesh->CreateBodySetup();
		BodySetup = Mesh->GetBodySetup();
	}

	if (!BodySetup)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("COL: Failed to create BodySetup for mesh"));
		return;
	}

	// Use the mesh geometry as the collision shape
	BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
	BodySetup->InvalidatePhysicsData();
	BodySetup->CreatePhysicsMeshes();

	UE_LOG(LogSMSImporter, Log, TEXT("COL: Applied complex collision (%d triangles) to %s"),
		Collision.Triangles.Num(), *Mesh->GetName());
}

// ============================================================================
// CreateDebugMesh
// ============================================================================

UStaticMesh* FCOLLoader::CreateDebugMesh(UObject* Outer, const FString& Name,
	const FSMSCollisionData& Collision, const FString& AssetPath)
{
	if (Collision.Triangles.Num() == 0)
	{
		UE_LOG(LogSMSImporter, Warning, TEXT("COL: No triangles for debug mesh '%s'"), *Name);
		return nullptr;
	}

	// Create the static mesh asset
	const FString MeshName = Name + TEXT("_COL_Debug");
	const FString PackagePath = AssetPath / MeshName;
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("COL: Failed to create package '%s'"), *PackagePath);
		return nullptr;
	}

	UStaticMesh* Mesh = NewObject<UStaticMesh>(Package, *MeshName, RF_Public | RF_Standalone);
	if (!Mesh)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("COL: Failed to create UStaticMesh '%s'"), *MeshName);
		return nullptr;
	}

	// Build mesh description
	FMeshDescription MeshDesc;
	FStaticMeshAttributes Attributes(MeshDesc);
	Attributes.Register();

	const int32 TriCount = Collision.Triangles.Num();
	const int32 VertCount = TriCount * 3;

	// Reserve space
	MeshDesc.ReserveNewVertices(VertCount);
	MeshDesc.ReserveNewVertexInstances(VertCount);
	MeshDesc.ReserveNewPolygons(TriCount);
	MeshDesc.ReserveNewEdges(TriCount * 3);

	// Create one polygon group
	const FPolygonGroupID GroupID = MeshDesc.CreatePolygonGroup();

	// Access attribute arrays
	TVertexAttributesRef<FVector3f> VertexPositions =
		Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> VertexNormals =
		Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector4f> VertexColors =
		Attributes.GetVertexInstanceColors();

	// Create vertices and triangles
	for (int32 i = 0; i < TriCount; i++)
	{
		const FSMSCollisionTriangle& Tri = Collision.Triangles[i];
		const FColor Color = GetSurfaceTypeColor(Tri.SurfaceType);
		const FVector4f ColorF(
			Color.R / 255.0f,
			Color.G / 255.0f,
			Color.B / 255.0f,
			Color.A / 255.0f
		);

		FVertexInstanceID TriInstances[3];

		for (int32 v = 0; v < 3; v++)
		{
			const FVertexID VertID = MeshDesc.CreateVertex();
			VertexPositions[VertID] = FVector3f(Tri.Vertices[v]);

			const FVertexInstanceID InstID = MeshDesc.CreateVertexInstance(VertID);
			VertexNormals[InstID] = FVector3f(Tri.Normal);
			VertexColors[InstID] = ColorF;

			TriInstances[v] = InstID;
		}

		// Create polygon (triangle) with correct winding
		TArray<FVertexInstanceID> PerimeterInstances;
		PerimeterInstances.Add(TriInstances[0]);
		PerimeterInstances.Add(TriInstances[1]);
		PerimeterInstances.Add(TriInstances[2]);
		MeshDesc.CreatePolygon(GroupID, PerimeterInstances);
	}

	// Commit to static mesh
	TArray<const FMeshDescription*> MeshDescriptions;
	MeshDescriptions.Add(&MeshDesc);
	Mesh->BuildFromMeshDescriptions(MeshDescriptions);

	// Set up collision on the debug mesh as well
	ApplyCollision(Mesh, Collision);

	// Notify asset registry
	FAssetRegistryModule::AssetCreated(Mesh);
	Mesh->MarkPackageDirty();

	UE_LOG(LogSMSImporter, Log,
		TEXT("COL: Created debug mesh '%s' with %d triangles"),
		*MeshName, TriCount);

	return Mesh;
}

// ============================================================================
// GetSurfaceTypeName
// ============================================================================

FString FCOLLoader::GetSurfaceTypeName(uint16 Type)
{
	// Build flag suffix
	FString FlagSuffix;
	if (Type & 0x4000)
	{
		FlagSuffix += TEXT("|Shadow");
	}
	if (Type & 0x8000)
	{
		FlagSuffix += TEXT("|CameraNoClip");
	}

	// Mask out flag bits to get the base type
	const uint16 BaseType = Type & 0x1FFF;

	FString BaseName;
	switch (BaseType)
	{
		case 0x0000: BaseName = TEXT("Normal"); break;
		case 0x0004: BaseName = TEXT("Wet"); break;
		case 0x0100: BaseName = TEXT("Water"); break;
		case 0x0101: BaseName = TEXT("Water_Damaging"); break;
		case 0x0102: BaseName = TEXT("Water_Sea"); break;
		case 0x0103: BaseName = TEXT("Water_DamagingSea"); break;
		case 0x0104: BaseName = TEXT("Water_Pool"); break;
		case 0x0105: BaseName = TEXT("Water_IndoorPool"); break;
		case 0x0106: BaseName = TEXT("UndergroundPathway"); break;
		case 0x0107: BaseName = TEXT("GroundPoundPassThrough"); break;
		case 0x0108: BaseName = TEXT("UndergroundSuperJump"); break;
		case 0x0109: BaseName = TEXT("Indoors"); break;
		case 0x010A: BaseName = TEXT("ClimbableFence"); break;
		case 0x0200: BaseName = TEXT("Warp"); break;
		case 0x0201: BaseName = TEXT("Warp_PhaseThrough"); break;
		case 0x0202: BaseName = TEXT("MapChange"); break;
		case 0x0203: BaseName = TEXT("MapChange_PhaseThrough"); break;
		case 0x0400: BaseName = TEXT("PhaseThrough_MarioOnly"); break;
		case 0x0401: BaseName = TEXT("PhaseThrough_WaterOnly"); break;
		case 0x0402: BaseName = TEXT("PhaseThrough_EnemyOnly"); break;
		case 0x0500: BaseName = TEXT("Hydrophobic"); break;
		case 0x0701: BaseName = TEXT("Sand"); break;
		case 0x0800: BaseName = TEXT("DeathPlane"); break;
		case 0x0801: BaseName = TEXT("PhaseThrough_AllButMapObj"); break;
		default:
			BaseName = FString::Printf(TEXT("Unknown_0x%04X"), BaseType);
			break;
	}

	if (FlagSuffix.Len() > 0)
	{
		return BaseName + FlagSuffix;
	}
	return BaseName;
}

// ============================================================================
// GetSurfaceTypeColor (private)
// ============================================================================

FColor FCOLLoader::GetSurfaceTypeColor(uint16 Type)
{
	const uint16 BaseType = Type & 0x1FFF;
	const uint16 Category = BaseType & 0xFF00;

	switch (Category)
	{
		case 0x0100: // Water types
			return FColor(50, 100, 255, 255);   // Blue
		case 0x0200: // Warp types
			return FColor(255, 128, 0, 255);    // Orange
		case 0x0400: // Phase-through types
			return FColor(180, 50, 255, 255);   // Purple
		case 0x0500: // Hydrophobic
			return FColor(0, 200, 200, 255);    // Cyan
		case 0x0700: // Sand
			return FColor(230, 200, 50, 255);   // Yellow
		case 0x0800: // Death plane
			return FColor(255, 30, 30, 255);    // Red
		default:
			break;
	}

	// Specific non-categorized types
	switch (BaseType)
	{
		case 0x0004: // Wet ground
			return FColor(100, 180, 220, 255);  // Light blue
		case 0x0000: // Normal
		default:
			return FColor(80, 200, 80, 255);    // Green
	}
}
