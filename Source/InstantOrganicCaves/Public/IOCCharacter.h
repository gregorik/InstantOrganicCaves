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
    class USpringArmComponent* CameraBoom;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera)
    class UCameraComponent* FollowCamera;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera)
    class USpotLightComponent* Flashlight;

protected:
    virtual void BeginPlay() override;
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

private:
    UPROPERTY()
    UInputMappingContext* DefaultMappingContext;

    UPROPERTY()
    UInputAction* MoveForwardAction;

    UPROPERTY()
    UInputAction* MoveRightAction;

    UPROPERTY()
    UInputAction* LookXAction;

    UPROPERTY()
    UInputAction* LookYAction;

    UPROPERTY()
    UInputAction* JumpAction;

    void MoveForward(const FInputActionValue& Value);
    void MoveRight(const FInputActionValue& Value);
    void TurnX(const FInputActionValue& Value);
    void TurnY(const FInputActionValue& Value);
};
