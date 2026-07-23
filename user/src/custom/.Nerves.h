#pragma once
#include "ModConfig.h"
#include "custom/.Globals.h"

// Handle lunging
inline void applyLunge(PlayerActorHakoniwa* player, float launchFrame, float speed) {
	float frame = player->mAnimator->getAnimFrame();
	if (frame < launchFrame) return;

	sead::Vector3f normal;
	rs::calcGroundNormalOrUpDir(&normal, player, player->mCollider);
	sead::Vector3f vel = al::getVelocity(player);

	if (frame - player->mAnimator->getAnimFrameRate() < launchFrame) {
		vel *= 0.5f;
		sead::Vector3f fwd;
		al::calcQuatFront(&fwd, player);
		fwd -= normal * fwd.dot(normal);
		fwd.normalize();
		vel += fwd * speed;
	} else {
		f32 vComp = vel.dot(normal);
		sead::Vector3f hVel = vel - normal * vComp;
		hVel *= 0.95f; // sheds 5% of horizontal speed per frame
		vel = hVel + normal * vComp;
	}

	al::setVelocity(player, vel);
}

// Home in on nearest target
inline void applyHomeIn(al::LiveActor* player, al::LiveActor* target) {
    if (!isNearTarget || !al::isAlive(isNearTarget) || isInHitBuffer(isNearTarget)) return;
    al::faceToDirection(player, al::getTrans(isNearTarget) - al::getTrans(player));

    sead::Vector3f grav = al::getGravity(player);
    sead::Vector3f* vel = al::getVelocityPtr(player);
    sead::Vector3f fwd;
    al::calcQuatFront(&fwd, player);

    f32 gravComp = vel->dot(grav);
    f32 hSpeed = (*vel - grav * gravComp).length();
    *vel = fwd * hSpeed + grav * gravComp;
}

// Custom Nerves
class PlayerStateSpinCapNrvGalaxySpinGround; 
extern PlayerStateSpinCapNrvGalaxySpinGround GalaxySpinGround; 

class PlayerStateSpinCapNrvGalaxySpinAir; 
extern PlayerStateSpinCapNrvGalaxySpinAir GalaxySpinAir; 

class PlayerStateSpinCapNrvGalaxySpinGround : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        PlayerStateSpinCap* state = keeper->getParent<PlayerStateSpinCap>();
        PlayerActorHakoniwa* player = static_cast<PlayerActorHakoniwa*>(state->mActor);
        static sead::Vector3f punchDir; // Cache direction
        static sead::Vector3f punchPos; // Cache position

        isSpinActive = true;

        if (al::isFirstStep(state)) {
            bool isSpinning = state->mAnimator->isAnim("SpinSeparate");
            bool isRotatingL = state->mAnimator->isAnim("SpinGroundL");
            bool isRotatingR = state->mAnimator->isAnim("SpinGroundR");
            bool isCarrying = player->mCarryKeeper->isCarry();
            bool didSpin = player->mInput->isSpinInput();
            int spinDir = player->mInput->mSpinInputAnalyzer->mSpinDirection;
            isNearTarget = findNearestTarget(player, 250.0f);
            isPunchRight = !isPunchRight;

            state->mAnimator->endSubAnim();

            if (!isSpinning) {
                if (!isCarrying && (isNearCollectible || isNearTreasure || isNearSwoonedEnemy)) {
                    state->mAnimator->startAnim(isNearCollectible ? "RabbitGet" : "Kick");
                    applyHomeIn(player, isNearTarget);
                }
                else if (didSpin) {
                    state->mAnimator->startSubAnim(spinDir > 0 ? "SpinAttackLeft" : "SpinAttackRight");
                    state->mAnimator->startAnim(spinDir > 0 ? "SpinAttackLeft" : "SpinAttackRight");
                    al::validateHitSensor(state->mActor, "DoubleSpin");
                    attackSensorRemaining = 41;
                }
                else if (isRotatingL || isRotatingR) {
                    state->mAnimator->startSubAnim(isRotatingL ? "SpinAttackLeft" : "SpinAttackRight");
                    state->mAnimator->startAnim(isRotatingL ? "SpinAttackLeft" : "SpinAttackRight");
                    al::validateHitSensor(state->mActor, "DoubleSpin");
                    attackSensorRemaining = 41;
                }
                else if (player->mInput->isHoldSquat()) {
                    state->mAnimator->startSubAnim("SpinLow");
                    state->mAnimator->startAnim("SpinLow");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    attackSensorRemaining = 12;
                }
                else if (isCarrying) {
                    state->mAnimator->startSubAnim("SpinSeparate");
                    state->mAnimator->startAnim("SpinSeparate");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    attackSensorRemaining = 21;
                    isGalaxySfx(player); // apply galaxy effects
                }
                else if (isFeather) {
                    al::setNerve(state, reinterpret_cast<al::Nerve*>(&GalaxySpinAir));
                    return;
                }
                else if (isTanooki) {
                    state->mAnimator->startSubAnim("TailAttack");
                    state->mAnimator->startAnim("TailAttack");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    attackSensorRemaining = 21;
                }
                else if (isWeaponOn) {
                    state->mAnimator->startSubAnim("SwingAttack");
                    state->mAnimator->startAnim("SwingAttack");
                    al::tryStartSe(player, "SwingAttack");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    attackSensorRemaining = 21;
                }
                else if (isConfig()->spinOnly) {
                    state->mAnimator->startSubAnim("SpinSeparate");
                    state->mAnimator->startAnim("SpinSeparate");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    attackSensorRemaining = 21;
                    isGalaxySfx(player); // apply galaxy effects
                }
                else {
                    al::calcQuatFront(&punchDir, player);
                    punchDir = -punchDir;
                    punchPos = al::getTrans(player);

                    if (isKoopa && KoopaBattle::isKillReady(isKoopa)) state->mAnimator->startAnim(isPunchRight ? "JumpPunchEndR" : "JumpPunchEndL");
                    else {
                        state->mAnimator->startSubAnim(isPunchRight ? "PunchR" : "PunchL");
                        state->mAnimator->startAnim(isPunchRight ? "PunchR" : "PunchL");
                    }
                }
            }
        }

        bool isPunch = state->mAnimator->isAnim("PunchR") || state->mAnimator->isAnim("PunchL");
        bool isLow = state->mAnimator->isAnim("SpinLow");
        bool isJumpPunch = state->mAnimator->isAnim("JumpPunchL") || state->mAnimator->isAnim("JumpPunchR");
        bool isBowserPunch = state->mAnimator->isAnim("JumpPunchEndL") || state->mAnimator->isAnim("JumpPunchEndR");
        float isFrame = state->mAnimator->getAnimFrame();

        if (isPunch) {
            if (player->mInput->isTriggerJump() && rs::isOnGround(player, player->mCollider)) { // cancel to jump punch
                hitBufferCount = 0;
                state->mAnimator->startAnim(isPunchRight ? "JumpPunchL" : "JumpPunchR");
                return;
            }
            if (isFrame >= state->mAnimator->getAnimFrameMax() - 8.0f && isPadTriggerGalaxySpin(-1)) { // cancel with punch
                hitBufferCount = 0;
                al::setNerve(state, &GalaxySpinGround);
                return;
            }
            applyLunge(player, 5.0f, 5.0f);
            if (isFrame == 6.0f) { al::validateHitSensor(state->mActor, "Punch"); attackSensorRemaining = 6; }
        }
        else if (isJumpPunch) {
            if (isFrame < 17.0f) { // decay jump punch
                sead::Vector3f vel = al::getVelocity(player);
                vel *= 0.8f;
                al::setVelocity(player, vel);
            } else if (isFrame == 17.0f) { // launch jump punch
                sead::Vector3f up = -al::getGravity(player);
                up.normalize();
                sead::Vector3f fwd;
                al::calcQuatFront(&fwd, player);
                fwd -= up * fwd.dot(up);
                fwd.normalize();
                al::setVelocity(player, up * 30.0f + fwd * 5.0f);
                al::validateHitSensor(state->mActor, "GalaxySpin");
                attackSensorRemaining = 13;
            } else { al::setNerve(state, getNerveAt(nrvSpinCapFall)); return; } // kill jump punch
        }
        else if (isBowserPunch) {
            al::faceToDirection(player, punchDir);
            al::setTrans(player, punchPos);
            al::setVelocity(player, al::getGravity(player));
            if (isFrame == 20.0f) { al::validateHitSensor(state->mActor, "GalaxySpin"); attackSensorRemaining = 10; }
            if (isFrame == 60.0f) al::tryEmitEffect(player, "Land", nullptr);
        }
        else if (isLow || state->mAnimator->isAnim("SwingAttack")) {
            applyLunge(player, 2.0f, 5.0f);
            if (isLow && !rs::isOnGround(player, player->mCollider)) { al::setNerve(state, getNerveAt(nrvSpinCapFall)); return;}
        }
        else if ((state->mAnimator->isAnim("RabbitGet") && isFrame == 7.0f) || (state->mAnimator->isAnim("Kick") && isFrame == 2.0f)) al::validateHitSensor(state->mActor, "Punch");

        if (!isJumpPunch && !isBowserPunch && !isLow) state->updateSpinGroundNerve();
        if (state->mAnimator->isAnimEnd()) { state->kill(); isSpinActive = false; }
    }
};

class PlayerStateSpinCapNrvGalaxySpinAir : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        PlayerStateSpinCap* state = keeper->getParent<PlayerStateSpinCap>();
        PlayerActorHakoniwa* player = static_cast<PlayerActorHakoniwa*>(state->mActor);

        bool isSpinning = state->mAnimator->isAnim("SpinSeparate");
        isSpinActive = true;
        
        if(al::isFirstStep(state)) {
            bool isRotatingAirL = state->mAnimator->isAnim("StartSpinJumpL") || state->mAnimator->isAnim("RestartSpinJumpL");
            bool isRotatingAirR = state->mAnimator->isAnim("StartSpinJumpR") || state->mAnimator->isAnim("RestartSpinJumpR");
            bool isCarrying = player->mCarryKeeper->isCarry();
            bool didSpin = player->mInput->isSpinInput();
            int spinDir = player->mInput->mSpinInputAnalyzer->mSpinDirection;
            bool isCape = (isMario && isCapeOn) || isFeather;

            if (!isSpinning) {
                if (didSpin) {
                    state->mAnimator->startAnim(spinDir > 0 ? "SpinAttackAirLeft" : "SpinAttackAirRight");
                    al::validateHitSensor(state->mActor, "DoubleSpin");
                    attackSensorRemaining = 41;
                }
                else if (isRotatingAirL || isRotatingAirR) {
                    state->mAnimator->startAnim(isRotatingAirL ? "SpinAttackAirLeft" : "SpinAttackAirRight");
                    al::validateHitSensor(state->mActor, "DoubleSpin");
                    attackSensorRemaining = 41;
                }
                else if (isCarrying) {
                    state->mAnimator->startAnim("SpinSeparate");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    attackSensorRemaining = 21;
                    isGalaxySfx(player); // apply galaxy effects
                }
                else if (isCape) {
                    state->mAnimator->startAnim("CapeAttack");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    attackSensorRemaining = 21;
                }
                else if (isTanooki) {
                    state->mAnimator->startAnim("TailAttack");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    attackSensorRemaining = 21;
                }
                else if (isWeaponOn) {
                    state->mAnimator->startAnim("SwingAirAttack");
                    al::tryStartSe(player, "SwingAttack");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    attackSensorRemaining = 21;
                }
                else {
                    state->mAnimator->startAnim("SpinSeparate");
                    al::validateHitSensor(state->mActor, "GalaxySpin");
                    attackSensorRemaining = 21;
                    isGalaxySfx(player); // apply galaxy effects
                }
            }
        }
        
        state->updateSpinAirNerve();

		if (state->mAnimator->isAnimEnd() || attackSensorRemaining <= 0) {
            al::setNerve(state, getNerveAt(nrvSpinCapFall));
            isSpinActive = false;
            return;
        }
    }
};

class PlayerActorHakoniwaNrvTauntLeft : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        auto* player = keeper->getParent<PlayerActorHakoniwa>();
        auto* anim = player->mAnimator;

        if (al::isFirstStep(player)) {
            //al::setVelocity(player, sead::Vector3f::zero);
            anim->endSubAnim();

            if (isFire || isIce || isBrawl) anim->startAnim("WearEndBrawl");
            else if (isMetal || isSuper) anim->startAnim("WearEndSuper");
            else anim->startAnim("WearEnd");
        }

        if (anim->isAnimEnd()) { al::setNerve(player, getNerveAt(nrvHakoniwaWait)); return; }
    }
};

class PlayerActorHakoniwaNrvTauntRight : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        auto* player = keeper->getParent<PlayerActorHakoniwa>();
        auto* anim = player->mAnimator;
        auto* model = player->mModelHolder->findModelActor("Normal");
        auto* cape = al::tryGetSubActor(model, "ケープ");
        auto* effect = static_cast<al::IUseEffectKeeper*>(model);

        if (al::isFirstStep(player)) {
            //al::setVelocity(player, sead::Vector3f::zero);
            anim->endSubAnim();

            if (isMarioActive == 1) anim->startAnim("TauntSuper");
            else if (isMarioActive == -1) anim->startAnim("AreaWaitSigh");
            else if (player->mInput->isHoldSquat()) {
                if (isBrawl) {
                    if (cape && al::isDead(cape)) anim->startAnim("LandJump3");
                    else anim->startAnim("TauntFeather");
                }
                else if (isFire || isIce || isSuper) anim->startAnim("TauntSuper");
                else if (isFeather || isTanooki) anim->startAnim("AreaWaitSayCheese");
                else anim->startAnim("AreaWait64");
            }
            else if (isFire || isBrawl || isSuper) anim->startAnim("TauntFire");
            else if (isFeather || isTanooki) anim->startAnim("TauntFeather");
            else if (isIce) anim->startAnim("TauntIce");
            else anim->startAnim("TauntMario");
        }

        if (anim->isAnim("LandJump3")) {
            if (al::isStep(player, 25)) {
                if (cape) cape->appear();
                isCapeActive = 1200;
                al::tryEmitEffect(effect, "AppearBloom", nullptr);
                al::tryStartSe(player, "Bloom");
            }
        }
        else if (anim->isAnim("TauntFire")
            || anim->isAnim("TauntIce")) {
            if (al::isStep(player, 65)) al::tryStartSe(player, "FireOn");
            if (al::isStep(player, 160)) {
                al::tryStopSe(player, "FireOn", -1, nullptr);
                if (isIce) al::tryStartSe(player, "IceOff");
                else al::tryStartSe(player, "FireOff");
            }
        }
        else if (anim->isAnim("TauntSuper")) {
            if (isFire) player->mStainControl->recordDamageFire();
            if (isIce) player->mStainControl->recordIceWater();
            if (al::isStep(player, 14)) {
                if (isIce) { al::tryEmitEffect(effect, "IceEffect", nullptr); al::tryStartSe(player, "FireOn"); }
                if (isMarioActive == 1) { player->mDamageKeeper->invalidate(60); al::tryStartSe(player, "HrPowerUpNormal"); }
                if (isFire || isSuper || isMarioActive == 1) { al::tryEmitEffect(effect, "BonfireSuper", nullptr); al::tryStartSe(player, "FireOn"); }
                if (isSuper) {
                    al::tryEmitEffect(player, "InvincibleStart", nullptr);
                    al::tryEmitEffect(effect, "LandFall", nullptr);
                    al::tryStartSe(player, "StartInvincible");
                }
            }
        }

        if (anim->isAnimEnd()) {
            isMarioActive = 0;
            al::tryDeleteEffect(effect, "BonfireSuper");
            al::tryDeleteEffect(effect, "IceEffect");
            al::tryStopSe(player, "FireOn", -1, nullptr);
            al::setNerve(player, getNerveAt(nrvHakoniwaWait));
            return;
        }
    }
};

// Hammer specific setup
inline sead::Matrix34f hammerMtx;
inline sead::Vector3f updateHammerMtx() {
    auto* model = isHakoniwa->mModelHolder->findModelActor("Normal");
    const sead::Matrix34f* mL = al::getJointMtxPtr(model, "ArmL2");
    const sead::Matrix34f* mR = al::getJointMtxPtr(model, "ArmR2");
    if (!mL || !mR) return al::getTrans(isHammer);

    sead::Vector3f mid = (mL->getTranslation() + mR->getTranslation()) * 0.5f;
    sead::Quatf qL, qR, qMid;
    mL->toQuat(qL);
    mR->toQuat(qR);
    al::slerpQuat(&qMid, qL, qR, 0.5f);
    hammerMtx.makeQT(qMid, mid);

    auto* weapon = static_cast<BrosWeaponBase*>(isHammer);
    sead::Matrix34f attachMtx;
    weapon->calcAttachMtx(&attachMtx, &hammerMtx, weapon->mTrans, weapon->mRotation);
    return attachMtx.getTranslation();
}

class PlayerActorHakoniwaNrvHammer : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        auto* player = keeper->getParent<PlayerActorHakoniwa>();
        auto* model = player->mModelHolder->findModelActor("Normal");
        auto* hammer = al::tryGetSubActor(model, "Hammer");
        bool isGround = rs::isOnGround(player, player->mCollider);

        if (al::isFirstStep(player)) {
            player->mAnimator->endSubAnim();
            hitBufferCount = 0;

            if (hammer) al::hideModelIfShow(hammer);
            updateHammerMtx();

            al::setScale(isHammer, sead::Vector3f::zero); // Handle hammer scaling start
            isHammer->makeActorAlive();
            isHammer->attach(&hammerMtx,
                sead::Vector3f(0.0f, -12.5f, -37.5f),
                sead::Vector3f(0.0f, sead::Mathf::deg2rad(-90.0f), 0.0f),
                nullptr);
            al::onCollide(isHammer);
            al::invalidateClipping(isHammer);
            al::showShadow(isHammer);

            if (isGround) player->mAnimator->startAnim("HammerAttack");
            else {
                player->mAnimator->startAnim("RollingStart");
                al::validateHitSensor(isHammer, "AttackHack");
            }
        }

        // Air physics + spin transition
        if (!isGround) {
            al::addVelocity(player, al::getGravity(player) * 0.5f);

            if (player->mAnimator->isAnim("RollingStart") && player->mAnimator->isAnimEnd()) {
                player->mAnimator->startAnim("Rolling");
                al::tryStartAction(isHammer, "Spin");
            }
        }

        // Air -> Ground transition
        if (isGround && (player->mAnimator->isAnim("RollingStart") || player->mAnimator->isAnim("Rolling"))) {
            player->mAnimator->endSubAnim();
            player->mAnimator->startAnim("HammerAttack");
            al::tryStartAction(isHammer, "Wait");
        }

        // Ground attack frames
        if (player->mAnimator->isAnim("HammerAttack")) {
            float frame = player->mAnimator->getAnimFrame();

            if (frame == 7.0f) {
                sead::Vector3f vel = al::getVelocity(player);
                vel *= 0.5f;
                al::setVelocity(player, vel);
                al::validateHitSensor(isHammer, "AttackHack");
            }
            if (frame == 11.0f && !rs::isCollidedWall(isHakoniwa->mCollider)) {
                al::tryEmitEffect(isHakoniwa, "HammerLand", nullptr);
                al::tryStartSe(isHammer, "HammerLand");
                al::tryStartSe(isHammer, "HammerHit");
            }
            if (frame >= 22.0f) al::invalidateHitSensor(isHammer, "AttackHack");
        }

        // Scale fade in/out
        if (al::isAlive(isHammer)) {
            float scale = sead::Mathf::min(1.0f, al::getNerveStep(player) / 4.0f);
            if (player->mAnimator->isAnim("HammerAttack") && player->mAnimator->getAnimFrame() >= 22.0f) scale = (30.0f - player->mAnimator->getAnimFrame()) / 8.0f;
            al::setScale(isHammer, sead::Vector3f(scale, scale, scale));
        }

        if (player->mAnimator->isAnimEnd()) {
            cleanup(hammer);
            al::setNerve(player, getNerveAt(nrvHakoniwaFall));
            return;
        }

        if (hammer && al::isDead(isHammer)) al::showModelIfHide(hammer);
    }

    void executeOnEnd(al::NerveKeeper* keeper) const override {
        auto* player = keeper->getParent<PlayerActorHakoniwa>();
        auto* model = player->mModelHolder->findModelActor("Normal");
        cleanup(al::tryGetSubActor(model, "Hammer"));
    }

private:
    static void cleanup(al::LiveActor* hammer) {
        if (hammer) al::showModelIfHide(hammer);
        if (isHammer) { al::invalidateHitSensor(isHammer, "AttackHack"); isHammer->makeActorDead(); }
    }
};

inline PlayerStateSpinCapNrvGalaxySpinGround GalaxySpinGround;
inline PlayerStateSpinCapNrvGalaxySpinAir GalaxySpinAir;
inline PlayerActorHakoniwaNrvTauntLeft TauntLeftNrv;
inline PlayerActorHakoniwaNrvTauntRight TauntRightNrv;
inline PlayerActorHakoniwaNrvHammer HammerNrv;