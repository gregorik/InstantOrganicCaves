// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#include "IOCBiomeVolume.h"
#include "Components/BrushComponent.h"
#include "Engine/CollisionProfile.h"

AIOCBiomeVolume::AIOCBiomeVolume()
{
    GetBrushComponent()->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
    GetBrushComponent()->Mobility = EComponentMobility::Static;
    
    // Default nice color for the volume
    // (Note: Volume color is usually handled by editor styles, but we can hint)
}
