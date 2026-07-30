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

            Orig(thisPtr, actorInfo, playerInfo);

            // Set Hakoniwa pointer
            isHakoniwa = thisPtr;

            #ifdef ALLOW_POWERUPS
                // Check for Super suit costume and cap
                const char* costume = GameDataFunction::getCurrentCostumeTypeName(thisPtr);
                const char* cap = GameDataFunction::getCurrentCapTypeName(thisPtr);

                isMario = isConfig()->enableMario && detectIsMario(costume, cap);
                isNoCap = (cap && al::isEqualString(cap, "MarioNoCap"));
                isFeather = (costume && al::isEqualString(costume, "MarioFeather"));
                isFire = (costume && al::isEqualString(costume, "MarioColorFire"))
                    && (cap && al::isEqualString(cap, "MarioColorFire"));
                isIce = (costume && al::isEqualString(costume, "MarioColorIce"))
                    && (cap && al::isEqualString(cap, "MarioColorIce"));
                isTanooki = (costume && al::isEqualString(costume, "MarioTanooki"))
                    && (cap && al::isEqualString(cap, "MarioTanooki"));
                isDrill = (costume && al::isEqualString(costume, "MarioDrill"))
                    && (cap && al::isEqualString(cap, "MarioDrill"));
                isMetal = (costume && al::isEqualString(costume, "MarioColorMetal"))
                    && (cap && al::isEqualString(cap, "MarioColorMetal"));
                isFly = (costume && al::isEqualString(costume, "MarioColorFly"))
                    && (cap && al::isEqualString(cap, "MarioColorFly"));
                isBrawl = (costume && al::isEqualString(costume, "MarioColorBrawl"))
                    && (cap && al::isEqualString(cap, "MarioColorBrawl"));
                isSuper = (costume && al::isEqualString(costume, "MarioColorSuper"))
                    && (cap && al::isEqualString(cap, "MarioColorSuper"));
                isKnight = (costume && al::isEqualString(costume, "MarioKnight"))
                    && (cap && al::isEqualString(cap, "MarioKnight"));
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
            auto* model  = thisPtr->mModelHolder->findModelActor("Normal");
            bool onGround = rs::isOnGround(thisPtr, thisPtr->mCollider);
            bool isMove = thisPtr->mInput->isMove();

            #ifdef ALLOW_POWERUPS
                PowerUps::executeMovement(thisPtr);
            #endif

            // Handle idle animation cycle
            CustomAnimation::updateIdleCycle(thisPtr);

            // Toggle configs
            if (al::isPadHoldL(-1) && al::isPadHoldPressLeftStick(-1)
            ) {
                bool isHack = thisPtr->mHackKeeper && thisPtr->mHackKeeper->mHackActor;
                bool isFlicker = thisPtr->mDamageKeeper && thisPtr->mDamageKeeper->mDamageInvalidCount > 0;
                if (isHack || isFlicker || rs::isActiveDemo(thisPtr)) return;
                bool changed = false;

                if (isPadTriggerGalaxySpin(-1)) { isConfig()->spinOnly = !isConfig()->spinOnly; changed = true; }
                else if (al::isPadTriggerY(-1) || al::isPadTriggerX(-1)) { isConfig()->attackButton = isConfig()->attackButton == 'Y' ? 'X' : 'Y'; changed = true; }
                else if (thisPtr->mInput->isTriggerJump()) { isConfig()->galaxySfx = !isConfig()->galaxySfx; changed = true; }
            #ifdef ALLOW_POWERUPS
                else if (al::isPadTriggerZR(-1) && onGround
                ) {
                    const char* costume = GameDataFunction::getCurrentCostumeTypeName(thisPtr);
                    const char* cap = GameDataFunction::getCurrentCapTypeName(thisPtr);
                    if (!detectIsMario(costume, cap)) return;
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
            const char* attackSensorNames[] = {"GalaxySpin", "DoubleSpin", "Punch"};
            al::HitSensor* attackSensors[3] = {
                al::getHitSensor(thisPtr, attackSensorNames[0]),
                al::getHitSensor(thisPtr, attackSensorNames[1]),
                al::getHitSensor(thisPtr, attackSensorNames[2]),
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
                    for (const char* name : attackSensorNames) al::invalidateHitSensor(thisPtr, name);
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
            if (!activeSensor && isHammer && al::isAlive(isHammer)) {
                al::HitSensor* sensorHack = al::getHitSensor(isHammer, "AttackHack");
                if (sensorHack && sensorHack->mIsValid) activeSensor = sensorHack;
            }
            // Wall bounce setup
            static int attackFrames = 0;
            if (activeSensor) attackFrames++;
            else attackFrames = 0;

            bool isHammerActive = isHammer && al::isAlive(isHammer);
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
                    auto* spinCap = *reinterpret_cast<PlayerStateSpinCap**>(reinterpret_cast<uintptr_t>(thisPtr) + 0x300);
                    al::setNerve(spinCap, getNerveAt(nrvSpinCapFall));
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

			if (wallSensor && isType(al::getSensorHost(wallSensor), "WindBlowMapParts")
			) {
				al::LiveActor* part = al::getSensorHost(wallSensor);
				al::LiveActor* pusher = isHammerWall ? static_cast<al::LiveActor*>(isHammer) : thisPtr;
				float dir = al::sign(al::getRailDir(part).dot(al::getTrans(part) - al::getTrans(pusher)));

				if (anim->isAnim("Push")) al::setSyncRailToCoord(part, al::getRailCoord(part) + dir * pushSpeed);
				else if (activeSensor) {
					float pushForce = (al::getSensorHost(activeSensor) == isHammer) ? 150.0f : 50.0f;
					rs::sendMsgByugoBlow(wallSensor, wallSensor, al::getRailDir(part) * (dir * pushForce));
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

            // Handle Mario's Carry sensor
            al::HitSensor* carrySensor = al::getHitSensor(thisPtr, "Carry");
            if (carrySensor && carrySensor->mIsValid) {
                // Check all sensors colliding with Carry sensor
                for (int i = 0; i < carrySensor->mSensorCount; i++) {
                    al::HitSensor* other = carrySensor->mSensors[i];
                    al::LiveActor* actor = al::getSensorHost(other);

                    if (!actor) continue;

                    if (al::isEqualSubString(typeid(*actor).name(), "Radish")
                        || al::isEqualSubString(typeid(*actor).name(), "BossRaidRivet")
                        || al::isEqualSubString(typeid(*actor).name(), "Stake")) { isNearCollectible = true; break;}

                    if (al::isEqualSubString(typeid(*actor).name(), "TreasureBox")
                        && !al::isModelName(actor, "TreasureBoxWood")) { isNearTreasure = true; break; }

                    if (al::isSensorEnemyBody(other)
                        && (al::isActionPlaying(actor, "SwoonStart") || al::isActionPlaying(actor, "SwoonStartLand")
                            || al::isActionPlaying(actor, "SwoonLoop") || al::isActionPlaying(actor, "Swoon"))) { isNearSwoonedEnemy = true; break; }
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
                    al::startActionSubActor(model, "顔", "SwimStand");

                if ((anim->isAnim("BattleWait") || isBrawl || isSuper) && !al::isActionPlayingSubActor(model, "顔", "WaitAngry"))
                    al::startActionSubActor(model, "顔", "WaitAngry");

                if (isMetal && !al::isActionPlayingSubActor(model, "顔", "AreaWaitFight"))
                    al::startActionSubActor(model, "顔", "AreaWaitFight");
            }

            // Handle guard attacks
            switch (guardStep) {
                case PlayerGuard::Idle: {
                    if ((isMario || isBrawl)
                        && ((al::isPadTriggerZL(-1) && al::isPadHoldZR(-1)) || (al::isPadTriggerZR(-1) && al::isPadHoldZL(-1)))) guardWindow = 5;
                    else if (guardWindow > 0) guardWindow--;
                    break;
                }
                case PlayerGuard::Blocked: {
                    anim->startUpperBodyAnim("HitGuard"); // upper body: keep falling/moving
                    if (onGround) anim->startAnim("HitGuard"); // standing: full body
                    al::tryEmitEffect(thisPtr, "HitGuard", nullptr);
                    al::tryStartSe(thisPtr, "HitGuard");
                    guardWindow = 0;
                    guardStep = PlayerGuard::Active;
                    break;
                }
                case PlayerGuard::Active: {
                    if (!anim->isUpperBodyAnim("HitGuard") && !anim->isAnim("HitGuard")) { guardStep = PlayerGuard::Idle; break; }

                    if (onGround ? anim->isAnimEnd() : anim->isUpperBodyAnimEnd()) {
                        anim->clearUpperBodyAnim();
                        al::setNerve(thisPtr, getNerveAt(onGround ? nrvHakoniwaWait : nrvHakoniwaFall));
                        guardStep = PlayerGuard::Idle;
                    }
                    break;
                }
            }

            #ifdef ALLOW_TAUNT // Handle Taunt actions
                if (!isMove
                    && (al::isNerve(thisPtr, getNerveAt(nrvHakoniwaWait)) || al::isNerve(thisPtr, getNerveAt(nrvHakoniwaSquat)))
                    && !al::isNerve(thisPtr, &TauntLeftNrv) && !al::isNerve(thisPtr, &TauntRightNrv)
                    && !isActionBusy()
                ) {
                    if (al::isPadTriggerLeft(-1)) al::setNerve(thisPtr, &TauntLeftNrv);
                    else if (al::isPadTriggerRight(-1)) al::setNerve(thisPtr, &TauntRightNrv);
                }
                if (al::isNerve(thisPtr, &TauntLeftNrv)
                ) {
                    if (anim->isAnim("WearEnd")) { al::tryStopSe(thisPtr, "WearEnd", -1, nullptr); al::tryStopSe(thisPtr, "WearEndSetCostume", -1, nullptr); }
                }
                if (!al::isNerve(thisPtr, &TauntLeftNrv)
                    && !al::isNerve(thisPtr, &TauntRightNrv)) al::tryDeleteEffect(model, "BonfireSuper");
            #endif
        }
    };

    struct PlayerActorHakoniwaReceiveMsgHook : public mallow::hook::Trampoline<PlayerActorHakoniwaReceiveMsgHook> {
        static bool Callback(PlayerActorHakoniwa* thisPtr, const al::SensorMsg* msg, al::HitSensor* source, al::HitSensor* target) {
            if (guardStep != PlayerGuard::Idle
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
                if ((al::isEqualSubString(anim->mCurAnim, "Punch")  && frame <= 6.0f) || (al::isEqualSubString(anim->mCurAnim, "JumpPunch") && frame <= 17.0f)) return false;

                // Parry - only on a hit that would otherwise land
                if (source && guardWindow > 0 && thisPtr->mDamageKeeper->mDamageInvalidCount <= 0) {
                    al::LiveActor* attacker = al::getSensorHost(source);

                    guardStep = PlayerGuard::Blocked;
                    al::setNerve(thisPtr, getNerveAt(nrvHakoniwaFall));
                    al::setVelocityBlowAttackAndTurnToTarget(thisPtr, al::getTrans(attacker), 10.0f, 10.0f); // push Mario
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
            if (!anim->isSubAnimEnd()
                && (guardStep != PlayerGuard::Idle || isDrillAnim(anim))) return;
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

    struct PlayerJudgeStartSquatHook : public mallow::hook::Trampoline<PlayerJudgeStartSquatHook> {
        static bool Callback(void* thisPtr) {
            if (guardStep != PlayerGuard::Idle
                || (isDrill && isHakoniwa->mHackCap->isPutOn() && al::isPadHoldZR(-1))) return false;
            return Orig(thisPtr);
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
        // Others
        PlayerJudgeStartSquatHook::InstallAtSymbol("_ZNK21PlayerJudgeStartSquat5judgeEv");
    }
}