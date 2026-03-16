// SMSSceneLoader.cpp - Central orchestrator for the SMS import pipeline

#include "Scene/SMSSceneLoader.h"
#include "SMSLevelImporterModule.h"

#include "ISO/GCISOReader.h"
#include "ISO/YAZ0Decoder.h"
#include "Util/BigEndianStream.h"
#include "Archive/RARCArchive.h"
#include "Formats/BMDLoader.h"
#include "Formats/BTILoader.h"
#include "Formats/BCKLoader.h"
#include "Formats/COLLoader.h"
#include "Formats/BTKLoader.h"
#include "Formats/BTPLoader.h"
#include "Formats/BRKLoader.h"
#include "Scene/SMSObjectFactory.h"
#include "Scene/SMSLevelDefinitions.h"

#include "Engine/StaticMeshActor.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Engine/World.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

// ---- Constructor / Destructor ----

FSMSSceneLoader::FSMSSceneLoader()
{
}

FSMSSceneLoader::~FSMSSceneLoader()
{
	CloseISO();
}

// ---- ISO management ----

bool FSMSSceneLoader::OpenISO(const FString& ISOPath)
{
	CloseISO();

	ISOReader = MakeUnique<FGCISOReader>();
	if (!ISOReader->Open(ISOPath))
	{
		UE_LOG(LogSMSImporter, Error, TEXT("Failed to open ISO: %s"), *ISOPath);
		ISOReader.Reset();
		return false;
	}

	if (!ISOReader->IsSMS())
	{
		UE_LOG(LogSMSImporter, Error, TEXT("ISO is not a Super Mario Sunshine disc: %s"), *ISOPath);
		ISOReader.Reset();
		return false;
	}

	UE_LOG(LogSMSImporter, Log, TEXT("Opened SMS ISO: %s (Region: %s)"), *ISOPath, *ISOReader->GetRegion());
	return true;
}

void FSMSSceneLoader::CloseISO()
{
	if (ISOReader.IsValid())
	{
		ISOReader->Close();
		ISOReader.Reset();
	}
}

bool FSMSSceneLoader::IsISOOpen() const
{
	return ISOReader.IsValid();
}

FString FSMSSceneLoader::GetRegion() const
{
	if (ISOReader.IsValid())
	{
		return ISOReader->GetRegion();
	}
	return FString();
}

TArray<FSMSLevelInfo> FSMSSceneLoader::GetAvailableLevels() const
{
	return FSMSLevelDefinitions::GetAllLevels();
}

// ---- Import orchestration ----

UWorld* FSMSSceneLoader::ImportScene(const FString& LevelName, int32 Episode,
	const FSMSImportOptions& Options, FOnSMSImportProgress ProgressCallback)
{
	bCancelRequested = false;

	if (!ISOReader.IsValid())
	{
		UE_LOG(LogSMSImporter, Error, TEXT("No ISO open — call OpenISO() first."));
		return nullptr;
	}

	// 1. Build scene path and asset output path
	FString ScenePath = FSMSLevelDefinitions::GetScenePath(LevelName, Episode);
	FString DisplayName = FSMSLevelDefinitions::GetDisplayName(LevelName);
	FString AssetPath = FString::Printf(TEXT("%s/%s/Episode%d"),
		*Options.OutputDirectory, *DisplayName.Replace(TEXT(" "), TEXT("")), Episode);

	ReportProgress(ProgressCallback, 0.0f, FString::Printf(TEXT("Loading %s..."), *ScenePath));

	// 2. Load and decompress the scene archive
	FRARCArchive Archive;
	if (!LoadAndDecompressScene(ScenePath, Archive))
	{
		UE_LOG(LogSMSImporter, Error, TEXT("Failed to load scene: %s"), *ScenePath);
		return nullptr;
	}
	if (bCancelRequested) return nullptr;

	ReportProgress(ProgressCallback, 0.1f, TEXT("Archive loaded, importing geometry..."));

	// 3. Import map geometry (BMD -> UStaticMesh)
	UStaticMesh* MapMesh = nullptr;
	if (Options.bImportGeometry)
	{
		MapMesh = ImportMapGeometry(Archive, Options, AssetPath);
	}
	if (bCancelRequested) return nullptr;

	ReportProgress(ProgressCallback, 0.4f, TEXT("Importing collision..."));

	// 4. Import collision
	if (Options.bImportCollision && MapMesh)
	{
		ImportCollision(Archive, MapMesh, Options, AssetPath);
	}
	if (bCancelRequested) return nullptr;

	ReportProgress(ProgressCallback, 0.5f, TEXT("Importing objects..."));

	// 5. Import object placements
	TArray<FSMSObjectPlacement> Placements;
	if (Options.bImportObjects)
	{
		Placements = ImportObjects(Archive, Options, AssetPath);
	}
	if (bCancelRequested) return nullptr;

	ReportProgress(ProgressCallback, 0.7f, TEXT("Importing animations..."));

	// 6. Import animations
	if (Options.bImportAnimations)
	{
		ImportAnimations(Archive, Options, AssetPath);
	}
	if (bCancelRequested) return nullptr;

	ReportProgress(ProgressCallback, 0.85f, TEXT("Creating level map..."));

	// 7. Create the UWorld (.umap) with everything placed
	UWorld* World = CreateLevelMap(LevelName, Episode, MapMesh, Placements, Options, AssetPath);

	ReportProgress(ProgressCallback, 1.0f, TEXT("Import complete!"));

	return World;
}

void FSMSSceneLoader::CancelImport()
{
	bCancelRequested = true;
}

bool FSMSSceneLoader::IsCancelled() const
{
	return bCancelRequested;
}

// ---- Pipeline stages ----

bool FSMSSceneLoader::LoadAndDecompressScene(const FString& ScenePath, FRARCArchive& OutArchive)
{
	// Read .szs from ISO
	TArray<uint8> CompressedData = ISOReader->ReadFile(ScenePath);
	if (CompressedData.Num() == 0)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("File not found or empty in ISO: %s"), *ScenePath);
		return false;
	}

	// Decompress YAZ0
	if (!FYAZ0Decoder::IsYAZ0(CompressedData))
	{
		UE_LOG(LogSMSImporter, Error, TEXT("File is not YAZ0 compressed: %s"), *ScenePath);
		return false;
	}

	TArray<uint8> DecompressedData = FYAZ0Decoder::Decode(CompressedData);
	if (DecompressedData.Num() == 0)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("YAZ0 decompression failed: %s"), *ScenePath);
		return false;
	}

	// Parse RARC archive
	if (!OutArchive.Parse(DecompressedData))
	{
		UE_LOG(LogSMSImporter, Error, TEXT("RARC parse failed: %s"), *ScenePath);
		return false;
	}

	return true;
}

UStaticMesh* FSMSSceneLoader::ImportMapGeometry(const FRARCArchive& Archive,
	const FSMSImportOptions& Options, const FString& AssetPath)
{
	// Find map BMD files
	TArray<FString> BmdFiles = Archive.FindFiles(TEXT(".bmd"));

	// Prefer "map.bmd" or a file with "map" in the name (but not "mapobj")
	FString MapBmdPath;
	for (const FString& Path : BmdFiles)
	{
		if (Path.Contains(TEXT("map")) && !Path.Contains(TEXT("mapobj")))
		{
			MapBmdPath = Path;
			break;
		}
	}
	if (MapBmdPath.IsEmpty() && BmdFiles.Num() > 0)
	{
		MapBmdPath = BmdFiles[0];
	}
	if (MapBmdPath.IsEmpty())
	{
		UE_LOG(LogSMSImporter, Warning, TEXT("No .bmd files found in archive"));
		return nullptr;
	}

	UE_LOG(LogSMSImporter, Log, TEXT("Importing map geometry from: %s"), *MapBmdPath);

	TArray<uint8> BmdData = Archive.GetFile(MapBmdPath);
	if (BmdData.Num() == 0)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("Failed to read BMD file: %s"), *MapBmdPath);
		return nullptr;
	}

	FBMDModel Model;
	if (!FBMDLoader::Parse(BmdData, Model))
	{
		UE_LOG(LogSMSImporter, Error, TEXT("Failed to parse BMD file: %s"), *MapBmdPath);
		return nullptr;
	}

	UStaticMesh* Mesh = FBMDLoader::CreateStaticMesh(GetTransientPackage(), TEXT("Map"), Model, AssetPath);
	if (!Mesh)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("Failed to create static mesh from BMD: %s"), *MapBmdPath);
	}

	return Mesh;
}

void FSMSSceneLoader::ImportCollision(const FRARCArchive& Archive, UStaticMesh* MapMesh,
	const FSMSImportOptions& Options, const FString& AssetPath)
{
	// Look for .col files
	TArray<FString> ColFiles = Archive.FindFiles(TEXT(".col"));
	if (ColFiles.Num() == 0)
	{
		UE_LOG(LogSMSImporter, Log, TEXT("No .col files found in archive, skipping collision"));
		return;
	}

	UE_LOG(LogSMSImporter, Log, TEXT("Importing collision from: %s"), *ColFiles[0]);

	TArray<uint8> ColData = Archive.GetFile(ColFiles[0]);
	if (ColData.Num() == 0)
	{
		UE_LOG(LogSMSImporter, Warning, TEXT("Failed to read collision file: %s"), *ColFiles[0]);
		return;
	}

	FSMSCollisionData Collision;
	if (!FCOLLoader::Parse(ColData, Collision))
	{
		UE_LOG(LogSMSImporter, Warning, TEXT("Failed to parse collision file: %s"), *ColFiles[0]);
		return;
	}

	FCOLLoader::ApplyCollision(MapMesh, Collision);
	FCOLLoader::CreateDebugMesh(GetTransientPackage(), TEXT("Map_Collision"), Collision, AssetPath);

	UE_LOG(LogSMSImporter, Log, TEXT("Imported %d collision triangles"), Collision.Triangles.Num());
}

TArray<FSMSObjectPlacement> FSMSSceneLoader::ImportObjects(const FRARCArchive& Archive,
	const FSMSImportOptions& Options, const FString& AssetPath)
{
	TArray<FSMSObjectPlacement> Placements;

	// Find scene.bin
	FString SceneBinPath;
	TArray<FString> AllFiles = Archive.ListFiles();
	for (const FString& Path : AllFiles)
	{
		if (Path.EndsWith(TEXT("scene.bin")))
		{
			SceneBinPath = Path;
			break;
		}
	}

	if (SceneBinPath.IsEmpty())
	{
		UE_LOG(LogSMSImporter, Log, TEXT("No scene.bin found in archive, skipping object import"));
		return Placements;
	}

	UE_LOG(LogSMSImporter, Log, TEXT("Importing objects from: %s"), *SceneBinPath);

	TArray<uint8> SceneBinData = Archive.GetFile(SceneBinPath);
	if (SceneBinData.Num() == 0)
	{
		UE_LOG(LogSMSImporter, Warning, TEXT("Failed to read scene.bin: %s"), *SceneBinPath);
		return Placements;
	}

	FSMSObjectFactory::ParseSceneBin(SceneBinData, Placements);

	UE_LOG(LogSMSImporter, Log, TEXT("Parsed %d object placements from scene.bin"), Placements.Num());

	return Placements;
}

void FSMSSceneLoader::ImportAnimations(const FRARCArchive& Archive,
	const FSMSImportOptions& Options, const FString& AssetPath)
{
	// ---- BTK (UV/texture SRT animations) ----
	TArray<FString> BtkFiles = Archive.FindFiles(TEXT(".btk"));
	for (const FString& BtkPath : BtkFiles)
	{
		if (bCancelRequested) return;

		TArray<uint8> BtkData = Archive.GetFile(BtkPath);
		if (BtkData.Num() == 0) continue;

		FBTKAnimation BtkAnim;
		if (FBTKLoader::Parse(BtkData, BtkAnim))
		{
			UE_LOG(LogSMSImporter, Log, TEXT("Parsed BTK: %s (%d entries, %d frames)"),
				*BtkPath, BtkAnim.Entries.Num(), BtkAnim.FrameCount);

			// Create curve assets
			FBTKLoader::CreateCurveAssets(GetTransientPackage(), BtkAnim, AssetPath);

			// Detect simple panners and log them for material setup
			for (const FBTKAnimEntry& Entry : BtkAnim.Entries)
			{
				float SpeedS = 0.0f, SpeedT = 0.0f;
				if (FBTKLoader::IsSimplePanner(Entry, SpeedS, SpeedT))
				{
					UE_LOG(LogSMSImporter, Log, TEXT("  Panner detected: %s TexGen%d  SpeedS=%.4f SpeedT=%.4f"),
						*Entry.MaterialName, Entry.TexGenIndex, SpeedS, SpeedT);
				}
			}
		}
		else
		{
			UE_LOG(LogSMSImporter, Warning, TEXT("Failed to parse BTK: %s"), *BtkPath);
		}
	}

	// ---- BTP (texture pattern / flipbook animations) ----
	TArray<FString> BtpFiles = Archive.FindFiles(TEXT(".btp"));
	for (const FString& BtpPath : BtpFiles)
	{
		if (bCancelRequested) return;

		TArray<uint8> BtpData = Archive.GetFile(BtpPath);
		if (BtpData.Num() == 0) continue;

		FBTPAnimation BtpAnim;
		if (FBTPLoader::Parse(BtpData, BtpAnim))
		{
			UE_LOG(LogSMSImporter, Log, TEXT("Parsed BTP: %s (%d entries, %d frames)"),
				*BtpPath, BtpAnim.Entries.Num(), BtpAnim.FrameCount);
		}
		else
		{
			UE_LOG(LogSMSImporter, Warning, TEXT("Failed to parse BTP: %s"), *BtpPath);
		}
	}

	// ---- BRK (TEV register color animations) ----
	TArray<FString> BrkFiles = Archive.FindFiles(TEXT(".brk"));
	for (const FString& BrkPath : BrkFiles)
	{
		if (bCancelRequested) return;

		TArray<uint8> BrkData = Archive.GetFile(BrkPath);
		if (BrkData.Num() == 0) continue;

		FBRKAnimation BrkAnim;
		if (FBRKLoader::Parse(BrkData, BrkAnim))
		{
			UE_LOG(LogSMSImporter, Log, TEXT("Parsed BRK: %s (%d CReg + %d KReg entries, %d frames)"),
				*BrkPath, BrkAnim.CRegEntries.Num(), BrkAnim.KRegEntries.Num(), BrkAnim.FrameCount);

			// Create color curve assets
			FBRKLoader::CreateColorCurves(GetTransientPackage(), BrkAnim, AssetPath);
		}
		else
		{
			UE_LOG(LogSMSImporter, Warning, TEXT("Failed to parse BRK: %s"), *BrkPath);
		}
	}
}

UWorld* FSMSSceneLoader::CreateLevelMap(const FString& LevelName, int32 Episode,
	UStaticMesh* MapMesh, const TArray<FSMSObjectPlacement>& Placements,
	const FSMSImportOptions& Options, const FString& AssetPath)
{
	FString DisplayName = FSMSLevelDefinitions::GetDisplayName(LevelName);
	FString MapName = FString::Printf(TEXT("L_%s_Ep%d"),
		*DisplayName.Replace(TEXT(" "), TEXT("")), Episode);
	FString MapPackagePath = FString::Printf(TEXT("%s/Maps/%s"), *AssetPath, *MapName);

	UPackage* MapPackage = CreatePackage(*MapPackagePath);
	if (!MapPackage)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("Failed to create package: %s"), *MapPackagePath);
		return nullptr;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::None, false, *MapName, MapPackage);
	if (!World)
	{
		UE_LOG(LogSMSImporter, Error, TEXT("Failed to create world: %s"), *MapName);
		return nullptr;
	}

	World->SetFlags(RF_Public | RF_Standalone);

	// Register with asset registry
	FAssetRegistryModule::AssetCreated(World);
	MapPackage->MarkPackageDirty();

	// Place map mesh as a static mesh actor
	if (MapMesh)
	{
		AStaticMeshActor* MapActor = World->SpawnActor<AStaticMeshActor>();
		if (MapActor)
		{
			MapActor->GetStaticMeshComponent()->SetStaticMesh(MapMesh);
			MapActor->SetActorLabel(TEXT("SMS_Map"));
			UE_LOG(LogSMSImporter, Log, TEXT("Placed map mesh actor in level"));
		}
	}

	// Spawn object placements
	if (Placements.Num() > 0)
	{
		TMap<FString, UStaticMesh*> EmptyMeshMap; // TODO: populate with object meshes
		FSMSObjectFactory::SpawnObjectsInLevel(World, Placements, AssetPath, EmptyMeshMap);
		UE_LOG(LogSMSImporter, Log, TEXT("Spawned %d object placements in level"), Placements.Num());
	}

	// Save the world package
	FString PackageFilename = FPackageName::LongPackageNameToFilename(
		MapPackagePath, FPackageName::GetMapPackageExtension());

	FSavePackageArgs SaveArgs;
	UPackage::SavePackage(MapPackage, World, *PackageFilename, SaveArgs);

	UE_LOG(LogSMSImporter, Log, TEXT("Saved level map: %s"), *PackageFilename);

	return World;
}

// ---- Character scanning and import ----

TArray<FSMSCharacterInfo> FSMSSceneLoader::ScanCharacterArchives()
{
	TArray<FSMSCharacterInfo> Characters;

	if (!ISOReader.IsValid())
	{
		return Characters;
	}

	// Get all known level paths to exclude
	TSet<FString> LevelPaths;
	TArray<FSMSLevelInfo> AllLevels = FSMSLevelDefinitions::GetAllLevels();
	for (const FSMSLevelInfo& Level : AllLevels)
	{
		if (Level.MaxEpisodes == -1)
		{
			LevelPaths.Add(FSMSLevelDefinitions::GetScenePath(Level.InternalName, 0).ToLower());
		}
		else
		{
			for (int32 Ep = 0; Ep < Level.MaxEpisodes; Ep++)
			{
				LevelPaths.Add(FSMSLevelDefinitions::GetScenePath(Level.InternalName, Ep).ToLower());
			}
		}
	}

	// Enumerate all .szs files in the ISO
	TArray<FString> AllFiles = ISOReader->ListFiles();
	for (const FString& FilePath : AllFiles)
	{
		if (!FilePath.EndsWith(TEXT(".szs"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		// Skip known level archives
		if (LevelPaths.Contains(FilePath.ToLower()))
		{
			continue;
		}

		// Try to decompress and scan this archive
		TArray<uint8> CompressedData = ISOReader->ReadFile(FilePath);
		if (CompressedData.Num() == 0)
		{
			continue;
		}

		if (!FYAZ0Decoder::IsYAZ0(CompressedData))
		{
			continue;
		}

		TArray<uint8> DecompressedData = FYAZ0Decoder::Decode(CompressedData);
		if (DecompressedData.Num() == 0)
		{
			continue;
		}

		FRARCArchive Archive;
		if (!Archive.Parse(DecompressedData))
		{
			continue;
		}

		// Check for BMD files with EVP1 block (skinning data)
		TArray<FString> BmdFiles = Archive.FindFiles(TEXT(".bmd"));
		bool bHasSkinning = false;

		for (const FString& BmdPath : BmdFiles)
		{
			TArray<uint8> BmdData = Archive.GetFile(BmdPath);
			if (BmdData.Num() < 0x20)
			{
				continue;
			}

			// Quick scan: look for EVP1 block magic in the file
			FBigEndianStream Stream(BmdData);
			uint32 Magic = Stream.ReadU32();
			if (Magic != 0x4A334431 && Magic != 0x4A334432) // J3D1 or J3D2
			{
				continue;
			}

			// Scan blocks for EVP1
			Stream.Skip(4); // type
			Stream.Skip(4); // file size
			uint32 BlockCount = Stream.ReadU32();
			Stream.Skip(16); // padding to 0x20

			for (uint32 b = 0; b < BlockCount && !Stream.IsEOF(); b++)
			{
				int64 BlockStart = Stream.Tell();
				if (BlockStart + 8 > Stream.Size()) break;
				uint32 BlockMagic = Stream.ReadU32();
				uint32 BlockSize = Stream.ReadU32();
				if (BlockMagic == 0x45565031) // EVP1
				{
					bHasSkinning = true;
					break;
				}
				if (BlockSize < 8) break;
				Stream.Seek(BlockStart + BlockSize);
			}

			if (bHasSkinning) break;
		}

		// Only include archives that have skinning or BCK animations
		TArray<FString> BckFiles = Archive.FindFiles(TEXT(".bck"));
		if (!bHasSkinning && BckFiles.Num() == 0)
		{
			continue;
		}

		// Build display name from filename
		FString FileName = FPaths::GetBaseFilename(FilePath);
		// Capitalize first letter
		if (FileName.Len() > 0)
		{
			FileName[0] = FChar::ToUpper(FileName[0]);
		}

		FSMSCharacterInfo CharInfo;
		CharInfo.ArchivePath = FilePath;
		CharInfo.DisplayName = FileName;
		CharInfo.BckFiles = BckFiles;
		CharInfo.bHasSkinning = bHasSkinning;

		Characters.Add(CharInfo);

		UE_LOG(LogSMSImporter, Log, TEXT("Found character archive: %s (%d BCK files, skinning=%s)"),
			*FilePath, BckFiles.Num(), bHasSkinning ? TEXT("yes") : TEXT("no"));
	}

	UE_LOG(LogSMSImporter, Log, TEXT("Character scan complete: %d character archives found"), Characters.Num());
	return Characters;
}

void FSMSSceneLoader::ImportCharacter(const FSMSCharacterInfo& Character,
	const TSet<FString>& SelectedBckFiles,
	const FSMSImportOptions& Options,
	FOnSMSImportProgress ProgressCallback)
{
	if (!ISOReader.IsValid())
	{
		UE_LOG(LogSMSImporter, Error, TEXT("No ISO open for character import"));
		return;
	}

	ReportProgress(ProgressCallback, 0.0f,
		FString::Printf(TEXT("Loading character: %s"), *Character.DisplayName));

	// Load and decompress the archive
	FRARCArchive Archive;
	if (!LoadAndDecompressScene(Character.ArchivePath, Archive))
	{
		UE_LOG(LogSMSImporter, Error, TEXT("Failed to load character archive: %s"), *Character.ArchivePath);
		return;
	}

	if (bCancelRequested) return;

	FString CharAssetPath = FString::Printf(TEXT("%s/Characters/%s"),
		*Options.OutputDirectory, *Character.DisplayName);

	// Find and parse the BMD file — select the best one for the character
	TArray<FString> BmdFiles = Archive.FindFiles(TEXT(".bmd"));
	if (BmdFiles.Num() == 0)
	{
		UE_LOG(LogSMSImporter, Warning, TEXT("No BMD files in character archive: %s"), *Character.ArchivePath);
		return;
	}

	ReportProgress(ProgressCallback, 0.1f, TEXT("Selecting best model..."));

	// Strategy: try to find a BMD named after the archive, otherwise pick the one with most joints
	FString ArchiveBaseName = FPaths::GetBaseFilename(Character.ArchivePath).ToLower();
	FString BestBmdPath;
	FBMDModel BestModel;
	int32 BestJointCount = -1;

	for (const FString& BmdPath : BmdFiles)
	{
		TArray<uint8> BmdData = Archive.GetFile(BmdPath);
		if (BmdData.Num() == 0) continue;

		FBMDModel CandidateModel;
		if (!FBMDLoader::Parse(BmdData, CandidateModel)) continue;

		FString BmdBaseName = FPaths::GetBaseFilename(BmdPath).ToLower();
		int32 JointCount = CandidateModel.Joints.Num();

		UE_LOG(LogSMSImporter, Log, TEXT("  BMD candidate: %s (%d joints, skinning=%s)"),
			*BmdPath, JointCount, CandidateModel.bHasSkinning ? TEXT("yes") : TEXT("no"));

		// Prefer: name match > most joints
		bool bNameMatch = (BmdBaseName == ArchiveBaseName);
		bool bBestNameMatch = !BestBmdPath.IsEmpty() &&
			(FPaths::GetBaseFilename(BestBmdPath).ToLower() == ArchiveBaseName);

		if (BestBmdPath.IsEmpty() ||
			(bNameMatch && !bBestNameMatch) ||
			(!bBestNameMatch && JointCount > BestJointCount))
		{
			BestBmdPath = BmdPath;
			BestModel = MoveTemp(CandidateModel);
			BestJointCount = JointCount;
		}
	}

	if (BestBmdPath.IsEmpty())
	{
		UE_LOG(LogSMSImporter, Error, TEXT("No valid BMD files in character archive: %s"), *Character.ArchivePath);
		return;
	}

	UE_LOG(LogSMSImporter, Log, TEXT("Selected BMD: %s (%d joints)"), *BestBmdPath, BestJointCount);

	ReportProgress(ProgressCallback, 0.2f, TEXT("Parsing model..."));

	FBMDModel& Model = BestModel;

	if (bCancelRequested) return;

	ReportProgress(ProgressCallback, 0.4f, TEXT("Creating skeletal mesh..."));

	// Create skeletal mesh if the model has joints (for animation support),
	// otherwise fall back to static mesh
	USkeletalMesh* SkelMesh = nullptr;
	if (Model.Joints.Num() > 1 || Model.bHasSkinning)
	{
		SkelMesh = FBMDLoader::CreateSkeletalMesh(GetTransientPackage(),
			Character.DisplayName, Model, CharAssetPath);
	}
	else
	{
		// Fall back to static mesh for non-skinned, single-joint models
		FBMDLoader::CreateStaticMesh(GetTransientPackage(),
			Character.DisplayName, Model, CharAssetPath);
		UE_LOG(LogSMSImporter, Log, TEXT("Character '%s' has no skeleton — imported as static mesh"),
			*Character.DisplayName);
	}

	if (bCancelRequested) return;

	// Import selected BCK animations
	if (SkelMesh && SelectedBckFiles.Num() > 0)
	{
		USkeleton* Skeleton = SkelMesh->GetSkeleton();
		int32 AnimIdx = 0;
		const int32 TotalAnims = SelectedBckFiles.Num();

		for (const FString& BckPath : SelectedBckFiles)
		{
			if (bCancelRequested) return;

			float AnimProgress = 0.5f + 0.5f * (static_cast<float>(AnimIdx) / FMath::Max(1, TotalAnims));
			ReportProgress(ProgressCallback, AnimProgress,
				FString::Printf(TEXT("Importing animation: %s"), *FPaths::GetBaseFilename(BckPath)));

			TArray<uint8> BckData = Archive.GetFile(BckPath);
			if (BckData.Num() == 0)
			{
				UE_LOG(LogSMSImporter, Warning, TEXT("Failed to read BCK file: %s"), *BckPath);
				AnimIdx++;
				continue;
			}

			FBCKAnimation BckAnim;
			if (FBCKLoader::Parse(BckData, BckAnim))
			{
				FString AnimName = FPaths::GetBaseFilename(BckPath);
				FBCKLoader::CreateAnimSequence(Skeleton, BckAnim,
					Model.Joints, Model.JointNames, AnimName, CharAssetPath);
			}
			else
			{
				UE_LOG(LogSMSImporter, Warning, TEXT("Failed to parse BCK: %s"), *BckPath);
			}

			AnimIdx++;
		}
	}

	ReportProgress(ProgressCallback, 1.0f,
		FString::Printf(TEXT("Character '%s' import complete"), *Character.DisplayName));
}

// ---- Progress reporting ----

void FSMSSceneLoader::ReportProgress(const FOnSMSImportProgress& Callback, float Progress, const FString& Message)
{
	UE_LOG(LogSMSImporter, Log, TEXT("[%.0f%%] %s"), Progress * 100.0f, *Message);

	if (Callback.IsBound())
	{
		Callback.Execute(Progress, Message);
	}
}
