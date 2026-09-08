// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#include "IOCSettings.h"

UIOCSettings::UIOCSettings()
{
    // Matches the material shipped in the plugin's own content. Projects that fork the
    // materials can repoint this without touching code.
    FallbackCaveMaterial = FSoftObjectPath(TEXT("/InstantOrganicCaves/MI_IOC_CaveWalls.MI_IOC_CaveWalls"));
}

const UIOCSettings& UIOCSettings::Get()
{
    const UIOCSettings* Settings = GetDefault<UIOCSettings>();
    check(Settings);
    return *Settings;
}
