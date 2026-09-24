#pragma once
#include "custom/.Globals.h"
#include "custom/.Nerves.h"
#include "custom/CustomSwoon.h"
#include "custom/PlayerFreeze.h"
#include "headers/PlayerIceCube.h"

// Check if HitImpact should play for this target/sensor pair
inline bool isHitImpact(const al::LiveActor* targetHost, const al::HitSensor* target) {
	if (isAnyType(targetHost, "CapRack")) return true;
	if (isAnyType(targetHost, "Gunetter")) return false;
	if (hasSensor(targetHost, al::isSensorMapObj)) return hasSensor(targetHost, al::isSensorCollision) && !hasSensor(targetHost, al::isSensorEnemyAttack);
	al::HitSensor* body = al::getHitSensor(targetHost, "Body");
	if (al::isSensorEnemy(target) && body) return target->getRadius() <= body->getRadius();
	return true;
}

namespace AttackSensor {

    struct HackCapAttackSensorHook : public mallow::hook::Trampoline<HackCapAttackSensorHook> {
        static void Callback(PlayerActorHakoniwa* thisPtr, al::HitSensor* source, al::HitSensor* target) {
            if (!thisPtr || !source || !target) return;
            if (!isValidAttackTarget(target)) return;

            al::LiveActor* targetHost = al::getSensorHost(target);
            if (isType(targetHost, "KoopaCap", "KoopaCap")) return;

            Orig(thisPtr, source, target);
        }
    };

    struct PlayerAttackSensorHook : public mallow::hook::Trampoline<PlayerAttackSensorHook> {
        static void Callback(PlayerActorHakoniwa* thisPtr, al::HitSensor* source, al::HitSensor* target) {
            if (!thisPtr || !source || !target) return;

            al::LiveActor* targetHost = al::getSensorHost(target);
            bool isAttackSensor = al::isSensorName(source, "GalaxySpin") || al::isSensorName(source, "DoubleSpin") || al::isSensorName(source, "Punch") || al::isSensorName(source, "HipDropKnockDown");

            if (!isAttackSensor || isInHitBuffer(targetHost)) { Orig(thisPtr, source, target); return; }
            if (!isValidAttackTarget(target) || al::isSensorName(target, "Brake") || isType(targetHost, "KoopaCap", "KoopaCap")
                || (isType(targetHost, "FireBall") && al::calcSpeedH(thisPtr) >= thisPtr->mConst->getDashFastBorderSpeed())) return;

            setupHitEffect(source, target);
            sead::Vector3f fireDir = getFireDir(thisPtr, targetHost);

            bool isSpinAttack = al::isSensorName(source, "GalaxySpin")
                && (isBaseSpinAnim(thisPtr->mAnimator)
                    || isJumpPunchAnim(thisPtr->mAnimator)
                    || isDrillAnim(thisPtr->mAnimator) // Allow drill attacks
                    || al::isActionPlaying(thisPtr->mModelHolder->findModelActor("Normal"), "MoveSuper")
                    || al::isEqualString(thisPtr->mAnimator->mCurAnim, "JumpBroad8") || al::isEqualString(thisPtr->mAnimator->mCurAnim, "Glide"));

            bool isDoubleSpinAttack = al::isSensorName(source, "DoubleSpin")
                && isDoubleSpinAnim(thisPtr->mAnimator);

            bool isSpinFallback = spin.isGalaxy
                && (al::isSensorName(source, "GalaxySpin") || al::isSensorName(source, "DoubleSpin"));

            bool isPunchAttack = al::isSensorName(source, "Punch")
                && isPunchAnim(thisPtr->mAnimator);

            bool isHipDrop = al::isSensorName(source, "HipDropKnockDown")
                && isHipDropAnim(thisPtr->mAnimator);

            al::HitSensor* foot = al::getHitSensor(thisPtr, "Foot");
            bool canTrample = rs::isEnableSendTrampleMsg(thisPtr, foot, target);
            bool isHipDropAttack = isHipDrop && !canTrample;

            if (isSpinAttack || isDoubleSpinAttack || isPunchAttack
                || isHipDropAttack || isSpinFallback
            ) {
                if (!isPunchAttack) {
                    if (isAnyType(targetHost, "BlockQuestion", "BlockBrick", "BossForestBlock")) {
                        if (!hitBufferCount) hitBuffer[hitBufferCount++] = nullptr; // Prevent wall bounce, keep hittable, and one slot is enough
                        rs::sendMsgHammerBrosHammerHackAttack(target, source);
                        return;
                    }
                    rs::sendMsgPaint(target, source, paintClear, 150, 0);
                }
                // Handle ice cubes
                if (isType(targetHost, "PlayerIceCube")) {
                    hitBuffer[hitBufferCount++] = targetHost; // Prevent wall bounce
                    ((PlayerIceCube*)targetHost)->markHit(source);
                    return;
                }
                if (isType(targetHost, "Koopa", "KoopaBig")) { KoopaBattle::attack(thisPtr, source, target); return; }

                const al::Nerve* sourceNrv = targetHost->getNerveKeeper() ? targetHost->getNerveKeeper()->getCurrentNerve() : nullptr;
                bool isStake = isType(targetHost, "Stake") && sourceNrv == getNerveAt(0x1D36D20);
                bool isRadish = isType(targetHost, "Radish") && sourceNrv == getNerveAt(0x1D22B70);
                bool isRivet = isType(targetHost, "BossRaidRivet") && sourceNrv == getNerveAt(0x1C5F330);
                bool isSwitch = isType(targetHost, "CapSwitch");
                bool isTimer = isType(targetHost, "CapSwitchTimer");

                if (isStake || isRadish || isRivet) {
                    hitBuffer[hitBufferCount++] = targetHost;
                    if (targetHost == isNearTarget && thisPtr->mAnimator->isAnim("RabbitGet")
                    ) {
                        if (isRivet) { al::invalidateCollisionParts(targetHost); al::setVelocityBlowAttack(targetHost, al::getTrans(thisPtr), 0.0f, 44.0f); }
                        al::setNerve(targetHost, isStake ? getNerveAt(0x1D36D30) : isRadish ? getNerveAt(0x1D22BD8) : getNerveAt(0x1C5F338));
                        isHitEffect(thisPtr, targetHost);
                        isNearCollectible = false;
                        isNearTarget = nullptr;
                    }
                    return;
                }
                if (isSwitch || isTimer) {
                    if (isTimer) al::invalidateClipping(targetHost);
                    al::setNerve(targetHost, isSwitch ? getNerveAt(0x1CE3E18) : getNerveAt(0x1CE4338));
                    hitBuffer[hitBufferCount++] = targetHost;
                    isHitEffect(thisPtr, targetHost);
                    return;
                }
                if (thisPtr->mAnimator->isAnim("SpinLow")
                    && (trySwoon(targetHost, false) || isAnyType(targetHost, "Ball", "Bomb", "Togezo", "!Damage"))
                ) {
                    bool isHit = trySendCapMsg(targetHost, source);
                    if (isHit || isType(targetHost, "FireBall") || trySwoon(targetHost, false)
                    ) {
                        if (trySwoon(targetHost, false)) trySwoon(targetHost);
                        handleStacked(targetHost, target, source);
                        if (!isHit && al::isCollidedGround(targetHost)
                            && al::isExistAction(targetHost, "Walk")) al::setVelocityBlowAttack(targetHost, al::getTrans(thisPtr), 12.5f, 25.0f);
                        hitBuffer[hitBufferCount++] = targetHost;
                        isHitEffect(thisPtr, targetHost);
                        return;
                    }
                }
                bool isBlock = isAnyType(targetHost, "BlockHard", "Marching");
                if (isBlock || isAnyType(targetHost, "Ball", "Board", "Bomb", "Break", "Bull", "Church", "Golem", "KickStone", "Moon", "Souvenir", "TreasureBox")
                ) {
                    if ((!isBlock || al::isSensorCollision(target))
                        && (rs::sendMsgHammerBrosHammerHackAttack(target, source) || al::sendMsgExplosion(target, source, nullptr)
                            || rs::sendMsgBullHackAttack(target, source) || rs::sendMsgTsukkunThrust(target, source, fireDir, 0, true)
                            || al::sendMsgPlayerObjHipDrop(target, source, nullptr) || rs::sendMsgCapTouchWall(target, source, sead::Vector3f{0,0,0}, sead::Vector3f{0,0,0})
                            || rs::sendMsgKoopaCapPunchL(target, source) || rs::sendMsgKoopaHackPunchCollide(target, source))
                    ) {
                        hitBuffer[hitBufferCount++] = targetHost;
                        isHitEffect(thisPtr, targetHost);
                        return;
                    } else if (isBlock) return;
                }
                if (rs::sendMsgHackAttack(target, source) || al::sendMsgPlayerSpinAttack(target, source, nullptr)
                    || rs::sendMsgCapReflect(target, source) || rs::sendMsgCapReflectCollide(target, source)
                    || al::sendMsgPlayerObjHipDropReflect(target, source, nullptr)
                    || rs::sendMsgCapAttack(target, source) || rs::sendMsgCapAttackCollide(target, source)
                    || rs::sendMsgByugoBlow(target, source, sead::Vector3f::zero)
                ) {
                    hitBuffer[hitBufferCount++] = targetHost;
                    if (isHitImpact(targetHost, target)) al::tryStartSe(thisPtr, "HitImpact");
                    if (al::isExistAction(targetHost, "BlowDown")) {
                        bool isJumpPunch = isJumpPunchAnim(thisPtr->mAnimator);
                        al::setVelocityBlowAttack(targetHost, al::getTrans(thisPtr), isJumpPunch ? 25.0f : 50.0f, isJumpPunch ? 50.0f : 25.0f);
                    }
                    return;
                }
            }
            Orig(thisPtr, source, target);
        }
    };

    struct HammerAttackSensorHook : public mallow::hook::Trampoline<HammerAttackSensorHook> {
        static void Callback(HammerBrosHammer* thisPtr, al::HitSensor* source, al::HitSensor* target) {
            if (!thisPtr || !source || !target) return;

            al::LiveActor* targetHost = al::getSensorHost(target);
            bool isAttackSensor = al::isNerve(isHakoniwa, &HammerNrv) && al::isSensorName(source, "AttackHack");

            if (!isAttackSensor || isInHitBuffer(targetHost)) { Orig(thisPtr, source, target); return; }
            if (!isValidAttackTarget(target) || al::isSensorName(target, "Brake") || isType(targetHost, "KoopaCap", "KoopaCap")) return;

            setupHitEffect(source, target);
            sead::Vector3f fireDir = getFireDir(thisPtr, targetHost);

            rs::sendMsgPaint(target, source, paintClear, 300, 0);

            bool isBlock = isAnyType(targetHost, "BlockHard", "Marching");
            if (isBlock || isAnyType(targetHost, "Ball", "Board", "Bomb", "Break", "Cactus", "Church", "Golem", "Koopa", "KickStone", "Moon", "TreasureBox", "TRex", "Wanwan")
            ) {
                if ((!isBlock || al::isSensorCollision(target))
                    && (rs::sendMsgSeedAttackBig(target, source) || rs::sendMsgWanwanReboundAttack(target, source)
                        || rs::sendMsgTRexAttack(target, source) || rs::sendMsgTsukkunThrust(target, source, fireDir, 0, true)
                        || rs::sendMsgHammerBrosHammerHackAttack(target, source) || al::sendMsgExplosion(target, source, nullptr)
                        || rs::sendMsgSphinxRideAttackTouchThrough(target, source, fireDir, fireDir) || rs::sendMsgStatueDrop(target, source)
                        || rs::sendMsgCapTouchWall(target, source, sead::Vector3f{0,0,0}, sead::Vector3f{0,0,0})
                        || rs::sendMsgKoopaCapPunchFinishL(target, source) || rs::sendMsgKoopaCapPunchL(target, source)
                        || rs::sendMsgKoopaHackPunchCollide(target, source))
                ) {
                    hitBuffer[hitBufferCount++] = targetHost;
                    isHitEffect(thisPtr, targetHost);
                    return;
                } else if (isBlock) return;
            }
            if (rs::sendMsgHackAttack(target, source) || al::sendMsgPlayerSpinAttack(target, source, nullptr)
                || al::sendMsgPlayerHipDrop(target, source, nullptr) || al::sendMsgPlayerObjHipDrop(target, source, nullptr)
                || al::sendMsgPlayerObjHipDropReflect(target, source, nullptr) || rs::sendMsgPlayerHipDropHipDropSwitch(target, source)
                || rs::sendMsgCapReflect(target, source) || rs::sendMsgCapReflectCollide(target, source)
                || rs::sendMsgCapAttack(target, source) || rs::sendMsgCapAttackCollide(target, source)
                || rs::sendMsgByugoBlow(target, source, sead::Vector3f::zero)
            ) {
                hitBuffer[hitBufferCount++] = targetHost;
                return;
            }
            Orig(thisPtr, source, target);
        }
    };

    struct FireballAttackSensorHook : public mallow::hook::Trampoline<FireballAttackSensorHook> {
        static void Callback(FireBrosFireBall* thisPtr, al::HitSensor* source, al::HitSensor* target) {
            if (!thisPtr || !source || !target) return;

            al::LiveActor* targetHost = al::getSensorHost(target);
            bool isAttackSensor = al::isSensorName(source, "AttackHack") && al::isEqualString(thisPtr->getName(), "MarioIceBall");

            if (!isAttackSensor || isInHitBuffer(targetHost)) { Orig(thisPtr, source, target); return; }
            if (!isValidAttackTarget(target) || al::isEqualString(targetHost->getName(), "MarioIceBall")) return;

            sead::Vector3f sourcePos = al::getSensorPos(source);

            if (isType(targetHost, "PlayerIceCube")
            ) {
                ((PlayerIceCube*)targetHost)->markHit(source);
                al::tryEmitEffect(thisPtr, "Disappear", &sourcePos);
                thisPtr->kill();
                return;
            }
            if (isAnyType(targetHost, "FireSwitch", "Candlestand")
            ) {
                if (rs::sendMsgByugoBlow(target, source, sead::Vector3f::zero)) {
                    al::tryEmitEffect(thisPtr, "Disappear", &sourcePos);
                    thisPtr->kill();
                }
                return;
            }
            if (!al::isHideModel(targetHost) && !isAnyType(targetHost, "Boss", "Breeda", "Koopa")
                && (al::isSensorEnemyBody(target) || isType(targetHost, "Rabbit"))
            ) {
                handleStacked(targetHost, target, source);
                hitBuffer[hitBufferCount++] = targetHost;
                PlayerFreeze::freezeActor(targetHost, 1800);
                al::tryEmitEffect(thisPtr, "Disappear", &sourcePos);
                thisPtr->kill();
                return;
            }
            Orig(thisPtr, source, target);
        }
    };

    struct FireballAttackSensorInline : public mallow::hook::Inline<FireballAttackSensorInline> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            auto* fireball = reinterpret_cast<FireBrosFireBall*>(ctx->X[19]);
            auto* source = reinterpret_cast<al::HitSensor*>(ctx->X[20]);
            auto* target = reinterpret_cast<al::HitSensor*>(ctx->X[21]);

            al::LiveActor* targetHost = al::getSensorHost(target);
            if (!isValidAttackTarget(target) || isType(targetHost, "KoopaCap", "KoopaCap")            
                || (!al::isEqualString(fireball->getName(), "MarioFireBall")
                    && !al::isEqualString(fireball->getName(), "MarioIceBall"))) return;

            setupHitEffect(source, target);

            if (!ctx->W[0] && (rs::sendMsgHackAttack(target, source) || al::sendMsgExplosion(target, source, nullptr)
                || al::sendMsgKickStoneAttackReflect(target, source) || rs::sendMsgBullHackAttack(target, source)
                || rs::sendMsgKoopaCapPunchL(target, source))
            ) {
                ctx->W[0] = true;
                isHitEffect(fireball, targetHost);
            }
        }
    };

    struct TankBulletAttackSensorInline : public mallow::hook::Inline<TankBulletAttackSensorInline> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            auto* bullet = reinterpret_cast<TankBullet*>(ctx->X[19]);
            auto* source = reinterpret_cast<al::HitSensor*>(ctx->X[22]);
            auto* target = reinterpret_cast<al::HitSensor*>(ctx->X[21]);
            
            al::LiveActor* targetHost = al::getSensorHost(target);
            if (!isValidAttackTarget(target) || isType(targetHost, "KoopaCap", "KoopaCap")            
                || !al::isEqualString(bullet->getName(), "MarioTankBullet")) return;

            rs::sendMsgSeedAttackBig(target, source);

            ctx->W[0] = ctx->W[0]
                || al::sendMsgPlayerFireBallAttack(target, source) || rs::sendMsgHackAttack(target, source)
                || al::sendMsgKickStoneAttackReflect(target, source) || rs::sendMsgBullHackAttack(target, source)
                || rs::sendMsgKoopaCapPunchL(target, source) || rs::sendMsgKoopaHackPunch(target, source);

            rs::sendMsgWeaponItemGet(target, source);
        }
    };

    inline void Install() {
        #ifndef ALLOW_CAPPY_ONLY
            HackCapAttackSensorHook::InstallAtSymbol("_ZN7HackCap12attackSensorEPN2al9HitSensorES2_");
            PlayerAttackSensorHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa12attackSensorEPN2al9HitSensorES2_");
        #endif
        HammerAttackSensorHook::InstallAtSymbol("_ZN16HammerBrosHammer12attackSensorEPN2al9HitSensorES2_");
        FireballAttackSensorHook::InstallAtSymbol("_ZN16FireBrosFireBall12attackSensorEPN2al9HitSensorES2_");
        FireballAttackSensorInline::InstallAtOffset(0x100E70);
        TankBulletAttackSensorInline::InstallAtOffset(0x189C7C);
    }
}