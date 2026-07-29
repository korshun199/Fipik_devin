plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "ru.korshun199.fipikconfig"
    compileSdk = 34

    defaultConfig {
        applicationId = "ru.korshun199.fipikconfig"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "0.1"
    }
}

kotlin {
    jvmToolchain(17)
}
