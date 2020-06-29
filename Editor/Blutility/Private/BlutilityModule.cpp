// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Framework/Docking/TabManager.h"
#include "IBlutilityModule.h"
#include "EditorUtilityWidget.h"
#include "EditorUtilityBlueprint.h"
#include "GlobalEditorUtilityBase.h"

#include "AssetToolsModule.h"
#include "PropertyEditorModule.h"
#include "AssetTypeActions_EditorUtilityBlueprint.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#include "Widgets/Docking/SDockTab.h"
#include "BlutilityContentBrowserExtensions.h"
#include "BlutilityLevelEditorExtensions.h"
#include "AssetTypeActions_EditorUtilityWidgetBlueprint.h"
#include "KismetCompiler.h"
#include "EditorUtilityWidgetBlueprint.h"
#include "ComponentReregisterContext.h"
#include "KismetCompilerModule.h"
#include "WidgetBlueprintCompiler.h"
#include "UMGEditorModule.h"
#include "EditorUtilitySubsystem.h"
#include "LevelEditor.h"
#include "Editor.h"
#include "UnrealEdMisc.h"
#include "EditorSupportDelegates.h"
#include "UObject/PurgingReferenceCollector.h"
#include "AssetRegistryModule.h"
#include "EditorUtilityCommon.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

DEFINE_LOG_CATEGORY(LogEditorUtilityBlueprint);

/////////////////////////////////////////////////////

namespace BlutilityModule
{
}

/////////////////////////////////////////////////////
// FBlutilityModule 

// Blutility module implementation (private)
class FBlutilityModule : public IBlutilityModule, public FGCObject
{
public:
	/** Asset type actions for editor utility assets.  Cached here so that we can unregister it during shutdown. */
	TSharedPtr<FAssetTypeActions_EditorUtilityBlueprint> EditorBlueprintAssetTypeActions;
	TSharedPtr<FAssetTypeActions_EditorUtilityWidgetBlueprint> EditorWidgetBlueprintAssetTypeActions;

public:
	virtual void StartupModule() override
	{
		// Register the asset type
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		EditorUtilityAssetCategory = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("EditorUtilities")), LOCTEXT("EditorUtilitiesAssetCategory", "Editor Utilities"));
		EditorBlueprintAssetTypeActions = MakeShareable(new FAssetTypeActions_EditorUtilityBlueprint);
		AssetTools.RegisterAssetTypeActions(EditorBlueprintAssetTypeActions.ToSharedRef());
		EditorWidgetBlueprintAssetTypeActions = MakeShareable(new FAssetTypeActions_EditorUtilityWidgetBlueprint);
		AssetTools.RegisterAssetTypeActions(EditorWidgetBlueprintAssetTypeActions.ToSharedRef());

		FKismetCompilerContext::RegisterCompilerForBP(UEditorUtilityWidgetBlueprint::StaticClass(), &UWidgetBlueprint::GetCompilerForWidgetBP);

		// Register widget blueprint compiler we do this no matter what.
		IUMGEditorModule& UMGEditorModule = FModuleManager::LoadModuleChecked<IUMGEditorModule>("UMGEditor");
		IKismetCompilerInterface& KismetCompilerModule = FModuleManager::LoadModuleChecked<IKismetCompilerInterface>("KismetCompiler");
		KismetCompilerModule.GetCompilers().Add(UMGEditorModule.GetRegisteredCompiler());

		FBlutilityContentBrowserExtensions::InstallHooks();
		FBlutilityLevelEditorExtensions::InstallHooks();

		ScriptedEditorWidgetsGroup = WorkspaceMenu::GetMenuStructure().GetToolsCategory()->AddGroup(
			LOCTEXT("WorkspaceMenu_EditorUtilityWidgetsGroup", "Editor Utility Widgets"),
			LOCTEXT("ScriptedEditorWidgetsGroupTooltipText", "Custom editor UI created with Blueprints or Python."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "WorkspaceMenu.AdditionalUI"),
			true);

		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		LevelEditorModule.OnTabManagerChanged().AddRaw(this, &FBlutilityModule::ReinitializeUIs);
		LevelEditorModule.OnMapChanged().AddRaw(this, &FBlutilityModule::OnMapChanged);
		FEditorSupportDelegates::PrepareToCleanseEditorObject.AddRaw(this, &FBlutilityModule::OnPrepareToCleanseEditorObject);

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		AssetRegistryModule.Get().OnAssetRemoved().AddRaw(this, &FBlutilityModule::HandleAssetRemoved);
	}

	void ReinitializeUIs()
	{
		UEditorUtilitySubsystem* EditorUtilitySubsystem = GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>();
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
		TArray<FSoftObjectPath> CorrectPaths;
		for (const FSoftObjectPath& BlueprintPath : EditorUtilitySubsystem->LoadedUIs)
		{
			UObject* BlueprintObject = BlueprintPath.TryLoad();
			if (BlueprintObject && !BlueprintObject->IsPendingKillOrUnreachable())
			{
				UEditorUtilityWidgetBlueprint* Blueprint = Cast<UEditorUtilityWidgetBlueprint>(BlueprintObject);
				if (Blueprint)
				{
					if (Blueprint->GeneratedClass)
					{
						const UEditorUtilityWidget* CDO = Blueprint->GeneratedClass->GetDefaultObject<UEditorUtilityWidget>();
						FName RegistrationName = FName(*(Blueprint->GetPathName() + LOCTEXT("ActiveTabSuffix", "_ActiveTab").ToString()));
						Blueprint->SetRegistrationName(RegistrationName);
						FText DisplayName = FText::FromString(Blueprint->GetName());
						if (LevelEditorTabManager && !LevelEditorTabManager->HasTabSpawner(RegistrationName))
						{
							LevelEditorTabManager->RegisterTabSpawner(RegistrationName, FOnSpawnTab::CreateUObject(Blueprint, &UEditorUtilityWidgetBlueprint::SpawnEditorUITab))
								.SetDisplayName(DisplayName)
								.SetGroup(GetMenuGroup().ToSharedRef());
							CorrectPaths.Add(BlueprintPath);
						}
					}
					else
					{
						UE_LOG(LogEditorUtilityBlueprint, Warning, TEXT("No generated class for: %s"), *BlueprintPath.ToString());
					}
				}
				else
				{
					UE_LOG(LogEditorUtilityBlueprint, Warning, TEXT("Expected object of class EditorUtilityWidgetBlueprint: %s"), *BlueprintPath.ToString());
				}
			}
			else
			{
				UE_LOG(LogEditorUtilityBlueprint, Warning, TEXT("Could not load: %s"), *BlueprintPath.ToString());
			}
		}

		EditorUtilitySubsystem->LoadedUIs = CorrectPaths;
		EditorUtilitySubsystem->SaveConfig();
	}

	void OnMapChanged(UWorld* InWorld, EMapChangeType MapChangeType)
	{
		for (const FSoftObjectPath& LoadedUI : GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>()->LoadedUIs)
		{
			UEditorUtilityWidgetBlueprint* LoadedEditorUtilityBlueprint = Cast<UEditorUtilityWidgetBlueprint>(LoadedUI.ResolveObject());
			if (LoadedEditorUtilityBlueprint)
			{
				UEditorUtilityWidget* CreatedWidget = LoadedEditorUtilityBlueprint->GetCreatedWidget();
				if (CreatedWidget)
				{
					if (MapChangeType == EMapChangeType::TearDownWorld)
					{
						CreatedWidget->Rename(*CreatedWidget->GetName(), GetTransientPackage());
					}
					else if (MapChangeType == EMapChangeType::LoadMap || MapChangeType == EMapChangeType::NewMap)
					{
						UWorld* World = GEditor->GetEditorWorldContext().World();
						check(World);
						CreatedWidget->Rename(*CreatedWidget->GetName(), World);
					}
				}
			}
		}
	}

	virtual void ShutdownModule() override
	{
		if (!UObjectInitialized())
		{
			return;
		}

		// Unregister widget blueprint compiler we do this no matter what.
		IUMGEditorModule& UMGEditorModule = FModuleManager::LoadModuleChecked<IUMGEditorModule>("UMGEditor");
		IKismetCompilerInterface& KismetCompilerModule = FModuleManager::LoadModuleChecked<IKismetCompilerInterface>("KismetCompiler");
		KismetCompilerModule.GetCompilers().Remove(UMGEditorModule.GetRegisteredCompiler());

		FBlutilityLevelEditorExtensions::RemoveHooks();
		FBlutilityContentBrowserExtensions::RemoveHooks();

		// Only unregister if the asset tools module is loaded.  We don't want to forcibly load it during shutdown phase.
		check( EditorBlueprintAssetTypeActions.IsValid() );
		if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
		{
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			AssetTools.UnregisterAssetTypeActions(EditorBlueprintAssetTypeActions.ToSharedRef());
			AssetTools.UnregisterAssetTypeActions(EditorWidgetBlueprintAssetTypeActions.ToSharedRef());
		}
		EditorBlueprintAssetTypeActions.Reset();
		EditorWidgetBlueprintAssetTypeActions.Reset();

		FEditorSupportDelegates::PrepareToCleanseEditorObject.RemoveAll(this);
	}

	virtual bool IsEditorUtilityBlueprint( const UBlueprint* Blueprint ) const override
	{
		const UClass* BPClass = Blueprint ? Blueprint->GetClass() : nullptr;

		if( BPClass && 
			(BPClass->IsChildOf( UEditorUtilityBlueprint::StaticClass())
			|| BPClass->IsChildOf(UEditorUtilityWidgetBlueprint::StaticClass())))
		{
			return true;
		}
		return false;
	}

	virtual TSharedPtr<class FWorkspaceItem> GetMenuGroup() const override
	{
		return ScriptedEditorWidgetsGroup;
	}

	virtual EAssetTypeCategories::Type GetAssetCategory() const override
	{
		return EditorUtilityAssetCategory;
	}

	virtual void AddLoadedScriptUI(class UEditorUtilityWidgetBlueprint* InBlueprint) override
	{
		UEditorUtilitySubsystem* EditorUtilitySubsystem = GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>();
		EditorUtilitySubsystem->LoadedUIs.AddUnique(InBlueprint);
		EditorUtilitySubsystem->SaveConfig();
	}


	virtual void RemoveLoadedScriptUI(class UEditorUtilityWidgetBlueprint* InBlueprint) override
	{
		UEditorUtilitySubsystem* EditorUtilitySubsystem = GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>();
		EditorUtilitySubsystem->LoadedUIs.Remove(InBlueprint);
		EditorUtilitySubsystem->SaveConfig();	
	}

protected:

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
	}

	void OnPrepareToCleanseEditorObject(UObject* InObject)
	{
		// Gather the list of live Editor Utility instances to purge references from
		TArray<UObject*> EditorUtilityInstances;
		ForEachObjectOfClass(UEditorUtilityWidget::StaticClass(), [&EditorUtilityInstances](UObject* InEditorUtilityInstance)
		{
			EditorUtilityInstances.Add(InEditorUtilityInstance);
		});
		ForEachObjectOfClass(UDEPRECATED_GlobalEditorUtilityBase::StaticClass(), [&EditorUtilityInstances](UObject* InEditorUtilityInstance)
		{
			EditorUtilityInstances.Add(InEditorUtilityInstance);
		});

		if (EditorUtilityInstances.Num() > 0)
		{
			// Build up the complete list of objects to purge
			FPurgingReferenceCollector PurgingReferenceCollector;
			PurgingReferenceCollector.AddObjectToPurge(InObject);
			ForEachObjectWithOuter(InObject, [&PurgingReferenceCollector](UObject* InInnerObject)
			{
				PurgingReferenceCollector.AddObjectToPurge(InInnerObject);
			}, true);

			// Run the purge for each Editor Utility instance
			FReferenceCollectorArchive& PurgingReferenceCollectorArchive = PurgingReferenceCollector.GetVerySlowReferenceCollectorArchive();
			for (UObject* EditorUtilityInstance : EditorUtilityInstances)
			{
				PurgingReferenceCollectorArchive.SetSerializingObject(EditorUtilityInstance);
				EditorUtilityInstance->Serialize(PurgingReferenceCollectorArchive);
				EditorUtilityInstance->CallAddReferencedObjects(PurgingReferenceCollector);
				PurgingReferenceCollectorArchive.SetSerializingObject(nullptr);
			}
		}
	}

	void HandleAssetRemoved(const FAssetData& InAssetData)
	{
		bool bDeletingLoadedUI = false;
		for (const FSoftObjectPath& LoadedUIPath : GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>()->LoadedUIs)
		{
			if (LoadedUIPath.GetAssetPathName() == InAssetData.ObjectPath)
			{
				bDeletingLoadedUI = true;
				break;
			}
		}

		if (bDeletingLoadedUI)
		{
			FName UIToCleanup = FName(*(InAssetData.ObjectPath.ToString() + LOCTEXT("ActiveTabSuffix", "_ActiveTab").ToString()));
			FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
			TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
			TSharedPtr<SDockTab> CurrentTab = LevelEditorTabManager->FindExistingLiveTab(UIToCleanup);
			if (CurrentTab.IsValid())
			{
				CurrentTab->RequestCloseTab();
			}
		}
	}
protected:
	/** Scripted Editor Widgets workspace menu item */
	TSharedPtr<class FWorkspaceItem> ScriptedEditorWidgetsGroup;

	EAssetTypeCategories::Type EditorUtilityAssetCategory;
};



IMPLEMENT_MODULE( FBlutilityModule, Blutility );
#undef LOCTEXT_NAMESPACE 