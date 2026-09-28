#pragma once
#include "custom/.Globals.h"
#include "Library/Collision/CollisionPartsTriangle.h"
#include "Library/Collision/CollisionPartsKeeperUtil.h"

namespace PlayerDrill {

	enum : int { Idle = -1, Enter = 0, Active = 1, Exit = 2 };

	inline sead::Vector3f defaultGravity = {0.f, -1.f, 0.f};
	inline sead::Vector3f stickGravity = defaultGravity;
	inline const al::Nerve* drillNerve = nullptr; // the state the drill plays in; anything else took the body

	inline void enterDrill() {
		al::hideModelIfShow(isMarioModel);
		al::hideSilhouetteModelIfShow(isMarioModel);
		al::tryStartSe(isMarioModel, "DrillIn");
	}

	inline void exitDrill() {
		al::showModelIfHide(isMarioModel);
		al::showSilhouetteModelIfHide(isMarioModel);
		al::tryStopSe(isMarioModel, "DrillMove", -1, nullptr);
		al::tryStartSe(isMarioModel, "DrillOut");
	}

	inline void popDrill() {
		exitDrill();
		al::tryEmitEffect(isMarioModel, "LandFall", nullptr);
		al::tryStartSe(isMarioModel, "DrillSpin");

		if (isHakoniwa) {
			al::validateHitSensor(isHakoniwa, "GalaxySpin");
			hitBufferCount = 0;
			drillSensorRemaining = 25; // Allow drill hitbox for a few frames after popping
		}
	}

	inline void resetGravity(PlayerActorHakoniwa* thisPtr) {
		stickGravity = defaultGravity;
		al::setGravity(thisPtr, defaultGravity);
	}

	inline void snapGravityToWall(PlayerActorHakoniwa* thisPtr) {
		stickGravity = -rs::getCollidedWallNormal(thisPtr->mCollider);
		al::setGravity(thisPtr, stickGravity);
	}

	// If it hits a surface, updates surface's normal and returns true
	inline bool isFoundSurface(PlayerActorHakoniwa* thisPtr) {
		const sead::Vector3f up = -stickGravity;
		al::Triangle tri;
		sead::Vector3f hitPos;

		if (alCollisionUtil::getFirstPolyOnArrow(thisPtr, &hitPos, &tri, al::getTrans(thisPtr) + up * 80.f, stickGravity * 150.f, nullptr, nullptr)) {
			stickGravity = -tri.mNormals[0];
			al::setGravity(thisPtr, stickGravity);
			return true;
		}
		return false;
	}

	// Start the drill-out anim (moving vs standing) and hand off to Exit
	inline void startDrillOut(PlayerActorHakoniwa* thisPtr, bool isMoving) {
		resetGravity(thisPtr);
		thisPtr->mAnimator->startSubAnim(isMoving ? "DrillOutFast" : "DrillOut");
		exitDrill();
		drillStep = Exit;
	}

	// Wall-stick state machine
	inline void updateWall(PlayerActorHakoniwa* thisPtr) {
		auto* anim = thisPtr->mAnimator;
		auto* input = thisPtr->mInput;
		bool onGround = rs::isPlayerOnGround(thisPtr);
		bool onWall = rs::isCollidedWall(thisPtr->mCollider);
		bool isHoldZR = al::isPadHoldZR(-1);

		switch (drillStep) {

			// Not drilling. Start on ZR+surface. Reset gravity if airborne.
			case Idle: {
				if (!onGround && !onWall) { resetGravity(thisPtr); return; }
				if (!isHoldZR || !canAction || input->isTriggerJump() || al::isInWater(thisPtr)
					|| PlayerEquipmentFunction::isEquipmentForceDash(thisPtr->mEquipmentUser)
					|| isActionBusy()) return;

				canAction = false;
				tryEndSubAnim(anim);
				al::setNerve(thisPtr, getNerveAt(nrvHakoniwaFall)); // Force-cancel any active player state (spin cap, taunt, etc).

				if (onWall) snapGravityToWall(thisPtr);
				else stickGravity = defaultGravity;

				// Wall snaps straight in; ground plays DrillIn unless mid-HipDrop
				if (onWall || !al::isEqualSubString(anim->mCurAnim, "HipDrop")) anim->startSubAnim("DrillIn");
				drillStep = Enter;
				break;
			}

			// DrillIn anim playing. Wait for it to finish. Nothing interrupts.
			case Enter: {
				drillNerve = thisPtr->getNerveKeeper()->getCurrentNerve(); // the forced Fall has resolved by now
				if (!anim->isSubAnim("DrillIn") || anim->isSubAnimEnd()) {
					enterDrill();
					drillStep = Active;
				}
				break;
			}

			// Drilled in, stuck to surface.
			case Active: {
				bool isMove = input->isMove();

				// Jump: pop out. Don't reset gravity — jump uses it to launch
				if (input->isTriggerJump()) {
					popDrill();
					drillStep = Idle;
					break;
				}

				// ZR released: drill out
				if (!isHoldZR) { startDrillOut(thisPtr, isMove); break; }

				// Stay stuck: wall takes priority, then raycast, else lost -> drill out
				if (onWall) snapGravityToWall(thisPtr);
				else if (!onGround && !isFoundSurface(thisPtr)) { startDrillOut(thisPtr, true); break; }

				// Fx + sound
				al::tryEmitEffect(isMarioModel, "DrillMove", nullptr);
				if (isMove && !al::checkIsPlayingSe(isMarioModel, "DrillMove", nullptr)) al::tryStartSe(isMarioModel, "DrillMove");
				else if (!isMove) al::tryStopSe(isMarioModel, "DrillMove", -1, nullptr);
				break;
			}

			// DrillOut anim playing. Climbing, grabbing and moving take the state; otherwise it runs to the end.
			case Exit: {
				if (!al::isNerve(thisPtr, drillNerve) || anim->isSubAnimEnd()) { tryEndSubAnim(anim); drillStep = Idle; }
				break;
			}
		}
	}

	// Full drill update: subactor, visuals, wall-stick, sensors
	inline void update(PlayerActorHakoniwa* thisPtr, bool isActive) {
		if (!isDrill) return;

		auto* drill = al::tryGetSubActor(isMarioModel, "Drill");
		auto* anim = thisPtr->mAnimator;

		bool controllable = !isHacking() && !rs::isActiveDemo(thisPtr); // not captured or in a demo
		bool capOn = thisPtr->mHackCap->isPutOn();
		bool inHipDrop = al::isNerve(thisPtr, getNerveAt(nrvHakoniwaHipDrop));
		bool inLand = anim->isAnim("HipDropLand");
		bool drillDrop = isActive && capOn && inHipDrop && (!inLand || anim->getAnimFrame() < 8.0f);

		// Drill subactor: spawn on drop, kill otherwise (always runs so warps/demos clean up)
		if (drill) {
			if (drillDrop && al::isDead(drill)) {
				drill->appear();
				al::tryStartAction(drill, "DrillSpin");
				al::tryEmitEffect(isMarioModel, "DrillSpinDrop", nullptr);
				al::tryStartSe(isMarioModel, "DrillSpin");
			} else if (!drillDrop && al::isAlive(drill)) drill->kill();
		}
		if (!inHipDrop || inLand) al::tryDeleteEffect(isMarioModel, "DrillSpinDrop");

		float legTarget = drillDrop ? 0.0f : 1.0f;
		legScale.set(legTarget, legTarget, legTarget);

		// Visuals: cap + head action, skip demos and captures
		if (capOn && controllable) {
			if (drillDrop) anim->forceCapOff();
			else anim->forceCapOn();

			auto* head = al::tryGetSubActor(isMarioModel, "頭");
			const char* headAction = isDrillAnim(anim) ? "DrillSpin" : "DrillWait";
			if (head && !al::isActionPlaying(head, headAction)) al::tryStartAction(head, headAction);
		}

		// Active-only mechanics
		if (isActive && capOn) {
			if (!inHipDrop || !rs::isCollidedWall(thisPtr->mCollider)) updateWall(thisPtr);

			bool isDrillAttack = isDrillAnim(anim);
			static bool wasDrillAttack = false;
			updateAttackSensor(thisPtr, "GalaxySpin", isDrillAttack, wasDrillAttack);

			if (drillSensorRemaining > 0) {
				al::tryEmitEffect(isMarioModel, "DrillSpin", nullptr);
				if (--drillSensorRemaining == 0) al::tryDeleteEffect(isMarioModel, "DrillSpin");
			}

			// Left the pop-out arc early: the countdown never reached zero, so delete here too
			if (drillSensorRemaining > 0
				&& !al::isNerve(thisPtr, getNerveAt(nrvHakoniwaJump))
				&& !al::isNerve(thisPtr, getNerveAt(nrvHakoniwaFall))) {
				drillSensorRemaining = 0;
				al::tryDeleteEffect(isMarioModel, "DrillSpin");
			}
		}
	}

}  // namespace PlayerDrill