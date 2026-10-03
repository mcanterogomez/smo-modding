#pragma once
#include "custom/.Globals.h"
#include "Library/Obj/PartsModel.h"
#include "Player/PlayerJudgeSameNerve.h"

namespace PlayerWeapon {

	enum Mode { Swing, Stance, Held }; // Swing: out for the attack only, Stance: stays out through the battle stance, Held: hold right takes it out and it shoots

	struct Weapon {
		const char* attack = nullptr; // model used to attack
		const char* carry = nullptr; // model worn on the body while it is away
		const char* pose = nullptr; // hand action while it is out
		const char* sheathe = nullptr; // upper body anim that puts it back, null shrinks it away
		Mode mode = Swing;
	};

	// One weapon per suit, first match wins
	inline Weapon get() {
		if (isClassic) return {.attack = "Wrench", .carry = "BeltWrench", .pose = "GrabCeilWait"};
		if (isKnight) return {.attack = "Axe", .carry = "StrapAxe", .pose = "GrabCeilWait", .sheathe = "SwingSleeve", .mode = Stance};
		if (isMario || isMarioActive == -1) return {.attack = "Blaster", .pose = "AreaWaitDance03", .mode = Held}; // -1 is the power-up ending, so it still gets put away
		return {};
	}

	inline al::LiveActor* find(al::LiveActor* model, const char* name) {
		return name ? al::tryGetSubActor(model, name) : nullptr;
	}

	// Show the worn model, or hide it while the weapon stands in for it
	inline void showCarry(al::LiveActor* model, const char* name, bool show) {
		auto* carry = find(model, name);
		if (carry) show ? al::showModelIfHide(carry) : al::hideModelIfShow(carry);
	}

	inline constexpr float fade = 4.0f; // frames the weapon takes to grow and to shrink, lower is faster
	inline constexpr int sheatheFrame = 12; // frame of the sheathe anim where the hand reaches where it's worn

	inline void setSize(al::LiveActor* weapon, float scale) {
		auto* parts = static_cast<al::PartsModel*>(weapon); // a worn weapon rebuilds its pose each frame, so scale its local one
		parts->mLocalTrans *= scale / parts->mLocalScale.x; // its offset from the hand scales too, so it grows and shrinks at the hand
		parts->mIsUseLocalScale = true;
		parts->mLocalScale = {scale, scale, scale};
	}

	// Scale up as it comes out, down over the end of endAnim, null when the current anim is the last one
	inline float setFade(al::LiveActor* weapon, const PlayerAnimator* anim, float step, const char* endAnim = nullptr) {
		float scale = sead::Mathf::min(1.0f, step / fade);

		if (!endAnim || anim->isAnim(endAnim)) {
			float left = anim->getAnimFrameMax() - anim->getAnimFrame();
			if (attackSensorRemaining > 0 && attackSensorRemaining < left) left = attackSensorRemaining; // the sensor can end the attack first
			if (left < fade) scale = left / fade;
		}

		al::setScale(weapon, {scale, scale, scale});
		return scale;
	}

	// The game's own check for the cap catch arm anim (none while carrying, hanging, climbing...), with the members it passes
	inline bool isCatchAnimAllowed(PlayerActorHakoniwa* p) {
		auto check = (bool (*)(PlayerActorHakoniwa*, void*, void*, void*, void*, void*, void*))((((u64)malloc) - 0x00724b94) + 0x41f8f4);
		return check(p, p->mCarryKeeper, p->mModelChanger, p->mStateWait, p->mStateJump, p->mStateWallAir, p->mStateSwim);
	}

	// Hands busy carrying, hanging from a ledge, a ceiling or a pole, or held by an object (motorcycle, cannon, pipe...)
	inline bool isHandsBusy(PlayerActorHakoniwa* p) {
		PlayerInfo* info = p->getPlayerInfo();
		return p->mCarryKeeper->isCarry() || info->mJudgeNrvWallCatch->judge() || info->mJudgeNrvGrabCeil->judge() || info->mJudgeNrvPoleClimb->judge() || p->mBindKeeper->getBindSensor();
	}

	// Attacks use a held weapon once it's out, any other one only with the spin toggle on, else punch and air spin
	inline bool isAttack() {
		Weapon setup = get();
		return setup.mode == Held ? isWeaponOn : isConfig()->spinOnly && isMarioModel && find(isMarioModel, setup.attack);
	}

	// A held weapon is out, so it shows and shoots between attacks
	inline bool isArmed() { return isWeaponOn && get().mode == Held; }

	inline void update(PlayerActorHakoniwa* player, bool isActive) {
		Weapon setup = get();
		auto* weapon = find(isMarioModel, setup.attack);
		if (!weapon) { isWeaponOn = false; return; }

		auto* anim = player->mAnimator;
		bool isOut = al::isAlive(weapon);

		// Hold right to take it out or put it away, and it drops when the power-up ends
		if (setup.mode == Held) {
			static int holdFrames = 0;
			holdFrames = al::isPadHoldRight(-1) ? holdFrames + 1 : 0;

			if ((isActive && holdFrames == 30) || (isOut && isMarioActive == -1)) {
				isOut ? weapon->kill() : weapon->appear();
				al::tryEmitEffect(isMarioModel, isOut ? "BlasterDisappear" : "BlasterAppear", nullptr);
				al::tryStartSe(player, "BlasterOpen");
			}
		}
		// An attack takes it out, a stance weapon stays in hand through the battle stance, then it goes back where it's worn
		else {
			static float scale = 0.0f;
			static int sheatheStep = -1; // frames since it started going back, -1 while it isn't
			bool isSwinging = isSwingAnim(anim);
			bool isWanted = isSwinging || (setup.mode == Stance && isOut && battleStance > 0 && !isHandsBusy(player)); // a near enemy keeps it out but never takes it out, and busy hands put it away

			// The sheathe anim clears when it ends, or when an attack cuts it short
			if (setup.sheathe && isUpperBodyAnim(anim, setup.sheathe) && (isWanted || isUpperBodyAnimEnd(anim))) tryClearUpperBodyAnim(anim);

			if (isWanted || !isOut) sheatheStep = -1;

			if (!isOut && !isSwinging) scale = 0.0f;
			else if (isWanted) scale = sead::Mathf::min(1.0f, scale + 1.0f / fade);
			else if (!setup.sheathe) scale -= 1.0f / fade;
			else {
				auto* carry = static_cast<al::PartsModel*>(find(isMarioModel, setup.carry));
				float worn = carry ? carry->mLocalScale.x : 1.0f;
				if (sheatheStep < 0 && !isCatchAnimAllowed(player)) sheatheStep = sheatheFrame - 1; // where the cap catch skips its arm anim, it goes straight back
				else if (sheatheStep < 0) tryStartUpperBodyAnim(anim, setup.sheathe);
				scale = ++sheatheStep >= sheatheFrame ? 0.0f : 1.0f - (1.0f - worn) * sheatheStep / sheatheFrame; // shrinks to the worn size, then swaps where the hand put it
				if (sheatheStep == sheatheFrame) al::tryStartSe(player, "Grab"); // the cap catch sound as it lands where it's worn
			}

			if (scale > 0.0f) {
				setSize(weapon, scale);
				if (!isOut) weapon->appear();
			}
			else if (isOut) weapon->kill();

			showCarry(isMarioModel, setup.carry, !al::isAlive(weapon));
		}

		isWeaponOn = al::isAlive(weapon);

		// Hand action while it is out
		auto* hand = isWeaponOn && setup.pose ? find(isMarioModel, "右手") : nullptr;
		if (hand && !al::isActionPlaying(hand, setup.pose)) al::startAction(hand, setup.pose);
	}

}  // namespace PlayerWeapon
