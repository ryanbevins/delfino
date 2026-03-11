// COLLoader.h - GameCube SMS collision data (.col) parser
//
// Parses per-object .col collision files from Super Mario Sunshine into
// intermediate structures that can be applied as UE5 collision meshes
// or visualized as debug geometry with color-coded surface types.

#pragma once

#include "CoreMinimal.h"

class UStaticMesh;

// ---- Data structures ----

struct FSMSCollisionTriangle
{
	FVector Vertices[3];  // Coordinate-converted (UE space)
	FVector Normal;
	uint16 SurfaceType;   // BGType flags
	int16 CustomData;     // Warp ID, jump power, etc.
	uint16 Flags;
};

struct FSMSCollisionData
{
	TArray<FSMSCollisionTriangle> Triangles;
	TMap<uint16, TArray<int32>> TrianglesByType; // Surface type -> triangle indices
};

// ---- Parser / Asset Creator ----

class SMSLEVELIMPORTER_API FCOLLoader
{
public:
	/**
	 * Parse COL binary data from a .col file.
	 * The .col format stores indexed triangle groups with surface type metadata.
	 * @param Data          Raw file bytes.
	 * @param OutCollision  Receives the parsed collision data.
	 * @return true on success.
	 */
	static bool Parse(const TArray<uint8>& Data, FSMSCollisionData& OutCollision);

	/**
	 * Apply collision triangles to an existing UStaticMesh as complex collision.
	 * Sets the mesh to use CTF_UseComplexAsSimple so the render geometry
	 * doubles as the physics shape.
	 */
	static void ApplyCollision(UStaticMesh* Mesh, const FSMSCollisionData& Collision);

	/**
	 * Create a debug visualization mesh with collision triangles as visible
	 * geometry, color-coded by surface type via vertex colors.
	 * @param Outer     Outer object for ownership.
	 * @param Name      Asset name for the mesh.
	 * @param Collision Parsed collision data from Parse().
	 * @param AssetPath Content path (e.g. "/Game/SMS/Stage").
	 * @return The created UStaticMesh, or nullptr on failure.
	 */
	static UStaticMesh* CreateDebugMesh(UObject* Outer, const FString& Name,
		const FSMSCollisionData& Collision, const FString& AssetPath);

	/** Get a human-readable name for a surface type code. */
	static FString GetSurfaceTypeName(uint16 Type);

private:
	/** Map a surface type to a vertex color for debug visualization. */
	static FColor GetSurfaceTypeColor(uint16 Type);
};
