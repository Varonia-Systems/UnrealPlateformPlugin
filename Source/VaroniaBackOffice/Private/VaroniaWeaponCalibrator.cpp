// VaroniaWeaponCalibrator.cpp

#include "VaroniaWeaponCalibrator.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/PlatformApplicationMisc.h"

UVaroniaWeaponCalibrator::UVaroniaWeaponCalibrator()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UVaroniaWeaponCalibrator::BeginPlay()
{
    Super::BeginPlay();
    InitialRelative = GetRelativeTransform();
}

FString UVaroniaWeaponCalibrator::GetOffsetString() const
{
    const FVector L = GetRelativeLocation();
    const FRotator R = GetRelativeRotation();
    return FString::Printf(
        TEXT("Loc=(X=%.3f,Y=%.3f,Z=%.3f) Rot=(P=%.3f,Y=%.3f,R=%.3f)"),
        L.X, L.Y, L.Z, R.Pitch, R.Yaw, R.Roll);
}

void UVaroniaWeaponCalibrator::CopyOffsetToClipboard()
{
    const FString S = GetOffsetString();
    FPlatformApplicationMisc::ClipboardCopy(*S);
    UE_LOG(LogTemp, Log, TEXT("[WeaponCalibrator] Copié dans le presse-papier : %s"), *S);
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 2.5f, FColor::Green, FString::Printf(TEXT("Offset copié : %s"), *S));
    }
}

void UVaroniaWeaponCalibrator::ResetOffset()
{
    SetRelativeTransform(InitialRelative);
}

void UVaroniaWeaponCalibrator::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    UWorld* W = GetWorld();
    APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
    if (!PC) return;

    // --- Maintenir KeyToggle ToggleHoldSeconds pour (dés)activer ---
    if (PC->IsInputKeyDown(KeyToggle))
    {
        ToggleHeld += DeltaTime;
        if (!bToggleConsumed && ToggleHeld >= ToggleHoldSeconds)
        {
            bCalibrating = !bCalibrating;
            bToggleConsumed = true;
            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Cyan,
                    FString::Printf(TEXT("Calibration %s"), bCalibrating ? TEXT("ON") : TEXT("OFF")));
            }
        }
        else if (!bToggleConsumed && bShowOnScreen && GEngine)
        {
            GEngine->AddOnScreenDebugMessage((uint64)(PTRINT)this + 1, 0.f, FColor::Silver,
                FString::Printf(TEXT("Maintiens %s... %.1f / %.1f s"),
                    *KeyToggle.GetDisplayName().ToString(), ToggleHeld, ToggleHoldSeconds));
        }
    }
    else
    {
        ToggleHeld = 0.f;
        bToggleConsumed = false;
    }

    if (!bCalibrating) return;

    // --- Maj : bascule translation <-> rotation (appui simple) ---
    if (PC->WasInputKeyJustPressed(KeyModeToggle)) bRotateMode = !bRotateMode;

    // Nudges discrets : un cran par appui (pas de maintien continu).
    auto Nudge = [&](const FVector& Axis, float Sign)
    {
        if (bRotateMode) AddLocalRotation(FQuat(Axis, FMath::DegreesToRadians(RotStep * Sign)));
        else             AddLocalOffset(Axis * MoveStep * Sign);
    };

    if (PC->WasInputKeyJustPressed(KeyXPlus))  Nudge(FVector::ForwardVector, +1.f);
    if (PC->WasInputKeyJustPressed(KeyXMinus)) Nudge(FVector::ForwardVector, -1.f);
    if (PC->WasInputKeyJustPressed(KeyYPlus))  Nudge(FVector::RightVector,   +1.f);
    if (PC->WasInputKeyJustPressed(KeyYMinus)) Nudge(FVector::RightVector,   -1.f);
    if (PC->WasInputKeyJustPressed(KeyZPlus))  Nudge(FVector::UpVector,      +1.f);
    if (PC->WasInputKeyJustPressed(KeyZMinus)) Nudge(FVector::UpVector,      -1.f);

    if (PC->WasInputKeyJustPressed(KeyCopy))  CopyOffsetToClipboard();
    if (PC->WasInputKeyJustPressed(KeyReset)) ResetOffset();

    if (bShowOnScreen && GEngine)
    {
        const uint64 Key = (uint64)(PTRINT)this;
        GEngine->AddOnScreenDebugMessage(Key, 0.f, FColor::Yellow,
            FString::Printf(TEXT("[CALIB %s] %s   |  axes: %s/%s %s/%s %s/%s   mode: %s   copy: %s   reset: %s"),
                bRotateMode ? TEXT("ROT") : TEXT("POS"),
                *GetOffsetString(),
                *KeyXPlus.GetDisplayName().ToString(), *KeyXMinus.GetDisplayName().ToString(),
                *KeyYPlus.GetDisplayName().ToString(), *KeyYMinus.GetDisplayName().ToString(),
                *KeyZPlus.GetDisplayName().ToString(), *KeyZMinus.GetDisplayName().ToString(),
                *KeyModeToggle.GetDisplayName().ToString(),
                *KeyCopy.GetDisplayName().ToString(), *KeyReset.GetDisplayName().ToString()));
    }
}
