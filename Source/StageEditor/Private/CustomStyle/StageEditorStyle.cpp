#include "CustomStyle/StageEditorStyle.h"

#include "Brushes/SlateImageBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleRegistry.h"

const FName FStageEditorStyleNames::StyleSetName(TEXT("StageEditor.Style"));
const FName FStageEditorStyleNames::TabIconBrushName(TEXT("StageEditor.TabIcon"));

bool FStageEditorStyleSetRegistry::bIsInitialized = false;
TSharedPtr<FSlateStyleSet> FStageEditorStyleSetRegistry::RegisteredStyleSet = nullptr;

void FStageEditorStyleSetRegistry::Initialize()
{
	if (bIsInitialized)
	{
		ensureMsgf(false, TEXT("StageEditorStyleRegistry already initialized."));
		return;
	}

	RegisteredStyleSet = CreateSlateStyleSet();
	FSlateStyleRegistry::RegisterSlateStyle(*RegisteredStyleSet);
	bIsInitialized = true;
}

void FStageEditorStyleSetRegistry::Shutdown()
{
	if (!bIsInitialized)
	{
		return;
	}

	if (RegisteredStyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*RegisteredStyleSet);
		RegisteredStyleSet.Reset();
	}
	bIsInitialized = false;
}

bool FStageEditorStyleSetRegistry::IsInitialized()
{
	return bIsInitialized;
}

FName FStageEditorStyleSetRegistry::GetStyleSetName()
{
	return FStageEditorStyleNames::StyleSetName;
}

const ISlateStyle& FStageEditorStyleSetRegistry::Get()
{
	checkf(RegisteredStyleSet.IsValid(), TEXT("StageEditorStyleRegistry::Get() called before Initialize."));
	return *RegisteredStyleSet.Get();
}

const FSlateBrush* FStageEditorStyleSetRegistry::GetBrush(const FName BrushName)
{
	return RegisteredStyleSet.IsValid() ? RegisteredStyleSet->GetBrush(BrushName) : nullptr;
}

TSharedRef<FSlateStyleSet> FStageEditorStyleSetRegistry::CreateSlateStyleSet()
{
	TSharedRef<FSlateStyleSet> CustomStyleSet = MakeShared<FSlateStyleSet>(FStageEditorStyleNames::StyleSetName);

	const TSharedPtr<IPlugin> StageEditorPlugin = IPluginManager::Get().FindPlugin(TEXT("StageEditor"));
	checkf(StageEditorPlugin.IsValid(), TEXT("StageEditor plugin descriptor not found when building styles."));

	const FString IconDirectory = StageEditorPlugin->GetBaseDir() / TEXT("Resources/Icons");
	CustomStyleSet->SetContentRoot(IconDirectory);

	const FVector2D Icon16x16(16.0f, 16.0f);
	const FVector2D Icon40x40(40.0f, 40.0f);

	// Tab icon (used in Window menu and tab bar)
	CustomStyleSet->Set(
		FStageEditorStyleNames::TabIconBrushName,
		new FSlateImageBrush(IconDirectory / TEXT("StageEditorIcon.png"), Icon16x16));

	return CustomStyleSet;
}
