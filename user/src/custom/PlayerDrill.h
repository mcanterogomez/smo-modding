#pragma once
#include "custom/.Globals.h"
#include "Library/Collision/CollisionPartsTriangle.h"
#include "Library/Collision/CollisionPartsKeeperUtil.h"

namespace PlayerDrill {

    enum : int { Idle = -1, Enter = 0, Active = 1, Exit = 2 };

    inline sead::Vector3f defaultGravity = {0.f, -1.f, 0.f};
    inline sead::Vector3f stickGravity = defaultGravity;

    inline void enterDrill(al::LiveActor* model) {
        al::hideModelIfShow(model);
        al::hideSilhouetteModelIfShow(model);
        al::tryStartSe(model, "DrillIn");
    }

    inline void exitDrill(al::LiveActor* model) {
        al::showModelIfHide(model);
        al::showSilhouetteModelIfHide(model);
        al::tryStopSe(model, "DrillMove", -1, nullptr);
        al::tryStartSe(model, "DrillOut");
    }

    inline void popDrill(al::LiveActor* model) {
        exitDrill(model);
        al::tryEmitEffect(model, "LandFall", nullptr);
        al::tryStartSe(model, "DrillSpin");

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
    inline void startDrillOut(PlayerActorHakoniwa* thisPtr, al::LiveActor* model, bool isMoving) {
        resetGravity(thisPtr);
        thisPtr->mAnimator->startSubAnim(isMoving ? "DrillOutFast" : "DrillOut");
        exitDrill(model);
        drillStep = Exit;
    }

    // Wall-stick state machine
    inline void updateWall(PlayerActorHakoniwa* thisPtr) {
        auto* model = thisPtr->mModelHolder->findModelActor("Normal");
        auto* anim = thisPtr->mAnimator;
        auto* input = thisPtr->mInput;
        bool onGround = rs::isPlayerOnGround(thisPtr);
        bool onWall = rs::isCollidedWall(thisPtr->mCollider);
        bool isHoldZR = al::isPadHoldZR(-1);
        bool isMove = input->isMove();

        switch (drillStep) {

            // Not drilling. Start on ZR+surface. Reset gravity if airborne.
            case Idle: {
                if (!onGround && !onWall) { resetGravity(thisPtr); return; }
                if (!isHoldZR || !canAction || isActionBusy()
                    || input->isTriggerJump() || al::isInWater(thisPtr)
                    || PlayerEquipmentFunction::isEquipmentForceDash(thisPtr->mEquipmentUser)) return;

                canAction = false;
                // Force-cancel any active player state (spin cap, taunt, etc).
                al::setNerve(thisPtr, getNerveAt(nrvHakoniwaFall));
                anim->endSubAnim();

                if (onWall) snapGravityToWall(thisPtr);
                else stickGravity = defaultGravity;

                // Wall snaps straight in; ground plays DrillIn unless mid-HipDrop
                if (onWall || !al::isEqualSubString(anim->mCurAnim, "HipDrop")) anim->startSubAnim("DrillIn");
                drillStep = Enter;
                break;
            }

            // DrillIn anim playing. Wait for it to finish. Nothing interrupts.
            case Enter: {
                if (!anim->isSubAnim("DrillIn") || anim->isSubAnimEnd()) {
                    enterDrill(model);
                    drillStep = Active;
                }
                break;
            }

            // Drilled in, stuck to surface.
            case Active: {
                // Jump: pop out. Don't reset gravity — jump uses it to launch
                if (input->isTriggerJump()) {
                    popDrill(model);
                    drillStep = Idle;
                    break;
                }

                // ZR released: drill out
                if (!isHoldZR) { startDrillOut(thisPtr, model, isMove); break; }

                // Stay stuck: wall takes priority, then raycast, else lost -> drill out
                if (onWall) snapGravityToWall(thisPtr);
                else if (!onGround && !isFoundSurface(thisPtr)) { startDrillOut(thisPtr, model, true); break; }

                // Fx + sound
                al::tryEmitEffect(model, "DrillMove", nullptr);
                if (isMove && !al::checkIsPlayingSe(model, "DrillMove", nullptr)) al::tryStartSe(model, "DrillMove");
                else if (!isMove) al::tryStopSe(model, "DrillMove", -1, nullptr);
                break;
            }

            // DrillOut anim playing. When done, return to Fall if airborne/moving, else Wait.
            case Exit: {
                if (!anim->isSubAnim("DrillOut") && !anim->isSubAnim("DrillOutFast")) { drillStep = Idle; break; }

                if (anim->isSubAnimEnd()) {
                    al::setNerve(thisPtr, getNerveAt((onGround && !isMove) ? nrvHakoniwaWait : nrvHakoniwaFall));
                    drillStep = Idle;
                }
                break;
            }
        }
    }

    // Full drill update: subactor, visuals, wall-stick, sensors
    inline void update(PlayerActorHakoniwa* thisPtr) {
        if (!isDrill) return;

        auto* model = thisPtr->mModelHolder->findModelActor("Normal");
        auto* head = al::tryGetSubActor(model, "頭");
        auto* drill = al::tryGetSubActor(model, "Drill");
        auto* anim = thisPtr->mAnimator;
        auto* damage = thisPtr->mDamageKeeper;

        bool isHack = thisPtr->mHackKeeper && thisPtr->mHackKeeper->mHackActor;
        bool isFlicker = damage && damage->mDamageInvalidCount > 0;
        bool controllable = !isHack && !rs::isActiveDemo(thisPtr); // not captured or in a demo
        bool isActive = !isFlicker && controllable;
        bool capOn = thisPtr->mHackCap->isPutOn();
        bool inHipDrop = al::isNerve(thisPtr, getNerveAt(nrvHakoniwaHipDrop));
        bool inLand = anim->isAnim("HipDropLand");
        bool drillDrop = isActive && capOn && inHipDrop && (!inLand || anim->getAnimFrame() < 8.0f);

        // Drill subactor: spawn on drop, kill otherwise (always runs so warps/demos clean up)
        if (drill) {
            if (drillDrop && al::isDead(drill)) {
                drill->appear();
                al::tryStartAction(drill, "DrillSpin");
                al::tryEmitEffect(model, "DrillSpinDrop", nullptr);
                al::tryStartSe(model, "DrillSpin");
            } else if (!drillDrop && al::isAlive(drill)) drill->kill();
        }
        if (!inHipDrop || inLand) al::tryDeleteEffect(model, "DrillSpinDrop");

        float legTarget = drillDrop ? 0.0f : 1.0f;
        legScale.set(legTarget, legTarget, legTarget);

        // Visuals: cap + head action, skip demos and captures
        if (capOn && controllable) {
            if (drillDrop) anim->forceCapOff();
            else anim->forceCapOn();

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
                al::tryEmitEffect(model, "DrillSpin", nullptr);
                if (--drillSensorRemaining == 0) al::tryDeleteEffect(model, "DrillSpin");
            }

            if (!al::isNerve(thisPtr, getNerveAt(nrvHakoniwaJump))
                && !al::isNerve(thisPtr, getNerveAt(nrvHakoniwaFall))) drillSensorRemaining = 0;
        }
    }

}  // namespace PlayerDrill