## meta quest camera2 plugin for unreal engine

simple, fast camera2 access for unreal engine projects on android and meta quest. streams camera frames into a ue texture for realtime use in games and xr apps. this is a fork of the original by @tark146 but since they are not seeming to be merging pull requests - this is now where i will be maintaining and pushing changes.

### what it does
- allows you to quickly access your quest passthrough camera
- camera2 frame path wired to a ue `texture2d` with bgra8 updates on the render thread
- camera intrinsics exposed (fx, fy, cx, cy, skew)
- lens distortion coefficients retrieved and mapped for ue usage
- camera characteristics json dump available for diagnostics
- original, pixel-array, and active-array sizes reported
- blueprint getters for texture, intrinsics, distortion, and resolutions

### quick start
1) enable the plugin in your project  
2) call `start camera preview` (blueprint) or the c++ equivalent  
3) get the camera texture and apply it to a material/mesh or ui image  
4) call `stop camera preview` when done


### requirements
- unreal engine 5.0+  
- android sdk 21+  
- camera permissions enabled on device
- works on standalone android (including meta quest 2/3/pro) [obviously won't work on windows because JNI is the base of it all]
- windows editor: returns null texture (for workflow only)

### current limits
- color calibration may vary across devices  
- resolution selection is fixed in code for now
- out the box setup could have a pawn for a quick turnkey experience

### troubleshooting
- ensure camera permissions are granted (first launch may require a restart)  
- if you see a black texture, wait for auto-exposure and check logcat

## installation

1. Copy the `AndroidCamera2Plugin` folder to your project's `Plugins` directory
2. Regenerate project files
3. Enable the plugin in your project settings or .uproject file
## usage

### blueprint setup

1. **start camera preview:**
   ```
   SimpleCamera2Test::StartCameraPreview() -> bool
   ```
   returns true if camera started successfully

2. **get camera texture:**
   ```
   SimpleCamera2Test::GetCameraTexture() -> Texture2D
   ```
   returns the camera feed texture (can be null if not started)

3. **stop camera preview:**
   ```
   SimpleCamera2Test::StopCameraPreview()
   ```
   stops the camera and releases resources

### quick start with sample blueprint

1. **using the provided sample:**
   - place `BP_CameraImage` actor (found in the plugin content) into your level
   - this actor contains a widget with complete blueprint implementation
   - refer to the widget blueprint for implementation details

2. **manual blueprint implementation:**
   - create a new actor blueprint
   - add a plane or ui image component
   - in beginplay:
     - call `StartCameraPreview`
     - get the camera texture using `GetCameraTexture`
     - create a dynamic material instance
     - set the texture parameter to the camera texture
     - apply the material to your plane/image

### blueprint/c++ api
- `USimpleCamera2Test::StartCameraPreview() -> bool` - start camera preview
- `USimpleCamera2Test::StopCameraPreview()` - stop camera preview
- `USimpleCamera2Test::GetCameraTexture() -> UTexture2D*` - current camera texture (null if not started)
- `USimpleCamera2Test::GetCameraFx() -> float`
- `USimpleCamera2Test::GetCameraFy() -> float`
- `USimpleCamera2Test::GetPrincipalPoint() -> FVector2D`
- `USimpleCamera2Test::GetCameraSkew() -> float`
- `USimpleCamera2Test::GetCalibrationResolution() -> FIntPoint`
- `USimpleCamera2Test::GetLensDistortion() -> TArray<float>`
- `USimpleCamera2Test::GetLensDistortionUE() -> TArray<float>`
- `USimpleCamera2Test::GetOriginalResolution() -> FIntPoint`
- `USimpleCamera2Test::GetCameraCharacteristicsJson() -> FString`
- `USimpleCamera2Test::RequestCameraCharacteristicsDump()` - ask java side to dump characteristics json

## permissions
android will display a permission request dialog for camera access.  grant all camera permissions and **restart the application** to enable camera functionality.

The plugin automatically adds the following permissions to your Android manifest:

- `android.permission.CAMERA`
- `horizonos.permission.HEADSET_CAMERA` (Meta Quest)
- `horizonos.permission.AVATAR_CAMERA` (Meta Quest)

## architecture
- JNI > c++ > bp
- frames have YUV_420_888 to RGBA conversion

## camera intrinsics

- original resolution received: 1280x960 
- pixel array size: 1280x960
- intrinsics received: fx=868.31 fy=868.31 cx=640.18 cy=482.07 skew=0.000 1280x960
- original resolution received: 1280x960
- camera ids: 50, 51
- no distortion array available on this device through Camera2Api

please note that these values would also differ depending on the Camera2 JNI configuration you have setup.

## contribution
make changes, PR and contribute! :)