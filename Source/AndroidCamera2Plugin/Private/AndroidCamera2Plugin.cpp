// AndroidCamera2Plugin.cpp

#include "Modules/ModuleManager.h"

class FAndroidCamera2PluginModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FAndroidCamera2PluginModule, AndroidCamera2Plugin);
