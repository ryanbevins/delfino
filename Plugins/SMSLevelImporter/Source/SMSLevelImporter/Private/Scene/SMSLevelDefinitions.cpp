// SMSLevelDefinitions.cpp - Level name mappings for Super Mario Sunshine scenes

#include "Scene/SMSLevelDefinitions.h"

TArray<FSMSLevelInfo> FSMSLevelDefinitions::LevelInfos;
bool FSMSLevelDefinitions::bInitialized = false;

void FSMSLevelDefinitions::InitializeLevels()
{
	if (bInitialized)
	{
		return;
	}
	bInitialized = true;

	// ---- Main levels (episode-based) ----
	LevelInfos.Add({ TEXT("dolpic"),     TEXT("Delfino Plaza"),       8 });  // episodes 0-7
	LevelInfos.Add({ TEXT("bianco"),     TEXT("Bianco Hills"),        9 });  // episodes 0-8
	LevelInfos.Add({ TEXT("ricco"),      TEXT("Ricco Harbor"),        9 });  // episodes 0-8
	LevelInfos.Add({ TEXT("mamma"),      TEXT("Gelato Beach"),        8 });  // episodes 0-7
	LevelInfos.Add({ TEXT("pinnaBeach"), TEXT("Pinna Beach"),         6 });  // episodes 0-5
	LevelInfos.Add({ TEXT("pinnaParco"), TEXT("Pinna Park"),          7 });  // episodes 0-6
	LevelInfos.Add({ TEXT("sirena"),     TEXT("Sirena Beach"),        8 });  // episodes 0-7
	LevelInfos.Add({ TEXT("mare"),       TEXT("Noki Bay"),            8 });  // episodes 0-7
	LevelInfos.Add({ TEXT("monte"),      TEXT("Pianta Village"),      8 });  // episodes 0-7
	LevelInfos.Add({ TEXT("corona"),     TEXT("Corona Mountain"),     1 });  // episode 0
	LevelInfos.Add({ TEXT("airport"),    TEXT("Airport"),             2 });  // episodes 0-1
	LevelInfos.Add({ TEXT("casino"),     TEXT("Casino"),              2 });  // episodes 0-1
	LevelInfos.Add({ TEXT("delfino"),    TEXT("Delfino Airstrip"),    6 });  // episodes 0-5

	// ---- Special archives (standalone, MaxEpisodes == -1) ----
	LevelInfos.Add({ TEXT("biancoBoss"),      TEXT("Bianco Hills Boss"),         -1 });
	LevelInfos.Add({ TEXT("delfinoBoss"),     TEXT("Delfino Airstrip Boss"),     -1 });
	LevelInfos.Add({ TEXT("dolpicEx0"),       TEXT("Delfino Plaza Secret 0"),    -1 });
	LevelInfos.Add({ TEXT("dolpicEx1"),       TEXT("Delfino Plaza Secret 1"),    -1 });
	LevelInfos.Add({ TEXT("dolpicEx2"),       TEXT("Delfino Plaza Secret 2"),    -1 });
	LevelInfos.Add({ TEXT("dolpicEx3"),       TEXT("Delfino Plaza Secret 3"),    -1 });
	LevelInfos.Add({ TEXT("dolpicEx4"),       TEXT("Delfino Plaza Secret 4"),    -1 });
	LevelInfos.Add({ TEXT("coronaBoss"),      TEXT("Corona Mountain Boss"),      -1 });
	LevelInfos.Add({ TEXT("mareEx0"),         TEXT("Noki Bay Secret"),           -1 });
	LevelInfos.Add({ TEXT("mareBoss"),        TEXT("Noki Bay Boss"),             -1 });
	LevelInfos.Add({ TEXT("mareUndersea"),    TEXT("Noki Bay Undersea"),         -1 });
	LevelInfos.Add({ TEXT("monteEx0"),        TEXT("Pianta Village Secret"),     -1 });
	LevelInfos.Add({ TEXT("pinnaBeachBoss0"), TEXT("Pinna Beach Boss 0"),        -1 });
	LevelInfos.Add({ TEXT("pinnaBeachBoss1"), TEXT("Pinna Beach Boss 1"),        -1 });
	LevelInfos.Add({ TEXT("riccoEx0"),        TEXT("Ricco Harbor Secret 0"),     -1 });
	LevelInfos.Add({ TEXT("riccoEx1"),        TEXT("Ricco Harbor Secret 1"),     -1 });
	LevelInfos.Add({ TEXT("sirenaEx0"),       TEXT("Sirena Beach Secret 0"),     -1 });
	LevelInfos.Add({ TEXT("sirenaEx1"),       TEXT("Sirena Beach Secret 1"),     -1 });
}

const TArray<FSMSLevelInfo>& FSMSLevelDefinitions::GetAllLevels()
{
	InitializeLevels();
	return LevelInfos;
}

FString FSMSLevelDefinitions::GetScenePath(const FString& InternalName, int32 Episode)
{
	InitializeLevels();

	// Look up the entry to determine if it is a special archive
	for (const FSMSLevelInfo& Info : LevelInfos)
	{
		if (Info.InternalName == InternalName)
		{
			if (Info.MaxEpisodes == -1)
			{
				// Special archive — path is just the internal name directly
				return FString::Printf(TEXT("/scene/%s.szs"), *InternalName);
			}
			else
			{
				// Episode-based level — append episode number
				return FString::Printf(TEXT("/scene/%s%d.szs"), *InternalName, Episode);
			}
		}
	}

	// Fallback: assume episode-based naming
	return FString::Printf(TEXT("/scene/%s%d.szs"), *InternalName, Episode);
}

FString FSMSLevelDefinitions::GetDisplayName(const FString& InternalName)
{
	InitializeLevels();

	for (const FSMSLevelInfo& Info : LevelInfos)
	{
		if (Info.InternalName == InternalName)
		{
			return Info.DisplayName;
		}
	}

	return FString();
}
