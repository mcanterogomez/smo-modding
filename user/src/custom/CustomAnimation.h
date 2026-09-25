#pragma once
#include "ModConfig.h"
#include "custom/.Globals.h"
#include "custom/PlayerWeapon.h"

class PlayerSpinCapAttack;

// Mario Definitive model installed
inline bool isDefinitve() {
	static bool value = al::isExistFile("ObjectData/MarioDefinitve.txt");
	return value;
}

// The area/temperature wait the engine picked for where the player stands, or nullptr
inline const char* areaWaitAnim(const PlayerActorHakoniwa* player) {
	const void* stateWait = *reinterpret_cast<const void* const*>(reinterpret_cast<const u8*>(player) + 0x2D8);
	return *reinterpret_cast<const char* const*>(reinterpret_cast<const u8*>(stateWait) + 0x90);
}

// Wait, or the area wait the engine swapped in for it
inline bool isIdleAnim(const char* name) {
	const char* areaWait = isHakoniwa ? areaWaitAnim(isHakoniwa) : nullptr;
	return al::isEqualString(name, "Wait") || (areaWait && al::isEqualString(name, areaWait));
}

// The player is loaded and the animator is its own, nullptr skips the animator check
inline bool isPlayerAnim(const PlayerAnimator* anim) {
	return isHakoniwa && (!anim || anim == isHakoniwa->mAnimator);
}

// Playback scale for the player's animations, 1.0f leaves everything untouched — add new cases here
inline float animRate(const PlayerAnimator* anim = nullptr) {
	if (!isPlayerAnim(anim)) return 1.0f;
	if (isMetal && isSubmerged(isHakoniwa)) return 0.5f;
	return 1.0f;
}

namespace CustomAnimation {

	// =========================================================
	//                     BATTLE STANCE
	// =========================================================

	// An attack holds the stance for 10 seconds, a live enemy holds it only while it stays close
	inline void updateBattleStance(PlayerActorHakoniwa* thisPtr) {
		if (isBrawl || isSuper) battleStance = 0; // their idles are already fighting poses, BattleWait only plays when the game asks for it
		else if (isAttacking(thisPtr)) battleStance = 300; // 10 seconds at 60fps
		else if (isEnemyNear(thisPtr)) battleStance = battleStance ? battleStance : 1; // 1 runs out the frame the enemy is gone
		else if (battleStance > 0) battleStance--;
	}

	// =========================================================
	//                        REMAP
	// =========================================================

	// Suit-specific replacement for an anim name, or nullptr to keep the original
	inline const char* remapAnim(const char* name, const PlayerAnimator* anim = nullptr) {
		if (!isPlayerAnim(anim) || rs::isPlayer2D(isHakoniwa)) return nullptr;

		auto is = [&](const char* other) { return al::isEqualString(name, other); };

		// Battle stance goes first so it wins over every other idle: it turns Wait and area waits into BattleWait, and the suits below pick their own
		bool isStance = battleStance > 0 && isIdleAnim(name);
		if (isStance) name = "BattleWait";

		if (PlayerWeapon::isArmed() && is("Wait")) return "BlastWait"; // a swapped weapon is gone by the time Wait starts

		if (isFly) {
			if (is("GlideFloat")) return "GlideFloatSuper";
			if (is("Wait")) return "WaitSuper";
		}
		if (isMetal) {
			if (is("Wait")) return "BattleWait";
			if (is("JumpDashFast")) return "Jump";
			if (is("WearEnd")) return "WearEndSuper";
		}
		if (isBrawl) {
			if (is("BattleWait")) return "WaitSmashFight";
			if (is("JumpDashFast")) return "Jump";
			if (is("Wait")) return "WaitSmash";
			if (is("WearEnd")) return "WearEndSmash";
		}
		if (isSuper) {
			if (is("BattleWait")) return "WaitSuperFight";
			if (is("GlideFloat")) return "GlideFloatSuper";
			if (is("Wait")) return "WaitSuper";
			if (is("WearEnd")) return "WearEndSuper";
		}

		bool isClassicMove = !isFeather && !isTanooki && !isFly && !isBrawl && !isSuper;
		if (isClassicMove && is("JumpDashFast")) return "JumpDashFastClassic";

		// Suits that punch out of a hip drop
		bool isSuit = (isMario && isCapeOn) || isFeather || isFly || isBrawl || isSuper;
		if (isSuit) {
			if (is("HipDrop")) return "HipDropPunch";
			if (is("HipDropLand")) return "HipDropPunchLand";
			if (is("HipDropReaction")) return "HipDropPunchReaction";
			if (is("HipDropStart")) return "HipDropPunchStart";
			if (is("SwimDive")) return "SwimHipDropPunch";
			if (is("SwimHipDrop")) return "SwimHipDropPunch";
			if (is("SwimHipDropLand")) return "SwimHipDropPunchLand";
			if (is("SwimHipDropStart")) return "SwimHipDropPunchStart";
		}
		if (isSuit || isMetal) {
			if (is("LandStiffen")) return "LandSuper";
			if (is("MofumofuDemoOpening2")) return "MofumofuDemoOpening2Super";
		}

		#ifdef ALLOW_DASH
			if (is("Move") || is("MoveMoon")) {
				if (isMetal || isBrawl) return "MoveSmash";
				if (isSuper) return "MoveSuper";
				if (isClassic) return "MoveClassic";
			}
		#endif

		return isStance ? name : nullptr; // no suit swapped the stance, so it plays plain BattleWait
	}

	// =========================================================
	//                         IDLE
	// =========================================================

	inline int idlePlayed = 0; // anims played this idle, reset when the nerve re-enters

	// Idle-cycle anim for the current suit, or nullptr when spent — add new suits here
	inline const char* idleCycleAnim(bool alt, const PlayerAnimator* anim = nullptr) {
		if (!isPlayerAnim(anim) || areaWaitAnim(isHakoniwa)) return nullptr; // in an area wait the engine owns the idle, leave it alone
		if (!remapAnim("Wait", anim) && idlePlayed > 2) return nullptr; // Wait is untouched, so vanilla relax exists: each anim once, then one held wait before handing over

		if (isMario && isDefinitve()) return alt ? "AreaWaitView" : "AreaWaitStretch";
		if (isBrawl) return "WaitSmash01";
		return nullptr;
	}

	// Keeps the standing idle current, and every 720 idle frames swaps Wait to the suit's cycle anim and back; the battle stance holds the cycle off
	inline void updateIdle(PlayerActorHakoniwa* thisPtr) {
		static const char* played = "Wait"; // idle the current Wait was started on
		static int base = 0; // step the idle window started on
		static bool isCycling = false;
		auto* anim = thisPtr->mAnimator;
		int step = al::getNerveStep(thisPtr); // setNerve rewinds this, so a step below the mark means the nerve re-entered
		bool isWait = al::isNerve(thisPtr, getNerveAt(nrvHakoniwaWait));

		// Wait only picks its anim when it starts, so restart it when the idle changes under a standing player: stance, blaster, a new stage's state
		// An attack's first frame, guard, drill and cutscenes still read as Wait, so the restart waits until they're done
		const char* idle = remapAnim("Wait");
		if (!idle) idle = "Wait";
		if (!isWait) played = idle; // the next Wait starts on the right one anyway
		else if (!al::isEqualString(idle, played) && !isAttacking(thisPtr) && !isActionBusy() && !rs::isActiveDemo(thisPtr)) {
			played = idle;
			al::setNerve(thisPtr, getNerveAt(nrvHakoniwaWait));
			return;
		}

		if (step < base || battleStance > 0 || areaWaitAnim(thisPtr) || !isWait) { base = step; idlePlayed = 0; isCycling = false; return; }
		if (isCycling) { if (anim->isAnimEnd()) { anim->startAnim("Wait"); base = step; isCycling = false; } return; }
		if (step - base < 720) return;
		if (isActionBusy()) { base = step; return; } // blast, guard and drill run inside this nerve, so hold the window open

		base = step; // window is consumed either way, so a spent cycle stops re-testing every frame
		const char* altAnim = idleCycleAnim(idlePlayed++ & 1); // post-increment, so the parity is the window index but the cap sees the new count
		if (!altAnim) return;

		anim->startAnim(altAnim);
		isCycling = true;
	}

	// Per-frame update, called from the player's movement
	inline void update(PlayerActorHakoniwa* thisPtr) {
		updateBattleStance(thisPtr);
		updateIdle(thisPtr);
	}

	// =========================================================
	//                         HOOKS
	// =========================================================

	// Remaps the name, then pushes the rate down the sub anim path startAnim leaves untouched
	struct PlayerAnimatorStartAnimHook : public mallow::hook::Trampoline<PlayerAnimatorStartAnimHook> {
		static void Callback(PlayerAnimator* thisPtr, const sead::SafeString& animName) {
			const char* swapped = remapAnim(animName.cstr(), thisPtr);
			if (swapped && isIdleAnim(animName.cstr()) && al::isEqualString(thisPtr->mCurAnim, swapped)) return; // already looping, so never snap it back to frame 0

			Orig(thisPtr, swapped ? swapped : animName.cstr());
			if (animRate(thisPtr) != 1.0f) thisPtr->setSubAnimRate(1.0f);
		}
	};

	// startAnim ends in setAnimRateCommon, startSubAnim returns without it, so route it there
	struct PlayerAnimatorStartSubAnimHook : public mallow::hook::Trampoline<PlayerAnimatorStartSubAnimHook> {
		static void Callback(PlayerAnimator* thisPtr, const sead::SafeString& animName) {
			Orig(thisPtr, animName);
			if (animRate(thisPtr) != 1.0f) thisPtr->setSubAnimRate(1.0f);
		}
	};

	// Makes isAnim match remapped names; "Wait" reads false while remap or the idle cycle owns it, so the engine never triggers WaitRelaxStart
	struct PlayerAnimatorIsAnimHook : public mallow::hook::Trampoline<PlayerAnimatorIsAnimHook> {
		static bool Callback(PlayerAnimator* thisPtr, const sead::SafeString& animName) {
			const char* swapped = remapAnim(animName.cstr(), thisPtr);
			if (al::isEqualString(animName.cstr(), "Wait") && (swapped || idleCycleAnim(false, thisPtr))) return false;
			return Orig(thisPtr, swapped ? swapped : animName.cstr());
		}
	};

	// Every rate path funnels here: startAnim, setAnimRate, setSubAnimRate, PlayerAnimControlRun
	struct PlayerAnimatorSetAnimRateCommonHook : public mallow::hook::Trampoline<PlayerAnimatorSetAnimRateCommonHook> {
		static void Callback(PlayerAnimator* thisPtr, float rate) {
			float scale = animRate(thisPtr);
			if (scale != 1.0f) {
				rate *= scale;
				thisPtr->mAnimFrameCtrl->mRate = rate; // the counter isAnimEnd reads
			}
			Orig(thisPtr, rate);
		}
	};

	// The cap releases on al::isStep, a raw frame count, so the throw frame has to stretch with the anim
	template <int Slot>
	struct PlayerSpinCapAttackThrowFrameHook : public mallow::hook::Trampoline<PlayerSpinCapAttackThrowFrameHook<Slot>> {
		using Base = mallow::hook::Trampoline<PlayerSpinCapAttackThrowFrameHook<Slot>>;
		static s32 Callback(const PlayerSpinCapAttack* thisPtr) {
			s32 frame = Base::Orig(thisPtr);
			float scale = animRate();
			return scale != 1.0f ? (s32)(frame / scale) : frame;
		}
	};

	// HackCap is a separate actor with its own nerves, so PlayerAnimator never reaches it
	struct HackCapMovementHook : public mallow::hook::Trampoline<HackCapMovementHook> {
		static void Callback(HackCap* thisPtr) {
			Orig(thisPtr);
			if (alAnimFunction::getAllAnimName(thisPtr)) al::setActionFrameRate(thisPtr, animRate()); // null name = no anim yet; startAction resets the rate
		}
	};

	// Swap for new 2d animation archive
	struct PlayerAnimation2DArchiveHook : public mallow::hook::Inline<PlayerAnimation2DArchiveHook> {
		static void Callback(exl::hook::InlineCtx* ctx) {
			const char* model = (const char*)ctx->X[20];
			static bool isNew2D = al::isExistFile("ObjectData/MarioNew2D.txt");

			bool isSwap = isNew2D && al::isEqualString(model, "Mario2D");
			#ifdef ALLOW_POWERUPS
				static const char* suits[] = {"MarioColorBrawl2D", "MarioColorClassic2D", "MarioColorFire2D", "MarioColorFly2D", "MarioColorIce2D",
					"MarioColorMetal2D", "MarioColorSuper2D", "MarioDrill2D", "MarioFeather2D", "MarioTanooki2D"};
				for (const char* suit : suits) isSwap = isSwap || al::isEqualString(model, suit);
			#endif
			if (isSwap) ctx->X[25] = (u64)"PlayerAnimationNew2D";
		}
	};

	inline void Install() {
		#ifdef ALLOW_POWERUPS
			PlayerAnimatorStartAnimHook::InstallAtSymbol("_ZN14PlayerAnimator9startAnimERKN4sead14SafeStringBaseIcEE");
			PlayerAnimatorStartSubAnimHook::InstallAtSymbol("_ZN14PlayerAnimator12startSubAnimERKN4sead14SafeStringBaseIcEE");
			PlayerAnimatorIsAnimHook::InstallAtSymbol("_ZNK14PlayerAnimator6isAnimERKN4sead14SafeStringBaseIcEE");
			PlayerAnimatorSetAnimRateCommonHook::InstallAtSymbol("_ZN14PlayerAnimator17setAnimRateCommonEf");

			PlayerSpinCapAttackThrowFrameHook<0>::InstallAtSymbol("_ZNK19PlayerSpinCapAttack19getThrowFrameGroundEv");
			PlayerSpinCapAttackThrowFrameHook<1>::InstallAtSymbol("_ZNK19PlayerSpinCapAttack16getThrowFrameAirEv");
			HackCapMovementHook::InstallAtSymbol("_ZN7HackCap8movementEv");
		#endif
		PlayerAnimation2DArchiveHook::InstallAtOffset(0x445664);
	}
}