#pragma once
#include "custom/.Globals.h"

namespace PlayerFireBall {

    inline void spawnProjectile(PlayerActorHakoniwa* thisPtr, al::LiveActor* model, al::LiveActorGroup* pool, const char* jointName, bool isBlast) {
        auto* projectile = pool->getDeadActor();
        if (!projectile || !al::isDead(projectile)) return; // pool may be empty

        // Home in on nearest target if it's roughly in front
        isNearTarget = findNearestTarget(thisPtr, isBlast ? 1600.0f : 800.0f);
        if (isNearTarget) {
            sead::Vector3f dir = al::getTrans(isNearTarget) - al::getTrans(thisPtr);
            dir.normalize();
            sead::Vector3f fwd; al::calcQuatFront(&fwd, model);
            if (fwd.dot(dir) > 0.85f) al::faceToDirection(model, dir);
        }

        hitBufferCount = 0;
        sead::Vector3f startPos;
        al::calcJointPos(&startPos, model, jointName);

        if (isBlast) {
            sead::Vector3f fwd; al::calcQuatFront(&fwd, model); fwd.normalize();
            ((TankBullet*)projectile)->shoot(startPos, fwd * 85.0f, 200, false, false);
            al::tryEmitEffect(model, "Shoot", nullptr);
            al::tryStartSe(projectile, "Shoot");
        } else {
            ((FireBrosFireBall*)projectile)->shoot(startPos, al::getQuat(model), sead::Vector3f::zero, true, 0, isSuper);
            al::tryStartSe(projectile, isIce ? "IceBallShoot" : "FireBallShoot");
            nextThrowLeft = !nextThrowLeft;
        }
    }

    inline void update(PlayerActorHakoniwa* thisPtr) {
        auto* anim = thisPtr->mAnimator;
        auto* model = thisPtr->mModelHolder->findModelActor("Normal");
        auto* blaster = al::tryGetSubActor(model, "Blaster");
        auto* weapon = isKnight ? al::tryGetSubActor(model, "Axe") : blaster;

        bool isMove = thisPtr->mInput->isMove();
        bool onGround = rs::isOnGround(thisPtr, thisPtr->mCollider);
        bool isWater = al::isInWater(thisPtr);
        bool isSurface = thisPtr->mWaterSurfaceFinder->isFoundSurface();
        bool isBlast = isWeaponOn && weapon == blaster;

        const char* jointName = isBlast ? "HandR" : (nextThrowLeft ? "HandL" : "HandR");
        const char* fireAnim = isBlast ? "BlastShoot" : (nextThrowLeft ? "FireL" : "FireR");

        al::LiveActorGroup* pool = isBlast ? tankBullets : (isIce ? iceBalls : fireBalls);
        if (!pool) return;

        int spawnFrame = isBlast ? 40 : 2;
        bool isFullBody = (!isMove && onGround && (!isWater || isSurface));
        auto restoreEyeRadius = [&]() { al::setSensorRadius(thisPtr, "Eye", 800.0f); }; // restore default

        // Trigger: start the shot
        if (fireStep < 0
            && (isMario || isFire || isIce || isBrawl || isSuper)
            && (canAction || al::isActionPlaying(model, "GlideFloat") || al::isActionPlaying(model, "GlideFloatSuper"))
            && al::isPadTriggerR(-1)
        ) {
            auto* projectile = pool->getDeadActor();
            if (projectile && al::isDead(projectile)) {
                fireStep = 0;
                canAction = false;
                anim->startUpperBodyAnim(fireAnim); // upper body: keep moving/gliding
                if (isFullBody) anim->startAnim(fireAnim); // standing: full body
                if (isBlast) { al::setSensorRadius(thisPtr, "Eye", 1600.0f); al::tryStartSe(thisPtr, "BlasterShoot"); } // widen for homing
            }
        }

        if (fireStep < 0) return;

        bool isShooting = anim->isUpperBodyAnim("FireL") || anim->isUpperBodyAnim("FireR") || anim->isUpperBodyAnim("BlastShoot") || anim->isAnim("FireL") || anim->isAnim("FireR") || anim->isAnim("BlastShoot");
        if (!isShooting) { fireStep = -1; restoreEyeRadius(); return;}

        if (fireStep == spawnFrame) spawnProjectile(thisPtr, model, pool, jointName, isBlast);

        if (isFullBody ? anim->isAnimEnd() : anim->isUpperBodyAnimEnd()) {
            fireStep = -1;
            restoreEyeRadius();
            anim->clearUpperBodyAnim();
            al::setNerve(thisPtr, getNerveAt(onGround ? nrvHakoniwaWait : nrvHakoniwaFall));
        }
        else fireStep++;
    }
}