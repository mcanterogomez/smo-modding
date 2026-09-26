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
	spin.fakethrowRemainder = -1;
	attackSensorRemaining = -1;

	al::invalidateHitSensor(actor, "Punch");
	al::invalidateHitSensor(actor, "GalaxySpin");
	al::invalidateHitSensor(actor, "DoubleSpin");

	auto* model = isHakoniwa->mModelHolder->findModelActor("Normal");
	al::tryDeleteEffect(model, "SpinAttack");
	al::tryStopSe(model, "SpinAttack", -1, nullptr);
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

			#ifndef ALLOW_CAPPY_ONLY
				if (!isSpinRethrow) spin.resetForNewSpin();

				if (isPadTriggerGalaxySpin(-1)
					&& !rs::is2D(player)
					&& !PlayerEquipmentFunction::isEquipmentNoCapThrow(player->mEquipmentUser)
				) {
					if (isSpinAnim(player->mAnimator) || isPunchAnim(player->mAnimator)) return false;

					spin.trigger = true;
					if (!spin.canGalaxy) spin.fakethrowRemainder = -2; // no galaxy left, queue a fake
					return true;
				}
			#endif

			if ((al::isPadTriggerR(-1) || al::isPadHoldZR(-1))
				&& !rs::is2D(player)
				&& !newIsCarry
				&& !PlayerEquipmentFunction::isEquipmentNoCapThrow(player->mEquipmentUser)) canAction = true;

			if (Base::Orig(player, a2)) { spin.trigger = false; return true; }
			return false;
		}
	};

	struct PlayerSpinCapAttackAppear : public mallow::hook::Trampoline<PlayerSpinCapAttackAppear> {
		static void Callback(PlayerStateSpinCap* state) {
			bool isGrounded = rs::isOnGround(state->mActor, state->mCollider) && !state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val2);
			bool forcedGroundSpin = state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val33);

			// Clear leftover fakethrow state from an area load mid-spin
			if (spin.fakethrowRemainder != -1
				&& !al::isNerve(state, &GalaxySpinGround)
				&& !al::isNerve(state, &GalaxySpinAir)
			) {
				spin.fakethrowRemainder = -1;
				spin.isGalaxy = false;
				// DO NOT reset spin.trigger here!
			}

			// Cross-spin transition queued by tryCapSpinAndRethrow
			if (spin.queued == isSpin::Galaxy) { spin.canStandard = false; spin.trigger = true; }
			else if (spin.queued == isSpin::Standard) { spin.canGalaxy = false; spin.trigger = false; }
			spin.queued = isSpin::None;

			// No trigger: plain cap throw
			if (!spin.trigger) {
				spin.canStandard = false;
				spin.isGalaxy = false;
				Orig(state);
				return;
			}

			hitBufferCount = 0;
			spin.isGalaxy = true;
			spin.canGalaxy = false;
			spin.trigger = false;

			Orig(state); // Vanilla appear() does every reset, velocity align, move control appear and _78 — only its nerve is wrong

			if (forcedGroundSpin || isGrounded) {
				if (!isGrounded) state->mActionGroundMoveControl->appear(); // vanilla took the air path
				al::setNerve(state, &GalaxySpinGround);
			} else {
				// -2 means a fakespin is queued, route straight to fall
				al::setNerve(state, spin.fakethrowRemainder == -2 ? getNerveAt(nrvSpinCapFall) : &GalaxySpinAir);
			}
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
			if (spin.fakethrowRemainder != -1 && state->mAnimator->isAnim("SpinSeparate")
				&& rs::isOnGround(state->mActor, state->mCollider)) {
				state->mActionGroundMoveControl->appear();
				al::setNerve(state, &GalaxySpinGround);
				return;
			}

			// Fakespin timer while still airborne
			if (spin.fakethrowRemainder == -2) {
				spin.fakethrowRemainder = 21;
				hitBufferCount = 0;
				al::validateHitSensor(state->mActor, "GalaxySpin");
				state->mAnimator->startAnim("SpinSeparate");
				attackSensorRemaining = 21;
			}
			else if (spin.fakethrowRemainder > 0) spin.fakethrowRemainder--;
			else if (spin.fakethrowRemainder == 0) spin.fakethrowRemainder = -1;
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
			float frame = anim->getAnimFrame();

			// New attack: reset the hit state, spins hit straight away
			if (spin.trigger && al::isFirstStep(thisPtr)) {
				spin.trigger = false;
				spin.isGalaxy = !isPunch;
				isSpinActive = true;
				hitBufferCount = 0;
				if (!isPunchAnim(anim)) {
					attackSensorRemaining = 32;
					if (!isSwingAnim(anim)) al::validateHitSensor(thisPtr->mActor, "GalaxySpin");
				}
			}

			// Lunges
			if (isPunch) applyLunge(isHakoniwa, 5.0f, 5.0f);
			if (anim->isAnim("SwingAttack")) applyLunge(isHakoniwa, 2.0f, 5.0f);

			// Everything else hits once its windup is over, like on land
			if (isSwingAnim(anim) && frame == 4.0f) al::validateHitSensor(thisPtr->mActor, "GalaxySpin");
			else if (isPunch && frame >= 6.0f) { al::validateHitSensor(thisPtr->mActor, "Punch"); attackSensorRemaining = 6; }
			else if (isHoming && frame >= (anim->isAnim("RabbitGet") ? 7.0f : 2.0f)) { al::validateHitSensor(thisPtr->mActor, "Punch"); attackSensorRemaining = 15; }

			if (isHoming) applyHomeIn(isHakoniwa, isNearTarget); // same homing as on land
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

			if (isNearCollectible) animator->startAnim("RabbitGet");
			else if (isNearTreasure || isNearSwoonedEnemy) animator->startAnim("Kick");
			else if (isFeather || (!isGround && isMario && isCapeOn)) animator->startAnim("CapeAttack");
			else if (isTanooki) animator->startAnim("TailAttack");
			else if (isGround) {
				if (PlayerWeapon::isAttack()) animator->startAnim("SwingAttack");
				else { isPunchRight = !isPunchRight; animator->startAnim(isPunchRight ? "PunchR" : "PunchL"); }
			}
			else if (PlayerWeapon::isAttack()) animator->startAnim("SwingAirAttack");
			else {
				animator->startAnim("SpinSeparateSwim");
				isGalaxySfx(isHakoniwa);
				return;
			}
			animator->setAnimRate(0.5f);
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
			if (spin.isGalaxy && (spin.fakethrowRemainder != -1 || player->mAnimator->isAnim("SpinSeparate"))) return;
			if (!spin.canGalaxy) { spin.fakethrowRemainder = -2; return; }

			al::setNerve(player, getNerveAt(spinCapNrvOffset));
			if (!spin.isGalaxy && !spin.canStandard) spin.queued = isSpin::Galaxy;
			return;
		}

		if (!spin.canStandard) return;
		al::setNerve(player, getNerveAt(spinCapNrvOffset));
		if (spin.isGalaxy) spin.queued = isSpin::Standard;
	}

	// Shared squat and roll trigger
	static bool TriggerSpinFromState(PlayerActorHakoniwa* thisPtr) {
		if (!isPadTriggerGalaxySpin(-1) || isSpinAnim(thisPtr->mAnimator)) return false;

		if ((isMario || isBrawl)
			&& (al::isPadTriggerZR(-1) || al::isPadHoldZR(-1))
			&& isHammer && al::isDead(isHammer)) al::setNerve(thisPtr, &HammerNrv);
		else {
			spin.trigger = true;
			al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
		}
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
			if (isSpinActive || attackSensorRemaining != -1 || spin.fakethrowRemainder != -1) return false;
			return Orig(state);
		}
	};

	struct PlayerCarryKeeperIsCarryDuringSpin : public mallow::hook::Inline<PlayerCarryKeeperIsCarryDuringSpin> {
		static void Callback(exl::hook::InlineCtx* ctx) {
			// In a galaxy spin, or already finished one while still airborne
			if (ctx->X[0] && (spin.isGalaxy || !spin.canGalaxy)) ctx->X[0] = false;
		}
	};

	struct PlayerCarryKeeperIsCarryDuringSwimSpin : public mallow::hook::Inline<PlayerCarryKeeperIsCarryDuringSwimSpin> {
		static void Callback(exl::hook::InlineCtx* ctx) {
			// In a galaxy spin
			if (ctx->X[0] && (spin.isGalaxy || spin.trigger)) ctx->X[0] = false;
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
			PlayerCarryKeeperIsCarryDuringSpin::InstallAtOffset(0x423A24);
			PlayerCarryKeeperIsCarryDuringSwimSpin::InstallAtOffset(0x489EE8);

			// Allow triggering another spin while falling from a spin
			exl::patch::CodePatcher fakethrowPatcher(0x423B80);
			fakethrowPatcher.WriteInst(0x1F2003D5);  // NOP
			fakethrowPatcher.WriteInst(0x1F2003D5);  // NOP
			fakethrowPatcher.WriteInst(0x1F2003D5);  // NOP
			fakethrowPatcher.WriteInst(0x1F2003D5);  // NOP
			fakethrowPatcher.Seek(0x423B9C);
			fakethrowPatcher.BranchInst(reinterpret_cast<void*>(&tryCapSpinAndRethrow));

			// Manually allow hacks and "special things" to use Y button
			exl::patch::CodePatcher yButtonPatcher(0x44C9FC);
			yButtonPatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W1, 100));  // isTriggerHackAction
			yButtonPatcher.Seek(0x44C718);
			yButtonPatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W1, 100));  // isTriggerAction
			yButtonPatcher.Seek(0x44C5F0);
			yButtonPatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W1, 100));  // isTriggerCarryStart
		#endif
	}
}