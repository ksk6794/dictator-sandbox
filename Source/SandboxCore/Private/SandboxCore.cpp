#include "SandboxCore.h"

#include "Containers/Ticker.h"
#include "Modules/ModuleManager.h"
#include "SandboxParamStore.h"

DEFINE_LOG_CATEGORY(LogSandbox);

class FSandboxCoreModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		// Hot reload of the tuning files: the store looks at them every few engine frames. It counts frames
		// and never reads the clock, and does nothing until someone has loaded it.
		TuningPoll = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
		{
			FSandboxParamStore::Get().TickAutoPoll();
			return true;
		}));
	}

	virtual void ShutdownModule() override
	{
		FTSTicker::RemoveTicker(TuningPoll);
	}

private:
	FTSTicker::FDelegateHandle TuningPoll;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FSandboxCoreModule, SandboxCore, "DictatorSandbox");
