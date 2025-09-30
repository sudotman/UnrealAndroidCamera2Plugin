#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "SimpleCamera2Test.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSimpleCamera2, Log, All);

/**
 * Simple Camera2 API - Basic camera to texture functionality
 */
UCLASS(BlueprintType)
class ANDROIDCAMERA2PLUGIN_API USimpleCamera2Test : public UObject
{
    GENERATED_BODY()

public:
    /**
     * Start camera preview using Camera2 API
     * @return true if camera started successfully
     */
    UFUNCTION(BlueprintCallable, Category = "Camera2")
    static bool StartCameraPreview();

    /**
     * Stop camera preview and cleanup resources
     */
    UFUNCTION(BlueprintCallable, Category = "Camera2")
    static void StopCameraPreview();

    /**
     * Get the camera preview texture (null if preview not started)
     * @return texture containing camera feed
     */
    UFUNCTION(BlueprintCallable, Category = "Camera2")
    static class UTexture2D* GetCameraTexture();

    // Intrinsic calibration accessors (pixels)
    UFUNCTION(BlueprintPure, Category = "Camera2|Intrinsics")
    static float GetCameraFx();

    UFUNCTION(BlueprintPure, Category = "Camera2|Intrinsics")
    static float GetCameraFy();

    UFUNCTION(BlueprintPure, Category = "Camera2|Intrinsics")
    static FVector2D GetPrincipalPoint();

    UFUNCTION(BlueprintPure, Category = "Camera2|Intrinsics")
    static float GetCameraSkew();

    UFUNCTION(BlueprintPure, Category = "Camera2|Intrinsics")
    static FIntPoint GetCalibrationResolution();
};