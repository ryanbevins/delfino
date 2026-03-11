// SMSSceneLoader.h - Central orchestrator for the SMS import pipeline
//
// Ties together the ISO reader, YAZ0 decompressor, RARC archive parser,
// BMD/COL/BTK/BTP/BRK loaders, object factory, and level definitions
// to import complete Super Mario Sunshine scenes into UE5 levels.

#pragma once

#include "CoreMinimal.h"

class FGCISOReader;
class UStaticMesh;
class UWorld;

struct FRARCArchive;
struct FSMSLevelInfo;
struct FSMSObjectPlacement;

DECLARE_DELEGATE_TwoParams(FOnSMSImportProgress, float /*Progress 0-1*/, const FString& /*StatusMessage*/);

/**
 * Options controlling which aspects of a scene are imported.
 */
struct FSMSImportOptions
{
	bool bImportGeometry = true;
	bool bImportTextures = true;
	bool bImportCollision = true;
	bool bImportObjects = true;
	bool bImportAnimations = true;
	float ScaleFactor = 1.0f;
	FString OutputDirectory = TEXT("/Game/SMS");
};

/**
 * Central orchestrator for importing Super Mario Sunshine scenes from a
 * GameCube ISO into Unreal Engine 5 levels.
 *
 * Usage:
 *   FSMSSceneLoader Loader;
 *   if (Loader.OpenISO(ISOPath))
 *   {
 *       TArray<FSMSLevelInfo> Levels = Loader.GetAvailableLevels();
 *       UWorld* World = Loader.ImportScene("dolpic", 0, Options, ProgressDelegate);
 *       Loader.CloseISO();
 *   }
 */
class SMSLEVELIMPORTER_API FSMSSceneLoader
{
public:
	FSMSSceneLoader();
	~FSMSSceneLoader();

	/** Open an ISO file and validate it is a Super Mario Sunshine disc. */
	bool OpenISO(const FString& ISOPath);

	/** Close the ISO file and release resources. */
	void CloseISO();

	/** Returns true if an ISO is currently open. */
	bool IsISOOpen() const;

	/** Get region string ("JP", "US", "EU", "KR") from the open ISO. */
	FString GetRegion() const;

	/** Get all available levels from the level definitions registry. */
	TArray<FSMSLevelInfo> GetAvailableLevels() const;

	/**
	 * Import a single scene from the open ISO.
	 * @param LevelName  Internal level name (e.g., "dolpic", "bianco").
	 * @param Episode    Episode/scenario number (0-based).
	 * @param Options    Import options controlling what gets imported.
	 * @param ProgressCallback  Optional delegate for progress reporting.
	 * @return The generated UWorld, or nullptr on failure.
	 */
	UWorld* ImportScene(const FString& LevelName, int32 Episode,
		const FSMSImportOptions& Options,
		FOnSMSImportProgress ProgressCallback = FOnSMSImportProgress());

	/** Request cancellation of an in-progress import. */
	void CancelImport();

	/** Returns true if cancellation has been requested. */
	bool IsCancelled() const;

private:
	TUniquePtr<FGCISOReader> ISOReader;
	bool bCancelRequested = false;

	// ---- Pipeline stages ----

	/** Read .szs from ISO, decompress YAZ0, and parse RARC archive. */
	bool LoadAndDecompressScene(const FString& ScenePath, FRARCArchive& OutArchive);

	/** Find and import the map .bmd file as a UStaticMesh. */
	UStaticMesh* ImportMapGeometry(const FRARCArchive& Archive,
		const FSMSImportOptions& Options, const FString& AssetPath);

	/** Find and import .col collision data, applying it to the map mesh. */
	void ImportCollision(const FRARCArchive& Archive, UStaticMesh* MapMesh,
		const FSMSImportOptions& Options, const FString& AssetPath);

	/** Parse scene.bin to extract object placements. */
	TArray<FSMSObjectPlacement> ImportObjects(const FRARCArchive& Archive,
		const FSMSImportOptions& Options, const FString& AssetPath);

	/** Find and parse .btk, .btp, .brk animation files. */
	void ImportAnimations(const FRARCArchive& Archive,
		const FSMSImportOptions& Options, const FString& AssetPath);

	/** Create a UWorld with the map mesh and object placements. */
	UWorld* CreateLevelMap(const FString& LevelName, int32 Episode,
		UStaticMesh* MapMesh, const TArray<FSMSObjectPlacement>& Placements,
		const FSMSImportOptions& Options, const FString& AssetPath);

	/** Fire the progress callback if bound. */
	void ReportProgress(const FOnSMSImportProgress& Callback, float Progress, const FString& Message);
};
