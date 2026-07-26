#include "custom/CustomAnimation.h"
#include "custom/KoopaBattle.h"
#include "custom/PowerUps.h"
#include "custom/PlayerCore.h"
#include "custom/PlayerSpinAttack.h"
#include "custom/PlayerSneak.h"
#include "custom/AttackSensor.h"

struct TriggerCameraReset : public mallow::hook::Trampoline<TriggerCameraReset> {
    static bool Callback(al::LiveActor* actor, int port) {
        if ((isMario || isFire || isIce || isBrawl || isSuper)
            && al::isPadTriggerR(-1)) return false;

        return Orig(actor, port);
    }
};

struct TriggerAmiibo : public mallow::hook::Trampoline<TriggerAmiibo> {
    static bool Callback(const al::IUseSceneObjHolder* holder) {
        auto* model = isHakoniwa->mModelHolder->findModelActor("Normal");
        if (isMario && al::tryGetSubActor(model, "Blaster")) return false;
        if (isKnight && al::tryGetSubActor(model, "Axe")) return false;
        return Orig(holder);
    }
};

struct AppRun : public mallow::hook::Trampoline<AppRun> {
    static void Callback(void* thisPtr) {
        nn::fs::MountSdCardForDebug("sd");
        if (!mallow::config::loadConfig(true)) {
            mallow::config::useDefaultConfig();
            mallow::config::saveConfig();
        }
        mallow::config::readConfigToStruct();
        Orig(thisPtr);
    }
};

extern "C" void userMain() {
    AppRun::InstallAtSymbol("_ZN11Application3runEv");
    PlayerCore::Install();
    PlayerSpinAttack::Install();
    PlayerSneak::Install();
    AttackSensor::Install();
    CustomAnimation::Install();
    KoopaBattle::Install();

    #ifdef ALLOW_POWERUPS
        PowerUps::Install();
        TriggerCameraReset::InstallAtSymbol("_ZN19PlayerInputFunction20isTriggerCameraResetEPKN2al9LiveActorEi");
        TriggerAmiibo::InstallAtSymbol("_ZN2rs19isTriggerAmiiboModeEPKN2al18IUseSceneObjHolderE");
    #endif
}