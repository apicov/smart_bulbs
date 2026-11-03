#!/bin/bash
# Build the Android app (debug APK) and install it on the phone connected over USB.
# Usage: ./install-android.sh [--no-install]      (adapted from pico-reader's rebuild-prod.sh)
set -e
cd "$(dirname "$0")"
INSTALL=true
[ "${1:-}" = "--no-install" ] && INSTALL=false

# Gradle needs JDK 21 (newer JDKs are too new for it); the Android SDK gives adb
if [ -z "$JAVA_HOME" ] || ! "$JAVA_HOME/bin/java" -version 2>&1 | grep -q '"21'; then
    for candidate in /usr/lib/jvm/java-21-openjdk /usr/lib/jvm/java-21 /usr/lib/jvm/temurin-21-jdk; do
        if [ -x "$candidate/bin/java" ]; then export JAVA_HOME="$candidate"; break; fi
    done
fi
"$JAVA_HOME/bin/java" -version 2>&1 | grep -q '"21' || { echo "❌ JDK 21 not found (dnf install java-21-openjdk-devel)"; exit 1; }
export ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/Sdk}"
ADB="$(command -v adb || echo "$ANDROID_HOME/platform-tools/adb")"

if [ "$INSTALL" = true ] && ! "$ADB" devices | grep -qw "device$"; then
    echo "❌ No phone connected (USB debugging on, and this computer allowed on the phone)."
    echo "   Check with: $ADB devices    — or build only: ./install-android.sh --no-install"
    exit 1
fi

echo "📦 Building the web app and syncing it into the Android project..."
npm run build
echo "📱 Building the APK..."
(cd android && ./gradlew assembleDebug -q)
APK=android/app/build/outputs/apk/debug/app-debug.apk
echo "✅ $APK"
if [ "$INSTALL" = true ]; then
    "$ADB" install -r "$APK"
    "$ADB" shell monkey -p xyz.apicov.bulbs -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 || true
    echo "✅ Installed and started Pico Bulbs"
fi
