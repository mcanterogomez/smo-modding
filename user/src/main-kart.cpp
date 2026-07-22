#include "custom/_Globals.h"
#include "custom/PlayerKart.h"

struct KartInitPlayerHook : public mallow::hook::Trampoline<KartInitPlayerHook> {
    static void Callback(PlayerActorHakoniwa* thisPtr, const al::ActorInitInfo& actorInfo, const PlayerInitInfo& playerInfo) {
        isHakoniwa = nullptr;
        Orig(thisPtr, actorInfo, playerInfo);
        isHakoniwa = thisPtr;
        PlayerKart::executeInitPlayer(thisPtr, &actorInfo, &playerInfo);
    }
};

struct KartInitAfterPlacementHook : public mallow::hook::Trampoline<KartInitAfterPlacementHook> {
    static void Callback(PlayerActorHakoniwa* thisPtr) {
        Orig(thisPtr);
        PlayerKart::executeInitAfterPlacement();
    }
};

struct KartMovementHook : public mallow::hook::Trampoline<KartMovementHook> {
    static void Callback(PlayerActorHakoniwa* thisPtr) {
        Orig(thisPtr);
        PlayerKart::executeMovement(thisPtr);
    }
};

struct MotorcycleAttackSensorInline : public mallow::hook::Inline<MotorcycleAttackSensorInline> {
    static void Callback(exl::hook::InlineCtx* ctx) {
        auto* source = reinterpret_cast<al::HitSensor*>(ctx->X[19]);
        auto* target = reinterpret_cast<al::HitSensor*>(ctx->X[20]);

        if (!isValidAttackTarget(target)) return;

        rs::sendMsgHackAttack(target, source) || al::sendMsgExplosion(target, source, nullptr)
        || rs::sendMsgSphinxRideAttack(target, source) || rs::sendMsgSphinxRideAttackReflect(target, source) 
        || rs::sendMsgBullHackAttack(target, source) || rs::sendMsgKoopaCapPunchL(target, source);
    }
};

extern "C" void userMain() {
    PlayerKart::Install();
    KartInitPlayerHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa10initPlayerERKN2al13ActorInitInfoERK14PlayerInitInfo");
    KartInitAfterPlacementHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa18initAfterPlacementEv");
    KartMovementHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa8movementEv");
    MotorcycleAttackSensorInline::InstallAtOffset(0x2C77EC);
}