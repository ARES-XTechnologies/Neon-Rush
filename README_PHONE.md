# NEON RUSH v0.2 — phone-only build

This milestone upgrades the prototype into a real combat loop while keeping the original constraints.

## Rules
- No Unity.
- No Unreal.
- No Godot.
- No external sprites, models, textures, audio files or asset packs.
- All gameplay visuals are generated procedurally in C++.
- Android OpenGL ES 2 is used directly for graphics.
- Java is only Android/JNI/UI glue.

## v0.2 combat
- Player health: 100 HP.
- Enemy health: 2–3 HP per drone.
- Player bullets deal 1 damage per hit.
- Enemy health bars show remaining HP.
- Bullet collision uses swept segment-vs-circle testing to reduce high-speed tunneling.
- Enemy hits produce hit sparks.
- Enemy death produces a larger procedural explosion.
- Enemy contact deals 15 damage.
- A brief player invulnerability window prevents repeated frame-by-frame damage.
- Player receives knockback on contact.
- On 0 HP the run enters a game-over state.
- Tap anywhere after game over to restart the run.
- Multi-touch is supported: left thumb can move while the right thumb keeps firing.
- HUD now shows score, player HP and combat instructions.

## Controls
- Lower-left: move.
- Lower-right: hold to fire at the nearest enemy.
- Game over: tap anywhere to restart.

## Next milestone
v0.3 adds the movement system: dash, dodge timing, energy/stamina, stronger movement feedback and more responsive escape tools.

## Cloud build — GitHub Actions

This project includes `.github/workflows/android-build.yml` so the APK can be built on a GitHub-hosted Ubuntu runner instead of installing the Android SDK/NDK on the phone.

The workflow installs JDK 17, Gradle 8.11.1, Android Platform 35, Build Tools 35.0.0 and NDK 27.0.12077973, then runs `:app:assembleDebug` and uploads `app-debug.apk` as a workflow artifact.

### From a phone
1. Create a GitHub repository.
2. Upload the contents of this project to the repository (the `.github` folder must be included).
3. Open **Actions → Build NEON RUSH Android APK → Run workflow**.
4. Wait for the workflow to finish.
5. Open the completed workflow run and download **NEON-RUSH-v0.2-debug** from Artifacts.
6. Extract the downloaded artifact and install `app-debug.apk` on the phone.
