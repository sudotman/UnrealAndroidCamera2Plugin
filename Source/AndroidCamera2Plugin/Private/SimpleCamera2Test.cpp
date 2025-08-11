#include "SimpleCamera2Test.h"
#include "Engine/Engine.h"
#include "Async/AsyncWork.h"
#include "Engine/Texture2D.h"

DEFINE_LOG_CATEGORY(LogSimpleCamera2);

#if PLATFORM_ANDROID
#include "Android/AndroidJNI.h"
#include "Android/AndroidApplication.h"
#endif





// Static variables for camera preview
static UTexture2D* CameraTexture = nullptr;
static bool bCameraPreviewActive = false;

#if PLATFORM_ANDROID
static jobject Camera2HelperInstance = nullptr;
#endif



// JNI callback for real Camera2 frames
#if PLATFORM_ANDROID
extern "C" JNIEXPORT void JNICALL
Java_com_epicgames_ue4_Camera2Helper_onFrameAvailable(JNIEnv* env, jclass clazz, 
    jbyteArray data, jint width, jint height)
{
    UE_LOG(LogSimpleCamera2, Log, TEXT("Camera2 frame received: %dx%d"), width, height);
    
    if (!CameraTexture || !data)
    {
        UE_LOG(LogSimpleCamera2, Warning, TEXT("CameraTexture or data is null"));
        return;
    }
        
    // Get frame data from Java
    jbyte* frameData = env->GetByteArrayElements(data, nullptr);
    if (!frameData)
    {
        UE_LOG(LogSimpleCamera2, Error, TEXT("Failed to get frame data from Java"));
        return;
    }
    
    // Copy frame data to avoid issues with async access
    int32 DataSize = width * height * 4;
    uint8* FrameDataCopy = new uint8[DataSize];
    FMemory::Memcpy(FrameDataCopy, frameData, DataSize);
    
    // Check first few bytes of frame data
    UE_LOG(LogSimpleCamera2, Warning, TEXT("Frame data sample: [%d, %d, %d, %d, %d, %d, %d, %d]"), 
        FrameDataCopy[0], FrameDataCopy[1], FrameDataCopy[2], FrameDataCopy[3],
        FrameDataCopy[4], FrameDataCopy[5], FrameDataCopy[6], FrameDataCopy[7]);
    
    // Release Java array immediately
    env->ReleaseByteArrayElements(data, frameData, JNI_ABORT);
        
    // Update texture on game thread
    AsyncTask(ENamedThreads::GameThread, [FrameDataCopy, width, height, DataSize]()
    {
        if (CameraTexture)
        {
            // Update texture data
            FTexture2DMipMap& Mip = CameraTexture->GetPlatformData()->Mips[0];
            void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
            FMemory::Memcpy(TextureData, FrameDataCopy, DataSize);
            Mip.BulkData.Unlock();
            CameraTexture->UpdateResource();
            
            UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera2 frame applied on game thread: %dx%d, DataSize: %d"), width, height, DataSize);
        }
        
        // Clean up the copied data
        delete[] FrameDataCopy;
    });
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
        UE_LOG(LogSimpleCamera2, Warning, TEXT("Creating new camera texture 320x240"));
        CameraTexture = UTexture2D::CreateTransient(320, 240, PF_B8G8R8A8);
        if (CameraTexture)
        {
            UE_LOG(LogSimpleCamera2, Warning, TEXT("Camera texture created successfully"));
            CameraTexture->AddToRoot(); // Prevent garbage collection
            
            // Initialize with dark pattern to show it's waiting for camera
            FTexture2DMipMap& Mip = CameraTexture->GetPlatformData()->Mips[0];
            void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
            FMemory::Memset(TextureData, 64, 320 * 240 * 4); // Dark gray
            Mip.BulkData.Unlock();
            CameraTexture->UpdateResource();
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
