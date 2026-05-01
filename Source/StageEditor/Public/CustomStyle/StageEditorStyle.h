#pragma once

#include "Styling/SlateStyle.h"

/**
 * Centralized style names for StageEditor to avoid hardcoded strings.
 */
struct FStageEditorStyleNames
{
	static const FName StyleSetName;
	static const FName TabIconBrushName;
};

/**
 * Style registry responsible for FSlateStyleSet lifecycle and access interface.
 */
class FStageEditorStyleSetRegistry
{
public:
	/**
	 * Initialize and register the Style Set at editor startup (called once).
	 */
	static void Initialize();

	/**
	 * Unregister Style Set and cleanup at module unload.
	 */
	static void Shutdown();

	/**
	 * Check if style has been initialized.
	 * @return true if Initialize has been called successfully
	 */
	static bool IsInitialized();

	/**
	 * Return the Style Set name for use with FSlateIcon.
	 * @return StageEditor's Style Set name
	 */
	static FName GetStyleSetName();

	/**
	 * Get Slate Style reference for accessing widget styles and brushes.
	 */
	static const ISlateStyle& Get();

	/**
	 * Get brush by name, returns nullptr if not initialized.
	 * @param BrushName Registered brush key
	 */
	static const FSlateBrush* GetBrush(const FName BrushName);

private:
	static TSharedRef<FSlateStyleSet> CreateSlateStyleSet();

	static bool bIsInitialized;
	static TSharedPtr<FSlateStyleSet> RegisteredStyleSet;
};
