#pragma once

#include "Library/LiveActor/LiveActor.h"
#include "Library/LiveActor/ActorInitUtil.h"
#include "Library/LiveActor/ActorPoseUtil.h"
#include "Library/LiveActor/ActorModelFunction.h"
#include "Library/LiveActor/ActorActionFunction.h"
#include "Library/LiveActor/ActorSensorUtil.h"
#include "Library/LiveActor/ActorCollisionFunction.h"
#include "Library/LiveActor/ActorMovementFunction.h"
#include "Library/HitSensor/HitSensorKeeper.h"
#include "Library/Collision/CollisionPartsKeeperUtil.h"

class PlayerIceCube : public al::LiveActor {
public:
    PlayerIceCube(const char* name) : al::LiveActor(name) {}

    void init(const al::ActorInitInfo& info) override {
        al::initActorWithArchiveName(this, info, "PlayerIceCube", nullptr);
        makeActorDead();
    }

    void control() override {
        if (mIsBreaking) {
            if (al::isActionEnd(this)) {
                mIsBreaking = false;
                makeActorDead();
            }
            return;
        }

        if (!mTarget || !al::isAlive(mTarget)) {
            mTarget = nullptr;
            makeActorDead();
            return;
        }

        syncToTarget();
    }

    void freeze(al::LiveActor* target) {
        mTarget = target;
        mWasHit = false;
        mAttacker = nullptr;
        mIsBreaking = false;

        calcScale(); // target's size can't change while frozen, so this only needs to run once
        syncToTarget();
        makeActorAlive();
        al::tryStartAction(this, "Appear");

        f32 effectScale = mScale * kEffectScaleMult;
        al::setEffectAllScale(this, "Appear", sead::Vector3f(effectScale, effectScale, effectScale));
    }

    void unfreeze() {
        if (mTarget && al::isAlive(mTarget)) syncToTarget();

        mTarget = nullptr;
        mWasHit = false;
        mAttacker = nullptr;

        makeActorAlive();
        mIsBreaking = al::tryStartAction(this, "Break");
        if (mIsBreaking) {
            f32 effectScale = mScale * kEffectScaleMult;
            al::setEffectAllScale(this, "Break", sead::Vector3f(effectScale, effectScale, effectScale));
        } else
            makeActorDead();
    }

    al::LiveActor* getTarget() const { return mTarget; }
    bool wasHit() const { return mWasHit; }
    al::HitSensor* getAttacker() const { return mAttacker; }

    void markHit(al::HitSensor* attacker) {
        mWasHit = true;
        mAttacker = attacker;
    }

private:
    static constexpr f32 kMinScale = 0.5f;
    static constexpr f32 kEffectScaleMult = 0.5f;
    static constexpr f32 kGroundRayLength = 500.0f;

    // Get cube's base bounding box and derive scale/sensor size from the frozen target.
    void calcScale() {
        if (!mTarget) return;

        sead::BoundBox3f cubeBox;
        al::calcModelBoundingBox(&cubeBox, this);
        f32 cubeMaxDim = sead::Mathf::max(cubeBox.getSizeX(),
                          sead::Mathf::max(cubeBox.getSizeY(), cubeBox.getSizeZ()));

        // Build bounding box from enemy body sensors
        al::HitSensorKeeper* keeper = mTarget->getHitSensorKeeper();
        if (keeper && cubeMaxDim > 0.001f) {
            sead::Vector3f bmin(FLT_MAX, FLT_MAX, FLT_MAX);
            sead::Vector3f bmax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
            const sead::Vector3f& actorPos = al::getTrans(mTarget);
            bool found = false;

            for (s32 i = 0; i < keeper->getSensorNum(); i++) {
                al::HitSensor* s = keeper->getSensor(i);
                if (!s || !al::isSensorEnemyBody(s) || !al::isEqualSubString(s->mName, "Body")) continue;

                const sead::Vector3f& sPos = al::getSensorPos(s);
                if (sPos.x == 0.0f && sPos.y == 0.0f && sPos.z == 0.0f) continue;

                sead::Vector3f offset = sPos - actorPos;
                f32 r = al::getSensorRadius(s);
                bmin.x = sead::Mathf::min(bmin.x, offset.x - r);
                bmin.y = sead::Mathf::min(bmin.y, offset.y - r);
                bmin.z = sead::Mathf::min(bmin.z, offset.z - r);
                bmax.x = sead::Mathf::max(bmax.x, offset.x + r);
                bmax.y = sead::Mathf::max(bmax.y, offset.y + r);
                bmax.z = sead::Mathf::max(bmax.z, offset.z + r);
                found = true;
            }

            sead::BoundBox3f enemyBox;
            if (found) enemyBox = sead::BoundBox3f(bmin, bmax);
            else al::calcModelBoundingBox(&enemyBox, mTarget);

            f32 enemyAvgDim = (enemyBox.getSizeX() + enemyBox.getSizeY() + enemyBox.getSizeZ()) / 3.0f;
            mScale = sead::Mathf::max(enemyAvgDim / cubeMaxDim, kMinScale);
        }

        // Apply visual scale
        al::setScaleAll(this, mScale);

        // Sensor matches cube visual
        al::setSensorRadius(this, "Body", cubeMaxDim * mScale * 0.5f);

        // Cache for the per-frame ground snap in syncToTarget()
        mHalfHeight = cubeBox.getSizeY() * mScale * 0.5f;
    }

    // Runs every frame via control()
    void syncToTarget() {
        if (!mTarget) return;

        sead::Vector3f pos = al::getTrans(mTarget);
        sead::Vector3f gravity = al::getGravity(mTarget);
        sead::Vector3f front;
        al::calcFrontDir(&front, mTarget);

        sead::Vector3f rayStart = pos - gravity;
        sead::Vector3f rayDelta = gravity * kGroundRayLength;
        sead::Vector3f groundPos, groundNormal;

        if (alCollisionUtil::getHitPosAndNormalOnArrow(mTarget, &groundPos, &groundNormal, rayStart, rayDelta, nullptr, nullptr)) {
            if ((groundPos - pos).dot(gravity) < mHalfHeight) pos = groundPos - (gravity * mHalfHeight);

            sead::Quatf quat;
            al::makeQuatUpFront(&quat, groundNormal, front);
            al::updatePoseQuat(this, quat);
        } else
            pos = pos - (gravity * mHalfHeight);

        al::setTrans(this, pos);
    }

    al::LiveActor* mTarget = nullptr;
    al::HitSensor* mAttacker = nullptr;
    f32 mScale = 1.0f;
    f32 mHalfHeight = 0.0f;
    bool mWasHit = false;
    bool mIsBreaking = false;
};