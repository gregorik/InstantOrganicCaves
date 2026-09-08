// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#include "IOCCharacter.h"
#include "IOCProceduralActor.h"
#include "InstantOrganicCavesModule.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/SplineComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"

AIOCCharacter::AIOCCharacter()
{
    GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = false;
    bUseControllerRotationRoll = false;

    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
    GetCharacterMovement()->JumpZVelocity = 500.f;
    GetCharacterMovement()->AirControl = 0.35f;
    GetCharacterMovement()->MaxWalkSpeed = 500.f;
    GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
    GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;

    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(RootComponent);
    CameraBoom->TargetArmLength = 400.0f;
    CameraBoom->bUsePawnControlRotation = true;

    FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
    FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    FollowCamera->bUsePawnControlRotation = false;

    Flashlight = CreateDefaultSubobject<USpotLightComponent>(TEXT("Flashlight"));
    Flashlight->SetupAttachment(FollowCamera);
    Flashlight->Intensity = 5000.0f;
    Flashlight->OuterConeAngle = 25.0f;
    Flashlight->AttenuationRadius = 2000.0f;
    Flashlight->SetRelativeLocation(FVector(20, 10, -10));
    Flashlight->CastShadows = true;

    // --- Enhanced Input Setup ---
    // Input Actions and Mapping Context are created as default sub-objects so each
    // spawned instance owns its own copies and they're properly tracked by GC.
    DefaultMappingContext = CreateDefaultSubobject<UInputMappingContext>(TEXT("IMC_IOC_Default"));

    MoveForwardAction = CreateDefaultSubobject<UInputAction>(TEXT("IA_IOC_MoveForward"));
    MoveForwardAction->ValueType = EInputActionValueType::Axis1D;

    MoveRightAction = CreateDefaultSubobject<UInputAction>(TEXT("IA_IOC_MoveRight"));
    MoveRightAction->ValueType = EInputActionValueType::Axis1D;

    LookXAction = CreateDefaultSubobject<UInputAction>(TEXT("IA_IOC_LookX"));
    LookXAction->ValueType = EInputActionValueType::Axis1D;

    LookYAction = CreateDefaultSubobject<UInputAction>(TEXT("IA_IOC_LookY"));
    LookYAction->ValueType = EInputActionValueType::Axis1D;

    JumpAction = CreateDefaultSubobject<UInputAction>(TEXT("IA_IOC_Jump"));
    JumpAction->ValueType = EInputActionValueType::Boolean;

    // Modifiers are default subobjects, not NewObject: calling NewObject during a UObject
    // constructor runs while the CDO is still being built and produces objects with the wrong
    // flags. CreateDefaultSubobject is the supported way to compose sub-objects here.
    NegateMoveBackward  = CreateDefaultSubobject<UInputModifierNegate>(TEXT("IOC_NegateMoveBackward"));
    NegateMoveLeft      = CreateDefaultSubobject<UInputModifierNegate>(TEXT("IOC_NegateMoveLeft"));
    NegateLookMouseY    = CreateDefaultSubobject<UInputModifierNegate>(TEXT("IOC_NegateLookMouseY"));
    NegateLookGamepadY  = CreateDefaultSubobject<UInputModifierNegate>(TEXT("IOC_NegateLookGamepadY"));

    // MoveForward: W = +1, S = -1, Gamepad left stick Y is already signed
    DefaultMappingContext->MapKey(MoveForwardAction, EKeys::W);
    {
        FEnhancedActionKeyMapping& M = DefaultMappingContext->MapKey(MoveForwardAction, EKeys::S);
        M.Modifiers.Add(NegateMoveBackward);
    }
    DefaultMappingContext->MapKey(MoveForwardAction, EKeys::Gamepad_LeftY);

    // MoveRight: D = +1, A = -1, Gamepad left stick X is already signed
    DefaultMappingContext->MapKey(MoveRightAction, EKeys::D);
    {
        FEnhancedActionKeyMapping& M = DefaultMappingContext->MapKey(MoveRightAction, EKeys::A);
        M.Modifiers.Add(NegateMoveLeft);
    }
    DefaultMappingContext->MapKey(MoveRightAction, EKeys::Gamepad_LeftX);

    // LookX: raw mouse X / gamepad right stick X
    DefaultMappingContext->MapKey(LookXAction, EKeys::MouseX);
    DefaultMappingContext->MapKey(LookXAction, EKeys::Gamepad_RightX);

    // LookY: negate so moving mouse up = looking up (matches legacy -1.0 scale)
    {
        FEnhancedActionKeyMapping& M = DefaultMappingContext->MapKey(LookYAction, EKeys::MouseY);
        M.Modifiers.Add(NegateLookMouseY);
    }
    {
        FEnhancedActionKeyMapping& M = DefaultMappingContext->MapKey(LookYAction, EKeys::Gamepad_RightY);
        M.Modifiers.Add(NegateLookGamepadY);
    }

    // Jump
    DefaultMappingContext->MapKey(JumpAction, EKeys::SpaceBar);
    DefaultMappingContext->MapKey(JumpAction, EKeys::Gamepad_FaceButton_Bottom);
}

void AIOCCharacter::BeginPlay()
{
    Super::BeginPlay();

    AddDefaultMappingContext();

    // Authority only: moving the pawn on a client desynchronises it from the server, and the
    // server will correct it back a moment later anyway.
    if (bSnapToCaveEntranceOnBeginPlay && HasAuthority())
    {
        SnapToNearestCaveEntrance();
    }
}

void AIOCCharacter::SnapToNearestCaveEntrance()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    // Pick the nearest cave rather than whichever one the actor iterator happens to yield
    // first: in a level with several caves (or a streaming manager) the old code teleported
    // the player to an arbitrary one, often across the map.
    const FVector CurrentLocation = GetActorLocation();
    const AIOCProceduralActor* BestCave = nullptr;
    double BestDistanceSq = TNumericLimits<double>::Max();

    for (TActorIterator<AIOCProceduralActor> It(World); It; ++It)
    {
        const AIOCProceduralActor* Cave = *It;
        if (!Cave || Cave->IsActorBeingDestroyed())
        {
            continue;
        }

        const double DistanceSq = FVector::DistSquared(Cave->GetActorLocation(), CurrentLocation);
        if (DistanceSq < BestDistanceSq)
        {
            BestDistanceSq = DistanceSq;
            BestCave = Cave;
        }
    }

    if (!BestCave)
    {
        return;
    }

    // In spline mode TunnelStart is unused, so the entrance is the start of the spline.
    FVector EntranceLocal = BestCave->TunnelStart;
    if (BestCave->bUseSpline && BestCave->CaveSpline && BestCave->CaveSpline->GetNumberOfSplinePoints() > 0)
    {
        EntranceLocal = BestCave->CaveSpline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::Local);
    }

    // Entrance floor: directly below the tunnel centreline by TunnelRadius.
    EntranceLocal.Z -= BestCave->TunnelRadius;

    FVector EntranceWorld = BestCave->GetActorTransform().TransformPosition(EntranceLocal);
    // Raise by capsule half height so the character stands on the tunnel floor.
    EntranceWorld.Z += GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

    SetActorLocation(EntranceWorld, false, nullptr, ETeleportType::TeleportPhysics);
}

void AIOCCharacter::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);
    AddDefaultMappingContext();
}

void AIOCCharacter::PawnClientRestart()
{
    Super::PawnClientRestart();
    AddDefaultMappingContext();
}

void AIOCCharacter::AddDefaultMappingContext()
{
    if (APlayerController* PC = Cast<APlayerController>(Controller))
    {
        if (!PC->IsLocalController())
        {
            return;
        }

        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            Subsystem->AddMappingContext(DefaultMappingContext, 0);
        }
    }
}

void AIOCCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    check(PlayerInputComponent);

    UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
    if (!EIC)
    {
        UE_LOG(LogIOC, Error, TEXT("IOCCharacter: Enhanced Input Component not found. "
            "Ensure the project uses the Enhanced Input plugin."));
        return;
    }

    EIC->BindAction(MoveForwardAction, ETriggerEvent::Triggered, this, &AIOCCharacter::MoveForward);
    EIC->BindAction(MoveRightAction,   ETriggerEvent::Triggered, this, &AIOCCharacter::MoveRight);
    EIC->BindAction(LookXAction,       ETriggerEvent::Triggered, this, &AIOCCharacter::TurnX);
    EIC->BindAction(LookYAction,       ETriggerEvent::Triggered, this, &AIOCCharacter::TurnY);
    EIC->BindAction(JumpAction,        ETriggerEvent::Started,   this, &ACharacter::Jump);
    EIC->BindAction(JumpAction,        ETriggerEvent::Completed, this, &ACharacter::StopJumping);
}

void AIOCCharacter::MoveForward(const FInputActionValue& Value)
{
    if (Controller)
    {
        const FRotator YawRotation(0, Controller->GetControlRotation().Yaw, 0);
        AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), Value.Get<float>());
    }
}

void AIOCCharacter::MoveRight(const FInputActionValue& Value)
{
    if (Controller)
    {
        const FRotator YawRotation(0, Controller->GetControlRotation().Yaw, 0);
        AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), Value.Get<float>());
    }
}

void AIOCCharacter::TurnX(const FInputActionValue& Value)
{
    AddControllerYawInput(Value.Get<float>());
}

void AIOCCharacter::TurnY(const FInputActionValue& Value)
{
    AddControllerPitchInput(Value.Get<float>());
}
