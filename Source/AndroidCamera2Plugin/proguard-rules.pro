# Keep Camera2 plugin classes
-keep class com.epicgames.ue4.SimpleCamera { *; }
-keep class com.epicgames.ue4.Camera2Helper { *; }

# Keep native methods
-keepclasseswithmembernames class * {
    native <methods>;
}