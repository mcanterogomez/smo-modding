#pragma once
#include "ModConfig.h"
#include "custom/.Globals.h"

class PlayerSpinCapAttack;

inline bool isDefinitve() {
	static bool value = al::isExistFile("ObjectData/MarioDefinitve.txt");
	return value;
}

// The area/temperature wait the engine picked for where the player stands, or nullptr
inline const char* areaWaitAnim(const PlayerActorHakoniwa* player) {
	const void* stateWait = *reinterpret_cast<const void* const*>(reinterpret_cast<const u8*>(player) + 0x2D8);
	return *reinterpret_cast<const char* const*>(reinterpret_cast<const u8*>(stateWait) + 0x90);
}

// Playback scale for the player's animations, 1.0f leaves everything untouched — add new cases here
inline float animRate(const PlayerAnimator* anim = nullptr) {
	if (!isHakoniwa || (anim && anim != isHakoniwa->mAnimator)) return 1.0f;
	if (isMetal && isSubmerged(isHakoniwa)) return 0.5f;
	return 1.0f;
}

namespace CustomAnimation {

	// Suit-specific replacement for an anim name, or nullptr to keep the original
	inline const char* remapAnim(const char* name, const PlayerAnimator* anim = nullptr) {
		if (!isHakoniwa || (anim && anim != isHakoniwa->mAnimator) || rs::isPlayer2D(isHakoniwa)) return nullptr;

		auto is = [&](const char* other) { return al::isEqualString(name, other); };

		if (isWeaponOn && is("Wait")) return "BattleWait";

		if (isTanooki) {
			if (is("Glide")) return "GlideAlt";
			if (is("JumpBroad8")) return "JumpBroad8Alt";
		}
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
			if (is("BattleWait")) return "WaitBrawlFight";
			if (is("JumpDashFast")) return "Jump";
			if (is("Wait")) return "WaitBrawl";
			if (is("WearEnd")) return "WearEndBrawl";
		}
		if (isSuper) {
			if (is("BattleWait")) return "WaitSuperFight";
			if (is("GlideFloat")) return "GlideFloatSuper";
			if (is("Wait")) return "WaitSuper";
			if (is("WearEnd")) return "WearEndSuper";
		}

		bool isClassic = !isFeather && !isTanooki && !isFly && !isBrawl && !isSuper;
		if (isClassic && is("JumpDashFast")) return "JumpDashFastClassic";

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

		if (is("BattleWait")) return "WaitBrawl";

		#ifdef ALLOW_DASH
			if (is("Move") || is("MoveMoon")) {
				if (isMetal || isBrawl) return "MoveBrawl";
				if (isSuper) return "MoveSuper";
				if (isClassic) return "MoveClassic";
			}
		#endif

		return nullptr;
	}

	inline int idlePlayed = 0; // anims played this idle, reset when the nerve re-enters

	// Idle-cycle anim for the current suit, or nullptr when spent — add new suits here
	inline const char* idleCycleAnim(bool alt, const PlayerAnimator* anim = nullptr) {
		if (!isHakoniwa || (anim && anim != isHakoniwa->mAnimator)) return nullptr;
		if (areaWaitAnim(isHakoniwa)) return nullptr; // the engine owns the wait here, leave it alone
		if (!remapAnim("Wait", anim) && idlePlayed > 2) return nullptr; // Wait is untouched, so vanilla relax exists: each anim once, then one held wait before handing over

		if (isMario && isDefinitve()) return alt ? "AreaWaitView" : "AreaWaitStretch";
		//if (isBrawl) return alt ? "AreaWaitView" : "AreaWaitStretch";
		return nullptr;
	}

	// Every 720 idle frames, swaps Wait to the suit's cycle anim and back
	inline void updateIdleCycle(PlayerActorHakoniwa* thisPtr) {
		static int base = 0; // step the idle window started on
		static bool isCycling = false;
		auto* anim = thisPtr->mAnimator;
		int step = al::getNerveStep(thisPtr); // setNerve rewinds this, so a step below the mark means the nerve re-entered

		if (step < base || areaWaitAnim(thisPtr) || !al::isNerve(thisPtr, getNerveAt(nrvHakoniwaWait))) { base = step; idlePlayed = 0; isCycling = false; return; }
		if (isCycling) { if (anim->isAnimEnd()) { anim->startAnim("Wait"); base = step; isCycling = false; } return; }
		if (step - base < 720) return;
		if (isActionBusy()) { base = step; return; } // blast, guard and drill run inside this nerve, so hold the window open

		base = step; // window is consumed either way, so a spent cycle stops re-testing every frame
		const char* altAnim = idleCycleAnim(idlePlayed++ & 1); // post-increment, so the parity is the window index but the cap sees the new count
		if (!altAnim) return;

		anim->startAnim(altAnim);
		isCycling = true;
	}

	// Remaps the name, then pushes the rate down the sub anim path startAnim leaves untouched
	struct PlayerAnimatorStartAnimHook : public mallow::hook::Trampoline<PlayerAnimatorStartAnimHook> {
		static void Callback(PlayerAnimator* thisPtr, const sead::SafeString& animName) {
			const char* swapped = remapAnim(animName.cstr(), thisPtr);
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

			if ((al::isEqualString(model, "Mario2D") && isNew2D)
				#ifdef ALLOW_POWERUPS
					|| al::isEqualString(model, "MarioFeather2D")
					|| al::isEqualString(model, "MarioColorFire2D")
					|| al::isEqualString(model, "MarioColorIce2D")
					|| al::isEqualString(model, "MarioTanooki2D")
					|| al::isEqualString(model, "MarioDrill2D")
					|| al::isEqualString(model, "MarioColorMetal2D")
					|| al::isEqualString(model, "MarioColorFly2D")
					|| al::isEqualString(model, "MarioColorBrawl2D")
					|| al::isEqualString(model, "MarioColorSuper2D")
				#endif
			) ctx->X[25] = (u64)"PlayerAnimationNew2D";
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