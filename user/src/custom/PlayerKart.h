#pragma once
#include "custom/_Globals.h"

inline bool isKartSubmerged(Motorcycle* kart) {
    if (!al::isInWater(kart)) return false;
    sead::Vector3f surfacePos, surfaceNormal;
    return !al::calcFindWaterSurface(&surfacePos, &surfaceNormal, kart, al::getTrans(kart), sead::Vector3f::ey, 75.0f);
}

namespace PlayerKart {

    inline void executeInitPlayer(PlayerActorHakoniwa* thisPtr, const al::ActorInitInfo* actorInfo, const PlayerInitInfo* playerInfo) {
        // Create custom kart
        if (al::isExistArchive("ObjectData/PlayerKart")) {
            isKart = new Motorcycle("Kart");
            al::initCreateActorNoPlacementInfo(isKart, *actorInfo);
            isKart->makeActorDead();

            // Wheel tilt (Z): one value, mirrored for the left side
            al::initJointLocalZRotator(isKart, &wheelTilt, "FrontTireR"); al::initJointLocalMinusZRotator(isKart, &wheelTilt, "FrontTireL");
            al::initJointLocalZRotator(isKart, &wheelTilt, "BackTireR"); al::initJointLocalMinusZRotator(isKart, &wheelTilt, "BackTireL");
            // Wheel spin (X): continuous axle rotation while hovering
            al::initJointLocalXRotator(isKart, &wheelSpin, "FrontTireR"); al::initJointLocalXRotator(isKart, &wheelSpin, "FrontTireL");
            al::initJointLocalXRotator(isKart, &wheelSpin, "BackTireR"); al::initJointLocalXRotator(isKart, &wheelSpin, "BackTireL");
            // Propeller
            al::initJointLocalZRotator(isKart, &propellerSpin, "Propeller"); al::initJointLocalTransControllerZ(isKart, &propellerOffset, "Propeller");
            al::initJointLocalScaleController(isKart, &propellerScale, "Propeller");
        }
    }

    inline void executeInitAfterPlacement() { if (isKart) isKart->makeActorDead(); }

    inline void executeMovement(PlayerActorHakoniwa* thisPtr) {
        bool isActive = !(thisPtr->mDamageKeeper && thisPtr->mDamageKeeper->mDamageInvalidCount > 0)
            && !(thisPtr->mHackKeeper && thisPtr->mHackKeeper->mHackActor) && !rs::isActiveDemo(thisPtr);

        // Handle kart spawning
        static int holdLeftFrames = 0;
        if (al::isPadHoldLeft(-1)) holdLeftFrames++;
        else holdLeftFrames = 0;

        if (!isKart || !isActive || holdLeftFrames != 30 || thisPtr->mInput->isMove()) return;

        if (al::isAlive(isKart)) {
            if (rs::isPlayerBinding(thisPtr)) return;

            al::tryEmitEffect(isKart, "Disappear", nullptr);
            al::tryStartSe(isKart, "CommonVanishS");
            isKart->kill();
            return;
        }

        sead::Vector3f front;
        al::calcFrontDir(&front, thisPtr);
        sead::Vector3f gravity = al::getGravity(thisPtr);
        sead::Vector3f marioPos = al::getTrans(thisPtr);
        sead::Vector3f target = marioPos + front * 500.0f;

        sead::Vector3f groundPos;
        if (!alCollisionUtil::getHitPosOnArrow(thisPtr, &groundPos, target - gravity * 1000.0f, gravity * 2000.0f, nullptr, nullptr)) {
            al::tryStartSe(thisPtr, "InvalidCapAction");
            return;
        }
        target = groundPos - gravity;

        al::setTrans(isKart, target);
        isKart->appear();
        al::tryEmitEffect(isKart, "Appear", nullptr);
        al::tryStartSe(isKart, "Appear");
    }

    // Swap motorcycle anim to kart
    struct FindAnimInfoHook : public mallow::hook::Trampoline<FindAnimInfoHook> {
        static void* Callback(void* table, const char* name) {
            if (!al::isEqualSubString(name, "Motorcycle") 
                || !isKart || !isHakoniwa || !isHakoniwa->mBindKeeper->mBindSensor
                || al::getSensorHost(isHakoniwa->mBindKeeper->mBindSensor) != (al::LiveActor*)isKart) return Orig(table, name);

            sead::FixedSafeString<64> kart;
            kart.format("Kart%s", name + strlen("Motorcycle"));
            if (void* result = Orig(table, kart.cstr())) return result;

            return Orig(table, name);
        }
    };

    struct CalcAnimHook : public mallow::hook::Trampoline<CalcAnimHook> {
        static void Callback(al::LiveActor* actor) {
            if (actor != (al::LiveActor*)isKart || !al::isAlive(actor)) { Orig(actor); return; }

            auto* kart = static_cast<Motorcycle*>(actor);

            // Dampen lean while tilted
            float savedLean = kart->mLean;
            kart->mLean *= wheelTilt / 90.0f;

            Orig(actor);

            kart->mLean = savedLean;
        }
    };

    struct InitActorSuffixHook : public mallow::hook::Trampoline<InitActorSuffixHook> {
        static void Callback(al::LiveActor* actor, const al::ActorInitInfo& info, const char* suffix) {
            if (actor == (al::LiveActor*)isKart) {
                al::initActorWithArchiveName(actor, info, "PlayerKart", suffix);
                return;
            }
            Orig(actor, info, suffix);
        }
    };

    struct MotorcycleMovementHook : public mallow::hook::Trampoline<MotorcycleMovementHook> {
        static void Callback(al::LiveActor* actor) {
            if (actor != (al::LiveActor*)isKart || !al::isAlive(actor)) { Orig(actor); return; }

            auto* kart = static_cast<Motorcycle*>(actor);
            auto* collider = static_cast<IUsePlayerCollision*>(kart);
            const char* actionName = al::getActionName(kart);

            // Hover state: hazard ground forces it on, safe ground releases it
            bool wasAntiGravity = isAntiGravity;
            if (isKartSubmerged(kart) || rs::isCollisionCodeDamageFireGround(collider) || rs::isCollisionCodePoisonTouch(collider)) isAntiGravity = true;
            else if (rs::isOnGround(kart, kart)) isAntiGravity = false;

            if (isAntiGravity != wasAntiGravity) al::tryStartSe(kart, isAntiGravity ? "HoverStart" : "HoverFinish");

            // World border: while ridden, feed the keeper the kart's data so it obeys the same edge walls Mario does
            if (isHakoniwa) {
                char* keeper = reinterpret_cast<char*>(isHakoniwa->mInfo->mWorldEndBorderKeeper);
                *reinterpret_cast<sead::Vector3f*>(keeper + 0x18) = al::getTrans(kart);
                *reinterpret_cast<sead::Vector3f*>(keeper + 0x24) = al::getVelocity(kart);
                *reinterpret_cast<bool*>(keeper + 0x30) = !rs::isCollidedGround(collider);
                reinterpret_cast<al::NerveExecutor*>(keeper)->updateNerve();

                *al::getTransPtr(kart) += *reinterpret_cast<sead::Vector3f*>(keeper + 0x54);
            }

            // Hover blend: single eased driver for tilt, propeller scale, and offset
            hoverBlend = al::lerpValue(hoverBlend, isAntiGravity ? 1.0f : 0.0f, 0.05f);
            wheelTilt = hoverBlend * 90.0f;
            propellerScale = {hoverBlend, hoverBlend, hoverBlend};
            propellerOffset = 25.0f * (1.0f - hoverBlend);

            // Tire light: on while hovering, off otherwise
            if (al::isExistMaterial(kart, "GravityMT")) {
                if (isAntiGravity) al::showMaterial(kart, "GravityMT");
                else al::hideMaterial(kart, "GravityMT");
            }

            // Wheels: continuous axle spin while hovering
            wheelSpin += hoverBlend;

            // Propeller: spins during run/land, eases in/out, effect follows the spin
            float targetSpeed = (al::isEqualSubString(actionName, "Run") || al::isEqualSubString(actionName, "Land")) ? 20.0f : 0.0f;
            propellerSpeed = al::lerpValue(propellerSpeed, targetSpeed, 0.1f);
            propellerSpin += propellerSpeed;

            bool isSpinning = isAntiGravity && propellerSpeed > 5.0f;
            if (isSpinning != al::isEffectEmitting(kart, "PropellerSpin")) {
                if (isSpinning) al::tryEmitEffect(kart, "PropellerSpin", nullptr);
                else al::tryDeleteEffect(kart, "PropellerSpin");
            }

            // Anim: while hovering, land/run should look like swimming instead
            if (isAntiGravity && !al::isEqualSubString(actionName, "Swim")) {
                if (al::isEqualSubString(actionName, "Land")) al::tryStartAction(kart, "SwimLand");
                else if (al::isEqualSubString(actionName, "Run")) al::tryStartAction(kart, "SwimRun");
            }

            Orig(actor);
        }
    };

    template <uintptr_t Offset>
    struct ForceOutOfWaterInline : public mallow::hook::Inline<ForceOutOfWaterInline<Offset>> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            if (reinterpret_cast<void*>(ctx->X[19]) != isKart) return;
            ctx->X[0] = 0; // force al::isInWater's return to false, kart only
        }
    };

    inline void Install() {
        InitActorSuffixHook::InstallAtSymbol("_ZN2al15initActorSuffixEPNS_9LiveActorERKNS_13ActorInitInfoEPKc");
        MotorcycleMovementHook::InstallAtSymbol("_ZN10Motorcycle8movementEv");
        CalcAnimHook::InstallAtSymbol("_ZN2al9LiveActor8calcAnimEv");
        FindAnimInfoHook::InstallAtSymbol("_ZNK2al13AnimInfoTable12findAnimInfoEPKc");
        ForceOutOfWaterInline<0x2C92D0>::InstallAtOffset(0x2C92D0); // Motorcycle::movement, while riding
        ForceOutOfWaterInline<0x2CA110>::InstallAtOffset(0x2CA110); // sub_71002CA0E0, on dismount/fall

        exl::patch::CodePatcher motorcycleJointCapPatcher(0x2C72A4); // Motorcycle's joint keeper bumped 6 to 20 to fit tilt + spin rotators
        //motorcycleJointCapPatcher.WriteInst(0x52800181); // MOV W1, #12
        motorcycleJointCapPatcher.WriteInst(0x52800281); // MOV W1, #20

        exl::patch::CodePatcher moveLimitFilterPatcher(0x2C7520); // Motorcycle's init to NOP wall boundaries
        moveLimitFilterPatcher.WriteInst(exl::armv8::inst::Nop());
    }
}