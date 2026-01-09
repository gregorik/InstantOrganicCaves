// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#include "InstantOrganicCavesModule.h"
#include "Elements/IOCVoxelCore.h" // Relative path matches Public folder structure
#include "Data/PCGPointData.h"

#define LOCTEXT_NAMESPACE "FInstantOrganicCavesModule"

void FInstantOrganicCavesModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; 
    // the exact timing is specified in the .uplugin file per-module
}

void FInstantOrganicCavesModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module. 
    // For modules that support dynamic reloading, we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FInstantOrganicCavesModule, InstantOrganicCaves)