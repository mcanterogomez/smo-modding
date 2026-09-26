#pragma once
#include "custom/.Globals.h"
#include "Library/Obj/PartsModel.h"

namespace PlayerWeapon {

	struct Weapon {
		const char* attack = nullptr; // model used to attack
		const char* carry = nullptr; // model worn on the body while it is away
		const char* pose = nullptr; // hand action while it is out
		bool isHeld = false; // stays out once armed with hold right
		bool isShoot = false; // shoots instead of throwing a fireball
	};

	// One weapon per suit, first match wins
	inline Weapon get() {
		if (isClassic) return {.attack = "Wrench", .carry = "BeltWrench", .pose = "GrabCeilWait"};
		if (isKnight) return {.attack = "Axe", .pose = "GrabCeilWait", .isHeld = true};
		if (isMario || isMarioActive == -1) return {.attack = "Blaster", .pose = "AreaWaitDance03", .isHeld = true, .isShoot = true}; // -1 is the power-up ending, so it still gets put away
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

	// Scale up as it comes out, down over the end of endAnim, null when the current anim is the last one
	inline float setFade(al::LiveActor* weapon, bool isWorn, const PlayerAnimator* anim, float step, const char* endAnim = nullptr) {
		float scale = sead::Mathf::min(1.0f, step / fade);

		if (!endAnim || anim->isAnim(endAnim)) {
			float left = anim->getAnimFrameMax() - anim->getAnimFrame();
			if (attackSensorRemaining > 0 && attackSensorRemaining < left) left = attackSensorRemaining; // the sensor can end the attack first
			if (left < fade) scale = left / fade;
		}

		sead::Vector3f size(scale, scale, scale);
		if (!isWorn) { al::setScale(weapon, size); return scale; }

		auto* parts = static_cast<al::PartsModel*>(weapon); // a worn weapon rebuilds its pose each frame, so scale its local one
		parts->mIsUseLocalScale = true;
		parts->mLocalScale = size;
		return scale;
	}

	// Attacks use a held weapon once armed (it's already in hand), any other one only with the spin toggle on, else punch and air spin
	inline bool isAttack() {
		Weapon setup = get();
		if (setup.isHeld) return isWeaponOn;
		return isConfig()->spinOnly && isHakoniwa && find(isHakoniwa->mModelHolder->findModelActor("Normal"), setup.attack);
	}

	// Out and staying out, so it shows between attacks
	inline bool isArmed() { return isWeaponOn && get().isHeld; }

	// Armed with a weapon that shoots
	inline bool isShooting() { return isArmed() && get().isShoot; }

	inline void update(PlayerActorHakoniwa* player, al::LiveActor* model, bool isActive) {
		Weapon setup = get();
		auto* weapon = find(model, setup.attack);
		if (!weapon) { isWeaponOn = false; return; }

		auto* anim = player->mAnimator;
		bool isOut = al::isAlive(weapon);

		// Hold right to take it out or put it away, and it drops when the power-up ends
		if (setup.isHeld) {
			static int holdFrames = 0;
			if (al::isPadHoldRight(-1)) holdFrames++;
			else holdFrames = 0;

			if ((isActive && holdFrames == 30) || (isOut && isMarioActive == -1)) {
				isOut ? weapon->kill() : weapon->appear();
				al::tryEmitEffect(model, isOut ? "BlasterDisappear" : "BlasterAppear", nullptr);
				al::tryStartSe(player, "BlasterOpen");
			}
		}
		// Not held, so it only comes out for the attack while the worn one hides
		else {
			bool isSwinging = isSwingAnim(anim);

			if (isSwinging) {
				float scale = setFade(weapon, true, anim, anim->getAnimFrame());
				if (scale > 0.0f && !isOut) weapon->appear(); // waits for a scale, so it never pops in full size
			}
			else if (isOut) weapon->kill();

			showCarry(model, setup.carry, !isSwinging);
		}

		isWeaponOn = al::isAlive(weapon);

		// Hand action while it is out
		auto* hand = isWeaponOn && setup.pose ? find(model, "右手") : nullptr;
		if (hand && !al::isActionPlaying(hand, setup.pose)) al::startAction(hand, setup.pose);
	}

}  // namespace PlayerWeapon
