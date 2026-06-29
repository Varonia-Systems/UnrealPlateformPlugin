#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "InputCoreTypes.h"                   // FKey / EKeys
#include "LBE_Types.h"
#include "VaroniaMqttClient.h"
#include "Interface/MqttClientInterface.h"   // IMqttClientInterface, FOnMessageDelegate, FMqttMessage
#include "VaroniaBackOfficeManager.generated.h"

// Custom log category — control in console: Log LogVaronia Verbose / Log LogVaronia Warning
DECLARE_LOG_CATEGORY_EXTERN(LogVaronia, Log, All);

// Event dispatchers (ex Event Dispatchers de BP_Varonia)
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBackOfficeIsReady);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnVaroniaStartWithTuto);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnVaroniaStartWithoutTuto);

// Input d'arme reçu via MQTT : (index dans Devices ou -1, type d'arme, bouton, pressé, MAC)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FOnVaroniaWeaponInput, int32, WeaponIndex, EVaroniaController, Controller, EVaroniaButton, Button, bool, bPressed, const FString&, Mac);

// Tir : gâchette principale (Primary) enfoncée. (index dans Devices ou -1, type d'arme, MAC)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnVaroniaWeaponFire, int32, WeaponIndex, EVaroniaController, Controller, const FString&, Mac);

class UStaticMesh;
class UMaterialInterface;
class USceneComponent;
class UCameraComponent;
class SWidget;
class FVaroniaDebugInputProcessor;

UCLASS()
class VARONIABACKOFFICE_API UVaroniaBackOfficeManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // --- Config ---

    // Interne : (re)charge GlobalConfig.json dans CurrentConfig. Appelé par Initialize().
    bool LoadLBEConfig();

    /** Lecture de la config globale (GlobalConfig.json déjà chargé en mémoire). */
    UFUNCTION(BlueprintPure, Category = "Varonia|Config")
    FLBEConfig GetGlobalConfig() const { return CurrentConfig; }

    /**
     * Résout l'arme à l'index donné (réplique la logique Unity) :
     * - si !ForceLegacyController et Devices[index] existe → Devices[index]
     * - sinon, pour index 0 → {Controller, WeaponMAC} legacy
     * - sinon → false.
     */
    UFUNCTION(BlueprintCallable, Category = "Varonia|Config")
    bool GetWeaponBinding(int32 WeaponIndex, FWeaponBinding& OutBinding) const;

    UPROPERTY(BlueprintReadWrite, Category = "Varonia|Config")
    bool GameStarted;

    UPROPERTY(BlueprintReadWrite, Category = "Varonia|Config")
    ESoftState CurrentSoftState = ESoftState::GAME_LAUNCHED;

    // Config globale en mémoire (GlobalConfig.json). Accès BP via GetGlobalConfig() ; C++ direct.
    UPROPERTY()
    FLBEConfig CurrentConfig;

    UPROPERTY(BlueprintReadOnly, Category = "Varonia|MQTT")
    UVaroniaMqttClient* MqttHandler = nullptr;

    // --- Event Dispatchers ---

    UPROPERTY(BlueprintAssignable, Category = "Varonia")
    FOnBackOfficeIsReady BackOfficeIsReady;

    UPROPERTY(BlueprintAssignable, Category = "Varonia")
    FOnVaroniaStartWithTuto OnStartWithTuto;

    UPROPERTY(BlueprintAssignable, Category = "Varonia")
    FOnVaroniaStartWithoutTuto OnStartWithoutTuto;

    /**
     * Tir / bouton d'arme reçu via MQTT (DeviceToUnity/<MAC>/<key>).
     * Bind tôt (BeginPlay). bPressed = true à l'appui, false au relâché.
     * Pour le tir principal : filtrer Button == Primary && bPressed.
     */
    UPROPERTY(BlueprintAssignable, Category = "Varonia|Input")
    FOnVaroniaWeaponInput OnWeaponInput;

    /**
     * Tir (down) : se déclenche au moment où la gâchette principale (Primary) est enfoncée,
     * pour n'importe quelle arme. Filtrer WeaponIndex == 0 pour la 1re arme.
     */
    UPROPERTY(BlueprintAssignable, Category = "Varonia|Input")
    FOnVaroniaWeaponFire OnWeaponFire;

    /** Tir (up) : gâchette principale (Primary) relâchée. */
    UPROPERTY(BlueprintAssignable, Category = "Varonia|Input")
    FOnVaroniaWeaponFire OnWeaponFireReleased;

    // --- Réglages réseau ---

    // Interne : délai avant le check de connexion MQTT (s). Non exposé au BP.
    float ConnectDelay = 0.5f;

    // Interne : période du heartbeat SET_SOFTSTATE (s). Non exposé au BP.
    float PingInterval = 1.0f;

    // --- Réglages visualisation des boundaries (lazy-load des assets par défaut) ---

    UPROPERTY(BlueprintReadWrite, Category = "Varonia|Boundaries")
    UStaticMesh* SegmentMesh = nullptr;   // défaut /Engine/BasicShapes/Cube

    UPROPERTY(BlueprintReadWrite, Category = "Varonia|Boundaries")
    UStaticMesh* PointMesh = nullptr;     // défaut /Engine/BasicShapes/Sphere

    UPROPERTY(BlueprintReadWrite, Category = "Varonia|Boundaries")
    UMaterialInterface* LineMaterial = nullptr;   // défaut /VaroniaBackOffice/M_LINE

    UPROPERTY(BlueprintReadWrite, Category = "Varonia|Boundaries")
    FVector2D SegmentScale = FVector2D(0.05f, 0.05f);

    UPROPERTY(BlueprintReadWrite, Category = "Varonia|Boundaries")
    float PointScale = 0.1f;

    // --- API MQTT (ex fonctions de BP_Varonia) ---

    UFUNCTION(BlueprintPure, Category = "Varonia|MQTT")
    FString SetTopic(const FString& Topic) const;

    UFUNCTION(BlueprintCallable, Category = "Varonia|MQTT")
    FString SetSoftState(ESoftState NewState);

    UFUNCTION(BlueprintCallable, Category = "Varonia|MQTT")
    FString SetSoftPartyStarted();

    UFUNCTION(BlueprintCallable, Category = "Varonia|MQTT")
    FString SetSoftPartyClosed();

    UFUNCTION(BlueprintCallable, Category = "Varonia|MQTT")
    void SubscribeTopic();

    // --- Input arme (MQTT DeviceToUnity) ---

    /** État courant d'un bouton d'arme (true = maintenu). Pour le tir : (idx, Primary). */
    UFUNCTION(BlueprintPure, Category = "Varonia|Input")
    bool GetWeaponButton(int32 WeaponIndex, EVaroniaButton Button) const;

    /** Index dans Devices du 1er device dont SerialNumber == Mac (-1 si introuvable). */
    UFUNCTION(BlueprintPure, Category = "Varonia|Input")
    int32 ResolveWeaponIndexByMac(const FString& Mac) const;

    /** Type/modèle lisible de l'arme à cet index (Unknown si hors Devices). */
    UFUNCTION(BlueprintPure, Category = "Varonia|Input")
    EVaroniaController GetWeaponController(int32 WeaponIndex) const;

    /** Index de la 1re arme de ce type dans Devices (-1 si aucune). Ex: la HK416. */
    UFUNCTION(BlueprintPure, Category = "Varonia|Input")
    int32 FindWeaponIndexByController(EVaroniaController Type) const;

    /** Tous les index d'armes de ce type (vide si aucune). */
    UFUNCTION(BlueprintPure, Category = "Varonia|Input")
    TArray<int32> FindWeaponIndicesByController(EVaroniaController Type) const;

    // --- Debug (simule les commandes serveur de démarrage de partie) ---

    /** Simule GET_SOFTPARTYSTART_RESULT : déclenche OnStartWithTuto. */
    UFUNCTION(BlueprintCallable, Category = "Varonia|Debug")
    void DebugStartWithTuto();

    /** Simule GET_SOFTPARTYSKIPTUTOANDSTART_RESULT : déclenche OnStartWithoutTuto. */
    UFUNCTION(BlueprintCallable, Category = "Varonia|Debug")
    void DebugStartWithoutTuto();

    /** Touche d'ouverture de l'overlay debug (global, géré par le subsystem — aucun composant à ajouter). */
    UPROPERTY(BlueprintReadWrite, Category = "Varonia|Debug")
    FKey DebugMenuKey = EKeys::F12;

    UFUNCTION(BlueprintCallable, Category = "Varonia|Debug")
    void ToggleDebugMenu();

    UFUNCTION(BlueprintCallable, Category = "Varonia|Debug")
    void ShowDebugMenu();

    UFUNCTION(BlueprintCallable, Category = "Varonia|Debug")
    void HideDebugMenu();

    /** ID Unity (3/6/.../777) → type lisible. */
    UFUNCTION(BlueprintPure, Category = "Varonia|Input")
    EVaroniaController ControllerIdToType(int32 ControllerId) const;

    /** Type lisible → ID Unity (3/6/.../777 ; -1 pour Unknown). */
    UFUNCTION(BlueprintPure, Category = "Varonia|Input")
    int32 ControllerTypeToId(EVaroniaController Type) const;

    // --- Spatial ---

    // Interne : chargé automatiquement par Initialize() (NewSpatial.json). Non exposé au BP.
    bool LoadSpatialConfig();

    // Config spatiale en mémoire (NewSpatial.json). Accès BP via GetSpatialConfig() ; C++ direct.
    UPROPERTY()
    FSpatialConfig SpatialConfig;

    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    bool bSpatialConfigLoaded = false;

    /** Lecture de la config spatiale complète (NewSpatial.json déjà chargé en mémoire). */
    UFUNCTION(BlueprintPure, Category = "Varonia|Spatial")
    FSpatialConfig GetSpatialConfig() const { return SpatialConfig; }

    UFUNCTION(BlueprintCallable, Category = "Varonia|Spatial")
    void SyncPosAndRot(AActor* PlayerVR);

    UFUNCTION(BlueprintCallable, Category = "Varonia|Spatial")
    void CoefMul(USceneComponent* PosMul, UCameraComponent* Camera);

    UFUNCTION(BlueprintCallable, Category = "Varonia|Spatial")
    void SpawnBoundaries(AActor* PlayerVR);

    UFUNCTION(BlueprintCallable, Category = "Varonia|Spatial")
    void SetSyncAndBoundaries(AActor* PlayerVR);

    /** Transform de synchro spatiale (position + rotation) à appliquer au rig depuis un BP. */
    UFUNCTION(BlueprintPure, Category = "Varonia|Spatial")
    FTransform GetSyncTransform() const;

    // --- Spatial Helpers ---

    UFUNCTION(BlueprintPure, Category = "Varonia|Spatial")
    bool GetMainBoundary(FSpatialBoundary& OutBoundary) const;

    UFUNCTION(BlueprintPure, Category = "Varonia|Spatial")
    TArray<FSpatialBoundary> GetSubBoundaries() const;

    /** Toutes les boundaries (principale + sous-zones), pour une visualisation custom. */
    UFUNCTION(BlueprintPure, Category = "Varonia|Spatial")
    TArray<FSpatialBoundary> GetAllBoundaries() const;

    /** Tous les points de toutes les boundaries, aplatis en une seule liste. */
    UFUNCTION(BlueprintPure, Category = "Varonia|Spatial")
    TArray<FVector> GetAllPoints() const;

    /** Points de la boundary principale (liste vide s'il n'y en a pas). */
    UFUNCTION(BlueprintPure, Category = "Varonia|Spatial")
    TArray<FVector> GetMainBoundaryPoints() const;

private:
    FString GetConfigPath();
    FString GetSpatialPath();

    void OnWorldCreated(UWorld* World, const UWorld::InitializationValues IValues);

    static FVector UnityToUnreal(float X, float Y, float Z);
    static FRotator UnityQuatToUnrealRotator(float X, float Y, float Z, float W);

    virtual void Deinitialize() override;

    // --- Runtime (ex logique de BP_Varonia) ---
    UPROPERTY()
    TScriptInterface<IMqttClientInterface> MqttClientInterface;

    FTimerHandle ConnectTimerHandle;
    FTimerHandle PingTimerHandle;

    IMqttClientInterface* GetClient();
    void AfterConnectDelay();
    void StartGameLogic();
    void PingTick();
    void PublishOnTopic(const FString& BaseTopic, const FString& Json);
    void EnsureBoundaryAssets();

    UFUNCTION()
    void OnMqttMessage(FMqttMessage Message);

    // Parse un message DeviceToUnity/<MAC>/<key> et broadcast OnWeaponInput.
    void HandleDeviceInput(const FString& Topic, const FString& Body);

    // État des boutons par MAC (bitmask : bit n = EVaroniaButton n).
    TMap<FString, uint8> WeaponButtonState;

    // --- Debug overlay (global, via input pre-processor Slate ; aucun composant requis) ---
    TSharedRef<SWidget> BuildDebugMenu();
    TSharedPtr<SWidget> DebugMenuWidget;
    TSharedPtr<FVaroniaDebugInputProcessor> DebugInputProcessor;
    bool bDebugMenuShown = false;
};
