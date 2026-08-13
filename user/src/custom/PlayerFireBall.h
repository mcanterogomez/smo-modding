#pragma once
#include "custom/.Globals.h"

namespace PlayerFireBall {

	enum : int { Idle = -1, Shooting = 0 };

	inline const al::Nerve* shotNerve = nullptr; // the state the shot rides, handed forward by locomotion; a mismatch means something took the body

	inline void spawnProjectile(PlayerActorHakoniwa* thisPtr, al::LiveActor* model, al::LiveActorGroup* pool, bool isBlast) {
		auto* projectile = pool->getDeadActor();
		if (!projectile || !al::isDead(projectile)) return; // pool may have emptied since the trigger

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
		al::calcJointPos(&startPos, model, (!isBlast && nextThrowLeft) ? "HandL" : "HandR");

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
		// Idle and nothing can start: skip the actor lookups below
		if (fireStep == Idle && (!(isMario || isFire || isIce || isBrawl || isSuper) || !al::isPadTriggerR(-1))) { canAction = false; return; }

		auto* anim = thisPtr->mAnimator;
		auto* model = thisPtr->mModelHolder->findModelActor("Normal");

		bool isBlast = isWeaponOn && !isKnight; // isWeaponOn is already the axe for a knight, the blaster otherwise
		al::LiveActorGroup* pool = isBlast ? tankBullets : (isIce ? iceBalls : fireBalls);
		bool isIdle = !thisPtr->mInput->isMove() && rs::isOnGround(thisPtr, thisPtr->mCollider); // the shot owns the whole body only here
		bool wasAction = canAction; // read half of the latch: the clear below runs on every path
		static int shotFrame = 0; // only the spawn cares about frames

		canAction = false; // one-frame latch: TryCapSpinPre stops running during a capture
		if (!pool) return;

		switch (fireStep) {

			// Not shooting. Start the shot and hand off the frame, so the forced nerve resolves before anything reads it
			case Idle: {
				if (!wasAction && !al::isEqualSubString(al::getActionName(model), "GlideFloat")) return;
				auto* projectile = pool->getDeadActor();
				if (!projectile || !al::isDead(projectile)) return;

				const char* shotAnim = isBlast ? "BlastShoot" : (nextThrowLeft ? "FireL" : "FireR");
				fireStep = Shooting;
				shotFrame = 0;
				anim->startUpperBodyAnim(shotAnim); // upper body: keep moving/gliding
				if (isIdle) { al::setNerve(thisPtr, getNerveAt(nrvHakoniwaFall)); anim->startSubAnim(shotAnim); } // force-cancel the state, sub anim masks whatever it lands in
				if (isBlast) { al::setSensorRadius(thisPtr, "Eye", 1600.0f); al::tryStartSe(thisPtr, "BlasterShoot"); } // widen for homing
				break;
			}

			// Shot playing. Any nerve switch took the body, except Jump/Run
			case Shooting: {
				if (shotFrame == 0) shotNerve = thisPtr->getNerveKeeper()->getCurrentNerve();
				if (shotFrame == (isBlast ? 39 : 1)) spawnProjectile(thisPtr, model, pool, isBlast);
				if (!isIdle) tryEndSubAnim(anim); // no longer standing, upper body carries the rest

				bool canShoot = al::isNerve(thisPtr, shotNerve)
					|| al::isNerve(thisPtr, getNerveAt(nrvHakoniwaJump)) || al::isNerve(thisPtr, getNerveAt(nrvHakoniwaRun));

				if (!canShoot || anim->isUpperBodyAnimEnd()) {
					fireStep = Idle;
					al::setSensorRadius(thisPtr, "Eye", 800.0f); // restore default
					anim->clearUpperBodyAnim();
				}
				else shotFrame++;
				break;
			}
		}
	}

}  // namespace PlayerFireBall