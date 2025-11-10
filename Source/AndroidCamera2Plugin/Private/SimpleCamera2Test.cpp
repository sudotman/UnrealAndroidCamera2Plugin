#include "SimpleCamera2Test.h"
#include "Engine/Engine.h"
#include "Async/AsyncWork.h"
#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "Rendering/Texture2DResource.h"

DEFINE_LOG_CATEGORY(LogSimpleCamera2);

#if PLATFORM_ANDROID
#include "Android/AndroidJNI.h"
#include "Android/AndroidApplication.h"
#endif





// Static variables for camera preview
static UTexture2D* CameraTexture = nullptr;
static bool bCameraPreviewActive = false;

// Intrinsics storage (pixels)
static float GCameraFx = 0.0f;
static float GCameraFy = 0.0f;
static float GCameraCx = 0.0f;
static float GCameraCy = 0.0f;
static float GCameraSkew = 0.0f;
static int32 GCameraCalibWidth = 0;
static int32 GCameraCalibHeight = 0;

// Lens distortion storage
static TArray<float> GLensDistortionCoeffs;
static int32 GLensDistortionLength = 0;

// Original resolution storage
static int32 GOriginalResolutionWidth = 0;
static int32 GOriginalResolutionHeight = 0;

// JSON dump of full CameraCharacteristics
static FString GCameraCharacteristicsJson;

#if PLATFORM_ANDROID
static jobject Camera2HelperInstance = nullptr;
#endif



// JNI callback for real Camera2 frames
#if PLATFORM_ANDROID
extern "C" JNIEXPORT void JNICALL
Java_com_epicgames_ue4_Camera2Helper_onFrameAvailable(JNIEnv* env, jclass clazz, 
    jbyteArray data, jint width, jint height)
{
    static bool bCamera2LogsOnce = false;
    if (!bCamera2LogsOnce)
    {
        UE_LOG(LogSimpleCamera2, Log, TEXT("Camera2 frame received: %dx%d"), width, height);
    }
    
    if (!CameraTexture || !data)
    {
        if (!bCamera2LogsOnce)
        {
            UE_LOG(LogSimpleCamera2, Warning, TEXT("CameraTexture or data is null"));
        }
        bCamera2LogsOnce = true;
        return;
    }
        
    // Get frame data from Java
    jbyte* frameData = env->GetByteArrayElements(data, nullptr);
    if (!frameData)
    {
        if (!bCamera2LogsOnce)
        {
            UE_LOG(LogSimpleCamera2, Error, TEXT("Failed to get frame data from Java"));
        }
        bCamera2LogsOnce = true;
        return;
    }
    
    // Copy frame data to avoid issues with async access
    int32 DataSize = width * height * 4;
    uint8* FrameDataCopy = new uint8[DataSize];
    FMemory::Memcpy(FrameDataCopy, frameData, DataSize);
    
    // Check first few bytes of frame data
    if (!bCamera2LogsOnce)
    {
        UE_LOG(LogSimpleCamera2, Warning, TEXT("Frame data sample: [%d, %d, %d, %d, %d, %d, %d, %d]"), 
            FrameDataCopy[0], FrameDataCopy[1], FrameDataCopy[2], FrameDataCopy[3],
            FrameDataCopy[4], FrameDataCopy[5], FrameDataCopy[6], FrameDataCopy[7]);
    }
    
    // Release Java array immediately
    env->ReleaseByteArrayElements(data, frameData, JNI_ABORT);
        
    // Helper: enqueue render-thread texture update taking ownership of buffer
    auto EnqueueTextureUpdateOwned = [](UTexture2D* Texture, uint8* OwnedBuffer, int32 W, int32 H)
    {
        if (!Texture || !Texture->GetResource())
        {
            if (OwnedBuffer) { delete[] OwnedBuffer; }
            return;
        }
        FTexture2DResource* TextureResource = static_cast<FTexture2DResource*>(Texture->GetResource());
        const uint32 SrcPitch = static_cast<uint32>(W) * 4u;
        FUpdateTextureRegion2D Region(0, 0, 0, 0, static_cast<uint32>(W), static_cast<uint32>(H));
        ENQUEUE_RENDER_COMMAND(UpdateCameraTexture2D)(
            [TextureResource, Region, OwnedBuffer, SrcPitch](FRHICommandListImmediate& RHICmdList)
            {
                RHICmdList.UpdateTexture2D(TextureResource->GetTexture2DRHI(), 0, Region, SrcPitch, OwnedBuffer);
                delete[] OwnedBuffer;
            });
    };

    // Update texture on game thread, then enqueue render update
    AsyncTask(ENamedThreads::Type::GameThread, [FrameDataCopy, width, height, DataSize, EnqueueTextureUpdateOwned]()
    {
        if (CameraTexture)
        {
            EnqueueTextureUpdateOwned(CameraTexture, FrameDataCopy, width, height);
            if (!bCamera2LogsOnce)
            {
                UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera2 frame enqueued to render thread: %dx%d, DataSize: %d"), width, height, DataSize);
            }
        }
        else
        {
            delete[] FrameDataCopy;
        }
        bCamera2LogsOnce = true;
    });
}
#endif

#if PLATFORM_ANDROID
// JNI callback for full CameraCharacteristics JSON dump
extern "C" JNIEXPORT void JNICALL
Java_com_epicgames_ue4_Camera2Helper_onCharacteristicsDumpAvailable(JNIEnv* env, jclass clazz,
    jstring jsonStr)
{
    const char* UTFChars = (jsonStr != nullptr) ? env->GetStringUTFChars(jsonStr, nullptr) : nullptr;
    if (UTFChars)
    {
        GCameraCharacteristicsJson = UTF8_TO_TCHAR(UTFChars);
        env->ReleaseStringUTFChars(jsonStr, UTFChars);
        UE_LOG(LogSimpleCamera2, Warning, TEXT("Received CameraCharacteristics JSON dump (%d chars)"), GCameraCharacteristicsJson.Len());
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Silver, TEXT("CameraCharacteristics dump received"));
        }
    }
    else
    {
        UE_LOG(LogSimpleCamera2, Error, TEXT("Failed to receive CameraCharacteristics JSON dump"));
    }
}

// JNI callback for intrinsics
extern "C" JNIEXPORT void JNICALL
Java_com_epicgames_ue4_Camera2Helper_onIntrinsicsAvailable(JNIEnv* env, jclass clazz,
    jfloat fx, jfloat fy, jfloat cx, jfloat cy, jfloat skew, jint width, jint height)
{
    UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera2 intrinsics received: fx=%.2f fy=%.2f cx=%.2f cy=%.2f skew=%.3f %dx%d"),
        fx, fy, cx, cy, skew, width, height);

    GCameraFx = fx;
    GCameraFy = fy;
    GCameraCx = cx;
    GCameraCy = cy;
    GCameraSkew = skew;
    GCameraCalibWidth = width;
    GCameraCalibHeight = height;

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Cyan,
            FString::Printf(TEXT("Intrinsics fx=%.0f fy=%.0f cx=%.0f cy=%.0f"), fx, fy, cx, cy));
    }
}

// JNI callback for SENSOR_INFO_PIXEL_ARRAY_SIZE
extern "C" JNIEXPORT void JNICALL
Java_com_epicgames_ue4_Camera2Helper_onPixelArraySizeAvailable(JNIEnv* env, jclass clazz,
    jint width, jint height)
{
    UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera2 pixel array size: %dx%d"), width, height);
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Silver,
            FString::Printf(TEXT("Pixel Array: %dx%d"), width, height));
    }
}

// JNI callback for SENSOR_INFO_ACTIVE_ARRAY_SIZE
extern "C" JNIEXPORT void JNICALL
Java_com_epicgames_ue4_Camera2Helper_onActiveArraySizeAvailable(JNIEnv* env, jclass clazz,
    jint width, jint height)
{
    UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera2 active array size: %dx%d"), width, height);
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Silver,
            FString::Printf(TEXT("Active Array: %dx%d"), width, height));
    }
}

// JNI callback for lens distortion coefficients
extern "C" JNIEXPORT void JNICALL
Java_com_epicgames_ue4_Camera2Helper_onDistortionAvailable(JNIEnv* env, jclass clazz,
    jfloatArray coeffs, jint length)
{
    UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera2 lens distortion received: length=%d"), length);

    if (coeffs && length > 0)
    {
        // Get distortion coefficients from Java
        jfloat* distortionData = env->GetFloatArrayElements(coeffs, nullptr);
        if (distortionData)
        {
            // Store distortion coefficients
            GLensDistortionCoeffs.SetNum(length);
            GLensDistortionLength = length;
            
            for (int32 i = 0; i < length; i++)
            {
                GLensDistortionCoeffs[i] = distortionData[i];
            }
            
            // Log first few coefficients for debugging
            FString CoeffStr = TEXT("Distortion coeffs: ");
            for (int32 i = 0; i < FMath::Min(length, 5); i++)
            {
                CoeffStr += FString::Printf(TEXT("%.4f "), GLensDistortionCoeffs[i]);
            }
            UE_LOG(LogSimpleCamera2, Warning, TEXT("%s"), *CoeffStr);
            
            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Magenta,
                    FString::Printf(TEXT("Lens Distortion: %d coeffs"), length));
            }
            
            // Release Java array
            env->ReleaseFloatArrayElements(coeffs, distortionData, JNI_ABORT);
        }
        else
        {
            UE_LOG(LogSimpleCamera2, Error, TEXT("Failed to get distortion data from Java"));
        }
    }
    else
    {
        UE_LOG(LogSimpleCamera2, Warning, TEXT("No lens distortion data available"));
        GLensDistortionCoeffs.Empty();
        GLensDistortionLength = 0;
    }
}

// JNI callback for original resolution
extern "C" JNIEXPORT void JNICALL
Java_com_epicgames_ue4_Camera2Helper_onOriginalResolutionAvailable(JNIEnv* env, jclass clazz,
    jint width, jint height)
{
    UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera2 original resolution received: %dx%d"), width, height);

    GOriginalResolutionWidth = width;
    GOriginalResolutionHeight = height;

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Orange,
            FString::Printf(TEXT("Original Resolution: %dx%d"), width, height));
    }
}
#endif

bool USimpleCamera2Test::StartCameraPreview()
{
    UE_LOG(LogSimpleCamera2, Warning, TEXT("=== StartCameraPreview CALLED FROM BLUEPRINT ==="));
    
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Red, TEXT("StartCameraPreview CALLED"));
    }
    
#if PLATFORM_ANDROID
    UE_LOG(LogSimpleCamera2, Warning, TEXT("Starting real Camera2 preview on Android"));
    
    // Auto-request camera permissions if not granted
    JNIEnv* Env = FAndroidApplication::GetJavaEnv();
    if (Env)
    {
        // Check and request permissions
        jobject Activity = FAndroidApplication::GetGameActivityThis();
        if (Activity)
        {
            UE_LOG(LogSimpleCamera2, Warning, TEXT("Checking camera permissions..."));
            
            // Get the Activity class and checkSelfPermission method
            jclass ActivityClass = Env->GetObjectClass(Activity);
            jmethodID CheckPermMethod = Env->GetMethodID(ActivityClass, 
                "checkSelfPermission", "(Ljava/lang/String;)I");
            jmethodID RequestPermMethod = Env->GetMethodID(ActivityClass,
                "requestPermissions", "([Ljava/lang/String;I)V");
            
            if (CheckPermMethod && RequestPermMethod)
            {
                // Check CAMERA permission
                jstring CameraPermStr = Env->NewStringUTF("android.permission.CAMERA");
                jint CameraPermResult = Env->CallIntMethod(Activity, CheckPermMethod, CameraPermStr);
                
                // PackageManager.PERMISSION_GRANTED = 0
                if (CameraPermResult != 0)
                {
                    UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera permission not granted, requesting..."));
                    
                    // Create permission array
                    jobjectArray PermArray = Env->NewObjectArray(3, 
                        Env->FindClass("java/lang/String"), nullptr);
                    
                    jstring Perm1 = Env->NewStringUTF("android.permission.CAMERA");
                    jstring Perm2 = Env->NewStringUTF("horizonos.permission.HEADSET_CAMERA");
                    jstring Perm3 = Env->NewStringUTF("horizonos.permission.AVATAR_CAMERA");
                    
                    Env->SetObjectArrayElement(PermArray, 0, Perm1);
                    Env->SetObjectArrayElement(PermArray, 1, Perm2);
                    Env->SetObjectArrayElement(PermArray, 2, Perm3);
                    
                    // Request permissions (request code = 1001)
                    Env->CallVoidMethod(Activity, RequestPermMethod, PermArray, 1001);
                    
                    // Clean up
                    Env->DeleteLocalRef(Perm1);
                    Env->DeleteLocalRef(Perm2);
                    Env->DeleteLocalRef(Perm3);
                    Env->DeleteLocalRef(PermArray);
                    
                    UE_LOG(LogSimpleCamera2, Warning, TEXT("Permission request sent. User must grant permission and retry."));
                    if (GEngine)
                    {
                        GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow, 
                            TEXT("Please grant camera permission and try again"));
                    }
                    
                    Env->DeleteLocalRef(CameraPermStr);
                    return false; // Return false, user needs to grant permission first
                }
                else
                {
                    UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera permission already granted"));
                }
                
                Env->DeleteLocalRef(CameraPermStr);
            }
        }
    }
    
    if (bCameraPreviewActive)
    {
        UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera preview already active"));
        return true;
    }
    
    // Create texture for camera feed if not already created
    UE_LOG(LogSimpleCamera2, Warning, TEXT("=== CHECKING CAMERA TEXTURE ==="));
    if (!CameraTexture)
    {
        UE_LOG(LogSimpleCamera2, Warning, TEXT("Creating new camera texture 1280x960"));
        CameraTexture = UTexture2D::CreateTransient(1280, 960, PF_B8G8R8A8);
        if (CameraTexture)
        {
            UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera texture created successfully"));
            CameraTexture->AddToRoot(); // Prevent garbage collection

            // Initialize with dark pattern asynchronously
            const int32 InitW = 1280;
            const int32 InitH = 960;
            const int32 InitSize = InitW * InitH * 4;
            uint8* InitData = new uint8[InitSize];
            FMemory::Memset(InitData, 64, InitSize); // Dark gray

            // Ensure resource is created before update
            CameraTexture->UpdateResource();
            AsyncTask(ENamedThreads::Type::GameThread, [InitData, InitW, InitH]()
            {
                if (CameraTexture)
                {
                    FTexture2DResource* TextureResource = static_cast<FTexture2DResource*>(CameraTexture->GetResource());
                    if (TextureResource)
                    {
                        const uint32 Pitch = static_cast<uint32>(InitW) * 4u;
                        FUpdateTextureRegion2D Region(0, 0, 0, 0, static_cast<uint32>(InitW), static_cast<uint32>(InitH));
                        ENQUEUE_RENDER_COMMAND(InitCameraTexture2D)(
                            [TextureResource, Region, InitData, Pitch](FRHICommandListImmediate& RHICmdList)
                            {
                                RHICmdList.UpdateTexture2D(TextureResource->GetTexture2DRHI(), 0, Region, Pitch, InitData);
                                delete[] InitData;
                            });
                    }
                    else
                    {
                        delete[] InitData;
                    }
                }
                else
                {
                    delete[] InitData;
                }
            });
        }
    }
    
    // Start real Camera2 using Camera2Helper
    UE_LOG(LogSimpleCamera2, Warning, TEXT("=== STARTING JNI CAMERA2HELPER ACCESS ==="));
    // Env already defined above, reuse it
    if (Env)
    {
        UE_LOG(LogSimpleCamera2, Warning, TEXT("JNI Environment obtained"));
        jobject Activity = FAndroidApplication::GetGameActivityThis();
        
        if (Activity)
        {
            UE_LOG(LogSimpleCamera2, Warning, TEXT("Game Activity obtained"));
        }
        else
        {
            UE_LOG(LogSimpleCamera2, Error, TEXT("Failed to get Game Activity"));
        }
        
        // Get Camera2Helper class - Use Activity's class loader
        UE_LOG(LogSimpleCamera2, Warning, TEXT("Finding Camera2Helper class..."));
        
        // Try using the Activity's class loader
        jclass ActivityClass = Env->GetObjectClass(Activity);
        jmethodID GetClassLoaderMethod = Env->GetMethodID(ActivityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
        jobject ClassLoader = Env->CallObjectMethod(Activity, GetClassLoaderMethod);
        
        jclass ClassLoaderClass = Env->GetObjectClass(ClassLoader);
        jmethodID LoadClassMethod = Env->GetMethodID(ClassLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        
        jstring ClassName = Env->NewStringUTF("com.epicgames.ue4.Camera2Helper");
        jclass Camera2Class = (jclass)Env->CallObjectMethod(ClassLoader, LoadClassMethod, ClassName);
        Env->DeleteLocalRef(ClassName);
        
        if (Camera2Class)
        {
            UE_LOG(LogSimpleCamera2, Warning, TEXT("✓ Camera2Helper class found successfully"));
            
            // Get getInstance method
            UE_LOG(LogSimpleCamera2, Warning, TEXT("Getting getInstance method..."));
            jmethodID GetInstanceMethod = Env->GetStaticMethodID(Camera2Class, 
                "getInstance", "(Landroid/content/Context;)Lcom/epicgames/ue4/Camera2Helper;");
                
            if (GetInstanceMethod)
            {
                UE_LOG(LogSimpleCamera2, Warning, TEXT("✓ getInstance method found"));
                
                // Get Camera2Helper instance
                UE_LOG(LogSimpleCamera2, Warning, TEXT("Calling getInstance method with Activity..."));
                jobject LocalCamera = Env->CallStaticObjectMethod(Camera2Class, 
                    GetInstanceMethod, Activity);
                    
                if (LocalCamera)
                {
                    UE_LOG(LogSimpleCamera2, Warning, TEXT("✓ Camera2Helper instance obtained"));
                    UE_LOG(LogSimpleCamera2, Warning, TEXT("Creating global reference..."));
                    Camera2HelperInstance = Env->NewGlobalRef(LocalCamera);
                    UE_LOG(LogSimpleCamera2, Warning, TEXT("✓ Global reference created"));
                    
                    // Start camera
                    UE_LOG(LogSimpleCamera2, Warning, TEXT("Getting startCamera method..."));
                    jmethodID StartMethod = Env->GetMethodID(Camera2Class, 
                        "startCamera", "()Z");
                        
                    if (StartMethod)
                    {
                        UE_LOG(LogSimpleCamera2, Warning, TEXT("✓ startCamera method found"));
                        UE_LOG(LogSimpleCamera2, Warning, TEXT("Calling startCamera method..."));
                        jboolean result = Env->CallBooleanMethod(Camera2HelperInstance, StartMethod);
                        UE_LOG(LogSimpleCamera2, Warning, TEXT("✓ startCamera method call completed"));
                        
                        bCameraPreviewActive = (result == JNI_TRUE);
                        
                        if (bCameraPreviewActive)
                        {
                            UE_LOG(LogSimpleCamera2, Warning, TEXT("✓ Real Camera2 started successfully"));
                            if (GEngine)
                            {
                                GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green, 
                                    TEXT("Camera2: Real Camera Started!"));
                            }
                        }
                        else
                        {
                            UE_LOG(LogSimpleCamera2, Error, TEXT("✗ Failed to start real Camera2 - Java method returned false"));
                            if (GEngine)
                            {
                                GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red, 
                                    TEXT("Camera2: Failed to start"));
                            }
                        }
                    }
                    else
                    {
                        UE_LOG(LogSimpleCamera2, Error, TEXT("✗ startCamera method not found"));
                    }
                    
                    UE_LOG(LogSimpleCamera2, Warning, TEXT("Cleaning up local reference..."));
                    Env->DeleteLocalRef(LocalCamera);
                    UE_LOG(LogSimpleCamera2, Warning, TEXT("✓ Local reference cleaned up"));
                }
                else
                {
                    UE_LOG(LogSimpleCamera2, Error, TEXT("✗ Failed to get Camera2Helper instance from getInstance call"));
                }
            }
            else
            {
                UE_LOG(LogSimpleCamera2, Error, TEXT("✗ getInstance method not found"));
            }
        }
        else
        {
            UE_LOG(LogSimpleCamera2, Error, TEXT("✗ Camera2Helper class not found"));
            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red, 
                    TEXT("Camera2Helper class not found"));
            }
        }
        
        // Check for JNI exceptions
        if (Env->ExceptionCheck())
        {
            UE_LOG(LogSimpleCamera2, Error, TEXT("✗ JNI Exception detected"));
            Env->ExceptionDescribe();  // Print to logcat
            Env->ExceptionClear();
        }
        else
        {
            UE_LOG(LogSimpleCamera2, Warning, TEXT("✓ No JNI exceptions detected"));
        }
    }
    else
    {
        UE_LOG(LogSimpleCamera2, Error, TEXT("✗ Failed to get JNI Environment"));
    }
    
    UE_LOG(LogSimpleCamera2, Warning, TEXT("=== StartCameraPreview FUNCTION COMPLETED ==="));
    return bCameraPreviewActive;
#else
    UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera preview: Not on Android platform"));
    return false;
#endif
}

void USimpleCamera2Test::StopCameraPreview()
{
    UE_LOG(LogSimpleCamera2, Log, TEXT("Stopping real Camera2 preview"));
    
#if PLATFORM_ANDROID
    // Stop Camera2 helper
    if (Camera2HelperInstance)
    {
        JNIEnv* Env = FAndroidApplication::GetJavaEnv();
        if (Env)
        {
            jclass Camera2Class = Env->FindClass("com/epicgames/ue4/Camera2Helper");
            if (Camera2Class)
            {
                jmethodID StopMethod = Env->GetMethodID(Camera2Class, "stopCamera", "()V");
                if (StopMethod)
                {
                    Env->CallVoidMethod(Camera2HelperInstance, StopMethod);
                }
            }
            
            Env->DeleteGlobalRef(Camera2HelperInstance);
            Camera2HelperInstance = nullptr;
        }
    }
#endif
    
    if (CameraTexture)
    {
        CameraTexture->RemoveFromRoot();
        CameraTexture = nullptr;
    }
    
    bCameraPreviewActive = false;
    
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow, TEXT("Real Camera2: Stopped"));
    }
}

UTexture2D* USimpleCamera2Test::GetCameraTexture()
{
    return CameraTexture;
}

// Blueprint accessors for intrinsics
float USimpleCamera2Test::GetCameraFx()
{
    return GCameraFx;
}

float USimpleCamera2Test::GetCameraFy()
{
    return GCameraFy;
}

FVector2D USimpleCamera2Test::GetPrincipalPoint()
{
    return FVector2D(GCameraCx, GCameraCy);
}

float USimpleCamera2Test::GetCameraSkew()
{
    return GCameraSkew;
}

FIntPoint USimpleCamera2Test::GetCalibrationResolution()
{
    return FIntPoint(GCameraCalibWidth, GCameraCalibHeight);
}

TArray<float> USimpleCamera2Test::GetLensDistortion()
{
    return GLensDistortionCoeffs;
}

FIntPoint USimpleCamera2Test::GetOriginalResolution()
{
    return FIntPoint(GOriginalResolutionWidth, GOriginalResolutionHeight);
}

TArray<float> USimpleCamera2Test::GetLensDistortionUE()
{
    // Always return 8 coefficients in the order: [K1,K2,P1,P2,K3,K4,K5,K6]
    TArray<float> Mapped;
    Mapped.SetNumZeroed(8);

    // Nothing recorded
    if (GLensDistortionLength <= 0 || GLensDistortionCoeffs.Num() <= 0)
    {
        return Mapped;
    }

    const int32 N = GLensDistortionLength;

    // Case 1: Android Brown model (5 floats): [k1, k2, k3, p1, p2]
    if (N == 5)
    {
        const float k1 = GLensDistortionCoeffs[0];
        const float k2 = GLensDistortionCoeffs[1];
        const float k3 = GLensDistortionCoeffs[2];
        const float p1 = GLensDistortionCoeffs[3];
        const float p2 = GLensDistortionCoeffs[4];

        Mapped[0] = k1; // K1
        Mapped[1] = k2; // K2
        Mapped[2] = p1; // P1
        Mapped[3] = p2; // P2
        Mapped[4] = k3; // K3
        // K4..K6 left at 0
        return Mapped;
    }

    // Case 2: Radial-only model (>=6 floats): [k1,k2,k3,k4,k5,k6,...]
    if (N >= 6)
    {
        Mapped[0] = GLensDistortionCoeffs[0]; // K1
        Mapped[1] = GLensDistortionCoeffs[1]; // K2
        // P1,P2 = 0
        Mapped[4] = GLensDistortionCoeffs[2]; // K3
        Mapped[5] = GLensDistortionCoeffs[3]; // K4
        Mapped[6] = GLensDistortionCoeffs[4]; // K5
        Mapped[7] = GLensDistortionCoeffs[5]; // K6
        return Mapped;
    }

    // Fallback: copy what we can for first two as K1,K2
    Mapped[0] = GLensDistortionCoeffs[0];
    if (N > 1) { Mapped[1] = GLensDistortionCoeffs[1]; }
    return Mapped;
}

FString USimpleCamera2Test::GetCameraCharacteristicsJson()
{
    return GCameraCharacteristicsJson;
}

void USimpleCamera2Test::RequestCameraCharacteristicsDump()
{
#if PLATFORM_ANDROID
    JNIEnv* Env = FAndroidApplication::GetJavaEnv();
    if (!Env)
    {
        UE_LOG(LogSimpleCamera2, Error, TEXT("JNI env not available for characteristics dump request"));
        return;
    }

    if (!Camera2HelperInstance)
    {
        UE_LOG(LogSimpleCamera2, Error, TEXT("Camera2HelperInstance is null; start camera first"));
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red, TEXT("Start camera before dumping characteristics"));
        }
        return;
    }

    jclass HelperClass = Env->GetObjectClass(Camera2HelperInstance);
    if (!HelperClass)
    {
        UE_LOG(LogSimpleCamera2, Error, TEXT("Failed to get Camera2Helper class for dump call"));
        return;
    }

    jmethodID DumpMethod = Env->GetMethodID(HelperClass, "dumpCameraCharacteristics", "()V");
    if (!DumpMethod)
    {
        UE_LOG(LogSimpleCamera2, Error, TEXT("dumpCameraCharacteristics() not found on Camera2Helper"));
        return;
    }

    Env->CallVoidMethod(Camera2HelperInstance, DumpMethod);

    if (Env->ExceptionCheck())
    {
        UE_LOG(LogSimpleCamera2, Error, TEXT("JNI exception during dumpCameraCharacteristics"));
        Env->ExceptionDescribe();
        Env->ExceptionClear();
    }
    else
    {
        UE_LOG(LogSimpleCamera2, Warning, TEXT("Requested CameraCharacteristics JSON dump from Java"));
    }
#else
    UE_LOG(LogSimpleCamera2, Warning, TEXT("Characteristics dump only available on Android"));
#endif
}
