#include "Modules/ModuleManager.h"
#include "SandboxInteractionTuning.h"
#include "SandboxParamStore.h"

class FSandboxInteractionModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		// The player's part of Config/Tuning: effectors and tools, with their range checks.
		FSandboxInteractionTuning::DeclareParams(FSandboxParamStore::Get());
	}
};

IMPLEMENT_GAME_MODULE(FSandboxInteractionModule, SandboxInteraction);
