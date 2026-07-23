#pragma once

#include "custom/.Globals.h"
#include "headers/PlayerIceCube.h"
#include "Library/Collision/CollisionParts.h"
#include "Library/Collision/CollisionPartsTriangle.h"
#include "Library/Collision/CollisionPartsKeeperUtil.h"

namespace PlayerFreeze {

    struct FrozenEntry {
        al::LiveActor* actor = nullptr;
        PlayerIceCube* cube = nullptr;
        s32 timer = 0;
        const al::CollisionParts* floorParts = nullptr;
        sead::Vector3f lastFloorPos = {0.0f, 0.0f, 0.0f};
    };

    inline constexpr s32 kMaxFrozen = 32;
    inline FrozenEntry sFrozenList[kMaxFrozen];
    inline s32 sFrozenCount = 0;

    inline void clearAllFrozen() { sFrozenCount = 0; }

    inline FrozenEntry* findEntry(const al::LiveActor* actor) {
        for (s32 i = 0; i < sFrozenCount; i++)
            if (sFrozenList[i].actor == actor) return &sFrozenList[i];
        return nullptr;
    }

    inline bool isFrozen(const al::LiveActor* actor) { return actor && findEntry(actor); }

    // Raycast down from actor to find what platform it's standing on
    inline const al::CollisionParts* findFloorParts(const al::LiveActor* actor, sead::Vector3f* outFloorPos) {
        sead::Vector3f gravity = al::getGravity(actor);
        sead::Vector3f rayStart = al::getTrans(actor) - gravity;
        sead::Vector3f rayDir = gravity * 500.0f;
        sead::Vector3f hitPos;
        al::Triangle tri;
        if (alCollisionUtil::getFirstPolyOnArrow(actor, &hitPos, &tri, rayStart, rayDir, nullptr, nullptr) && tri.mCollisionParts) {
            if (outFloorPos) *outFloorPos = tri.mCollisionParts->getBaseMtx().getTranslation();
            return tri.mCollisionParts;
        }
        return nullptr;
    }

    // Remove entry by swapping with last
    inline void removeEntry(FrozenEntry* entry) {
        *entry = sFrozenList[--sFrozenCount];
    }

    inline void unfreezeActor(al::LiveActor* actor) {
        FrozenEntry* entry = findEntry(actor);
        if (!entry) return;
        if (entry->cube && al::isAlive(entry->cube)) entry->cube->unfreeze();
        if (actor && al::isAlive(actor)) {
            al::setActionFrameRate(actor, 1.0f);
            al::validateHitSensors(actor);
        }
        removeEntry(entry);
    }

    inline void freezeActor(al::LiveActor* actor, s32 duration) {
        if (!actor || isFrozen(actor) || sFrozenCount >= kMaxFrozen) return;
        PlayerIceCube* cube = nullptr;
        if (iceCubes) {
            cube = static_cast<PlayerIceCube*>(iceCubes->getDeadActor());
            if (cube) cube->freeze(actor);
        }
        sead::Vector3f floorPos = {0.0f, 0.0f, 0.0f};
        const al::CollisionParts* floor = findFloorParts(actor, &floorPos);
        sFrozenList[sFrozenCount++] = { actor, cube, duration, floor, floorPos };
        al::setActionFrameRate(actor, 0.0f);
        al::invalidateHitSensors(actor);
        al::deleteEffectAll(actor);
        al::tryStopAllSeFromUser(actor, 0, nullptr);
    }

    inline bool updateFrozenActor(al::LiveActor* actor) {
        FrozenEntry* entry = findEntry(actor);
        if (!entry) return false;

        // Actor died externally
        if (!actor || !al::isAlive(actor)) {
            if (entry->cube && al::isAlive(entry->cube)) entry->cube->makeActorDead();
            removeEntry(entry);
            return false;
        }

        // Cube gone (scene change, etc) -> unfreeze
        if (entry->cube && !al::isAlive(entry->cube)) {
            entry->cube = nullptr;
            unfreezeActor(actor);
            return false;
        }

        // Cube was hit -> damage enemy
        if (entry->cube && entry->cube->wasHit()) {
            al::HitSensor* attacker = entry->cube->getAttacker();
            unfreezeActor(actor);
            al::HitSensor* body = al::getHitSensor(actor, "Body");

            if (body && (al::sendMsgPlayerFireBallAttack(body, body)
                || rs::sendMsgFireBrosFireBallCollide(body, body)
                || rs::sendMsgHackAttack(body, body)
                || rs::sendMsgCapReflect(body, body)
                || rs::sendMsgCapAttack(body, body)
                || al::sendMsgPlayerObjHipDropReflect(body, body, nullptr)
                || al::sendMsgExplosion(body, body, nullptr)
            )) {
                sead::Vector3f dir = al::getTrans(actor) - al::getTrans(isHakoniwa);
                al::tryNormalizeOrZero(&dir);
                al::addVelocity(actor, dir * 12.5f);

                sead::Vector3f pos = (al::getTrans(actor) + al::getSensorPos(attacker)) * 0.5f;
                al::tryEmitEffect(isHakoniwa, "Hit", &pos);
            }

            hitBuffer[hitBufferCount++] = actor;
            return false;
        }

        // Follow moving platform
        sead::Vector3f curFloorPos = entry->lastFloorPos;
        const al::CollisionParts* curFloor = findFloorParts(actor, &curFloorPos);
        if (curFloor && curFloor == entry->floorParts) {
            sead::Vector3f delta = curFloorPos - entry->lastFloorPos;
            if (delta.squaredLength() > 0.0f) al::setTrans(actor, al::getTrans(actor) + delta);
        }
        entry->floorParts = curFloor;
        entry->lastFloorPos = curFloorPos;

        al::setActionFrameRate(actor, 0.0f);

        if (--entry->timer <= 0) {
            unfreezeActor(actor);
            return false;
        }
        return true;
    }

    inline bool handleReceiveMsg(const al::SensorMsg* msg, al::HitSensor* source) {
        if (!msg || !source) return false;
        al::LiveActor* attacker = al::getSensorHost(source);
        return attacker && isFrozen(attacker) && al::isMsgEnemyAttack(msg);
    }

}  // namespace PlayerFreeze