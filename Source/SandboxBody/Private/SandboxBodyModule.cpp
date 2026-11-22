#include "Modules/ModuleManager.h"
#include "SandboxBodyTuning.h"
#include "SandboxParamStore.h"

class FSandboxBodyModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		// The body's part of Config/Tuning: its names, its states and its range checks.
		FSandboxBodyTuning::DeclareParams(FSandboxParamStore::Get());
	}
};

IMPLEMENT_GAME_MODULE(FSandboxBodyModule, SandboxBody);
