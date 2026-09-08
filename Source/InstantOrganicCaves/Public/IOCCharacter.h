// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "IOCCharacter.generated.h"

class UInputMappingContext;
class UInputAction;

UCLASS()
class INSTANTORGANICCAVES_API AIOCCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    AIOCCharacter();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera)
    TObjectPtr<class USpringArmComponent> CameraBoom;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera)
    TObjectPtr<class UCameraComponent> FollowCamera;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera)
    TObjectPtr<class USpotLightComponent> Flashlight;

    /**
     * Move this character to the entrance of the nearest IOC cave when play begins.
     *
     * On by default so the shipped demos drop the player inside the tunnel rather than
     * inside rock. Turn it off if you place this character yourself and want your own
     * PlayerStart or spawn transform respected.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC",
        meta = (DisplayName = "Snap To Cave Entrance On Begin Play"))
    bool bSnapToCaveEntranceOnBeginPlay = true;

protected:
    virtual void BeginPlay() override;
    virtual void PossessedBy(AController* NewController) override;
    virtual void PawnClientRestart() override;
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

private:
    UPROPERTY()
    TObjectPtr<UInputMappingContext> DefaultMappingContext;

    UPROPERTY()
    TObjectPtr<UInputAction> MoveForwardAction;

    UPROPERTY()
    TObjectPtr<UInputAction> MoveRightAction;

    UPROPERTY()
    TObjectPtr<UInputAction> LookXAction;

    UPROPERTY()
    TObjectPtr<UInputAction> LookYAction;

    UPROPERTY()
    TObjectPtr<UInputAction> JumpAction;

    UPROPERTY()
    TObjectPtr<class UInputModifierNegate> NegateMoveBackward;

    UPROPERTY()
    TObjectPtr<class UInputModifierNegate> NegateMoveLeft;

    UPROPERTY()
    TObjectPtr<class UInputModifierNegate> NegateLookMouseY;

    UPROPERTY()
    TObjectPtr<class UInputModifierNegate> NegateLookGamepadY;

    void AddDefaultMappingContext();
    void SnapToNearestCaveEntrance();
    void MoveForward(const FInputActionValue& Value);
    void MoveRight(const FInputActionValue& Value);
    void TurnX(const FInputActionValue& Value);
    void TurnY(const FInputActionValue& Value);
};
