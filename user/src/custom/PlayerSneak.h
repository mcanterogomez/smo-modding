#pragma once
#include "custom/.Globals.h"

struct PlayerAnimControlRun { PlayerAnimator* mAnimator; };

namespace PlayerSneak {

	// Actors asking where the player is get a position far away while sneaking, null means answer normally
	inline const sead::Vector3f* tryGetHiddenPos(const al::LiveActor* actor) {
		if (!isSneaking || actor == isHakoniwa) return nullptr;
		if (!hasSensor(actor, [](const al::HitSensor* s) { return al::isSensorEnemyBody(s) || al::isSensorNpc(s); })) return nullptr;
		if (actor->getPoseKeeper() && al::isFaceToTargetDegree(actor, al::getTrans(isHakoniwa), 50.0f)) return nullptr; // facing Mario, sneaking doesn't hide him

		static sead::Vector3f pos;
		pos = al::getTrans(actor) + sead::Vector3f(100000.0f, 100000.0f, 100000.0f);
		return &pos;
	}

	// Swaps Walk to WalkSoft while the stick is held lightly, and slows Mario to a sneak
	struct AnimControlRunUpdateHook : public mallow::hook::Trampoline<AnimControlRunUpdateHook> {
		static void Callback(PlayerAnimControlRun* thisPtr, f32 speed, const sead::Vector3f& moveInput) {
			Orig(thisPtr, speed, moveInput);

			static float t = 1.0f;
			if (al::isFirstStep(isHakoniwa)) t = 1.0f; // clear stale blend when movement restarts
			isSneaking = moveInput.length() < 0.25f; // 0.25f: stick tilt below this counts as "slow"

			t = al::converge(t, isSneaking ? 0.0f : 1.0f, 0.1f); // 0.1f: blend speed once it commits
			if (isSneaking) al::limitVelocity(isHakoniwa, isHakoniwa->mConst->getNormalMinSpeed() * 2.5f); // 2.5f: sneak speed, lower is slower

			float* w = thisPtr->mAnimator->mSklAnimBlendWeights;
			if (w[0] <= 0.0f) return; // Run/Dash/DashFast tier, no Walk to split
			thisPtr->mAnimator->setBlendWeight(w[0] * t, w[1], w[2], w[3], w[4], w[0] * (1.0f - t));
		}
	};

	struct RsPlayerPosHook : public mallow::hook::Trampoline<RsPlayerPosHook> {
		static const sead::Vector3f& Callback(const al::LiveActor* actor) {
			if (auto* pos = tryGetHiddenPos(actor)) return *pos;
			return Orig(actor);
		}
	};

	struct RsPlayerBodyPosHook : public mallow::hook::Trampoline<RsPlayerBodyPosHook> {
		static const sead::Vector3f& Callback(const al::LiveActor* actor) {
			if (auto* pos = tryGetHiddenPos(actor)) return *pos;
			return Orig(actor);
		}
	};

	struct AlPlayerPosHook : public mallow::hook::Trampoline<AlPlayerPosHook> {
		static const sead::Vector3f& Callback(const al::LiveActor* actor, int port) {
			if (auto* pos = tryGetHiddenPos(actor)) return *pos;
			return Orig(actor, port);
		}
	};

	struct AlNearestPlayerIdHook : public mallow::hook::Trampoline<AlNearestPlayerIdHook> {
		static s32 Callback(const al::LiveActor* actor, f32 dist) {
			if (tryGetHiddenPos(actor)) return -1;
			return Orig(actor, dist);
		}
	};

	inline void Install() {
		// Slow walk and animation blend
		AnimControlRunUpdateHook::InstallAtSymbol("_ZN20PlayerAnimControlRun6updateEfRKN4sead7Vector3IfEE");
		// Every way an actor can ask for the player's position
		RsPlayerPosHook::InstallAtSymbol("_ZN2rs12getPlayerPosEPKN2al9LiveActorE");
		RsPlayerBodyPosHook::InstallAtSymbol("_ZN2rs16getPlayerBodyPosEPKN2al9LiveActorE");
		AlPlayerPosHook::InstallAtSymbol("_ZN2al12getPlayerPosEPKNS_9LiveActorEi");
		AlNearestPlayerIdHook::InstallAtSymbol("_ZN2al19findNearestPlayerIdEPKNS_9LiveActorEf");
	}
}