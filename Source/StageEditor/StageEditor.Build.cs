using UnrealBuildTool;

public class StageEditor : ModuleRules
{
	public StageEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
			
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"StageEditorRuntime", // Dependency on our runtime module
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"InputCore",
				"UnrealEd",
				"ToolMenus",
				"EditorStyle",
				"WorkspaceMenuStructure",
				"PropertyEditor",
				"SceneOutliner",
				"LevelEditor",
				"DataLayerEditor",
				"WorldPartitionEditor",
				"EditorSubsystem",  // For UEditorSubsystem base class
				"Projects",         // For IPluginManager (StyleSet icon loading)
				"AssetRegistry",    // For FindExistingRegistryAsset() asset search
				"SourceControl",    // For Multi-user Source Control integration
				"SourceControlWindows", // For opening Changelist panel (Phase 13.5)
				"ContentBrowser",   // For asset picker in Select Existing Registry
				"DesktopPlatform",  // For file dialogs (Import/Export settings)
				"Json",             // For JSON serialization
				"JsonUtilities",    // For JSON utilities
				// ... add private dependencies that you statically link with here ...
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
