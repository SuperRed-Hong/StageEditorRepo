// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystems/StageEditorSubsystem.h"
#include "Data/StageRegistryAsset.h"
#include "Actors/Stage.h"
#include "Subsystems/StageManagerSubsystem.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlWindowsModule.h"
#include "SourceControlOperations.h"
#include "SourceControlHelpers.h"
#include "PackageTools.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "DirectoryWatcherModule.h"
#include "IDirectoryWatcher.h"
#include "Containers/Ticker.h"

// Static member initialization
const FString UStageEditorSubsystem::RegistryChangelistName = TEXT("Auto-Registry-Updates");

DEFINE_LOG_CATEGORY_STATIC(LogStageEditorSubsystem, Log, All);

//----------------------------------------------------------------
// Lifecycle
//----------------------------------------------------------------

void UStageEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Subscribe to Stage lifecycle events from all RuntimeSubsystems
	// We use World delegates to handle multiple worlds
	FWorldDelegates::OnPostWorldInitialization.AddLambda(
		[this](UWorld* World, const UWorld::InitializationValues IVS)
		{
			if (UStageManagerSubsystem* RuntimeSub = World->GetSubsystem<UStageManagerSubsystem>())
			{
				RuntimeSub->OnStageLoadedDelegate.AddUObject(
					this, &UStageEditorSubsystem::HandleStageLoaded);
				RuntimeSub->OnStageUnloadedDelegate.AddUObject(
					this, &UStageEditorSubsystem::HandleStageUnloaded);

				// Phase 13.8: Preload metadata cache from Registry
				RefreshMetadataCacheFromRegistry(World);
			}
		});

	// Phase 13.8 Phase 3: Register file watcher and Source Control hooks
	RegisterRegistryFileWatcher();
	RegisterSourceControlHooks();

	UE_LOG(LogStageEditorSubsystem, Log, TEXT("StageEditorSubsystem initialized"));
}

void UStageEditorSubsystem::Deinitialize()
{
	// Phase 13.8 Phase 3: Unregister hooks
	UnregisterRegistryFileWatcher();
	UnregisterSourceControlHooks();

	// Clear cache
	LoadedRegistries.Empty();

	UE_LOG(LogStageEditorSubsystem, Log, TEXT("StageEditorSubsystem deinitialized"));

	Super::Deinitialize();
}

//----------------------------------------------------------------
// Registry Asset Management
//----------------------------------------------------------------

UStageRegistryAsset* UStageEditorSubsystem::GetOrLoadRegistryAsset(UWorld* Level)
{
	if (!Level)
	{
		UE_LOG(LogStageEditorSubsystem, Error, TEXT("GetOrLoadRegistryAsset: Level is nullptr"));
		return nullptr;
	}

	// Get Level soft object path for cache key
	FSoftObjectPath LevelPath(Level);

	// Check cache first
	if (UStageRegistryAsset** CachedRegistry = LoadedRegistries.Find(LevelPath))
	{
		if (*CachedRegistry && (*CachedRegistry)->IsValidLowLevel())
		{
			UE_LOG(LogStageEditorSubsystem, Verbose, TEXT("GetOrLoadRegistryAsset: Found in cache for Level '%s'"),
				*Level->GetName());
			return *CachedRegistry;
		}
		else
		{
			// Cache entry is stale, remove it
			LoadedRegistries.Remove(LevelPath);
		}
	}

	// Not in cache, search on disk
	FString ExistingAssetPath = FindExistingRegistryAsset(Level);
	if (ExistingAssetPath.IsEmpty())
	{
		// Verbose instead of Warning to avoid log spam from UI visibility checks
		UE_LOG(LogStageEditorSubsystem, Verbose, TEXT("GetOrLoadRegistryAsset: No Registry found for Level '%s'"),
			*Level->GetName());
		return nullptr;
	}

	// Load from disk
	UStageRegistryAsset* LoadedRegistry = LoadRegistryAsset(ExistingAssetPath);
	if (LoadedRegistry)
	{
		// Add to cache
		LoadedRegistries.Add(LevelPath, LoadedRegistry);
		UE_LOG(LogStageEditorSubsystem, Log, TEXT("GetOrLoadRegistryAsset: Loaded Registry from '%s'"),
			*ExistingAssetPath);
	}

	return LoadedRegistry;
}

UStageRegistryAsset* UStageEditorSubsystem::CreateRegistryAsset(UWorld* Level, ECollaborationMode Mode)
{
	if (!Level)
	{
		UE_LOG(LogStageEditorSubsystem, Error, TEXT("CreateRegistryAsset: Level is nullptr"));
		return nullptr;
	}

	// Check if Registry already exists
	if (DoesRegistryExist(Level))
	{
		UE_LOG(LogStageEditorSubsystem, Warning, TEXT("CreateRegistryAsset: Registry already exists for Level '%s'"),
			*Level->GetName());
		return GetOrLoadRegistryAsset(Level);
	}

	// Multi-user mode: Check Source Control
	if (Mode == ECollaborationMode::Multi)
	{
		if (!IsSourceControlEnabled())
		{
			UE_LOG(LogStageEditorSubsystem, Error,
				TEXT("CreateRegistryAsset: Multi-user mode requires Source Control to be enabled"));
			return nullptr;
		}
	}

	// Create new Registry asset
	FString AssetPath = GetRegistryAssetPath(Level);
	FString PackageName = AssetPath;
	FString AssetName = FPaths::GetBaseFilename(AssetPath);

	// Check if package already exists on disk (orphaned file without Asset Registry entry)
	FString PackageFileName = FPackageName::LongPackageNameToFilename(AssetPath, FPackageName::GetAssetPackageExtension());
	if (IFileManager::Get().FileExists(*PackageFileName))
	{
		UE_LOG(LogStageEditorSubsystem, Warning,
			TEXT("CreateRegistryAsset: Registry file already exists on disk at '%s', deleting orphaned file"), *PackageFileName);

		if (!IFileManager::Get().Delete(*PackageFileName, false, true))
		{
			UE_LOG(LogStageEditorSubsystem, Error,
				TEXT("CreateRegistryAsset: Failed to delete existing Registry file. Please delete manually: %s"), *PackageFileName);
			return nullptr;
		}
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UE_LOG(LogStageEditorSubsystem, Error, TEXT("CreateRegistryAsset: Failed to create package '%s'"),
			*PackageName);
		return nullptr;
	}

	// Create UStageRegistryAsset object
	UStageRegistryAsset* NewRegistry = NewObject<UStageRegistryAsset>(
		Package, *AssetName, RF_Public | RF_Standalone);

	if (!NewRegistry)
	{
		UE_LOG(LogStageEditorSubsystem, Error, TEXT("CreateRegistryAsset: Failed to create RegistryAsset object"));
		return nullptr;
	}

	// Initialize Registry
	TSoftObjectPtr<UWorld> LevelSoftPtr(Level);
	NewRegistry->Initialize(LevelSoftPtr, Mode);

	// Save to disk
	if (!SaveRegistryAsset(NewRegistry, PackageName))
	{
		UE_LOG(LogStageEditorSubsystem, Error, TEXT("CreateRegistryAsset: Failed to save Registry to disk"));
		return nullptr;
	}

	// Add to cache
	FSoftObjectPath LevelPath(Level);
	LoadedRegistries.Add(LevelPath, NewRegistry);

	// Add to Source Control (mark for add if new, or checkout if already tracked)
	if (!CheckOutRegistryFile(NewRegistry))
	{
		UE_LOG(LogStageEditorSubsystem, Warning,
			TEXT("CreateRegistryAsset: Failed to add/checkout Registry file to Source Control"));
	}

	UE_LOG(LogStageEditorSubsystem, Log, TEXT("CreateRegistryAsset: Created Registry at '%s' (Mode=%s)"),
		*AssetPath, Mode == ECollaborationMode::Solo ? TEXT("Solo") : TEXT("Multi"));

	return NewRegistry;
}

bool UStageEditorSubsystem::DoesRegistryExist(UWorld* Level) const
{
	if (!Level)
	{
		return false;
	}

	// Check cache (but validate it's still valid)
	FSoftObjectPath LevelPath(Level);
	if (LoadedRegistries.Contains(LevelPath))
	{
		UStageRegistryAsset* CachedRegistry = LoadedRegistries[LevelPath];
		// Verify cached Registry is still valid (not deleted)
		if (IsValid(CachedRegistry))
		{
			return true;
		}
		// Cache is stale, will check disk below
	}

	// Check disk
	FString ExistingAssetPath = FindExistingRegistryAsset(Level);
	return !ExistingAssetPath.IsEmpty();
}

void UStageEditorSubsystem::ClearRegistryCache(UWorld* Level)
{
	if (!Level)
	{
		return;
	}

	FSoftObjectPath LevelPath(Level);
	if (LoadedRegistries.Remove(LevelPath) > 0)
	{
		UE_LOG(LogStageEditorSubsystem, Log, TEXT("ClearRegistryCache: Cleared cache for Level '%s'"), *Level->GetName());
	}
}

FString UStageEditorSubsystem::GetRegistryAssetPath(UWorld* Level) const
{
	if (!Level)
	{
		return FString();
	}

	// Get Level package path
	FString LevelPackagePath = Level->GetPackage()->GetName();
	FString LevelName = FPaths::GetBaseFilename(LevelPackagePath);

	// Registry asset path: in Level data subfolder with unique naming
	// Folder: "LevelName_StageData" - clearly indicates data asset folder
	// Asset: "LevelName_StageRegistry" - uniquely identifies which level's Registry
	// Example: "/Game/Levels/MyLevel" → "/Game/Levels/MyLevel_StageData/MyLevel_StageRegistry"
	FString FolderName = FString::Printf(TEXT("%s_StageData"), *LevelName);
	FString AssetName = FString::Printf(TEXT("%s_StageRegistry"), *LevelName);
	FString RegistryPath = FPaths::Combine(FPaths::GetPath(LevelPackagePath), FolderName, AssetName);

	return RegistryPath;
}

//----------------------------------------------------------------
// Manual Registry Association
//----------------------------------------------------------------

void UStageEditorSubsystem::SetManualRegistryAssociation(UWorld* Level, const FString& RegistryPath)
{
	if (!Level)
	{
		return;
	}

	// Ensure associations are loaded
	if (!bManualAssociationsLoaded)
	{
		LoadManualAssociationsFromConfig();
	}

	FString LevelPackagePath = Level->GetPackage()->GetName();
	ManualRegistryAssociations.Add(LevelPackagePath, RegistryPath);

	// Clear cache to pick up new association
	ClearRegistryCache(Level);

	// Save to config
	SaveManualAssociationsToConfig();

	UE_LOG(LogStageEditorSubsystem, Log, TEXT("SetManualRegistryAssociation: Level '%s' -> Registry '%s'"),
		*LevelPackagePath, *RegistryPath);
}

void UStageEditorSubsystem::ClearManualRegistryAssociation(UWorld* Level)
{
	if (!Level)
	{
		return;
	}

	// Ensure associations are loaded
	if (!bManualAssociationsLoaded)
	{
		LoadManualAssociationsFromConfig();
	}

	FString LevelPackagePath = Level->GetPackage()->GetName();
	if (ManualRegistryAssociations.Remove(LevelPackagePath) > 0)
	{
		// Clear cache
		ClearRegistryCache(Level);

		// Save to config
		SaveManualAssociationsToConfig();

		UE_LOG(LogStageEditorSubsystem, Log, TEXT("ClearManualRegistryAssociation: Cleared for Level '%s'"),
			*LevelPackagePath);
	}
}

FString UStageEditorSubsystem::GetManualRegistryAssociation(UWorld* Level) const
{
	if (!Level)
	{
		return FString();
	}

	// Ensure associations are loaded (cast away const for lazy load)
	if (!bManualAssociationsLoaded)
	{
		const_cast<UStageEditorSubsystem*>(this)->LoadManualAssociationsFromConfig();
	}

	FString LevelPackagePath = Level->GetPackage()->GetName();
	const FString* RegistryPath = ManualRegistryAssociations.Find(LevelPackagePath);
	return RegistryPath ? *RegistryPath : FString();
}

void UStageEditorSubsystem::LoadManualAssociationsFromConfig()
{
	ManualRegistryAssociations.Empty();

	// Load from EditorPerProjectUserSettings.ini
	// Section: [StageEditor.ManualRegistryAssociations]
	// Format: LevelPath=RegistryPath

	const FString SectionName = TEXT("StageEditor.ManualRegistryAssociations");
	TArray<FString> ConfigLines;

	if (GConfig->GetSection(*SectionName, ConfigLines, GEditorPerProjectIni))
	{
		for (const FString& Line : ConfigLines)
		{
			FString LevelPath, RegistryPath;
			if (Line.Split(TEXT("="), &LevelPath, &RegistryPath))
			{
				ManualRegistryAssociations.Add(LevelPath, RegistryPath);
				UE_LOG(LogStageEditorSubsystem, Verbose, TEXT("LoadManualAssociationsFromConfig: '%s' -> '%s'"),
					*LevelPath, *RegistryPath);
			}
		}
	}

	bManualAssociationsLoaded = true;
	UE_LOG(LogStageEditorSubsystem, Log, TEXT("LoadManualAssociationsFromConfig: Loaded %d associations"),
		ManualRegistryAssociations.Num());
}

void UStageEditorSubsystem::SaveManualAssociationsToConfig()
{
	const FString SectionName = TEXT("StageEditor.ManualRegistryAssociations");

	// Clear existing section
	GConfig->EmptySection(*SectionName, GEditorPerProjectIni);

	// Write all associations
	for (const auto& Pair : ManualRegistryAssociations)
	{
		GConfig->SetString(*SectionName, *Pair.Key, *Pair.Value, GEditorPerProjectIni);
	}

	// Flush to disk
	GConfig->Flush(false, GEditorPerProjectIni);

	UE_LOG(LogStageEditorSubsystem, Log, TEXT("SaveManualAssociationsToConfig: Saved %d associations"),
		ManualRegistryAssociations.Num());
}

//----------------------------------------------------------------
// Source Control Integration
//----------------------------------------------------------------

bool UStageEditorSubsystem::IsSourceControlEnabled() const
{
	ISourceControlModule& SCModule = FModuleManager::LoadModuleChecked<ISourceControlModule>("SourceControl");
	return SCModule.IsEnabled() && SCModule.GetProvider().IsAvailable();
}

bool UStageEditorSubsystem::CheckOutRegistryFile(UStageRegistryAsset* Registry)
{
	if (!Registry)
	{
		return false;
	}

	if (!IsSourceControlEnabled())
	{
		// Source Control not enabled, treat as success (no-op)
		return true;
	}

	ISourceControlModule& SCModule = FModuleManager::LoadModuleChecked<ISourceControlModule>("SourceControl");
	ISourceControlProvider& SCProvider = SCModule.GetProvider();

	FString PackageFileName = SourceControlHelpers::PackageFilename(Registry->GetPackage());
	FSourceControlStatePtr FileState = SCProvider.GetState(PackageFileName, EStateCacheUsage::ForceUpdate);

	if (!FileState.IsValid())
	{
		UE_LOG(LogStageEditorSubsystem, Warning, TEXT("CheckOutRegistryFile: Failed to get file state for '%s'"),
			*PackageFileName);
		return false;
	}

	// Case 1: File is not in Source Control yet (new file) - need to Mark for Add
	if (!FileState->IsSourceControlled())
	{
		if (FileState->CanAdd())
		{
			TSharedRef<FMarkForAdd, ESPMode::ThreadSafe> AddOperation = ISourceControlOperation::Create<FMarkForAdd>();
			ECommandResult::Type Result = SCProvider.Execute(AddOperation, PackageFileName);

			if (Result == ECommandResult::Succeeded)
			{
				UE_LOG(LogStageEditorSubsystem, Log, TEXT("CheckOutRegistryFile: Successfully marked for add '%s'"),
					*PackageFileName);
				return true;
			}
			else
			{
				UE_LOG(LogStageEditorSubsystem, Error, TEXT("CheckOutRegistryFile: Failed to mark for add '%s'"),
					*PackageFileName);
				return false;
			}
		}
		else
		{
			UE_LOG(LogStageEditorSubsystem, Warning,
				TEXT("CheckOutRegistryFile: File '%s' is not in Source Control and cannot be added"),
				*PackageFileName);
			return false;
		}
	}

	// Case 2: File is already checked out
	if (FileState->IsCheckedOut())
	{
		UE_LOG(LogStageEditorSubsystem, Verbose, TEXT("CheckOutRegistryFile: File '%s' is already checked out"),
			*PackageFileName);
		return true;
	}

	// Case 3: File can be checked out
	if (FileState->CanCheckout())
	{
		TSharedRef<FCheckOut, ESPMode::ThreadSafe> CheckOutOperation = ISourceControlOperation::Create<FCheckOut>();
		ECommandResult::Type Result = SCProvider.Execute(CheckOutOperation, PackageFileName);

		if (Result == ECommandResult::Succeeded)
		{
			UE_LOG(LogStageEditorSubsystem, Log, TEXT("CheckOutRegistryFile: Successfully checked out '%s'"),
				*PackageFileName);
			return true;
		}
		else
		{
			UE_LOG(LogStageEditorSubsystem, Error, TEXT("CheckOutRegistryFile: Failed to check out '%s'"),
				*PackageFileName);
			return false;
		}
	}

	// Case 4: File is in Source Control but cannot be checked out (e.g., checked out by another user)
	UE_LOG(LogStageEditorSubsystem, Warning,
		TEXT("CheckOutRegistryFile: File '%s' cannot be checked out (may be locked by another user)"),
		*PackageFileName);
	return false;
}

//----------------------------------------------------------------
// Debugging & Validation
//----------------------------------------------------------------

bool UStageEditorSubsystem::ValidateAllRegistries(TArray<FString>& OutErrors) const
{
	OutErrors.Empty();

	if (LoadedRegistries.Num() == 0)
	{
		UE_LOG(LogStageEditorSubsystem, Log, TEXT("ValidateAllRegistries: No Registries loaded"));
		return true;
	}

	bool bAllValid = true;

	for (const auto& Pair : LoadedRegistries)
	{
		UStageRegistryAsset* Registry = Pair.Value;
		if (!Registry || !Registry->IsValidLowLevel())
		{
			OutErrors.Add(FString::Printf(TEXT("Registry for Level '%s' is invalid (nullptr or garbage)"),
				*Pair.Key.ToString()));
			bAllValid = false;
			continue;
		}

		TArray<FString> RegistryErrors;
		if (!Registry->ValidateIntegrity(RegistryErrors))
		{
			for (const FString& Error : RegistryErrors)
			{
				OutErrors.Add(FString::Printf(TEXT("[%s] %s"), *Registry->GetName(), *Error));
			}
			bAllValid = false;
		}
	}

	return bAllValid;
}

void UStageEditorSubsystem::DumpAllRegistriesToLog() const
{
	UE_LOG(LogStageEditorSubsystem, Display, TEXT("========== All Loaded Registries =========="));
	UE_LOG(LogStageEditorSubsystem, Display, TEXT("Total Loaded: %d"), LoadedRegistries.Num());

	if (LoadedRegistries.Num() == 0)
	{
		UE_LOG(LogStageEditorSubsystem, Display, TEXT("(No Registries loaded)"));
	}
	else
	{
		for (const auto& Pair : LoadedRegistries)
		{
			UStageRegistryAsset* Registry = Pair.Value;
			if (Registry && Registry->IsValidLowLevel())
			{
				UE_LOG(LogStageEditorSubsystem, Display, TEXT("Level: %s"), *Pair.Key.ToString());
				Registry->DumpToLog();
			}
			else
			{
				UE_LOG(LogStageEditorSubsystem, Warning, TEXT("Level: %s - INVALID REGISTRY"), *Pair.Key.ToString());
			}
		}
	}

	UE_LOG(LogStageEditorSubsystem, Display, TEXT("==========================================="));
}

//----------------------------------------------------------------
// Internal Helpers
//----------------------------------------------------------------

FString UStageEditorSubsystem::FindExistingRegistryAsset(UWorld* Level) const
{
	if (!Level)
	{
		return FString();
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Method 0 (Highest Priority): Check manual association first
	// This handles plugin export scenarios where Registry is in a different location
	FString ManualPath = GetManualRegistryAssociation(Level);
	if (!ManualPath.IsEmpty())
	{
		// Verify the asset exists
		FAssetData ManualAssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(ManualPath));
		if (ManualAssetData.IsValid())
		{
			UE_LOG(LogStageEditorSubsystem, Log,
				TEXT("FindExistingRegistryAsset: Using manual association: %s"),
				*ManualPath);
			return ManualPath;
		}
		else
		{
			// Manual association points to non-existent asset, warn user
			UE_LOG(LogStageEditorSubsystem, Warning,
				TEXT("FindExistingRegistryAsset: Manual association '%s' is invalid (asset not found)"),
				*ManualPath);
		}
	}

	// Method 1: Check expected path first (fast path for new naming convention)
	FString ExpectedPath = GetRegistryAssetPath(Level);
	FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(ExpectedPath));
	if (AssetData.IsValid())
	{
		return ExpectedPath;
	}

	// Method 2: Check file existence directly at expected path
	FString PackageFileName = FPackageName::LongPackageNameToFilename(ExpectedPath, FPackageName::GetAssetPackageExtension());
	if (IFileManager::Get().FileExists(*PackageFileName))
	{
		UE_LOG(LogStageEditorSubsystem, Verbose,
			TEXT("FindExistingRegistryAsset: Found Registry file on disk (Asset Registry not ready): %s"),
			*PackageFileName);
		return ExpectedPath;
	}

	// Method 3: Search for *_StageRegistry assets in level's directory and subdirectories
	// This handles legacy naming conventions and plugin export scenarios
	FString LevelPackagePath = Level->GetPackage()->GetName();
	FString LevelDirectory = FPaths::GetPath(LevelPackagePath);

	FARFilter Filter;
	Filter.ClassPaths.Add(UStageRegistryAsset::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(FName(*LevelDirectory));
	Filter.bRecursivePaths = true;  // Search subdirectories too

	TArray<FAssetData> FoundAssets;
	AssetRegistry.GetAssets(Filter, FoundAssets);

	// Find any asset ending with "_StageRegistry" or named "StageRegistry"
	for (const FAssetData& Asset : FoundAssets)
	{
		FString AssetName = Asset.AssetName.ToString();
		if (AssetName.EndsWith(TEXT("_StageRegistry")) || AssetName == TEXT("StageRegistry"))
		{
			FString FoundPath = Asset.GetSoftObjectPath().GetAssetPath().ToString();
			UE_LOG(LogStageEditorSubsystem, Log,
				TEXT("FindExistingRegistryAsset: Found Registry in level directory: %s"),
				*FoundPath);
			return FoundPath;
		}
	}

	return FString();
}

UStageRegistryAsset* UStageEditorSubsystem::LoadRegistryAsset(const FString& AssetPath)
{
	if (AssetPath.IsEmpty())
	{
		return nullptr;
	}

	UStageRegistryAsset* LoadedRegistry = LoadObject<UStageRegistryAsset>(nullptr, *AssetPath);
	if (!LoadedRegistry)
	{
		UE_LOG(LogStageEditorSubsystem, Error, TEXT("LoadRegistryAsset: Failed to load asset at '%s'"), *AssetPath);
		return nullptr;
	}

	return LoadedRegistry;
}

bool UStageEditorSubsystem::SaveRegistryAsset(UStageRegistryAsset* Registry, const FString& AssetPath)
{
	if (!Registry)
	{
		return false;
	}

	UPackage* Package = Registry->GetPackage();
	if (!Package)
	{
		UE_LOG(LogStageEditorSubsystem, Error, TEXT("SaveRegistryAsset: Registry has no package"));
		return false;
	}

	// Mark package dirty
	Package->MarkPackageDirty();

	// Save package
	FString PackageFileName = FPackageName::LongPackageNameToFilename(AssetPath, FPackageName::GetAssetPackageExtension());

	UE_LOG(LogStageEditorSubsystem, Log, TEXT("SaveRegistryAsset: Attempting to save..."));
	UE_LOG(LogStageEditorSubsystem, Log, TEXT("  AssetPath: %s"), *AssetPath);
	UE_LOG(LogStageEditorSubsystem, Log, TEXT("  PackageFileName: %s"), *PackageFileName);

	// Check if directory exists, create if needed
	FString DirectoryPath = FPaths::GetPath(PackageFileName);
	if (!IFileManager::Get().DirectoryExists(*DirectoryPath))
	{
		UE_LOG(LogStageEditorSubsystem, Warning, TEXT("SaveRegistryAsset: Directory does not exist, creating: %s"), *DirectoryPath);
		if (!IFileManager::Get().MakeDirectory(*DirectoryPath, true))
		{
			UE_LOG(LogStageEditorSubsystem, Error, TEXT("SaveRegistryAsset: Failed to create directory: %s"), *DirectoryPath);
			return false;
		}
	}

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;

	bool bSaveSucceeded = UPackage::SavePackage(Package, Registry, *PackageFileName, SaveArgs);

	if (bSaveSucceeded)
	{
		UE_LOG(LogStageEditorSubsystem, Log, TEXT("SaveRegistryAsset: Successfully saved to '%s'"), *PackageFileName);
	}
	else
	{
		UE_LOG(LogStageEditorSubsystem, Error, TEXT("SaveRegistryAsset: Failed to save to '%s'"), *PackageFileName);
		UE_LOG(LogStageEditorSubsystem, Error, TEXT("  Possible reasons:"));
		UE_LOG(LogStageEditorSubsystem, Error, TEXT("  1. Directory is read-only (e.g., inside Plugin folder)"));
		UE_LOG(LogStageEditorSubsystem, Error, TEXT("  2. Insufficient permissions"));
		UE_LOG(LogStageEditorSubsystem, Error, TEXT("  3. Path contains invalid characters"));
	}

	return bSaveSucceeded;
}

//----------------------------------------------------------------
// Phase 13.5: Multi-User Registry Sync
//----------------------------------------------------------------

FRegistryLockInfo UStageEditorSubsystem::GetRegistryLockInfo(UStageRegistryAsset* Registry)
{
	FRegistryLockInfo Info;

	if (!Registry)
	{
		return Info;
	}

	if (!IsSourceControlEnabled())
	{
		// SC not enabled, treat as unlocked
		return Info;
	}

	ISourceControlModule& SCModule = FModuleManager::LoadModuleChecked<ISourceControlModule>("SourceControl");
	ISourceControlProvider& SCProvider = SCModule.GetProvider();

	// Get file path
	FString FilePath = SourceControlHelpers::PackageFilename(Registry->GetPackage());

	// Update status from server (BLOCKING call - use GetCachedRegistryLockInfo for UI)
	TSharedRef<FUpdateStatus> UpdateOp = ISourceControlOperation::Create<FUpdateStatus>();
	SCProvider.Execute(UpdateOp, FilePath);

	// Get state
	FSourceControlStatePtr FileState = SCProvider.GetState(FilePath, EStateCacheUsage::Use);
	if (FileState.IsValid())
	{
		Info.bIsCheckedOut = FileState->IsCheckedOut();
		Info.bIsCheckedOutByMe = FileState->IsCheckedOut() && !FileState->IsCheckedOutOther();
		Info.bIsCheckedOutByOther = FileState->IsCheckedOutOther();

		if (Info.bIsCheckedOutByOther)
		{
			// Use IsCheckedOutOther with output parameter to get the user name
			FString WhoHasIt;
			FileState->IsCheckedOutOther(&WhoHasIt);
			Info.OtherUserName = WhoHasIt;
		}
	}

	// Update cache
	CachedLockInfo = Info;
	LastLockInfoQueryTime = FPlatformTime::Seconds();

	return Info;
}

FRegistryLockInfo UStageEditorSubsystem::GetCachedRegistryLockInfo(UStageRegistryAsset* Registry, bool bForceRefresh)
{
	if (!Registry)
	{
		return FRegistryLockInfo();
	}

	if (!IsSourceControlEnabled())
	{
		// SC not enabled, treat as unlocked
		return FRegistryLockInfo();
	}

	const double CurrentTime = FPlatformTime::Seconds();
	const bool bCacheExpired = (CurrentTime - LastLockInfoQueryTime) >= LockInfoQueryThrottleInterval;

	// Return cached result if still valid and not forcing refresh
	if (!bForceRefresh && !bCacheExpired)
	{
		UE_LOG(LogStageEditorSubsystem, Verbose, TEXT("GetCachedRegistryLockInfo: Using cached result (%.1fs old)"),
			CurrentTime - LastLockInfoQueryTime);
		return CachedLockInfo;
	}

	// Cache expired or force refresh - query server
	UE_LOG(LogStageEditorSubsystem, Verbose, TEXT("GetCachedRegistryLockInfo: Querying server (cache %s)"),
		bForceRefresh ? TEXT("force refresh") : TEXT("expired"));

	return GetRegistryLockInfo(Registry);
}

FSourceControlChangelistStatePtr UStageEditorSubsystem::GetOrCreateRegistryChangelist()
{
	if (!IsSourceControlEnabled())
	{
		return nullptr;
	}

	ISourceControlModule& SCModule = FModuleManager::LoadModuleChecked<ISourceControlModule>("SourceControl");
	ISourceControlProvider& SCProvider = SCModule.GetProvider();

	// Phase 18.1: Use cached Changelist if valid (avoid expensive server query)
	if (bChangelistCacheValid && CachedRegistryChangelist.IsValid())
	{
		// Verify cached changelist still exists using local cache (cheap operation)
		FSourceControlChangelistStatePtr CLState = SCProvider.GetState(
			CachedRegistryChangelist.ToSharedRef(), EStateCacheUsage::Use);
		if (CLState.IsValid())
		{
			UE_LOG(LogStageEditorSubsystem, Verbose, TEXT("GetOrCreateRegistryChangelist: Using cached changelist"));
			return CLState;
		}
		// Cache invalid, clear it
		bChangelistCacheValid = false;
		CachedRegistryChangelist.Reset();
	}

	// Get all pending changelists - use local cache first, only ForceUpdate if needed
	TArray<FSourceControlChangelistRef> Changelists = SCProvider.GetChangelists(EStateCacheUsage::Use);

	// Find existing changelist with our name
	for (const FSourceControlChangelistRef& CL : Changelists)
	{
		// Get changelist state to check description
		FSourceControlChangelistStatePtr CLState = SCProvider.GetState(CL, EStateCacheUsage::Use);
		if (CLState.IsValid() && CLState->GetDescriptionText().ToString().Contains(RegistryChangelistName))
		{
			UE_LOG(LogStageEditorSubsystem, Verbose, TEXT("GetOrCreateRegistryChangelist: Found existing changelist"));
			// Cache the found changelist
			CachedRegistryChangelist = CL;
			bChangelistCacheValid = true;
			return CLState;
		}
	}

	// Not found in cache, try ForceUpdate to check server (only when actually needed)
	Changelists = SCProvider.GetChangelists(EStateCacheUsage::ForceUpdate);
	for (const FSourceControlChangelistRef& CL : Changelists)
	{
		FSourceControlChangelistStatePtr CLState = SCProvider.GetState(CL, EStateCacheUsage::Use);
		if (CLState.IsValid() && CLState->GetDescriptionText().ToString().Contains(RegistryChangelistName))
		{
			UE_LOG(LogStageEditorSubsystem, Verbose, TEXT("GetOrCreateRegistryChangelist: Found existing changelist (after refresh)"));
			CachedRegistryChangelist = CL;
			bChangelistCacheValid = true;
			return CLState;
		}
	}

	// Create new changelist using FNewChangelist operation
	TSharedRef<FNewChangelist> NewCLOp = ISourceControlOperation::Create<FNewChangelist>();
	NewCLOp->SetDescription(FText::Format(
		NSLOCTEXT("StageEditor", "RegistryCLDesc", "[{0}] Stage Editor Auto Updates - Review and submit manually"),
		FText::FromString(RegistryChangelistName)
	));

	ECommandResult::Type Result = SCProvider.Execute(NewCLOp);
	if (Result == ECommandResult::Succeeded)
	{
		FSourceControlChangelistPtr NewChangelist = NewCLOp->GetNewChangelist();
		if (NewChangelist.IsValid())
		{
			UE_LOG(LogStageEditorSubsystem, Log, TEXT("GetOrCreateRegistryChangelist: Created new changelist '%s'"),
				*RegistryChangelistName);

			// Cache the new changelist
			CachedRegistryChangelist = NewChangelist;
			bChangelistCacheValid = true;

			// Get state for the new changelist
			FSourceControlChangelistRef CLRef = NewChangelist.ToSharedRef();
			return SCProvider.GetState(CLRef, EStateCacheUsage::Use);
		}
	}

	UE_LOG(LogStageEditorSubsystem, Warning, TEXT("GetOrCreateRegistryChangelist: Failed to create changelist"));
	return nullptr;
}

void UStageEditorSubsystem::AppendStageChangeToChangelist(int32 StageID, const FString& StageName, bool bAdded)
{
	if (!IsSourceControlEnabled())
	{
		return;
	}

	FSourceControlChangelistStatePtr ChangelistState = GetOrCreateRegistryChangelist();
	if (!ChangelistState.IsValid())
	{
		UE_LOG(LogStageEditorSubsystem, Warning, TEXT("AppendStageChangeToChangelist: Failed to get changelist"));
		return;
	}

	ISourceControlModule& SCModule = FModuleManager::LoadModuleChecked<ISourceControlModule>("SourceControl");
	ISourceControlProvider& SCProvider = SCModule.GetProvider();

	// Get current description
	FString CurrentDesc = ChangelistState->GetDescriptionText().ToString();

	// Build change entry: [+] or [-] followed by SUID and name
	FString ChangeEntry = FString::Printf(TEXT("\n%s SUID:%d \"%s\""),
		bAdded ? TEXT("[+]") : TEXT("[-]"),
		StageID,
		*StageName);

	// Check if this exact entry already exists (avoid duplicates)
	if (CurrentDesc.Contains(ChangeEntry))
	{
		UE_LOG(LogStageEditorSubsystem, Verbose, TEXT("AppendStageChangeToChangelist: Entry already exists"));
		return;
	}

	// Append new entry to description
	FString NewDesc = CurrentDesc + ChangeEntry;

	// Update changelist description using FEditChangelist operation
	TSharedRef<FEditChangelist> EditOp = ISourceControlOperation::Create<FEditChangelist>();
	EditOp->SetDescription(FText::FromString(NewDesc));

	ECommandResult::Type Result = SCProvider.Execute(EditOp, ChangelistState->GetChangelist());
	if (Result == ECommandResult::Succeeded)
	{
		UE_LOG(LogStageEditorSubsystem, Log, TEXT("AppendStageChangeToChangelist: %s Stage SUID:%d '%s'"),
			bAdded ? TEXT("Added") : TEXT("Removed"), StageID, *StageName);
	}
	else
	{
		UE_LOG(LogStageEditorSubsystem, Warning, TEXT("AppendStageChangeToChangelist: Failed to update description"));
	}
}

void UStageEditorSubsystem::AppendRegistryCreationToChangelist(const FString& MapName, int32 StageCount, ECollaborationMode Mode)
{
	if (!IsSourceControlEnabled())
	{
		return;
	}

	FSourceControlChangelistStatePtr ChangelistState = GetOrCreateRegistryChangelist();
	if (!ChangelistState.IsValid())
	{
		UE_LOG(LogStageEditorSubsystem, Warning, TEXT("AppendRegistryCreationToChangelist: Failed to get changelist"));
		return;
	}

	ISourceControlModule& SCModule = FModuleManager::LoadModuleChecked<ISourceControlModule>("SourceControl");
	ISourceControlProvider& SCProvider = SCModule.GetProvider();

	FString CurrentDesc = ChangelistState->GetDescriptionText().ToString();

	FString ModeStr = (Mode == ECollaborationMode::Multi) ? TEXT("Multi") : TEXT("Solo");
	FString CreationEntry = FString::Printf(TEXT("\n[Registry Created] Map: %s | Stages: %d | Mode: %s"),
		*MapName, StageCount, *ModeStr);

	if (CurrentDesc.Contains(CreationEntry))
	{
		UE_LOG(LogStageEditorSubsystem, Verbose, TEXT("AppendRegistryCreationToChangelist: Entry already exists"));
		return;
	}

	FString NewDesc = CurrentDesc + CreationEntry;

	TSharedRef<FEditChangelist> EditOp = ISourceControlOperation::Create<FEditChangelist>();
	EditOp->SetDescription(FText::FromString(NewDesc));

	ECommandResult::Type Result = SCProvider.Execute(EditOp, ChangelistState->GetChangelist());
	if (Result == ECommandResult::Succeeded)
	{
		UE_LOG(LogStageEditorSubsystem, Log, TEXT("AppendRegistryCreationToChangelist: Map=%s, Stages=%d, Mode=%s"),
			*MapName, StageCount, *ModeStr);
	}
	else
	{
		UE_LOG(LogStageEditorSubsystem, Warning, TEXT("AppendRegistryCreationToChangelist: Failed to update description"));
	}
}

bool UStageEditorSubsystem::CheckOutToChangelist(UStageRegistryAsset* Registry, FString& OutErrorMessage)
{
	if (!Registry)
	{
		OutErrorMessage = TEXT("Registry is null");
		return false;
	}

	if (!IsSourceControlEnabled())
	{
		// SC not enabled, treat as success (no-op for Solo mode)
		return true;
	}

	ISourceControlModule& SCModule = FModuleManager::LoadModuleChecked<ISourceControlModule>("SourceControl");
	ISourceControlProvider& SCProvider = SCModule.GetProvider();

	FString FilePath = SourceControlHelpers::PackageFilename(Registry->GetPackage());
	TArray<FString> Files;
	Files.Add(FilePath);

	// Update status first
	TSharedRef<FUpdateStatus> UpdateOp = ISourceControlOperation::Create<FUpdateStatus>();
	SCProvider.Execute(UpdateOp, Files);

	// Check current state
	FSourceControlStatePtr FileState = SCProvider.GetState(FilePath, EStateCacheUsage::Use);
	if (FileState.IsValid())
	{
		// Already checked out by me
		if (FileState->IsCheckedOut() && !FileState->IsCheckedOutOther())
		{
			UE_LOG(LogStageEditorSubsystem, Verbose, TEXT("CheckOutToChangelist: Already checked out by me"));
			return true;
		}

		// Checked out by someone else
		if (FileState->IsCheckedOutOther())
		{
			FString WhoHasIt;
			FileState->IsCheckedOutOther(&WhoHasIt);
			OutErrorMessage = FString::Printf(TEXT("Locked by %s"), *WhoHasIt);
			UE_LOG(LogStageEditorSubsystem, Warning, TEXT("CheckOutToChangelist: %s"), *OutErrorMessage);
			return false;
		}
	}

	// Get or create our changelist
	FSourceControlChangelistStatePtr ChangelistState = GetOrCreateRegistryChangelist();

	// Execute CheckOut
	TSharedRef<FCheckOut> CheckOutOp = ISourceControlOperation::Create<FCheckOut>();
	ECommandResult::Type Result = SCProvider.Execute(CheckOutOp, Files);

	if (Result != ECommandResult::Succeeded)
	{
		// Try MarkForAdd if file is new
		FSourceControlStatePtr State = SCProvider.GetState(FilePath, EStateCacheUsage::Use);
		if (State.IsValid() && !State->IsSourceControlled() && State->CanAdd())
		{
			TSharedRef<FMarkForAdd> AddOp = ISourceControlOperation::Create<FMarkForAdd>();
			Result = SCProvider.Execute(AddOp, Files);

			if (Result == ECommandResult::Succeeded)
			{
				UE_LOG(LogStageEditorSubsystem, Log, TEXT("CheckOutToChangelist: Marked for add '%s'"), *FilePath);
			}
		}

		if (Result != ECommandResult::Succeeded)
		{
			OutErrorMessage = TEXT("CheckOut操作失败");
			UE_LOG(LogStageEditorSubsystem, Error, TEXT("CheckOutToChangelist: Failed to checkout '%s'"), *FilePath);
			return false;
		}
	}

	// Move to our changelist using FMoveToChangelist operation
	if (ChangelistState.IsValid())
	{
		TSharedRef<FMoveToChangelist> MoveOp = ISourceControlOperation::Create<FMoveToChangelist>();
		ECommandResult::Type MoveResult = SCProvider.Execute(MoveOp, ChangelistState->GetChangelist(), Files);
		if (MoveResult == ECommandResult::Succeeded)
		{
			UE_LOG(LogStageEditorSubsystem, Log, TEXT("CheckOutToChangelist: Moved to changelist '%s'"), *RegistryChangelistName);
		}
		else
		{
			UE_LOG(LogStageEditorSubsystem, Warning, TEXT("CheckOutToChangelist: Failed to move to changelist, file remains in default changelist"));
		}
	}

	UE_LOG(LogStageEditorSubsystem, Log, TEXT("CheckOutToChangelist: Successfully checked out '%s'"), *FilePath);
	return true;
}

bool UStageEditorSubsystem::SaveRegistryToDisk(UStageRegistryAsset* Registry)
{
	if (!Registry)
	{
		return false;
	}

	UPackage* Package = Registry->GetPackage();
	if (!Package)
	{
		UE_LOG(LogStageEditorSubsystem, Error, TEXT("SaveRegistryToDisk: Registry has no package"));
		return false;
	}

	FString PackageFileName = SourceControlHelpers::PackageFilename(Package);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;

	bool bSaved = UPackage::SavePackage(Package, Registry, *PackageFileName, SaveArgs);

	if (bSaved)
	{
		UE_LOG(LogStageEditorSubsystem, Log, TEXT("SaveRegistryToDisk: Successfully saved '%s'"), *PackageFileName);
	}
	else
	{
		UE_LOG(LogStageEditorSubsystem, Error, TEXT("SaveRegistryToDisk: Failed to save '%s'"), *PackageFileName);
	}

	return bSaved;
}

void UStageEditorSubsystem::OpenChangelistPanel()
{
	// Use the SourceControlWindows module to open the Changelist tab
	if (FModuleManager::Get().IsModuleLoaded("SourceControlWindows"))
	{
		ISourceControlWindowsModule& SCWindowsModule = FModuleManager::LoadModuleChecked<ISourceControlWindowsModule>("SourceControlWindows");
		if (SCWindowsModule.CanShowChangelistsTab())
		{
			SCWindowsModule.ShowChangelistsTab();
		}
	}
	else
	{
		// Fallback: Try to invoke tab directly with correct name
		FGlobalTabmanager::Get()->TryInvokeTab(FName("SourceControlChangelists"));
	}

	// Show notification to remind user
	FNotificationInfo Info(NSLOCTEXT("StageEditor", "ReviewChangelist",
		"Registry modified and saved. Please review in Changelist panel and submit manually."));
	Info.ExpireDuration = 5.0f;
	Info.bUseThrobber = false;
	Info.Image = FCoreStyle::Get().GetBrush("Icons.InfoWithColor");
	FSlateNotificationManager::Get().AddNotification(Info);
}

//----------------------------------------------------------------
// Stage Registration
//----------------------------------------------------------------

int32 UStageEditorSubsystem::RegisterStage(AStage* Stage)
{
	if (!Stage)
	{
		UE_LOG(LogStageEditorSubsystem, Error, TEXT("RegisterStage: Stage is null"));
		return -1;
	}

	UWorld* World = Stage->GetWorld();
	if (!World)
	{
		UE_LOG(LogStageEditorSubsystem, Error, TEXT("RegisterStage: World is null"));
		return -1;
	}

	// 1. Get Registry
	UStageRegistryAsset* Registry = GetOrLoadRegistryAsset(World);
	if (!Registry)
	{
		UE_LOG(LogStageEditorSubsystem, Error,
			TEXT("RegisterStage: No Registry for Level. Create one first."));
		return -1;
	}

	// 2. Multi-user mode check and CheckOut
	if (Registry->CollaborationMode == ECollaborationMode::Multi)
	{
		// Critical: Multi-user mode requires SC to be available
		if (!IsSourceControlEnabled())
		{
			UE_LOG(LogStageEditorSubsystem, Error,
				TEXT("RegisterStage: Source Control is OFFLINE. Cannot register Stage in Multi-user mode."));

			// Show notification to user
			FNotificationInfo Info(NSLOCTEXT("StageEditor", "SCOfflineError",
				"Source Control is offline! Cannot register Stage in Multi-user mode.\nPlease connect to Source Control or switch to Solo mode."));
			Info.ExpireDuration = 8.0f;
			Info.bUseThrobber = false;
			Info.Image = FCoreStyle::Get().GetBrush("Icons.ErrorWithColor");
			FSlateNotificationManager::Get().AddNotification(Info);

			return -1;  // Abort registration
		}

		FString ErrorMsg;
		if (!CheckOutToChangelist(Registry, ErrorMsg))
		{
			UE_LOG(LogStageEditorSubsystem, Error,
				TEXT("RegisterStage: Failed to CheckOut Registry - %s"), *ErrorMsg);

			// Show notification to user
			FNotificationInfo Info(FText::Format(
				NSLOCTEXT("StageEditor", "CheckOutFailed",
					"Failed to check out Registry: {0}\nCannot register Stage in Multi-user mode."),
				FText::FromString(ErrorMsg)));
			Info.ExpireDuration = 8.0f;
			Info.bUseThrobber = false;
			Info.Image = FCoreStyle::Get().GetBrush("Icons.ErrorWithColor");
			FSlateNotificationManager::Get().AddNotification(Info);

			return -1;  // Abort registration
		}
	}

	// 3. Allocate StageID
	int32 NewStageID = Registry->AllocateAndRegister(Stage);
	if (NewStageID <= 0)
	{
		UE_LOG(LogStageEditorSubsystem, Error, TEXT("RegisterStage: AllocateAndRegister failed"));
		return -1;
	}

	// 4. Update Stage's SUID
	Stage->SUID.StageID = NewStageID;
	Stage->MarkPackageDirty();

	// 5. Add to runtime cache
	UStageManagerSubsystem* RuntimeSub = World->GetSubsystem<UStageManagerSubsystem>();
	if (RuntimeSub)
	{
		RuntimeSub->AddStageToRuntimeCache(Stage);
	}

	// 6. Mark Registry dirty
	Registry->MarkPackageDirty();

	// 7. Record to Changelist
	AppendStageChangeToChangelist(NewStageID, Stage->GetStageName(), true);

	UE_LOG(LogStageEditorSubsystem, Log,
		TEXT("Registered Stage '%s' with ID %d"),
		*Stage->GetStageName(), NewStageID);

	return NewStageID;
}

bool UStageEditorSubsystem::UnregisterStage(AStage* Stage)
{
	if (!Stage)
	{
		return false;
	}

	int32 StageID = Stage->SUID.StageID;
	if (StageID <= 0)
	{
		return false;
	}

	UWorld* World = Stage->GetWorld();
	if (!World)
	{
		return false;
	}

	// 1. Get Registry
	UStageRegistryAsset* Registry = GetOrLoadRegistryAsset(World);
	if (!Registry)
	{
		return false;
	}

	// 2. Multi-user mode CheckOut
	if (Registry->CollaborationMode == ECollaborationMode::Multi)
	{
		// Critical: Multi-user mode requires SC to be available
		if (!IsSourceControlEnabled())
		{
			UE_LOG(LogStageEditorSubsystem, Error,
				TEXT("UnregisterStage: Source Control is OFFLINE. Cannot unregister Stage in Multi-user mode."));

			// Show notification to user
			FNotificationInfo Info(NSLOCTEXT("StageEditor", "SCOfflineErrorUnregister",
				"Source Control is offline! Cannot delete Stage in Multi-user mode.\nPlease connect to Source Control or switch to Solo mode."));
			Info.ExpireDuration = 8.0f;
			Info.bUseThrobber = false;
			Info.Image = FCoreStyle::Get().GetBrush("Icons.ErrorWithColor");
			FSlateNotificationManager::Get().AddNotification(Info);

			return false;  // Abort unregistration
		}

		FString ErrorMsg;
		if (!CheckOutToChangelist(Registry, ErrorMsg))
		{
			UE_LOG(LogStageEditorSubsystem, Error,
				TEXT("UnregisterStage: Failed to CheckOut Registry - %s"), *ErrorMsg);

			// Show notification to user
			FNotificationInfo Info(FText::Format(
				NSLOCTEXT("StageEditor", "CheckOutFailedUnregister",
					"Failed to check out Registry: {0}\nCannot delete Stage in Multi-user mode."),
				FText::FromString(ErrorMsg)));
			Info.ExpireDuration = 8.0f;
			Info.bUseThrobber = false;
			Info.Image = FCoreStyle::Get().GetBrush("Icons.ErrorWithColor");
			FSlateNotificationManager::Get().AddNotification(Info);

			return false;  // Abort unregistration
		}
	}

	// 3. Remove from Registry
	FString StageName = Stage->GetStageName();
	Registry->Unregister(StageID);
	Registry->MarkPackageDirty();

	// 4. Remove from runtime cache
	UStageManagerSubsystem* RuntimeSub = World->GetSubsystem<UStageManagerSubsystem>();
	if (RuntimeSub)
	{
		RuntimeSub->RemoveStageFromRuntimeCache(StageID);
	}

	// 5. Record to Changelist
	AppendStageChangeToChangelist(StageID, StageName, false);

	UE_LOG(LogStageEditorSubsystem, Log,
		TEXT("Unregistered Stage '%s' (ID:%d)"), *StageName, StageID);

	return true;
}

AStage* UStageEditorSubsystem::FindStageByDataLayerInRegistry(UDataLayerAsset* DataLayerAsset, UWorld* World) const
{
	if (!DataLayerAsset)
	{
		return nullptr;
	}

	// Use editor world if not specified
	if (!World)
	{
		if (!GEditor)
		{
			return nullptr;
		}
		World = GEditor->GetEditorWorldContext().World();
	}

	if (!World)
	{
		return nullptr;
	}

	// Get RuntimeSubsystem for cache access
	UStageManagerSubsystem* RuntimeSub = World->GetSubsystem<UStageManagerSubsystem>();
	if (!RuntimeSub)
	{
		return nullptr;
	}

	// ============================================================
	// Step 1: Query MetadataCache (NEW - Phase 13.8 Phase 2)
	// ============================================================
	// MetadataCache is preloaded from Registry at Initialize()
	// Decoupled from PostLoad events - always available
	FStageMetadata* Metadata = RuntimeSub->FindMetadataByDataLayer(DataLayerAsset);
	if (Metadata && Metadata->StageID > 0)
	{
		// ✅ Found StageID in metadata, try to get Actor object
		AStage* Stage = RuntimeSub->GetStage(Metadata->StageID);

		if (Stage)
		{
			// ✅ Perfect: Metadata and Actor both found
			UE_LOG(LogStageEditorSubsystem, Verbose,
				TEXT("FindStageByDataLayerInRegistry: Found Stage '%s' (ID:%d) via MetadataCache"),
				*Stage->GetActorLabel(), Metadata->StageID);
			return Stage;
		}
		else
		{
			// ⚠️ Metadata found but Actor not loaded (World Partition scenario)
			UE_LOG(LogStageEditorSubsystem, Log,
				TEXT("FindStageByDataLayerInRegistry: Metadata found for StageID %d, but Actor not loaded (World Partition?)"),
				Metadata->StageID);

			// TODO: If needed, trigger Actor loading here
			// For now, return nullptr indicating "known StageID but Actor not loaded"
			return nullptr;
		}
	}

	// ============================================================
	// Step 2: Fallback - Direct World iteration (兼容旧数据)
	// ============================================================
	// This branch triggers only when:
	// 1. MetadataCache not yet updated (Registry externally modified)
	// 2. Newly created Stage not yet synced to Registry

	UE_LOG(LogStageEditorSubsystem, Verbose,
		TEXT("FindStageByDataLayerInRegistry: MetadataCache miss for DataLayer '%s', using Fallback query"),
		*DataLayerAsset->GetName());

	TArray<AStage*> AllStages = RuntimeSub->GetAllStages();

	for (AStage* Stage : AllStages)
	{
		if (!Stage) continue;

		// Check Stage-level DataLayer
		if (Stage->StageDataLayerAsset == DataLayerAsset)
		{
			UE_LOG(LogStageEditorSubsystem, Verbose,
				TEXT("FindStageByDataLayerInRegistry: Found Stage '%s' (ID:%d) via Fallback (Stage DataLayer)"),
				*Stage->GetActorLabel(), Stage->GetStageID());
			return Stage;
		}

		// Check Act-level DataLayers
		for (const FAct& Act : Stage->Acts)
		{
			if (Act.AssociatedDataLayer == DataLayerAsset)
			{
				UE_LOG(LogStageEditorSubsystem, Verbose,
					TEXT("FindStageByDataLayerInRegistry: Found Stage '%s' (ID:%d) via Fallback (Act DataLayer)"),
					*Stage->GetActorLabel(), Stage->GetStageID());
				return Stage;
			}
		}
	}

	// Truly not found
	UE_LOG(LogStageEditorSubsystem, Verbose,
		TEXT("FindStageByDataLayerInRegistry: No Stage found for DataLayer '%s'"),
		*DataLayerAsset->GetName());

	return nullptr;
}

void UStageEditorSubsystem::HandleStageLoaded(AStage* Stage)
{
	if (!Stage)
	{
		UE_LOG(LogStageEditorSubsystem, Warning, TEXT("HandleStageLoaded: Stage is nullptr"));
		return;
	}

	UWorld* World = Stage->GetWorld();
	if (!World || World->IsPlayInEditor() || World->IsGameWorld())
	{
		UE_LOG(LogStageEditorSubsystem, Verbose, TEXT("HandleStageLoaded: Skipping Stage '%s' (PIE or GameWorld)"), *Stage->GetActorLabel());
		return;
	}

	UE_LOG(LogStageEditorSubsystem, Log, TEXT("HandleStageLoaded: Processing Stage '%s' (StageID=%d)"),
		*Stage->GetActorLabel(), Stage->SUID.StageID);

	// Already has valid StageID, just add to cache
	if (Stage->SUID.StageID > 0)
	{
		UStageManagerSubsystem* RuntimeSub = World->GetSubsystem<UStageManagerSubsystem>();
		if (!RuntimeSub)
		{
			UE_LOG(LogStageEditorSubsystem, Error, TEXT("HandleStageLoaded: RuntimeSubsystem is nullptr for Stage '%s'"), *Stage->GetActorLabel());
			return;
		}

		bool bAlreadyRegistered = RuntimeSub->IsStageIDRegistered(Stage->SUID.StageID);
		UE_LOG(LogStageEditorSubsystem, Log, TEXT("HandleStageLoaded: Stage '%s' (ID:%d) - RuntimeCache check: %s"),
			*Stage->GetActorLabel(), Stage->SUID.StageID, bAlreadyRegistered ? TEXT("Already in cache") : TEXT("NOT in cache, will add"));

		if (!bAlreadyRegistered)
		{
			RuntimeSub->AddStageToRuntimeCache(Stage);
			UE_LOG(LogStageEditorSubsystem, Log,
				TEXT("HandleStageLoaded: Added existing Stage '%s' (ID:%d) to cache"),
				*Stage->GetActorLabel(), Stage->SUID.StageID);
		}
	}
	else
	{
		// StageID invalid, needs migration
		UE_LOG(LogStageEditorSubsystem, Warning,
			TEXT("HandleStageLoaded: Stage '%s' has invalid ID, needs migration"),
			*Stage->GetActorLabel());
		// Migration handled by StageEditorPanel's Migration Dialog
	}
}

void UStageEditorSubsystem::HandleStageUnloaded(AStage* Stage)
{
	// WP unloaded, remove from runtime cache (but NOT from Registry)
	if (!Stage)
	{
		return;
	}

	UWorld* World = Stage->GetWorld();
	if (!World)
	{
		return;
	}

	UStageManagerSubsystem* RuntimeSub = World->GetSubsystem<UStageManagerSubsystem>();
	if (RuntimeSub)
	{
		RuntimeSub->RemoveStageFromRuntimeCache(Stage->SUID.StageID);
		UE_LOG(LogStageEditorSubsystem, Log,
			TEXT("HandleStageUnloaded: Removed Stage '%s' (ID:%d) from cache"),
			*Stage->GetActorLabel(), Stage->SUID.StageID);
	}
}

void UStageEditorSubsystem::RefreshMetadataCacheFromRegistry(UWorld* World)
{
	// Throttle: Avoid excessive refreshes during level load or SC state changes
	const double CurrentTime = FPlatformTime::Seconds();
	if (CurrentTime - LastMetadataCacheRefreshTime < MetadataCacheRefreshThrottleInterval)
	{
		UE_LOG(LogStageEditorSubsystem, Verbose,
			TEXT("RefreshMetadataCacheFromRegistry: Throttled (%.2fs since last refresh)"),
			CurrentTime - LastMetadataCacheRefreshTime);
		return;
	}
	LastMetadataCacheRefreshTime = CurrentTime;

	if (!World)
	{
		UE_LOG(LogStageEditorSubsystem, Warning,
			TEXT("RefreshMetadataCacheFromRegistry: World is nullptr"));
		return;
	}

	UStageManagerSubsystem* RuntimeSub = World->GetSubsystem<UStageManagerSubsystem>();
	if (!RuntimeSub)
	{
		UE_LOG(LogStageEditorSubsystem, Warning,
			TEXT("RefreshMetadataCacheFromRegistry: StageManagerSubsystem not found"));
		return;
	}

	UStageRegistryAsset* Registry = GetOrLoadRegistryAsset(World);
	if (!Registry)
	{
		UE_LOG(LogStageEditorSubsystem, Warning,
			TEXT("RefreshMetadataCacheFromRegistry: Registry not found for World '%s'"),
			*World->GetName());
		return;
	}

	// Get all registered Stages from Registry
	const TMap<int32, FStageRegistryEntry>& RegisteredStages = Registry->GetAllStages();

	// Clear and rebuild metadata cache
	int32 LoadedCount = 0;
	for (const auto& Pair : RegisteredStages)
	{
		const FStageRegistryEntry& Entry = Pair.Value;

		FStageMetadata Metadata;
		Metadata.StageID = Entry.StageID;
		Metadata.StageName = Entry.StageName;
		Metadata.StageDataLayerAsset = Entry.StageDataLayerAsset;
		Metadata.OwnerLevel = Entry.OwnerLevel;

		// Load Act DataLayers from Runtime Stage object (Registry doesn't store them to avoid SC conflicts)
		// NOTE: This may fail if Stage is not yet loaded (World Partition lazy loading)
		// In that case, Fallback mechanism in FindStageByDataLayerInRegistry() will handle it
		AStage* RuntimeStage = RuntimeSub->GetStage(Entry.StageID);
		if (RuntimeStage)
		{
			int32 ActDataLayerCount = 0;
			for (const FAct& Act : RuntimeStage->Acts)
			{
				if (Act.AssociatedDataLayer)
				{
					Metadata.ActDataLayers.Add(Act.SUID.ActID, TSoftObjectPtr<UDataLayerAsset>(Act.AssociatedDataLayer));
					ActDataLayerCount++;
				}
			}

			if (ActDataLayerCount > 0)
			{
				UE_LOG(LogStageEditorSubsystem, Verbose,
					TEXT("RefreshMetadataCacheFromRegistry: Loaded %d Act DataLayer(s) for Stage '%s' (ID:%d)"),
					ActDataLayerCount, *Metadata.StageName, Metadata.StageID);
			}
		}
		else
		{
			UE_LOG(LogStageEditorSubsystem, Verbose,
				TEXT("RefreshMetadataCacheFromRegistry: Stage '%s' (ID:%d) not loaded yet, Act DataLayers will use Fallback"),
				*Metadata.StageName, Metadata.StageID);
		}

		RuntimeSub->UpdateStageMetadata(Entry.StageID, Metadata);
		LoadedCount++;

		UE_LOG(LogStageEditorSubsystem, Verbose,
			TEXT("RefreshMetadataCacheFromRegistry: Loaded metadata for Stage '%s' (ID:%d)"),
			*Metadata.StageName, Metadata.StageID);
	}

	UE_LOG(LogStageEditorSubsystem, Log,
		TEXT("RefreshMetadataCacheFromRegistry: Loaded %d Stage metadata entries from Registry"),
		LoadedCount);
}

//----------------------------------------------------------------
// Phase 13.8 Phase 3: MetadataCache Sync Mechanisms
//----------------------------------------------------------------

void UStageEditorSubsystem::RefreshStageMetadataCache()
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		UE_LOG(LogStageEditorSubsystem, Warning,
			TEXT("RefreshStageMetadataCache: No active World"));
		return;
	}

	UStageManagerSubsystem* RuntimeSub = World->GetSubsystem<UStageManagerSubsystem>();
	if (!RuntimeSub)
	{
		UE_LOG(LogStageEditorSubsystem, Warning,
			TEXT("RefreshStageMetadataCache: StageManagerSubsystem not found"));
		return;
	}

	RefreshMetadataCacheFromRegistry(World);

	UE_LOG(LogStageEditorSubsystem, Log,
		TEXT("RefreshStageMetadataCache: MetadataCache manually refreshed"));

	// Notify user
	FNotificationInfo Info(FText::FromString(TEXT("Stage Metadata Cache Refreshed")));
	Info.ExpireDuration = 3.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
}

//----------------------------------------------------------------
// Phase 13.8 Phase 3: File Watcher (Strategy 1)
//----------------------------------------------------------------

void UStageEditorSubsystem::RegisterRegistryFileWatcher()
{
	FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	IDirectoryWatcher* DirectoryWatcher = DirectoryWatcherModule.Get();

	if (!DirectoryWatcher)
	{
		UE_LOG(LogStageEditorSubsystem, Warning,
			TEXT("RegisterRegistryFileWatcher: DirectoryWatcher not available"));
		return;
	}

	// Get Registry file so we can monitor its directory
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		UE_LOG(LogStageEditorSubsystem, Verbose,
			TEXT("RegisterRegistryFileWatcher: No active World yet, file watcher will be registered when Registry is loaded"));
		return;
	}

	UStageRegistryAsset* Registry = GetOrLoadRegistryAsset(World);
	if (!Registry)
	{
		UE_LOG(LogStageEditorSubsystem, Verbose,
			TEXT("RegisterRegistryFileWatcher: Registry not found, cannot register file watcher yet"));
		return;
	}

	// Get Registry file path
	FString RegistryAssetPath = Registry->GetPathName();  // "/Game/StageData/StageRegistry.StageRegistry"
	CachedRegistryFilePath = FPackageName::LongPackageNameToFilename(RegistryAssetPath, TEXT(".uasset"));

	// Monitor directory
	FString DirectoryToWatch = FPaths::GetPath(CachedRegistryFilePath);

	// Register file change callback
	auto OnDirectoryChanged = IDirectoryWatcher::FDirectoryChanged::CreateUObject(
		this, &UStageEditorSubsystem::OnRegistryFileChanged);

	if (DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(DirectoryToWatch, OnDirectoryChanged, RegistryFileWatcherHandle))
	{
		UE_LOG(LogStageEditorSubsystem, Log,
			TEXT("RegisterRegistryFileWatcher: Watching directory '%s' for Registry changes"),
			*DirectoryToWatch);
	}
	else
	{
		UE_LOG(LogStageEditorSubsystem, Warning,
			TEXT("RegisterRegistryFileWatcher: Failed to register directory watcher for '%s'"),
			*DirectoryToWatch);
	}
}

void UStageEditorSubsystem::UnregisterRegistryFileWatcher()
{
	if (RegistryFileWatcherHandle.IsValid())
	{
		FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
		IDirectoryWatcher* DirectoryWatcher = DirectoryWatcherModule.Get();

		if (DirectoryWatcher)
		{
			FString DirectoryToWatch = FPaths::GetPath(CachedRegistryFilePath);
			DirectoryWatcher->UnregisterDirectoryChangedCallback_Handle(DirectoryToWatch, RegistryFileWatcherHandle);

			UE_LOG(LogStageEditorSubsystem, Log,
				TEXT("UnregisterRegistryFileWatcher: Unregistered file watcher"));
		}

		RegistryFileWatcherHandle.Reset();
	}
}

void UStageEditorSubsystem::OnRegistryFileChanged(const TArray<FFileChangeData>& FileChanges)
{
	for (const FFileChangeData& Change : FileChanges)
	{
		// Check if it's the Registry file that was modified
		if (Change.Filename == CachedRegistryFilePath)
		{
			UE_LOG(LogStageEditorSubsystem, Log,
				TEXT("OnRegistryFileChanged: Registry file modified externally, refreshing MetadataCache"));

			// Delay refresh to avoid file lock conflicts (Registry might still be being written)
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float DeltaTime)
			{
				// Refresh MetadataCache
				if (UWorld* World = GEditor->GetEditorWorldContext().World())
				{
					if (UStageManagerSubsystem* RuntimeSub = World->GetSubsystem<UStageManagerSubsystem>())
					{
						RefreshMetadataCacheFromRegistry(World);

						UE_LOG(LogStageEditorSubsystem, Log,
							TEXT("OnRegistryFileChanged: MetadataCache refreshed after external file change"));
					}
				}

				return false;  // Only execute once
			}), 0.5f);  // Delay 0.5 seconds

			break;
		}
	}
}

//----------------------------------------------------------------
// Phase 13.8 Phase 3: Source Control Hooks (Strategy 2)
//----------------------------------------------------------------

void UStageEditorSubsystem::RegisterSourceControlHooks()
{
	ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
	if (!SourceControlModule.IsEnabled())
	{
		UE_LOG(LogStageEditorSubsystem, Verbose,
			TEXT("RegisterSourceControlHooks: Source Control not enabled, skipping hook registration"));
		return;
	}

	ISourceControlProvider& Provider = SourceControlModule.GetProvider();

	// Subscribe to Source Control state changed events
	// Note: FSourceControlStateChanged is a simple delegate with no parameters
	SourceControlStateChangedHandle = Provider.RegisterSourceControlStateChanged_Handle(
		FSourceControlStateChanged::FDelegate::CreateLambda([this]()
		{
			// When SC state changes, check if Registry was modified
			UWorld* World = GEditor->GetEditorWorldContext().World();
			if (!World)
			{
				return;
			}

			UStageRegistryAsset* Registry = GetOrLoadRegistryAsset(World);
			if (Registry)
			{
				// Refresh metadata cache when SC state changes
				// This catches Sync/Update operations
				RefreshMetadataCacheFromRegistry(World);

				UE_LOG(LogStageEditorSubsystem, Verbose,
					TEXT("RegisterSourceControlHooks: Registry refreshed after Source Control state change"));
			}

			// Phase 18: ROLLBACK - Removed Broadcast to fix infinite loop
			// Root cause: Panel's RefreshAllStatusTexts() queries SC state, which triggers FSourceControlStateChanged again
			// This creates: SC Event → Broadcast → Panel Refresh → SC Query → SC Event (infinite loop)
			// Solution: Reverted to Phase 13.8 stable state
			// - Path 1 (editor operations) still auto-refreshes via OnModelChanged
			// - Path 2 (manual refresh button) still works as fallback
			// - Path 1.5 (P4V external operations) requires manual refresh for now
			// TODO Phase 18.5: Implement SC state caching to avoid query-triggered events
			// OnSourceControlStateChanged.Broadcast();  // ❌ REMOVED - Causes infinite loop
		}));

	UE_LOG(LogStageEditorSubsystem, Log,
		TEXT("RegisterSourceControlHooks: Registered Source Control hooks"));
}

void UStageEditorSubsystem::UnregisterSourceControlHooks()
{
	if (SourceControlStateChangedHandle.IsValid())
	{
		ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
		if (SourceControlModule.IsEnabled())
		{
			ISourceControlProvider& Provider = SourceControlModule.GetProvider();
			Provider.UnregisterSourceControlStateChanged_Handle(SourceControlStateChangedHandle);
		}

		SourceControlStateChangedHandle.Reset();

		UE_LOG(LogStageEditorSubsystem, Log,
			TEXT("UnregisterSourceControlHooks: Unregistered Source Control hooks"));
	}
}
