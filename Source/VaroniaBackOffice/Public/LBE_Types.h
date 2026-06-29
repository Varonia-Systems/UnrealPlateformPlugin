#pragma once

#include "CoreMinimal.h"
#include "LBE_Types.generated.h"

// ========================
// Enums
// ========================

UENUM(BlueprintType)
enum class EDeviceMode : uint8 {
    Server_Spectator = 0,
    Server_Player = 1,
    Client_Spectator = 2,
    Client_Player = 3
};

UENUM(BlueprintType)
enum class EMainHand : uint8 {
    Right = 0,
    Left = 1
};

UENUM(BlueprintType)
enum class ESoftState : uint8 {
    UNKNOWN = 0,
    READY = 1,
    GAME_INLOBBY = 110,
    GAME_LAUNCHED = 112,
    GAME_INPARTY = 115,
    GAME_CHECKING = 122,
    GAME_SAFETYING = 125,
    GAME_HOSTCONNECTING = 128
};

/**
 * Boutons d'arme reçus via MQTT (topic DeviceToUnity/<MAC>/<key>).
 * La valeur de l'enum = le numéro de touche du protocole :
 * 1=Primary (gâchette/tir), 2=Secondary, 3=Tertiary, 4=Quaternary.
 */
UENUM(BlueprintType)
enum class EVaroniaButton : uint8 {
    None = 0,
    Primary = 1,
    Secondary = 2,
    Tertiary = 3,
    Quaternary = 4
};

/**
 * Type / modèle d'arme (valeur lisible du champ Controller de FWeaponBinding).
 * Mapping vers les IDs Unity du catalogue via ControllerIdToType / ControllerTypeToId.
 * (Enum uint8 séquentiel : les IDs Unity 416/417/501/777 ne tiennent pas dans un uint8.)
 */
UENUM(BlueprintType)
enum class EVaroniaController : uint8 {
    Unknown            UMETA(DisplayName = "Unknown"),
    Focus3_VaroniaGun  UMETA(DisplayName = "Focus3 VaroniaGun (3)"),
    Pico_CTRL          UMETA(DisplayName = "Pico CTRL (6)"),
    Focus3_Striker     UMETA(DisplayName = "Focus3 Striker (50)"),
    Pico_VaroniaGun    UMETA(DisplayName = "Pico VaroniaGun (70)"),
    Pico_Striker       UMETA(DisplayName = "Pico Striker (80)"),
    Focus3_HK416       UMETA(DisplayName = "Focus3 HK416 (101)"),
    Pico_HK416         UMETA(DisplayName = "Pico HK416 (416)"),
    Pico_Glock         UMETA(DisplayName = "Pico Glock (417)"),
    Vortex_Focus       UMETA(DisplayName = "Vortex Focus (501)"),
    HMD                UMETA(DisplayName = "HMD (777)")
};

// ========================
// Weapon binding (multi-arme) — l'index dans FLBEConfig.Devices = weaponIndex
// ========================

USTRUCT(BlueprintType)
struct FWeaponBinding {
    GENERATED_BODY()

    /** ID contrôleur / modèle d'arme (valeurs Unity: -1..777). int32 car hors plage uint8. */
    UPROPERTY(BlueprintReadWrite, Category = "Varonia|Devices")
    int32 Controller = -1;

    /** Numéro de série / MAC du device. */
    UPROPERTY(BlueprintReadWrite, Category = "Varonia|Devices")
    FString SerialNumber;

    /** ID de tracking (serial SteamVR, id OpenXR…). Exclusif avec ForceSteamId. */
    UPROPERTY(BlueprintReadWrite, Category = "Varonia|Devices")
    FString TrackingId;

    /** Force un index device SteamVR (-1 = auto). Exclusif avec TrackingId. */
    UPROPERTY(BlueprintReadWrite, Category = "Varonia|Devices")
    int32 ForceSteamId = -1;
};

// ========================
// Global Config (GlobalConfig.json)
// ========================

USTRUCT(BlueprintType)
struct FLBEConfig {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Varonia")
    FString ServerIP = TEXT("localhost");

    UPROPERTY(BlueprintReadWrite, Category = "Varonia")
    FString MQTT_ServerIP = TEXT("localhost");

    UPROPERTY(BlueprintReadWrite, Category = "Varonia")
    int32 MQTT_IDClient = 0;

    UPROPERTY(BlueprintReadWrite, Category = "Varonia")
    EDeviceMode DeviceMode = EDeviceMode::Server_Player;

    UPROPERTY(BlueprintReadWrite, Category = "Varonia")
    FString Language = TEXT("Fr");

    UPROPERTY(BlueprintReadWrite, Category = "Varonia")
    EMainHand MainHand = EMainHand::Right;

    UPROPERTY(BlueprintReadWrite, Category = "Varonia")
    FString PlayerName = TEXT("Varonia Player");

    // --- Legacy (mono-arme) ---

    /** Force l'ancien système : ignore Devices et utilise Controller + WeaponMAC (arme 0). */
    UPROPERTY(BlueprintReadWrite, Category = "Varonia")
    bool ForceLegacyController = false;

    /** Legacy : ID contrôleur de l'arme 0. */
    UPROPERTY(BlueprintReadWrite, Category = "Varonia")
    int32 Controller = 0;

    /** Legacy : MAC / serial de l'arme 0. */
    UPROPERTY(BlueprintReadWrite, Category = "Varonia")
    FString WeaponMAC;

    // --- Multi-armes ---

    /** Liste typée d'armes ; l'index = weaponIndex. */
    UPROPERTY(BlueprintReadWrite, Category = "Varonia")
    TArray<FWeaponBinding> Devices;

    // --- Divers ---

    UPROPERTY(BlueprintReadWrite, Category = "Varonia")
    int32 HideMode = 0;

    UPROPERTY(BlueprintReadWrite, Category = "Varonia")
    bool Direct = false;

    /** Override manuel du nom de casque (vide = auto-détection). */
    UPROPERTY(BlueprintReadWrite, Category = "Varonia")
    FString HeadsetName;
};

// ========================
// Spatial Config (Spatial.json)
// ========================

/** Single boundary zone (main playarea or sub-zone like doors) */
USTRUCT(BlueprintType)
struct FSpatialBoundary {
    GENERATED_BODY()

    /** Boundary ID (e.g. "BoundaryMain", "Boundary0") */
    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    FString ID;

    /** Points already converted to Unreal coords (cm, Z-up) */
    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    TArray<FVector> Points;

    /** Boundary color */
    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    FLinearColor BoundaryColor = FLinearColor::Red;

    /** Distance at which the boundary becomes visible to the player */
    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    float DisplayDistance = 1.5f;

    /** If true, the boundary wall faces inward (sub-zone exclusion) */
    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    bool bReverse = false;

    /** Enhanced visibility */
    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    bool bBoundaryMoreVisible = false;

    /** Trigger alert when player approaches limit */
    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    bool bAlertLimit = true;

    /** Is this the main play area boundary? */
    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    bool bMainBoundary = true;

    /** Is boundary visible at all? */
    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    bool bVisible = true;
};

/** Complete spatial configuration parsed from Spatial.json */
USTRUCT(BlueprintType)
struct FSpatialConfig {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    FString ID;

    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    FString Name;

    /** Area description string (e.g. "58.89 sqm {7.5x5.5}") */
    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    FString AreaValue;

    /** Max rectangle dimensions string (e.g. "7.5x5.5") */
    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    FString MaxRect;

    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    FString GroupName;

    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    int32 MaxPlayer = 10;

    /** Sync position (already converted to Unreal coords, cm) */
    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    FVector SyncPosition = FVector::ZeroVector;

    /** Sync rotation (converted from Unity quaternion) */
    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    FRotator SyncRotation = FRotator::ZeroRotator;

    /** All boundaries (main + sub-zones) */
    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    TArray<FSpatialBoundary> Boundaries;

    /** Multiplier value from JSON */
    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    float Multiplier = 0.05f;

    /** OrthoKey reference (e.g. "Hostel-BedRooms-Small_6") */
    UPROPERTY(BlueprintReadOnly, Category = "Varonia|Spatial")
    FString OrthoKey;
};