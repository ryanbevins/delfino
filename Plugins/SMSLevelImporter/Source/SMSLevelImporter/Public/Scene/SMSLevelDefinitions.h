// SMSLevelDefinitions.h - Level name mappings for Super Mario Sunshine scenes

#pragma once

#include "CoreMinimal.h"

/**
 * Metadata for a single SMS level or special archive.
 * MaxEpisodes == -1 indicates a standalone special archive (boss, secret, etc.).
 */
struct FSMSLevelInfo
{
	FString InternalName;    // e.g., "dolpic", "biancoBoss"
	FString DisplayName;     // e.g., "Delfino Plaza", "Bianco Hills Boss"
	int32 MaxEpisodes;       // Number of episodes/scenarios, or -1 for special archives
};

/**
 * Static registry of all SMS levels and special scene archives.
 *
 * Provides lookup from internal level names to display names and
 * scene archive paths within the GameCube ISO filesystem.
 * Used by the Scene Loader to resolve .szs paths and by the
 * Editor UI to display friendly names in the level browser.
 */
class SMSLEVELIMPORTER_API FSMSLevelDefinitions
{
public:
	/** Returns the full list of all levels and special archives. */
	static const TArray<FSMSLevelInfo>& GetAllLevels();

	/**
	 * Get scene archive path for a level + episode.
	 * e.g., ("dolpic", 0) -> "/scene/dolpic0.szs"
	 * For special archives (MaxEpisodes == -1), Episode is ignored
	 * and the direct archive path is returned.
	 */
	static FString GetScenePath(const FString& InternalName, int32 Episode);

	/** Map internal name to display name. Returns empty string if not found. */
	static FString GetDisplayName(const FString& InternalName);

private:
	/** Lazily-initialized level table. */
	static TArray<FSMSLevelInfo> LevelInfos;
	static bool bInitialized;

	/** Populate the level table on first access. */
	static void InitializeLevels();
};
