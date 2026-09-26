#pragma once
#include "custom/.Globals.h"

inline void isKartReset() {
    isAntiGravity = false;
    hoverBlend = 0.0f;
    propellerSpeed = 0.0f;
    al::hideMaterial(isKart, "GravityMT");
    rs::resetCollision(static_cast<IUsePlayerCollision*>(isKart));
}

inline bool isKartSubmerged(const Motorcycle* kart) {
    if (!al::isInWater(kart)) return false;
    sead::Vector3f surfacePos, surfaceNormal;
    return !al::calcFindWaterSurface(&surfacePos, &surfaceNormal, kart, al::getTrans(kart), sead::Vector3f::ey, 75.0f);
}

// Vanilla only applies the world border to the player, so give the kart its own
class WorldEndBorderKeeper : public al::NerveExecutor {
public:
	WorldEndBorderKeeper(const al::LiveActor*);
	void update(const sead::Vector3f& trans, const sead::Vector3f& velocity, bool isAirborne);

	char gap[0x54 - 0x10];
	sead::Vector3f mPullBack;	// 0x54
};

namespace PlayerKart {

    inline void executeInitPlayer(PlayerActorHakoniwa* thisPtr, const al::ActorInitInfo* actorInfo, const PlayerInitInfo* playerInfo) {
        // Create custom kart
        if (al::isExistArchive("ObjectData/PlayerKart")) {
            isKart = new Motorcycle("Kart");
            al::initCreateActorNoPlacementInfo(isKart, *actorInfo);
            kartBorder = new WorldEndBorderKeeper(isKart);
            isKart->makeActorDead();

            // Wheel tilt (Z): one value, mirrored for the left side
            al::initJointLocalZRotator(isKart, &wheelTilt, "FrontTireR"); al::initJointLocalMinusZRotator(isKart, &wheelTilt, "FrontTireL");
            al::initJointLocalZRotator(isKart, &wheelTilt, "BackTireR"); al::initJointLocalMinusZRotator(isKart, &wheelTilt, "BackTireL");
            // Wheel spin (X): continuous axle rotation while hovering
            al::initJointLocalXRotator(isKart, &wheelSpin, "FrontTireR"); al::initJointLocalXRotator(isKart, &wheelSpin, "FrontTireL");
            al::initJointLocalXRotator(isKart, &wheelSpin, "BackTireR"); al::initJointLocalXRotator(isKart, &wheelSpin, "BackTireL");
            // Wheel steer (Y): post-quat rotates the joint in parent space, so the anim's spin turns with it; registered last to wrap tilt and spin too
            al::initJointPostQuatController(isKart, &wheelSteerQuat, "FrontTireR"); al::initJointPostQuatController(isKart, &wheelSteerQuat, "FrontTireL");
            // Propeller
            al::initJointLocalZRotator(isKart, &propellerSpin, "Propeller"); al::initJointLocalTransControllerZ(isKart, &propellerOffset, "Propeller");
            al::initJointLocalScaleController(isKart, &propellerScale, "Propeller");
        }
    }

    inline void executeInitAfterPlacement() { if (isKart) isKart->makeActorDead(); }

    inline void executeMovement(PlayerActorHakoniwa* thisPtr) {
        bool isActive = !(thisPtr->mDamageKeeper && thisPtr->mDamageKeeper->mDamageInvalidCount > 0)
            && !isHacking() && !rs::isActiveDemo(thisPtr);

        // Handle kart spawning
        static int holdLeftFrames = 0;
        if (al::isPadHoldLeft(-1) && !thisPtr->mInput->isMove() && isActive) holdLeftFrames++;
        else holdLeftFrames = 0;

        if (!isKart || holdLeftFrames != 30) return;

        if (al::isAlive(isKart)) {
            if (rs::isPlayerBinding(thisPtr)) return;

            al::tryEmitEffect(isKart, "Disappear", nullptr);
            al::tryStartSe(isKart, "CommonVanishS");
            isKart->kill();
            return;
        }

        float maxStep = 500.0f;	// tallest ledge the kart will spawn on

        sead::Vector3f front;
        al::calcFrontDir(&front, thisPtr);
        sead::Vector3f gravity = al::getGravity(thisPtr);
        sead::Vector3f rayStart = al::getTrans(thisPtr) - gravity * maxStep;

        // Refuse if something blocks the way, or if there is no ground to land on
        sead::Vector3f hitPos, groundNormal;
        if (alCollisionUtil::getHitPosOnArrow(thisPtr, &hitPos, rayStart, front * 500.0f, nullptr, nullptr)
            || !alCollisionUtil::getHitPosAndNormalOnArrow(thisPtr, &hitPos, &groundNormal, rayStart + front * 500.0f, gravity * (maxStep + 1000.0f), nullptr, nullptr)) {
            al::tryStartSe(thisPtr, "InvalidCapAction");
            return;
        }

        sead::Quatf quat;
        al::makeQuatUpFront(&quat, groundNormal, front);

        al::setTrans(isKart, hitPos - gravity);
        al::updatePoseQuat(isKart, quat);
        isKart->appear();
        isKartReset();

        al::tryEmitEffect(isKart, "Appear", nullptr);
        al::tryStartSe(isKart, "Appear");
    }

    struct InitActorSuffixHook : public mallow::hook::Trampoline<InitActorSuffixHook> {
        static void Callback(al::LiveActor* actor, const al::ActorInitInfo& info, const char* suffix) {
            if (actor == (al::LiveActor*)isKart) {
                al::initActorWithArchiveName(actor, info, "PlayerKart", suffix);
                return;
            }
            Orig(actor, info, suffix);
        }
    };

    // Swap motorcycle anim to kart
    struct FindAnimInfoHook : public mallow::hook::Trampoline<FindAnimInfoHook> {
        static void* Callback(void* table, const char* name) {
            if (!name) return Orig(table, name);

            // Mario riding: Motorcycle* -> Kart*
            if (table == getAnimTable(isHakoniwa)
                && isHakoniwa->mBindKeeper->mBindSensor
                && al::getSensorHost(isHakoniwa->mBindKeeper->mBindSensor) == (al::LiveActor*)isKart
                && al::isEqualSubString(name, "Motorcycle")
            ) {
                sead::FixedSafeString<64> kart;
                kart.format("Kart%s", name + strlen("Motorcycle"));
                if (auto* result = Orig(table, kart.cstr())) return result;
                return Orig(table, name);
            }

            // Kart actor: ground anims -> swim anims in anti-gravity
            if (isAntiGravity && table == getAnimTable(isKart)
            ) {
                if (al::isEqualString(name, "Run") || al::isEqualString(name, "RunCollide")) {
                    if (auto* result = Orig(table, "SwimRun")) return result;
                }
                else if (al::isEqualString(name, "Land")) {
                    if (auto* result = Orig(table, "SwimLand")) return result;
                }
            }

            return Orig(table, name);
        }
    };

    struct MotorcycleCalcAnimHook : public mallow::hook::Trampoline<MotorcycleCalcAnimHook> {
        static void Callback(al::LiveActor* actor) {
            if (actor != (al::LiveActor*)isKart || !al::isAlive(actor)) { Orig(actor); return; }

            auto* kart = static_cast<Motorcycle*>(actor);

            // Lean: none on the ground, 75% while hovering
            float savedLean = kart->mLean;
            kart->mLean *= 0.75f * hoverBlend;

            // Steer: hand it to the per-tire post-quats and zero the vanilla one, whose Cowl joint parents both front tires
            al::makeQuatYDegree(&wheelSteerQuat, kart->mSteer);
            kart->mSteer = 0.0f;

            Orig(actor);

            kart->mLean = savedLean;
        }
    };
    struct MotorcycleMovementHook : public mallow::hook::Trampoline<MotorcycleMovementHook> {
		static void Callback(al::LiveActor* actor) {
			if (actor != (al::LiveActor*)isKart || !al::isAlive(actor)) { Orig(actor); return; }

			auto* kart = static_cast<Motorcycle*>(actor);
			auto* collider = static_cast<IUsePlayerCollision*>(kart);
			const char* actionName = al::getActionName(kart);
			bool isSubmerged = isKartSubmerged(kart);

			// Hover state: hazard ground forces it on, safe ground releases it
			bool wasAntiGravity = isAntiGravity;
			if (isSubmerged || rs::isCollisionCodeDamageFireGround(collider) || rs::isCollisionCodePoisonTouch(collider) || isHakoniwa->mInfo->mIsMoon) isAntiGravity = true;
			else if (rs::isOnGround(kart, kart)) isAntiGravity = false;

			// Hover edge: sound, tire light, and re-run the anim lookup so the swap lands this frame
			if (isAntiGravity != wasAntiGravity) {
				al::tryStartSe(kart, isAntiGravity ? "HoverStart" : "HoverFinish");
				al::startAction(kart, actionName);
				if (isAntiGravity) { al::tryEmitEffect(kart, "HoverOn", nullptr); al::showMaterial(kart, "GravityMT"); }
				else al::hideMaterial(kart, "GravityMT");
			}

			// Hover drives: tilt and wheels follow the hover state
			hoverBlend = al::lerpValue(hoverBlend, isAntiGravity ? 1.0f : 0.0f, 0.05f);
			wheelTilt = hoverBlend * 90.0f;
			wheelSpin += hoverBlend;

			// Propeller: deploys underwater only, spins during run/land
			float blend = al::lerpValue(propellerScale.x, isSubmerged ? 1.0f : 0.0f, 0.05f);
			propellerScale = {blend, blend, blend};
			propellerOffset = 25.0f * (1.0f - blend);

			float targetSpeed = isSubmerged && (al::isEqualSubString(actionName, "Run") || al::isEqualSubString(actionName, "Land")) ? 20.0f : 0.0f;
			propellerSpeed = al::lerpValue(propellerSpeed, targetSpeed, 0.1f);
			propellerSpin += propellerSpeed;

			bool isDashing = propellerSpeed > 5.0f;
			if (isDashing) { al::tryEmitEffect(kart, "PropellerSwim", nullptr); al::tryEmitEffect(kart, "PropellerSpin", nullptr); }
			else { al::tryDeleteEffect(kart, "PropellerSwim"); al::tryDeleteEffect(kart, "PropellerSpin"); }

			// Submerged: ground dust/idle effects would look wrong underwater
			if (isSubmerged) {
				al::tryDeleteEffect(kart, "IdolingL"); al::tryDeleteEffect(kart, "IdolingR");
				al::tryDeleteEffect(kart, "MotorcycleDriveL"); al::tryDeleteEffect(kart, "MotorcycleDriveR");
			}

			// World border: same edge walls Mario obeys
			kartBorder->update(al::getTrans(kart), al::getVelocity(kart), !rs::isCollidedGround(collider));
			*al::getTransPtr(kart) += kartBorder->mPullBack;

			Orig(actor);
		}
	};

    // Forces al::isInWater false for the kart, at the two sites vanilla checks it while riding
    template <uintptr_t Offset>
    struct ForceOutOfWaterInline : public mallow::hook::Inline<ForceOutOfWaterInline<Offset>> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            if (reinterpret_cast<void*>(ctx->X[19]) != isKart) return;
            ctx->X[0] = 0; // force al::isInWater's return to false, kart only
        }
    };

    // Moon: hijacks vanilla's own isInWater branch to pick its softer fall gravity (exeRideRunFall/Wheelie share one helper, exeRideRunJump has its own)
    template <uintptr_t Offset>
    struct MoonGravityInline : public mallow::hook::Inline<MoonGravityInline<Offset>> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            if (reinterpret_cast<void*>(ctx->X[19]) != isKart) return;
            if (!isHakoniwa->mInfo->mIsMoon) return;
            ctx->X[0] = 1; // force al::isInWater's return to true, kart only
        }
    };

    // Moon/water: halves every al::addVelocityY call for the kart (exeRideWaitJump, exeRideRunClash, exeFall, exeJump)
    struct AddVelocityYHook : public mallow::hook::Trampoline<AddVelocityYHook> {
        static void Callback(al::LiveActor* actor, float v) {
            if (actor == (al::LiveActor*)isKart && (isHakoniwa->mInfo->mIsMoon || isKartSubmerged(static_cast<Motorcycle*>(actor)))) v *= 0.5f;
            Orig(actor, v);
        }
    };

    // Kill on reset so the spawn toggle's isAlive() check works
    struct MotorcycleResetHook : public mallow::hook::Trampoline<MotorcycleResetHook> {
        static void Callback(Motorcycle* thisPtr) {
            bool isReset = thisPtr == isKart && al::isGreaterEqualStep(thisPtr, 60);
            Orig(thisPtr);
            if (isReset) thisPtr->kill();
        }
    };

    inline void Install() {
        InitActorSuffixHook::InstallAtSymbol("_ZN2al15initActorSuffixEPNS_9LiveActorERKNS_13ActorInitInfoEPKc");
        FindAnimInfoHook::InstallAtSymbol("_ZNK2al13AnimInfoTable12findAnimInfoEPKc");

        MotorcycleCalcAnimHook::InstallAtSymbol("_ZN10Motorcycle8calcAnimEv");
        MotorcycleMovementHook::InstallAtSymbol("_ZN10Motorcycle8movementEv");

        ForceOutOfWaterInline<0x2C92D0>::InstallAtOffset(0x2C92D0); // Motorcycle::movement, while riding
        ForceOutOfWaterInline<0x2CA110>::InstallAtOffset(0x2CA110); // sub_71002CA0E0, on dismount/fall
        MoonGravityInline<0x2CD100>::InstallAtOffset(0x2CD100); // Motorcycle::exeRideRunFall/Wheelie, gravity select
        MoonGravityInline<0x2CE128>::InstallAtOffset(0x2CE128); // Motorcycle::exeRideRunJump, gravity select

        AddVelocityYHook::InstallAtSymbol("_ZN2al12addVelocityYEPNS_9LiveActorEf");
        MotorcycleResetHook::InstallAtSymbol("_ZN10Motorcycle8exeResetEv");

        exl::patch::CodePatcher motorcycleJointCapPatcher(0x2C72A4); // Motorcycle's joint keeper bumped 6 to 20 to fit tilt + spin rotators
        motorcycleJointCapPatcher.WriteInst(0x52800281); // MOV W1, #20

        exl::patch::CodePatcher moveLimitFilterPatcher(0x2C7520); // Motorcycle's init to NOP wall boundaries
        moveLimitFilterPatcher.WriteInst(0x1F2003D5); // NOP
    }
}