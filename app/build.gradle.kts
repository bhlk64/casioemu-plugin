val pluginDir = file("../plugins/plugins")

val pluginFiles = pluginDir
    .listFiles()
    ?.filter { it.isFile && it.extension == "cpp" }
    ?: emptyList()

val pluginVariants = pluginFiles.map { source ->
    val pluginName = source.nameWithoutExtension
    val flavorName = pluginName.replaceFirstChar { it.lowercase() }

    flavorName to pluginName
}

plugins {
    alias(libs.plugins.android.application)
}

android {
    namespace = "com.bhlk64.casioemu"
    compileSdk = 35

    defaultConfig {
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "1.0.0"
        
        externalNativeBuild {
            cmake {
                arguments("-DANDROID_CPP_FEATURES=rtti;exceptions")
                abiFilters.addAll(listOf("arm64-v8a", "armeabi-v7a"))
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../plugins/CMakeLists.txt")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
    }

    lint { 
        checkReleaseBuilds = false
        abortOnError = false 
    }
    
    flavorDimensions += "plugin"

    productFlavors {
        pluginVariants.forEach { (flavorName, pluginName) ->
            create(flavorName) {
                dimension = "plugin"
    
                applicationIdSuffix = ".$flavorName"
    
                externalNativeBuild {
                    cmake {
                        arguments += "-DPLUGIN_NAME=$pluginName"
                    }
                }
            }
        }
    }
}

dependencies {}