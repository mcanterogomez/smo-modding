#pragma once
#include "ModConfig.h"
#include "custom/.Globals.h"
#include "custom/.Nerves.h"

// Clears all spin state, sensors and effects
inline void cleanupSpinAttackState(al::LiveActor* actor) {
	isSpinActive = false;
	isNearCollectible = false;
	isNearTreasure = false;
	isNearSwoonedEnemy = false;

	spin.isGalaxy = false;
	spin.isFakeQueued = false;
	attackSensorRemaining = -1;
	attack = {};

	al::invalidateHitSensor(actor, "Punch");
	al::invalidateHitSensor(actor, "GalaxySpin");
	al::invalidateHitSensor(actor, "DoubleSpin");

	al::tryDeleteEffect(isMarioModel, "SpinAttack");
	al::tryStopSe(isMarioModel, "SpinAttack", -1, nullptr);
}

namespace PlayerSpinAttack {

	struct InputIsTriggerActionXexclusivelyHook : public mallow::hook::Trampoline<InputIsTriggerActionXexclusivelyHook> {
		static bool Callback(const al::LiveActor* actor, int port) {
			if (port == 100) return Orig(actor, PlayerFunction::getPlayerInputPort(actor));
			return Orig(actor, port)
				&& (isConfig()->attackButton == 'X' ? al::isPadTriggerY(port) : al::isPadTriggerX(port));
		}
	};

	// Impl and BindEnd are two entry points into the same decision
	template <int Variant>
	struct TryCapSpinHook : public mallow::hook::Trampoline<TryCapSpinHook<Variant>> {
		using Base = mallow::hook::Trampoline<TryCapSpinHook<Variant>>;
		static bool Callback(PlayerActorHakoniwa* player, bool a2) {
			bool newIsCarry = player->mCarryKeeper->isCarry();
			if (newIsCarry && !prevIsCarry) { prevIsCarry = newIsCarry; return false; }
			prevIsCarry = newIsCarry;

			if (isActionBusy()) return false;

			bool canThrow = !rs::is2D(player) && !PlayerEquipmentFunction::isEquipmentNoCapThrow(player->mEquipmentUser);

			#ifndef ALLOW_CAPPY_ONLY
				if (!isSpinRethrow) spin.resetForNewSpin();

				if (isPadTriggerGalaxySpin(-1) && canThrow) {
					if (isSpinAnim(player->mAnimator) || isPunchAnim(player->mAnimator)) return false;

					spin.trigger = true;
					return true;
				}
			#endif

			if ((al::isPadTriggerR(-1) || al::isPadHoldZR(-1)) && canThrow && !newIsCarry) canAction = true;

			if (Base::Orig(player, a2)) { spin.trigger = false; return true; }
			return false;
		}
	};

	struct PlayerSpinCapAttackAppear : public mallow::hook::Trampoline<PlayerSpinCapAttackAppear> {
		static void Callback(PlayerStateSpinCap* state) {
			bool isGrounded = rs::isOnGround(state->mActor, state->mCollider) && !state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val2);
			bool forcedGroundSpin = state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val33);

			// No trigger: plain cap throw
			if (!spin.trigger) {
				spin.canStandard = false;
				spin.isGalaxy = false;
				Orig(state);
				return;
			}

			spin.isGalaxy = true;
			spin.canGalaxy = false;
			spin.trigger = false;

			Orig(state); // Vanilla appear() does every reset, velocity align, move control appear and _78 — only its nerve is wrong

			if (forcedGroundSpin || isGrounded) {
				if (!isGrounded) state->mActionGroundMoveControl->appear(); // vanilla took the air path
				al::setNerve(state, &GalaxySpinGround);
			} else al::setNerve(state, &GalaxySpinAir);
		}
	};

	struct PlayerStateSpinCapKill : public mallow::hook::Trampoline<PlayerStateSpinCapKill> {
		static void Callback(PlayerStateSpinCap* state) {
			Orig(state);
			cleanupSpinAttackState(state->mActor);
		}
	};

	struct PlayerStateSpinCapFall : public mallow::hook::Trampoline<PlayerStateSpinCapFall> {
		static void Callback(PlayerStateSpinCap* state) {
			Orig(state);

			// Landed mid-fakethrow: go to ground spin without restarting the anim
			if ((spin.isFakeQueued || attackSensorRemaining > 0) && state->mAnimator->isAnim("SpinSeparate")
				&& rs::isOnGround(state->mActor, state->mCollider)) {
				state->mActionGroundMoveControl->appear();
				al::setNerve(state, &GalaxySpinGround);
				return;
			}

			// Fakespin start while still airborne, its hit timer ends it
			if (spin.isFakeQueued) {
				spin.isFakeQueued = false;
				startAttack(static_cast<PlayerActorHakoniwa*>(state->mActor), "SpinSeparate", "GalaxySpin", 0, 21);
			}
		}
	};

	struct PlayerStateSpinCapIsEnableCancelHipDrop : public mallow::hook::Trampoline<PlayerStateSpinCapIsEnableCancelHipDrop> {
		static bool Callback(PlayerStateSpinCap* state) {
			return Orig(state) || (al::isNerve(state, &GalaxySpinAir) && al::isGreaterStep(state, 10));
		}
	};

	struct PlayerStateSpinCapIsEnableCancelAir : public mallow::hook::Trampoline<PlayerStateSpinCapIsEnableCancelAir> {
		static bool Callback(PlayerStateSpinCap* state) {
			return Orig(state) && !(!state->mIsDead && al::isNerve(state, &GalaxySpinAir) && al::isLessEqualStep(state, 22));
		}
	};

	struct PlayerStateSpinCapIsSpinAttackAir : public mallow::hook::Trampoline<PlayerStateSpinCapIsSpinAttackAir> {
		static bool Callback(PlayerStateSpinCap* state) {
			return Orig(state) || (!state->mIsDead && al::isNerve(state, &GalaxySpinAir) && al::isLessEqualStep(state, 22));
		}
	};

	struct PlayerStateSpinCapIsEnableCancelGround : public mallow::hook::Trampoline<PlayerStateSpinCapIsEnableCancelGround> {
		static bool Callback(PlayerStateSpinCap* state) {
			return Orig(state) || (al::isNerve(state, &GalaxySpinGround) && isSpinAnim(state->mAnimator) && !state->mAnimator->isAnim("SpinLow") && al::isGreaterStep(state, 10));
		}
	};

	struct PlayerSpinCapAttackIsSeparateSingleSpin : public mallow::hook::Trampoline<PlayerSpinCapAttackIsSeparateSingleSpin> {
		static bool Callback(PlayerStateSwim* thisPtr) { return spin.trigger || Orig(thisPtr); }
	};

	// Underwater and surface swim spin share one nerve body
	template <int Variant>
	struct SwimSpinCapHook : public mallow::hook::Trampoline<SwimSpinCapHook<Variant>> {
		using Base = mallow::hook::Trampoline<SwimSpinCapHook<Variant>>;
		static void Callback(PlayerStateSwim* thisPtr) {
			Base::Orig(thisPtr);

			auto* anim = isHakoniwa->mAnimator;
			bool isPunch = anim->isAnim("PunchR") || anim->isAnim("PunchL");
			bool isHoming = anim->isAnim("RabbitGet") || anim->isAnim("Kick");

			// New attack: reset the hit state
			if (spin.trigger && al::isFirstStep(thisPtr)) {
				spin.trigger = false;
				spin.isGalaxy = !isPunch;
				isSpinActive = true;
			}

			if (isPunch || anim->isAnim("SwingAttack")) applyLunge(isHakoniwa); // same lunge as on land

			if (isHoming && al::isFirstStep(thisPtr)) applyHomeIn(isHakoniwa, isNearTarget); // same homing as on land, once at the start
		}
	};

	struct PlayerStateSwimKill : public mallow::hook::Trampoline<PlayerStateSwimKill> {
		static void Callback(PlayerStateSwim* state) {
			Orig(state);
			cleanupSpinAttackState(state->mActor);
		}
	};

	// Underwater and surface variants of the same anim start
	template <int Variant>
	struct StartSpinSeparateSwimHook : public mallow::hook::Trampoline<StartSpinSeparateSwimHook<Variant>> {
		using Base = mallow::hook::Trampoline<StartSpinSeparateSwimHook<Variant>>;
		static void Callback(PlayerSpinCapAttack* thisPtr, PlayerAnimator* animator) {
			if (!spin.isGalaxy && !spin.trigger) { Base::Orig(thisPtr, animator); return; }

			bool isGround = rs::isOnGround(isHakoniwa, isHakoniwa->mCollider);

			if (isGround && isNearCollectible) startAttack(isHakoniwa, "RabbitGet", "Punch", 7, -1); // pull and kick only when standing
			else if (isGround && (isNearTreasure || isNearSwoonedEnemy)) startAttack(isHakoniwa, "Kick", "Punch", 2, -1);
			else if (isFeather || (!isGround && isMario && isCapeOn)) startAttack(isHakoniwa, "CapeAttack", "GalaxySpin", 0, 16);
			else if (isTanooki) startAttack(isHakoniwa, "TailAttack", "GalaxySpin", 0, 16);
			else if (isGround && PlayerWeapon::isAttack()) startAttack(isHakoniwa, "SwingAttack", "GalaxySpin", 4, 16);
			else if (isGround && !isConfig()->spinOnly) { isPunchRight = !isPunchRight; startAttack(isHakoniwa, isPunchRight ? "PunchR" : "PunchL", "Punch", 6, -1); }
			else if (PlayerWeapon::isAttack()) startAttack(isHakoniwa, "SwingAirAttack", "GalaxySpin", 4, 16);
			else {
				startAttack(isHakoniwa, "SpinSeparateSwim", "GalaxySpin", 0, 32);
				isGalaxySfx(isHakoniwa);
				return;
			}
			animator->setAnimRate(0.5f);
			animator->setSubAnimRate(0.5f);
		}
	};

	struct DisallowCancelOnUnderwaterSpinPatch : public mallow::hook::Inline<DisallowCancelOnUnderwaterSpinPatch> {
		static void Callback(exl::hook::InlineCtx* ctx) {
			if (spin.isGalaxy) ctx->W[20] = true;
		}
	};

	struct DisallowCancelOnWaterSurfaceSpinPatch : public mallow::hook::Inline<DisallowCancelOnWaterSurfaceSpinPatch> {
		static void Callback(exl::hook::InlineCtx* ctx) {
			if (spin.isGalaxy) ctx->W[21] = true;
		}
	};

	static void tryCapSpinAndRethrow(PlayerActorHakoniwa* player, bool a2) {
		if (isJumpPunchAnim(player->mAnimator)) spin.canGalaxy = true;

		isSpinRethrow = true;
		bool trySpin = player->tryActionCapSpinAttackImpl(a2);
		isSpinRethrow = false;
		if (!trySpin) return;

		if (isPadTriggerGalaxySpin(-1)) {
			if (spin.isGalaxy && (spin.isFakeQueued || player->mAnimator->isAnim("SpinSeparate"))) return;
			if (!spin.canGalaxy) { spin.isFakeQueued = true; return; }

			startSpinCap(player);
			return;
		}

		if (!spin.canStandard) return;
		if (spin.isGalaxy) spin.canGalaxy = false; // a cap throw after a galaxy spin uses up the galaxy
		startSpinCap(player, false);
	}

	// Shared squat and roll trigger
	static bool TriggerSpinFromState(PlayerActorHakoniwa* thisPtr) {
		if (!isPadTriggerGalaxySpin(-1) || isSpinAnim(thisPtr->mAnimator)) return false;

		if ((isMario || isBrawl)
			&& (al::isPadTriggerZR(-1) || al::isPadHoldZR(-1))
			&& isHammer && al::isDead(isHammer)) al::setNerve(thisPtr, &HammerNrv);
		else startSpinCap(thisPtr);
		return true;
	}

	// Block squat when doing certain actions
	struct PlayerJudgeStartSquatHook : public mallow::hook::Trampoline<PlayerJudgeStartSquatHook> {
		static bool Callback(void* thisPtr) {
			if (isActionBusy() || (isDrill && isHakoniwa->mHackCap->isPutOn() && al::isPadHoldZR(-1))) return false;

			#ifndef ALLOW_CAPPY_ONLY
				// Squat only — this judge also runs in Wait/Run/Jump
				if (al::isNerve(isHakoniwa, getNerveAt(nrvHakoniwaSquat)) && TriggerSpinFromState(isHakoniwa)) return false;
			#endif

			return Orig(thisPtr);
		}
	};

	struct PlayerActorHakoniwaExeRolling : public mallow::hook::Trampoline<PlayerActorHakoniwaExeRolling> {
		static void Callback(PlayerActorHakoniwa* thisPtr) {
			if (TriggerSpinFromState(thisPtr)) return;
			Orig(thisPtr);
		}
	};

	struct PlayerCarryKeeperStartThrowNoSpin : public mallow::hook::Trampoline<PlayerCarryKeeperStartThrowNoSpin> {
		static bool Callback(PlayerCarryKeeper* state) {
			if (isSpinActive || attackSensorRemaining != -1 || spin.isFakeQueued) return false;
			return Orig(state);
		}
	};

	// Land and water share one rule: no carry spin in a galaxy spin, one about to start, or once the galaxy is used
	template <int Variant>
	struct PlayerCarryKeeperIsCarryDuringSpin : public mallow::hook::Inline<PlayerCarryKeeperIsCarryDuringSpin<Variant>> {
		static void Callback(exl::hook::InlineCtx* ctx) {
			if (ctx->X[0] && (spin.isGalaxy || spin.trigger || !spin.canGalaxy)) ctx->X[0] = false;
		}
	};

	inline void Install() {

		TryCapSpinHook<0>::InstallAtSymbol("_ZN19PlayerActorHakoniwa26tryActionCapSpinAttackImplEb");
		TryCapSpinHook<1>::InstallAtSymbol("_ZN19PlayerActorHakoniwa29tryActionCapSpinAttackBindEndEv");
		PlayerJudgeStartSquatHook::InstallAtSymbol("_ZNK21PlayerJudgeStartSquat5judgeEv");

		#ifndef ALLOW_CAPPY_ONLY
			// Modify triggers
			InputIsTriggerActionXexclusivelyHook::InstallAtSymbol("_ZN19PlayerInputFunction15isTriggerActionEPKN2al9LiveActorEi");

			// Trigger spin instead of cap throw
			PlayerSpinCapAttackAppear::InstallAtSymbol("_ZN18PlayerStateSpinCap6appearEv");
			PlayerStateSpinCapKill::InstallAtSymbol("_ZN18PlayerStateSpinCap4killEv");
			PlayerStateSpinCapFall::InstallAtSymbol("_ZN18PlayerStateSpinCap7exeFallEv");
			PlayerStateSpinCapIsEnableCancelHipDrop::InstallAtSymbol("_ZNK18PlayerStateSpinCap21isEnableCancelHipDropEv");
			PlayerStateSpinCapIsEnableCancelAir::InstallAtSymbol("_ZNK18PlayerStateSpinCap17isEnableCancelAirEv");
			PlayerStateSpinCapIsSpinAttackAir::InstallAtSymbol("_ZNK18PlayerStateSpinCap15isSpinAttackAirEv");
			PlayerStateSpinCapIsEnableCancelGround::InstallAtSymbol("_ZNK18PlayerStateSpinCap20isEnableCancelGroundEv");
			PlayerSpinCapAttackIsSeparateSingleSpin::InstallAtSymbol("_ZNK19PlayerSpinCapAttack20isSeparateSingleSpinEv");
			SwimSpinCapHook<0>::InstallAtSymbol("_ZN15PlayerStateSwim14exeSwimSpinCapEv");
			SwimSpinCapHook<1>::InstallAtSymbol("_ZN15PlayerStateSwim21exeSwimSpinCapSurfaceEv");
			PlayerStateSwimKill::InstallAtSymbol("_ZN15PlayerStateSwim4killEv");
			StartSpinSeparateSwimHook<0>::InstallAtSymbol("_ZN19PlayerSpinCapAttack28startSpinSeparateSwimSurfaceEP14PlayerAnimator");
			StartSpinSeparateSwimHook<1>::InstallAtSymbol("_ZN19PlayerSpinCapAttack21startSpinSeparateSwimEP14PlayerAnimator");
			DisallowCancelOnUnderwaterSpinPatch::InstallAtOffset(0x489F30);
			DisallowCancelOnWaterSurfaceSpinPatch::InstallAtOffset(0x48A3C8);

			// Allow triggering spin on roll and squat
			PlayerActorHakoniwaExeRolling::InstallAtSymbol("_ZN19PlayerActorHakoniwa10exeRollingEv");

			// Allow carrying an object during a GalaxySpin
			PlayerCarryKeeperStartThrowNoSpin::InstallAtSymbol("_ZN17PlayerCarryKeeper10startThrowEb");
			PlayerCarryKeeperIsCarryDuringSpin<0>::InstallAtOffset(0x423A24);
			PlayerCarryKeeperIsCarryDuringSpin<1>::InstallAtOffset(0x489EE8);

			// Allow triggering another spin while falling from a spin
			exl::patch::CodePatcher fakethrowPatcher(0x423B80);
			for (int i = 0; i < 4; i++) fakethrowPatcher.WriteInst(exl::armv8::inst::Nop());
			fakethrowPatcher.Seek(0x423B9C);
			fakethrowPatcher.BranchInst(reinterpret_cast<void*>(&tryCapSpinAndRethrow));

			// Manually allow hacks and "special things" to use Y button
			exl::patch::CodePatcher yButtonPatcher(0x44C9FC);
			yButtonPatcher.WriteInst(exl::armv8::inst::Movz(exl::armv8::reg::W1, 100));  // isTriggerHackAction
			yButtonPatcher.Seek(0x44C718);
			yButtonPatcher.WriteInst(exl::armv8::inst::Movz(exl::armv8::reg::W1, 100));  // isTriggerAction
			yButtonPatcher.Seek(0x44C5F0);
			yButtonPatcher.WriteInst(exl::armv8::inst::Movz(exl::armv8::reg::W1, 100));  // isTriggerCarryStart
		#endif
	}
}