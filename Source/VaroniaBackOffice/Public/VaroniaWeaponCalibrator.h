// VaroniaWeaponCalibrator.h — composant d'offset d'arme calibrable au clavier en live.
//
// Attache le mesh / le muzzle du gun SOUS ce composant : son RelativeTransform EST l'offset
// tracker → arme. En PIE/VR tu ajustes l'offset au clavier, puis une touche copie les valeurs
// dans le presse-papier (à coller dans les defaults du composant ou dans ta config).
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "InputCoreTypes.h"
#include "VaroniaWeaponCalibrator.generated.h"

UCLASS(ClassGroup = (Varonia), meta = (BlueprintSpawnableComponent))
class VARONIABACKOFFICE_API UVaroniaWeaponCalibrator : public USceneComponent
{
    GENERATED_BODY()

public:
    UVaroniaWeaponCalibrator();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** Mode calibration actif (écoute le clavier). OFF par défaut : maintiens KeyToggle (B) ToggleHoldSeconds pour l'activer. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Varonia|Calibration")
    bool bCalibrating = false;

    /** Pas de translation par appui (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Varonia|Calibration")
    float MoveStep = 0.25f;

    /** Pas de rotation par appui (°). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Varonia|Calibration")
    float RotStep = 1.0f;

    /** Affiche l'offset courant + l'aide touches à l'écran. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Varonia|Calibration")
    bool bShowOnScreen = true;

    /** Durée de maintien de KeyToggle pour (dés)activer la calibration (s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Varonia|Calibration")
    float ToggleHoldSeconds = 3.0f;

    /** Mode courant : false = translation, true = rotation. Bascule avec KeyModeToggle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Varonia|Calibration")
    bool bRotateMode = false;

    // --- Touches (rebindables). Sans modifier = translation ; Maj maintenu = rotation. ---
    // Axes en espace LOCAL du composant : X=avant, Y=droite, Z=haut (rotation = autour de cet axe).

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Varonia|Calibration|Keys")
    FKey KeyXPlus = EKeys::I;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Varonia|Calibration|Keys")
    FKey KeyXMinus = EKeys::K;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Varonia|Calibration|Keys")
    FKey KeyYPlus = EKeys::L;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Varonia|Calibration|Keys")
    FKey KeyYMinus = EKeys::J;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Varonia|Calibration|Keys")
    FKey KeyZPlus = EKeys::O;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Varonia|Calibration|Keys")
    FKey KeyZMinus = EKeys::U;

    /** Bascule translation <-> rotation (appui simple, pas de maintien). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Varonia|Calibration|Keys")
    FKey KeyModeToggle = EKeys::LeftShift;

    /** Copie l'offset (RelativeLocation/Rotation) dans le presse-papier. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Varonia|Calibration|Keys")
    FKey KeyCopy = EKeys::C;
    /** Remet l'offset à sa valeur de départ. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Varonia|Calibration|Keys")
    FKey KeyReset = EKeys::R;
    /** Active/désactive le mode calibration. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Varonia|Calibration|Keys")
    FKey KeyToggle = EKeys::B;

    /** Copie l'offset courant dans le presse-papier (texte lisible). */
    UFUNCTION(BlueprintCallable, Category = "Varonia|Calibration")
    void CopyOffsetToClipboard();

    /** Remet l'offset à la valeur capturée au BeginPlay. */
    UFUNCTION(BlueprintCallable, Category = "Varonia|Calibration")
    void ResetOffset();

    /** Offset courant formaté "Loc=(...) Rot=(...)". */
    UFUNCTION(BlueprintPure, Category = "Varonia|Calibration")
    FString GetOffsetString() const;

private:
    FTransform InitialRelative;

    // Suivi du maintien de KeyToggle.
    float ToggleHeld = 0.f;
    bool  bToggleConsumed = false;
};
