#pragma once

#pragma region Imports
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#pragma endregion Imports

class FStageEditorRuntimeModule : public IModuleInterface
{
public:

#pragma region Module Interface
	/** 
	 * @brief Called when the module is started.
	 * @details This is where you can initialize module-specific data or register types.
	 */
	virtual void StartupModule() override;

	/** 
	 * @brief Called when the module is shut down.
	 * @details Cleanup module-specific data here.
	 */
	virtual void ShutdownModule() override;
#pragma endregion Module Interface
};
