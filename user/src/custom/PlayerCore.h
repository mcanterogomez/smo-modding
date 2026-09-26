#pragma once
#include "ModConfig.h"
#include "custom/.Globals.h"
#include "custom/.Nerves.h"
#include "custom/CustomAnimation.h"
#include "custom/PowerUps.h"
#include "custom/PlayerFreeze.h"

inline bool detectIsMario(const char* costume, const char* cap) {
    return (costume && al::isEqualString(costume, "Mario"))
        && (cap && al::isEqualString(cap, "Mario"));
}

namespace PlayerCore {

    struct PlayerActorHakoniwaInitPlayer : public mallow::hook::Trampoline<PlayerActorHakoniwaInitPlayer> {
        static void Callback(PlayerActorHakoniwa* thisPtr, const al::ActorInitInfo* actorInfo, const PlayerInitInfo* playerInfo) {
            isHakoniwa = nullptr;
            isKoopa = nullptr;
            isNearTarget = nullptr;
            battleStance = 0;
            isSneaking = false;

            Orig(thisPtr, actorInfo, playerInfo);

            // Set Hakoniwa pointer
            isHakoniwa = thisPtr;
            isMarioModel = thisPtr->mModelHolder->findModelActor("Normal");

            #ifdef ALLOW_POWERUPS
                // Check for Super suit costume and cap
                const char* costume = GameDataFunction::getCurrentCostumeTypeName(thisPtr);
                const char* cap = GameDataFunction::getCurrentCapTypeName(thisPtr);

                isNoCap = cap && al::isEqualString(cap, "MarioNoCap");
                isFeather = costume && al::isEqualString(costume, "MarioFeather");

                auto isSuit = [&](const char* name) {
                    return costume && cap && al::isEqualString(costume, name) && al::isEqualString(cap, name);
                };

                isClassic = isSuit("MarioColorClassic");
                isFire = isSuit("MarioColorFire");
                isIce = isSuit("MarioColorIce");
                isTanooki = isSuit("MarioTanooki");
                isDrill = isSuit("MarioDrill");
                isMetal = isSuit("MarioColorMetal");
                isFly = isSuit("MarioColorFly");
                isBrawl = isSuit("MarioColorBrawl");
                isSuper = isSuit("MarioColorSuper");
                isKnight = isSuit("MarioKnight");
                isMario = isConfig()->enableMario && detectIsMario(costume, cap);

                // Set Cap sounds
                if ((isMetal || isKnight) && thisPtr->mHackCap) al::setSeKeeperPlayNamePrefix(thisPtr->mHackCap, "Iron");

                PowerUps::executeInitPlayer(thisPtr, actorInfo, playerInfo);
            #endif
        }
    };

    struct PlayerActorHakoniwaInitAfterPlacement : public mallow::hook::Trampoline<PlayerActorHakoniwaInitAfterPlacement> {
        static void Callback(PlayerActorHakoniwa* thisPtr) {
            Orig(thisPtr);
            PowerUps::executeInitAfterPlacement();
        }
    };

    struct PlayerMovementHook : public mallow::hook::Trampoline<PlayerMovementHook> {
        static void Callback(PlayerActorHakoniwa* thisPtr) {
            Orig(thisPtr);

            auto* anim = thisPtr->mAnimator;
            auto* model = thisPtr->mModelHolder->findModelActor("Normal");
            bool onGround = rs::isOnGround(thisPtr, thisPtr->mCollider);
            bool isMove = thisPtr->mInput->isMove();

            #ifdef ALLOW_POWERUPS
                PowerUps::executeMovement(thisPtr);
            #endif

            // Handle battle stance and idle cycle
            CustomAnimation::update(thisPtr);

            // Toggle configs, not during captures, i-frames or cutscenes
            bool isFlicker = thisPtr->mDamageKeeper && thisPtr->mDamageKeeper->mDamageInvalidCount > 0;
            if (al::isPadHoldL(-1) && al::isPadHoldPressLeftStick(-1) && !isHacking() && !isFlicker && !rs::isActiveDemo(thisPtr)
            ) {
                bool changed = false;

                if (isPadTriggerGalaxySpin(-1)) { isConfig()->spinOnly = !isConfig()->spinOnly; changed = true; }
                else if (al::isPadTriggerY(-1) || al::isPadTriggerX(-1)) { isConfig()->attackButton = isConfig()->attackButton == 'Y' ? 'X' : 'Y'; changed = true; }
                else if (thisPtr->mInput->isTriggerJump()) { isConfig()->galaxySfx = !isConfig()->galaxySfx; changed = true; }
            #ifdef ALLOW_POWERUPS
                else if (al::isPadTriggerZR(-1) && onGround
                    && detectIsMario(GameDataFunction::getCurrentCostumeTypeName(thisPtr), GameDataFunction::getCurrentCapTypeName(thisPtr))
                ) {
                    isMario = !isMario;
                    isConfig()->enableMario = isMario;
                    isMarioActive = isMario ? 1 : -1;

                    al::setNerve(thisPtr, &TauntRightNrv);
                    if (!isMario) { thisPtr->mDamageKeeper->invalidate(60); al::tryStartSe(thisPtr, "EndInvincible"); }
                    mallow::config::saveConfig();
                }
            #endif
                if (changed) { mallow::config::saveConfig(); al::tryStartSe(thisPtr, "DemoReturnHomeJingle"); }
            }

            // Spin-type sensors all attack wall and ceiling contacts
            al::HitSensor* attackSensors[3] = {
                al::getHitSensor(thisPtr, "GalaxySpin"),
                al::getHitSensor(thisPtr, "DoubleSpin"),
                al::getHitSensor(thisPtr, "Punch"),
            };
            for (auto* sensor : attackSensors) {
                if (sensor && sensor->mIsValid) {
                    thisPtr->attackSensor(sensor, rs::tryGetCollidedCeilingSensor(thisPtr->mCollider));
                    thisPtr->attackSensor(sensor, rs::tryGetCollidedWallSensor(thisPtr->mCollider));
                }
            }

            al::HitSensor* sensorHipDrop = al::getHitSensor(thisPtr, "HipDropKnockDown");
            if (sensorHipDrop && sensorHipDrop->mIsValid) thisPtr->attackSensor(sensorHipDrop, rs::tryGetCollidedGroundSensor(thisPtr->mCollider));

            // Handle sensor invalidation after timer expires
            if (attackSensorRemaining > 0) {
                attackSensorRemaining--;

                bool animEnded = anim->isAnimEnd();
                if (attackSensorRemaining == 0 || animEnded
                ) {
                    for (auto* sensor : attackSensors) if (sensor) sensor->invalidate();
                    spin.isGalaxy = false;
                    attackSensorRemaining = -1;
                }
            }

            // Handle wall bounce for attacks
            al::HitSensor* activeSensor = nullptr;
            for (auto* sensor : attackSensors) {
                if (sensor && sensor->mIsValid) { activeSensor = sensor; break; }
            }
            // Handle wall bounce for hammer
            bool isHammerActive = isHammer && al::isAlive(isHammer);
            if (!activeSensor && isHammerActive) {
                al::HitSensor* sensorHack = al::getHitSensor(isHammer, "AttackHack");
                if (sensorHack && sensorHack->mIsValid) activeSensor = sensorHack;
            }
            // Wall bounce setup
            static int attackFrames = 0;
            if (activeSensor) attackFrames++;
            else attackFrames = 0;

            bool isHammerWall = isHammerActive && al::isCollidedWall(isHammer);
            bool wallHit = rs::isCollidedWall(thisPtr->mCollider) || isHammerWall;

            if (activeSensor && attackFrames >= 2
                && wallHit && hitBufferCount == 0
                && !isDrillAnim(anim)
            ) {
                sead::Vector3f wallPos = isHammerWall ? al::getCollidedWallPos(isHammer) : rs::getCollidedWallPos(thisPtr->mCollider);

                if (isHammerActive) {
                    al::tryEmitEffect(isHammer, "Break", &wallPos);
                    al::tryStartSe(isHammer, "Hit");
                    al::setNerve(thisPtr, getNerveAt(nrvHakoniwaFall));
                } else {
                    al::setNerve(thisPtr->mStateSpinCap, getNerveAt(nrvSpinCapFall));
                }

                al::tryEmitEffect(thisPtr, "HitSmall", &wallPos);
                al::tryStartSe(thisPtr, "HitImpact");
                al::setVelocityBlowAttack(thisPtr, wallPos, isMove ? 15.0f : 5.0f, 10.0f);
                attackFrames = 0;
            }

            // Push wind-blow map parts along their rail, away from whatever hit them
            constexpr float pushSpeed = 1.0f; // rail units/frame while pushing
            static bool wasPushing = false;
            al::HitSensor* wallSensor = isHammerWall ? al::tryGetCollidedWallSensor(isHammer) : rs::tryGetCollidedWallSensor(thisPtr->mCollider);
            al::LiveActor* part = wallSensor ? al::getSensorHost(wallSensor) : nullptr;

            if (part && isType(part, "WindBlowMapParts")) {
                al::LiveActor* pusher = isHammerWall ? static_cast<al::LiveActor*>(isHammer) : thisPtr;
                const sead::Vector3f& railDir = al::getRailDir(part);
                float dir = al::sign(railDir.dot(al::getTrans(part) - al::getTrans(pusher)));

                if (anim->isAnim("Push")) al::setSyncRailToCoord(part, al::getRailCoord(part) + dir * pushSpeed);
                else if (activeSensor) {
                    float pushForce = (al::getSensorHost(activeSensor) == isHammer) ? 150.0f : 50.0f;
                    rs::sendMsgByugoBlow(wallSensor, wallSensor, railDir * (dir * pushForce));
                    if (!wasPushing) {
                        sead::Vector3f hitPos = isHammerWall ? al::getCollidedWallPos(isHammer) : rs::getCollidedWallPos(thisPtr->mCollider);
                        al::tryEmitEffect(thisPtr, "HitSmall", &hitPos);
                        al::tryStartSe(thisPtr, "HitImpact");
                        wasPushing = true;
                    }
                }
            }
            if (!activeSensor) wasPushing = false;

            // Reset proximity flag
            isNearCollectible = false;
            isNearTreasure = false;
            isNearSwoonedEnemy = false;
            isNearTarget = nullptr; // the kick homes onto whatever the scan below matched

            // Handle Mario's Carry sensor
            al::HitSensor* carrySensor = al::getHitSensor(thisPtr, "Carry");
            if (carrySensor && carrySensor->mIsValid) {
                // Check all sensors colliding with Carry sensor
                for (int i = 0; i < carrySensor->mSensorCount; i++) {
                    al::HitSensor* other = carrySensor->mSensors[i];
                    al::LiveActor* actor = al::getSensorHost(other);

                    if (!actor) continue;

                    if (isAnyType(actor, "Radish", "BossRaidRivet", "Stake")) { isNearCollectible = true; isNearTarget = actor; break; }
                    if (isType(actor, "TreasureBox") && !al::isModelName(actor, "TreasureBoxWood")) { isNearTreasure = true; isNearTarget = actor; break; }

                    if (al::isSensorEnemyBody(other)
                        && (al::isActionPlaying(actor, "SwoonStart") || al::isActionPlaying(actor, "SwoonStartLand")
                            || al::isActionPlaying(actor, "SwoonLoop") || al::isActionPlaying(actor, "Swoon"))) { isNearSwoonedEnemy = true; isNearTarget = actor; break; }
                }
            }

            // Add attack to hipdrop
            static bool wasAttackMove = false;
            updateAttackSensor(thisPtr, "HipDropKnockDown", isHipDropAnim(anim), wasAttackMove);

            // Change face animations
            al::LiveActor* face = al::tryGetSubActor(model, "顔");
            if (face) {
                bool isWater = !thisPtr->mWaterSurfaceFinder->isFoundSurface() && al::isInWater(thisPtr);
                const char* actionName = al::getActionName(face);

                if (isWater && (!al::isEqualSubString(actionName, "Swim") || al::isEqualSubString(actionName, "Spin")))
                    al::startAction(face, "SwimStand");

                bool tauntSmash = al::isActionPlaying(model, "TauntSmash") || al::isActionPlaying(model, "TauntSmash01");

                // Metal and the battle stance fight, Brawl and Super glare the rest of the time
                const char* faceAnim = nullptr;
                if (isMetal || anim->isAnim("BattleWait")) faceAnim = "AreaWaitFight01";
                else if ((isBrawl && !tauntSmash) || isSuper) faceAnim = "WaitAngry";

                if (faceAnim && !al::isActionPlaying(face, faceAnim)) al::startAction(face, faceAnim);
            }

            // Handle guard window
            static bool wasGuardHold = false;
            bool isGuardHold = al::isPadHoldZL(-1) && al::isPadHoldZR(-1);

            if ((isMario || isBrawl) && isGuardHold && !wasGuardHold) guardWindow = 10;
            else if (guardWindow > 0) guardWindow--;
            wasGuardHold = isGuardHold;

            // Handle Taunt actions
            #ifdef ALLOW_TAUNT
                if ((al::isNerve(thisPtr, getNerveAt(nrvHakoniwaWait)) || al::isNerve(thisPtr, getNerveAt(nrvHakoniwaSquat)))
                    && !isActionBusy()
                ) {
                    if (al::isPadTriggerLeft(-1)) al::setNerve(thisPtr, &TauntLeftNrv);
                    else if (al::isPadTriggerRight(-1)) al::setNerve(thisPtr, &TauntRightNrv);
                }
                if (al::isNerve(thisPtr, &TauntLeftNrv)
                    && anim->isAnim("WearEnd")) { al::tryStopSe(thisPtr, "WearEnd", -1, nullptr); al::tryStopSe(thisPtr, "WearEndSetCostume", -1, nullptr); }
            #endif
        }
    };

    struct PlayerActorHakoniwaReceiveMsgHook : public mallow::hook::Trampoline<PlayerActorHakoniwaReceiveMsgHook> {
        static bool Callback(PlayerActorHakoniwa* thisPtr, const al::SensorMsg* msg, al::HitSensor* source, al::HitSensor* target) {
            if (isGuarding()
                || drillStep != PlayerDrill::Idle || drillSensorRemaining > 0
                || PlayerFreeze::handleReceiveMsg(msg, source)) return false;

            bool isDamage = rs::isMsgPlayerDamage(msg) || al::isMsgHit(msg)
                || al::isMsgHitStrong(msg) || al::isMsgHitVeryStrong(msg)
                || rs::isMsgPlayerDamageBlowDown(msg) || al::isMsgExplosion(msg);

            if (isDamage) {
                auto* anim = thisPtr->mAnimator;

                // Already immune - the hit never lands
                if (isMetal || isSuper || isHipDropAnim(anim)) return false;
                if (source && al::isEqualString(al::getSensorHost(source)->getName(), "MarioTankBullet")) return false;

                float frame = anim->getAnimFrame();
                if ((al::isEqualSubString(anim->mCurAnim, "Punch")  && frame <= 6.0f)
                    || (al::isEqualSubString(anim->mCurAnim, "JumpPunch") && frame <= 17.0f)
                    || (isSwingAnim(anim) && frame <= 4.0f)) return false;

                // Parry - only on a hit that would otherwise land
                if (source && guardWindow > 0 && thisPtr->mDamageKeeper->mDamageInvalidCount <= 0) {
                    al::LiveActor* attacker = al::getSensorHost(source);

                    al::setNerve(thisPtr, &GuardNrv);
                    al::setVelocityBlowAttackAndTurnToTarget(thisPtr, al::getTrans(attacker), 5.0f, 5.0f); // push Mario
                    if (al::isExistAction(attacker, "BlowDown")) al::setVelocityBlowAttack(attacker, al::getTrans(thisPtr), 10.0f, 10.0f); // push attacker
                    return false;
                }
            }
            return Orig(thisPtr, msg, source, target);
        }
    };

    // Protect sub-anims from being killed by game code
    struct EndSubAnimGuard : public mallow::hook::Trampoline<EndSubAnimGuard> {
        static void Callback(PlayerAnimator* anim) {
            if (!anim->isSubAnimEnd() && isDrillAnim(anim)) return;
            Orig(anim);
        }
    };

    struct EmitEmittersHook : public mallow::hook::Trampoline<EmitEmittersHook> {
        static bool Callback(al::Effect* effect, const sead::Vector3f* pos, bool useCurrentPos) {
            const char* name = *reinterpret_cast<const char**>(effect);
            if (al::isEqualSubString(name, "Hit")) isEffect = true;

            if (isConfig()->galaxySfx && al::isEqualString(name, "SpinCapStart2Right")
                && isHakoniwa && al::isEqualSubString(isHakoniwa->mAnimator->mCurAnim, "SpinSeparate")) return false;
            return Orig(effect, pos, useCurrentPos);
        }
    };

    struct EffectHitReactionLimitHook : public mallow::hook::Inline<EffectHitReactionLimitHook> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            static u8 sNodes[32 * 0xE8] = {};
            static void* sBuf[64] = {};

            for (int i = 0; i < 31; i++)
                *(void**)(sNodes + i * 0xE8) = sNodes + (i + 1) * 0xE8;

            *(void**)(ctx->X[2] - 0xE8) = sNodes;
            ctx->X[1] = 0x40;
            ctx->X[2] = (u64)sBuf;
        }
    };

    inline void Install() {
        // Initialize player actor
        PlayerActorHakoniwaInitPlayer::InstallAtSymbol("_ZN19PlayerActorHakoniwa10initPlayerERKN2al13ActorInitInfoERK14PlayerInitInfo");
        PlayerActorHakoniwaInitAfterPlacement::InstallAtSymbol("_ZN19PlayerActorHakoniwa18initAfterPlacementEv");
        // Handles control/movement
        PlayerMovementHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa8movementEv");
        PlayerActorHakoniwaReceiveMsgHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa10receiveMsgEPKN2al9SensorMsgEPNS0_9HitSensorES5_");
        EndSubAnimGuard::InstallAtSymbol("_ZN14PlayerAnimator10endSubAnimEv");
        // Handles effects
        EmitEmittersHook::InstallAtSymbol("_ZN2al6Effect15tryEmitEmittersEPKN4sead7Vector3IfEEb");
        EffectHitReactionLimitHook::InstallAtOffset(0xA5B938);
    }
}