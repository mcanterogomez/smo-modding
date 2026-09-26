#pragma once
#include "ModConfig.h"
#include "custom/.Globals.h"
#include "custom/.Nerves.h"
#include "custom/PlayerDrill.h"
#include "custom/PlayerFireBall.h"
#include "custom/PlayerFreeze.h"
#include "headers/PlayerIceCube.h"
#include "custom/PlayerWeapon.h"
#include "Library/Action/ActorActionKeeper.h"

// Shared core of both water surface run judges
template <typename Judge>
static bool canRunOnSurface(const Judge* thisPtr) {
	return thisPtr->mWaterSurfaceFinder->isFoundSurface()
		&& al::isNearZeroOrGreater(thisPtr->mWaterSurfaceFinder->getDistance())
		&& al::calcSpeedH(thisPtr->mPlayer) >= MIN_SPEED_RUN_ON_WATER;
}

namespace PowerUps {

	// Swap the iceball archive in for the ice suit
	struct FireBrosFireBallInitArchive : public mallow::hook::Inline<FireBrosFireBallInitArchive> {
		static void Callback(exl::hook::InlineCtx* ctx) {
			auto* actor = reinterpret_cast<al::LiveActor*>(ctx->X[0]);
			if (isIce && al::isEqualString(actor->getName(), "MarioIceBall")) ctx->X[8] = reinterpret_cast<u64>("PlayerIceBall");
		}
	};

	// Route the tank bullet to its custom archive
	struct InitActorArchiveHook : public mallow::hook::Trampoline<InitActorArchiveHook> {
		static void Callback(al::LiveActor* actor, const al::ActorInitInfo& info, const sead::SafeString& archive, const char* suffix) {
			if (al::isEqualString(actor->getName(), "MarioTankBullet")) {
				sead::SafeString custom("PlayerBullet");
				Orig(actor, info, custom, suffix);
				return;
			}
			Orig(actor, info, archive, suffix);
		}
	};

	inline void executeInitPlayer(PlayerActorHakoniwa* thisPtr, const al::ActorInitInfo* actorInfo, const PlayerInitInfo* playerInfo) {
		// Clear joint modifiers
		glideRot = {0.0f, 0.0f, 0.0f};

		// Handle joint modifiers
		al::initJointLocalRotator(isMarioModel, &glideRot, "JointRoot");
		al::initJointLocalScaleController(isMarioModel, &legScale, "LegL1");
		al::initJointLocalScaleController(isMarioModel, &legScale, "LegR1");

		// Only build the hammer this suit uses: Brawl gets the Smash Hammer, everyone else the Classic one
		if (isBrawl && al::isExistArchive("ObjectData/SmashHammer")) { // Smash Hammer
			isSmashHammer = new HammerBrosHammer("HammerBrosHammer", isMarioModel, "SmashHammer", true);
			al::initCreateActorNoPlacementInfo(isSmashHammer, *actorInfo);
			isHammer = isSmashHammer;
		}
		else if (al::isExistArchive("ObjectData/PlayerHammer")) { // Classic Hammer
			isHammer = new HammerBrosHammer("HammerBrosHammer", isMarioModel, "PlayerHammer", true);
			al::initCreateActorNoPlacementInfo(isHammer, *actorInfo);
		}

		// Create and hide fireballs
		fireBalls = new al::LiveActorGroup("FireBrosFireBall", 4);
		while (!fireBalls->isFull()) {
			auto* fb = new FireBrosFireBall("MarioFireBall", isMarioModel);
			al::initCreateActorNoPlacementInfo(fb, *actorInfo);
			fireBalls->registerActor(fb);
		}
		fireBalls->makeActorDeadAll();

		// Create and hide iceballs
		if (al::isExistArchive("ObjectData/PlayerIceBall")) {
			iceBalls = new al::LiveActorGroup("PlayerIceBall", 4);
			while (!iceBalls->isFull()) {
				auto* ib = new FireBrosFireBall("MarioIceBall", isMarioModel);
				al::initCreateActorNoPlacementInfo(ib, *actorInfo);
				iceBalls->registerActor(ib);
			}
			iceBalls->makeActorDeadAll();
		}

		// Create ice cube
		if (al::isExistArchive("ObjectData/PlayerIceCube")) {
			iceCubes = new al::LiveActorGroup("IceCubes", 32);
			while (!iceCubes->isFull()) {
				auto* cube = new PlayerIceCube("IceCube");
				al::initCreateActorNoPlacementInfo(cube, *actorInfo);
				iceCubes->registerActor(cube);
			}
			iceCubes->makeActorDeadAll();
		}

		// Create and hide tank bullets
		if (al::isExistArchive("ObjectData/PlayerBullet")) {
			tankBullets = new al::LiveActorGroup("TankBullet", 4);
			while (!tankBullets->isFull()) {
				auto* tb = new TankBullet("MarioTankBullet");
				al::initCreateActorNoPlacementInfo(tb, *actorInfo);
				tankBullets->registerActor(tb);
			}
			tankBullets->makeActorDeadAll();
		}

		// Create custom gauge
		isGauge = new CustomGauge(*actorInfo->layoutInitInfo);
	}

	inline void executeInitAfterPlacement() {
		PlayerFreeze::clearAllFrozen();
		if (isHammer) isHammer->makeActorDead();
		if (fireBalls) fireBalls->makeActorDeadAll();
		if (iceBalls) iceBalls->makeActorDeadAll();
		if (iceCubes) iceCubes->makeActorDeadAll();
		if (tankBullets) tankBullets->makeActorDeadAll();
	}

	inline void executeMovement(PlayerActorHakoniwa* thisPtr) {
		auto* anim = thisPtr->mAnimator;
		auto* damage = thisPtr->mDamageKeeper;

		bool onGround = rs::isOnGround(thisPtr, thisPtr->mCollider);
		bool isWater = al::isInWater(thisPtr);
		bool isFlicker = damage && damage->mDamageInvalidCount > 0;
		bool isActive = !isFlicker && !isHacking() && !rs::isActiveDemo(thisPtr);

		// Re-arms the double jump; the Jump/Fall hooks never see the grounded frames
		if (onGround || isWater) { isDoubleJump = false; isDoubleJumpConsume = false; }

		f32 speedH = al::calcSpeedH(thisPtr);
		f32 dashBorder = thisPtr->mConst->getDashFastBorderSpeed();

		// Handle hammer attack
		if (isHammer && al::isAlive(isHammer)
			&& !al::isNerve(thisPtr, &HammerNrv)) { isHammer->makeActorDead(); al::invalidateHitSensor(isHammer, "AttackHack"); }

		// Handle weapon arming and swapping
		PlayerWeapon::update(thisPtr, isMarioModel, isActive);

		// Handle logic for Drill Suit
		PlayerDrill::update(thisPtr, isMarioModel, isActive);

		// Fireball / Iceball / Blaster
		PlayerFireBall::update(thisPtr, isMarioModel);

		bool isGlide = al::isActionPlaying(isMarioModel, "Glide");
		bool isGliding = isGlide || al::isActionPlaying(isMarioModel, "JumpBroad8") || al::isActionPlaying(isMarioModel, "GlideFloatStart")
			|| al::isActionPlaying(isMarioModel, "GlideFloat") || al::isActionPlaying(isMarioModel, "GlideFloatSuper");

		// Handle logic for Tanooki suit
		if (isTanooki) {
			bool isFloating = al::isActionPlaying(isMarioModel, "TailFloat");
			if (onGround) isNotFloat = false; // a glide blocks the float for the rest of the airtime

			// Float: hold A/B while descending to hover
			const al::Nerve* currentNerve = thisPtr->getNerveKeeper()->getCurrentNerve();
			const al::Nerve* fallNerve = getNerveAt(nrvHakoniwaFall);

			const bool canFloat = !onGround && !isNotFloat && al::calcSpeedV(thisPtr) < 0.0f && (al::isPadHoldA(-1) || al::isPadHoldB(-1))
				&& (currentNerve == fallNerve || currentNerve == getNerveAt(nrvHakoniwaJump));

			if (canFloat) {
				if (currentNerve != fallNerve) al::setNerve(thisPtr, fallNerve); // force Fall, hand off the frame
				else if (!isFloating) anim->startAnim("TailFloat");
				else { al::setVelocityY(thisPtr, 0.0f); al::limitVelocityH(thisPtr, 7.5f); }
			}
			else if (isFloating && currentNerve == fallNerve) al::setNerve(thisPtr, fallNerve);

			// TailFloat SE loops every 15 frames
			static int floatSeTimer = 0;
			if (!isFloating) floatSeTimer = 0;
			else if (--floatSeTimer <= 0) { al::tryStartSe(isMarioModel, "TailFloat"); floatSeTimer = 15; }

			// Tail spins through both the glide and the float
			auto* tail = al::tryGetSubActor(isMarioModel, "尻尾");
			bool spinTail = isGliding || isFloating;

			if (tail && al::isAlive(tail)) {
				if (spinTail && !al::isActionPlaying(tail, "TailSpin")) {
					al::tryStartAction(tail, "TailSpin");
					al::tryEmitEffect(isMarioModel, "TailSpin", nullptr);
					al::tryStartSe(thisPtr, "SpinJumpDownFall");
				}
				else if (!spinTail && al::isActionPlaying(tail, "TailSpin")) {
					al::tryStartAction(tail, "Wait");
					al::tryDeleteEffect(isMarioModel, "TailSpin");
					al::tryStopSe(thisPtr, "SpinJumpDownFall", -1, nullptr);
				}
			}
		}

		// Handle logic for Cape suits
		auto* cape = al::tryGetSubActor(isMarioModel, "ケープ");
		isCapeOn = cape && al::isAlive(cape);

		if (isCapeOn) {
			if (isGlide && !al::isActionPlaying(cape, "Glide")) al::tryStartAction(cape, "Glide");
			else if (!isGlide && al::isActionPlaying(cape, "Glide")) al::tryStartAction(cape, "Wait");
		}
		// Handle cape spawn and despawn
		if (cape && (isMario || isBrawl)) {
			bool capeTimer = !isGliding && isActive && isCapeActive > 0 && --isCapeActive == 0;

			if (!isCapeOn) isCapeActive = -1;
			else if (isMarioActive == -1 || capeTimer) {
				cape->kill();
				al::tryEmitEffect(isMarioModel, "AppearBloom", nullptr);
				al::tryStartSe(thisPtr, "Bloom");
				isCapeActive = -1;
			}
		}

		// Handle Glide Gauge
		if (isGauge && !isSuper) {
			static bool wasInAir = false;
			static bool wasStartup = false;
			static bool hadStartup = false;
			bool inAir = !onGround && !isWater;
			bool isStartup = al::isActionPlaying(isMarioModel, "JumpBroad8");

			// Landing
			if (wasInAir && !inAir && isGauge->isAlive()) {
				isGauge->refill();
				isGauge->endMax();
				hadStartup = false;
				wasStartup = false;
			}
			// Mark first startup as consumed on its falling edge
			if (!isStartup && wasStartup) hadStartup = true;
			wasStartup = isStartup;
			// Gliding
			if (isGliding && isGauge->canUse()) {
				isGauge->start();
				isGauge->drain();
				if (isStartup && hadStartup) isGauge->setRate(isGauge->getRate() - 0.004f);
				if (isGauge->isEmpty()) isGauge->startTimer();
			}
			// Penalty
			if (isGauge->tickTimer() && isGliding) al::setNerve(thisPtr, getNerveAt(nrvHakoniwaFall));
			wasInAir = inAir;
		}

		// Handle logic for Metal suit
		if (isMetal) {
			if (thisPtr->mInfo->mIsMoon) applyMetalMarioMoonConst(thisPtr->mConst);
			else applyMetalMarioConst(thisPtr->mConst);
			// Stop physics for nose/mustache
			if (thisPtr->mJointControlKeeper) thisPtr->mJointControlKeeper->resetPartsDynamics();
			// Ground speed is scaled in PlayerActionGroundMoveControlUpdate
			if (!onGround && isSubmerged(thisPtr)) al::scaleVelocityHV(thisPtr, 0.95f, al::getVelocity(thisPtr).y < 0.0f ? 0.85f : 1.0f); // floaty descent
		}

		// Handle logic for Flying suit
		if (isFly) {
			if (isActive && !al::isHideModel(isMarioModel)) {
				if (isGliding) {
					al::tryDeleteEffect(isMarioModel, "GlideWindL");
					al::tryDeleteEffect(isMarioModel, "GlideWindR");
					al::tryDeleteEffect(isMarioModel, "FlyingState");

					al::tryEmitEffect(isMarioModel, "FlyingL", nullptr);
					al::tryEmitEffect(isMarioModel, "FlyingLTrail", nullptr);
					al::tryEmitEffect(isMarioModel, "FlyingR", nullptr);
					al::tryEmitEffect(isMarioModel, "FlyingRTrail", nullptr);
				} else {
					al::tryDeleteEffect(isMarioModel, "FlyingL");
					al::tryDeleteEffect(isMarioModel, "FlyingLTrail");
					al::tryDeleteEffect(isMarioModel, "FlyingR");
					al::tryDeleteEffect(isMarioModel, "FlyingRTrail");

					al::tryEmitEffect(isMarioModel, "FlyingState", nullptr);
				}
			} else al::tryKillEmitterAndParticleAll(isMarioModel);
		}

		// Handle logic for Super suit
		if (isSuper) {
			applyMoonMarioConst(thisPtr->mConst); // force Moon physics

			// Add attack to Super moves
			static bool wasMoveSuper = false;
			bool isMoveSuper = speedH >= dashBorder || anim->isAnim("JumpBroad8") || anim->isAnim("Glide");
			updateAttackSensor(thisPtr, "GalaxySpin", isMoveSuper, wasMoveSuper);

			// Apply effects for DashFastSuper
			bool isDash = al::isPadHoldR(-1) && !isActionBusy() && al::isActionPlaying(isMarioModel, "MoveSuper") && speedH >= dashBorder;

			if (isDash) al::tryEmitEffect(isMarioModel, "DashSuper", nullptr);
			else if (isGlide && !isActionBusy()) al::tryEmitEffect(isMarioModel, "DashSuperGlide", nullptr);
			else { al::tryDeleteEffect(isMarioModel, "DashSuper"); al::tryDeleteEffect(isMarioModel, "DashSuperGlide"); }

			// Apply effects for Invincibility
			if (isActive && !al::isHideModel(isMarioModel)) {
				if (damage) {
					if (!damage->mIsPreventDamage) damage->activatePreventDamage();
					damage->mInvincibilityTimer = INT_MAX;
				}
				al::tryEmitEffect(isMarioModel, "Bonfire", nullptr);
			} else {
				if (isHacking() && damage) damage->mInvincibilityTimer = 0;
				al::tryDeleteEffect(isMarioModel, "Bonfire");
			}
		}

		// Handle life recovery
		static int stillFrames = 0;
		static int healFrames = 0;

		bool isWait = isActive && al::isNerve(thisPtr, getNerveAt(nrvHakoniwaWait));
		bool canHeal = (isMario || isNoCap) && isWait && !GameDataFunction::isPlayerHitPointMax(thisPtr);

		if (canHeal) {
			if (stillFrames < 120) stillFrames++;

			int interval = (stillFrames >= 120) ? 60 : 600;
			if (++healFrames >= interval) { GameDataFunction::recoveryPlayer(thisPtr); healFrames = 0; }
		}
		else { stillFrames = 0; healFrames = 0; }

		#ifdef ALLOW_DASH // Handles the dash windup, animations and effects
			bool isRunning = al::isNerve(thisPtr, getNerveAt(nrvHakoniwaRun));

			// Charge winds down while running with R held, then holds at 0
			static int slowFrames = 0;
			if (speedH >= thisPtr->mConst->mRunBorderSpeed || isDashDelay != 0) slowFrames = 0;
			else slowFrames++; // frames spent below a walk while charged

			if (!al::isPadHoldR(-1) || slowFrames > 30) isDashDelay = -1; // 30: grace before a slowdown drops the charge
			else if (isRunning && !isActionBusy() && isDashDelay != 0) isDashDelay = isDashDelay < 0 ? 119 : isDashDelay - 1; // 119: windup length, 120 frames (2s)

			// One shot per dash, re-armed by leaving the run, dropping the charge or braking — terrain breaks none of them
			static bool wasDash = false;
			if (!isRunning || isDashDelay != 0 || slowFrames > 0) wasDash = false;
			else if (!wasDash && speedH >= dashBorder) {
				const char* fx = isSuper ? "AccelSecond" : "Accel";
				al::tryStartSe(thisPtr, fx);
				al::tryEmitEffect(isMarioModel, fx, nullptr);
				wasDash = true;
			}
		#endif
	}

	// Freezes actors and keeps the hammer glued to its joint
	struct LiveActorMovementHook : public mallow::hook::Trampoline<LiveActorMovementHook> {
		static void Callback(al::LiveActor* actor) {
			// Check if this actor is frozen
			if (PlayerFreeze::updateFrozenActor(actor)) return; // Skip normal movement

			if (actor == isHammer && al::isAlive(isHammer)) {
				sead::Vector3f correctPos = updateHammerMtx();
				Orig(actor);
				al::setTrans(isHammer, correctPos);

				al::HitSensor* sensorHammer = al::getHitSensor(isHammer, "AttackHack");
				if (!sensorHammer || !sensorHammer->mIsValid) return;
				if (auto* sensor = rs::tryGetCollidedWallSensor(isHakoniwa->mCollider)) isHammer->attackSensor(sensorHammer, sensor);
				if (auto* sensor = al::tryGetCollidedWallSensor(isHammer)) isHammer->attackSensor(sensorHammer, sensor);
				if (auto* sensor = al::tryGetCollidedCeilingSensor(isHammer)) isHammer->attackSensor(sensorHammer, sensor);
				if (auto* sensor = al::tryGetCollidedGroundSensor(isHammer)) isHammer->attackSensor(sensorHammer, sensor);
				return;
			}

			Orig(actor);
		}
	};

	// Blocks carry start while the hammer nerve owns the player
	struct PlayerCarryKeeperStartCarry : public mallow::hook::Trampoline<PlayerCarryKeeperStartCarry> {
		static void Callback(PlayerCarryKeeper* thisPtr, al::HitSensor* sensor) {
			if (isHakoniwa && al::isNerve(isHakoniwa, &HammerNrv)) return;

			Orig(thisPtr, sensor);
		}
	};

	// Double jump, from the Jump and Fall nerves
	template <int Variant>
	struct PlayerActorHakoniwaDoubleJump : public mallow::hook::Trampoline<PlayerActorHakoniwaDoubleJump<Variant>> {
		using Base = mallow::hook::Trampoline<PlayerActorHakoniwaDoubleJump<Variant>>;
		static void Callback(PlayerActorHakoniwa* thisPtr) {
			Base::Orig(thisPtr);
			if (!isBrawl && !isFeather && !isMario) return; // no suit here can double jump, skip the lookups

			auto* cape = al::tryGetSubActor(isMarioModel, "ケープ");
			bool isCape = isFeather || (isMario && cape && al::isAlive(cape));

			// Mario needs the cape out, Brawl always has it
			if (!isBrawl && !isCape) return;

			if (!isDoubleJump && !rs::isOnGround(thisPtr, thisPtr->mCollider) && (al::isPadTriggerA(-1) || al::isPadTriggerB(-1))
			) {
				isDoubleJump = true;
				isDoubleJumpConsume = true;

				al::setVelocityY(thisPtr, 0.0f); // the Jump nerve lands next frame, a touchdown before then would eat it
				thisPtr->mContinuousJump->clear(); // clear jump chain to always reset power
				if (isCape) al::tryStartSe(thisPtr, "CapeFloat");
				al::tryEmitEffect(isMarioModel, "DoubleJump", nullptr);
				al::setNerve(thisPtr, getNerveAt(nrvHakoniwaJump));
			}

			if (isDoubleJumpConsume && al::isFirstStep(thisPtr)) {
				thisPtr->mAnimator->startAnim(isCape ? "DoubleJump" : "DoubleJumpSmash");
				isDoubleJumpConsume = false;
			}
		}
	};

	// Brawl drives its own jump count
	struct PlayerStateJumpTryCountUp : public mallow::hook::Trampoline<PlayerStateJumpTryCountUp> {
		static void Callback(PlayerStateJump* state, PlayerContinuousJump* cont) {
			if (isBrawl) return;
			Orig(state, cont);
		}
	};

	// Turns the dive into a glide with lean, pitch and cancels
	struct PlayerActorHakoniwaExeHeadSliding : public mallow::hook::Trampoline<PlayerActorHakoniwaExeHeadSliding> {
		static void Callback(PlayerActorHakoniwa* thisPtr) {
			Orig(thisPtr);
			static bool blockGlide = false;

			if (al::isFirstStep(thisPtr)) blockGlide = (isGauge && isGauge->isEmpty());
			if (blockGlide) return;
			if (!isMario && !isFeather && !isTanooki && !isFly && !isBrawl && !isSuper) return;

			auto* anim = thisPtr->mAnimator;
			if (al::getVelocity(thisPtr).y < -2.5f) al::setVelocityY(thisPtr, -2.5f);

			float lean = 0.0f;
			bool isGlide = anim->isAnim("Glide");

			if (isGlide) {
				sead::Vector3f camSide, marioSide;
				al::calcCameraSideDir(&camSide, thisPtr, 0);
				al::calcSideDir(&marioSide, thisPtr);
				lean = camSide.dot(marioSide) * al::getLeftStick(-1).x;
			}

			// Lean drives yaw and roll, pitch follows its magnitude at a shallower angle
			sead::Vector3f target = {fabsf(lean) * -12.5f, lean * -32.5f, lean * -32.5f};
			glideRot += (target - glideRot) * (isGlide ? 0.025f : 0.2f);

			if (al::isFirstStep(thisPtr)) {
				auto* cape = al::tryGetSubActor(isMarioModel, "ケープ");

				if ((isMario || isBrawl) && cape && al::isDead(cape)) {
					cape->appear();
					al::tryEmitEffect(isMarioModel, "Appear", nullptr);
					al::tryStartSe(thisPtr, "Appear"); al::tryStartSe(thisPtr, "CapeGet");
				}
				anim->startAnim("JumpBroad8");
			}
			else if (anim->isAnimEnd() && anim->isAnim("JumpBroad8")) anim->startAnim("Glide");
			else if (al::calcSpeed(thisPtr) < 10.f) {
				if (isGlide) anim->startAnim("GlideFloatStart");
				if (anim->isAnimEnd() && anim->isAnim("GlideFloatStart")) anim->startAnim("GlideFloat");
			}

			if (!al::isGreaterStep(thisPtr, 25)) return;

			if (al::isPadTriggerA(-1) || al::isPadTriggerB(-1)) al::setNerve(thisPtr, getNerveAt(nrvHakoniwaFall));
			if (al::isPadTriggerZL(-1) || al::isPadTriggerZR(-1)) al::setNerve(thisPtr, getNerveAt(nrvHakoniwaHipDrop));

			bool spinPress = isPadTriggerGalaxySpin(-1);
			bool throwPress = al::isPadTriggerX(-1) || al::isPadTriggerY(-1);
			if ((!spinPress && !throwPress) || al::isNerve(thisPtr, getNerveAt(spinCapNrvOffset))) return;

			// Cap throw with no cap available just drops the glide
			if (!spinPress && (!thisPtr->mHackCap || !thisPtr->mHackCap->isEnableThrow())) {
				al::setNerve(thisPtr, getNerveAt(nrvHakoniwaFall));
				return;
			}

			spin.resetForNewSpin();
			spin.trigger = spinPress;
			al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
		}
	};

	// Resets glide joints and arms the cape despawn timer
	struct PlayerHeadSlidingKill : public mallow::hook::Trampoline<PlayerHeadSlidingKill> {
		static void Callback(PlayerStateHeadSliding* state) {
			glideRot = {0.0f, 0.0f, 0.0f};
			isCapeActive = 1200;
			isNotFloat = true;

			tryClearUpperBodyAnim(state->mAnimator);
			Orig(state);
		}
	};

	// Super dives faster
	struct PlayerConstGetHeadSlidingSpeed : public mallow::hook::Trampoline<PlayerConstGetHeadSlidingSpeed> {
		static float Callback(const PlayerConst* thisPtr) {
			float speed = Orig(thisPtr);
			if (isSuper && !isHacking()) speed *= 1.5f;
			return speed;
		}
	};

	// Drill owns the jump button while spinning
	struct PlayerInputFunctionIsTriggerJump : public mallow::hook::Trampoline<PlayerInputFunctionIsTriggerJump> {
		static bool Callback(const al::LiveActor* actor, s32 port) {
			if (isDrillAnim(isHakoniwa ? isHakoniwa->mAnimator : nullptr)) return false;
			return Orig(actor, port);
		}
	};

	// R counts as the action hold unless the cap is flying
	struct PlayerInputFunctionIsHoldAction : public mallow::hook::Trampoline<PlayerInputFunctionIsHoldAction> {
		static bool Callback(const al::LiveActor* actor, s32 port) {
			bool isFlying = isHakoniwa && isHakoniwa->mHackCap && isHakoniwa->mHackCap->isFlying();
			return Orig(actor, port) || (al::isPadHoldR(port) && !isFlying);
		}
	};

	// Ground speed cap for this update: wading, dash and sneak, vanilla's restored after
	struct PlayerActionGroundMoveControlUpdate : public mallow::hook::Trampoline<PlayerActionGroundMoveControlUpdate> {
		static float Callback(PlayerActionGroundMoveControl* thisPtr) {
			if (isHacking()) return Orig(thisPtr);

			float isMaxSpeed = thisPtr->mMaxSpeed;
			if (isMetal && isSubmerged(isHakoniwa)) thisPtr->mMaxSpeed *= 0.6f;
			#ifdef ALLOW_DASH
				else if (isDashDelay >= 0 && !isActionBusy()) {
					float full = isSuper ? 28.0f : 21.0f; // dash tier
					thisPtr->mMaxSpeed = isDashDelay == 0 ? full : sead::Mathf::min((isMaxSpeed + full) * 0.5f, thisPtr->mConst->getDashFastBorderSpeed() - 0.5f); // windup: halfway, under the border
				}
			#endif
			else if (isSneaking) thisPtr->mMaxSpeed = thisPtr->mConst->getNormalMinSpeed() * (isMetal ? 1.25f : 2.5f); // sneak ceiling

			float update = Orig(thisPtr);
			thisPtr->mMaxSpeed = isMaxSpeed;
			return update;
		}
	};

	// Metal has no mouth sounds; they come from the 3D model's own action keeper, so only its update needs the stop
	struct ActorActionKeeperUpdatePostHook : public mallow::hook::Trampoline<ActorActionKeeperUpdatePostHook> {
		static void Callback(al::ActorActionKeeper* thisPtr) {
			Orig(thisPtr);
			if (isMetal && thisPtr->mParentActor == isMarioModel) ::al::stopAllSeFromUser(isHakoniwa, 0, "Mouth");
		}
	};

	// Metal and Knight always land on metal
	struct TryUpdateSeMaterialCodeHook : public mallow::hook::Trampoline<TryUpdateSeMaterialCodeHook> {
		static void Callback(al::IUseAudioKeeper* keeper, const char* material) {
			if (isMetal || isKnight) return Orig(keeper, "Metal");
			Orig(keeper, material);
		}
	};

	// Metal walks the bottom instead of swimming
	struct JudgeInWater : public mallow::hook::Trampoline<JudgeInWater> {
		static bool Callback(const PlayerJudgeInWater* thisPtr) {
			if (isMetal) return false;
			return Orig(thisPtr);
		}
	};

	// Super never runs out of air
	struct ReduceOxygen : public mallow::hook::Trampoline<ReduceOxygen> {
		static void Callback(void* thisPtr) {
			if (isSuper) return;
			Orig(thisPtr);
		}
	};

	// Super may start running on water, with an extra descent check
	struct StartWaterSurfaceRunJudge : public mallow::hook::Trampoline<StartWaterSurfaceRunJudge> {
		static bool Callback(const PlayerJudgeStartWaterSurfaceRun* thisPtr) {
			if (!isSuper) return Orig(thisPtr);
			return canRunOnSurface(thisPtr)
				&& al::getGravity(thisPtr->mPlayer).dot(al::getVelocity(thisPtr->mPlayer)) >= 0.0f;
		}
	};

	// Super keeps running on water, and the result drives the two patches below
	struct WaterSurfaceRunJudge : public mallow::hook::Trampoline<WaterSurfaceRunJudge> {
		static bool Callback(const PlayerJudgeWaterSurfaceRun* thisPtr) {
			isSuperRunningOnSurface = false;
			if (!isSuper) return Orig(thisPtr);

			isSuperRunningOnSurface = canRunOnSurface(thisPtr);
			return isSuperRunningOnSurface;
		}
	};

	// Nonzero W8 skips the sink branch
	struct RunWaterSurfaceDisableSink : public mallow::hook::Inline<RunWaterSurfaceDisableSink> {
		static void Callback(exl::hook::InlineCtx* ctx) {
			if (isSuperRunningOnSurface) ctx->W[8] = 1;
		}
	};

	// Redirect turnVecToVecRate at scratch so the call is ignored
	struct WaterSurfaceRunDisableSlowdown : public mallow::hook::Inline<WaterSurfaceRunDisableSlowdown> {
		static void Callback(exl::hook::InlineCtx* ctx) {
			static sead::Vector3f garbageVec;
			if (isSuperRunningOnSurface) ctx->X[0] = reinterpret_cast<u64>(&garbageVec);
		}
	};

	// Terrain damage codes Metal and Super ignore
	template <typename... Args>
	struct TouchCodeHook : public mallow::hook::Trampoline<TouchCodeHook<Args...>> {
		using Base = mallow::hook::Trampoline<TouchCodeHook<Args...>>;
		static bool Callback(Args... args) {
			if (isMetal || isSuper) return false;
			return Base::Orig(args...);
		}
	};

	using TouchDamageCode = TouchCodeHook<const al::LiveActor*, const IUsePlayerCollision*>;
	using TouchDamageFireCode = TouchCodeHook<const al::LiveActor*, const IUsePlayerCollision*, const IPlayerModelChanger*>;
	using TouchDeadCode = TouchCodeHook<const al::LiveActor*, const IUsePlayerCollision*, const IPlayerModelChanger*, const IUseDimension*, float>;

	inline void Install() {
		// Custom archives
		FireBrosFireBallInitArchive::InstallAtOffset(0x10082C);
		InitActorArchiveHook::InstallAtSymbol("_ZN2al24initActorWithArchiveNameEPNS_9LiveActorERKNS_13ActorInitInfoERKN4sead14SafeStringBaseIcEEPKc");

		// Handles control/movement
		LiveActorMovementHook::InstallAtSymbol("_ZN2al9LiveActor8movementEv");

		// Handles Hammer while Carrying
		PlayerCarryKeeperStartCarry::InstallAtSymbol("_ZN17PlayerCarryKeeper10startCarryEPN2al9HitSensorE");

		// Handles Double Jump
		PlayerActorHakoniwaDoubleJump<0>::InstallAtSymbol("_ZN19PlayerActorHakoniwa7exeJumpEv");
		PlayerActorHakoniwaDoubleJump<1>::InstallAtSymbol("_ZN19PlayerActorHakoniwa7exeFallEv");
		PlayerStateJumpTryCountUp::InstallAtSymbol("_ZN15PlayerStateJump24tryCountUpContinuousJumpEP20PlayerContinuousJump");

		// Handles Glide
		PlayerActorHakoniwaExeHeadSliding::InstallAtSymbol("_ZN19PlayerActorHakoniwa14exeHeadSlidingEv");
		PlayerHeadSlidingKill::InstallAtSymbol("_ZN22PlayerStateHeadSliding4killEv");
		PlayerConstGetHeadSlidingSpeed::InstallAtSymbol("_ZNK11PlayerConst19getHeadSlidingSpeedEv");

		PlayerInputFunctionIsTriggerJump::InstallAtSymbol("_ZN19PlayerInputFunction13isTriggerJumpEPKN2al9LiveActorEi");

		// Ground speed cap, dash tiers gated inside the callback
		PlayerActionGroundMoveControlUpdate::InstallAtSymbol("_ZN29PlayerActionGroundMoveControl6updateEv");

		#ifdef ALLOW_DASH // Handles Dash
			PlayerInputFunctionIsHoldAction::InstallAtSymbol("_ZN19PlayerInputFunction12isHoldActionEPKN2al9LiveActorEi");

			// Handles running on water
			StartWaterSurfaceRunJudge::InstallAtSymbol("_ZNK31PlayerJudgeStartWaterSurfaceRun5judgeEv");
			WaterSurfaceRunJudge::InstallAtSymbol("_ZNK26PlayerJudgeWaterSurfaceRun5judgeEv");
			RunWaterSurfaceDisableSink::InstallAtOffset(0x48023C);
			WaterSurfaceRunDisableSlowdown::InstallAtOffset(0x4184C0);

			// Powerups ignore terrain damage
			TouchDamageCode::InstallAtSymbol("_ZN2rs17isTouchDamageCodeEPKN2al9LiveActorEPK19IUsePlayerCollision");
			TouchDamageFireCode::InstallAtSymbol("_ZN2rs21isTouchDamageFireCodeEPKN2al9LiveActorEPK19IUsePlayerCollisionPK19IPlayerModelChanger");
			TouchDeadCode::InstallAtSymbol("_ZN2rs15isTouchDeadCodeEPKN2al9LiveActorEPK19IUsePlayerCollisionPK19IPlayerModelChangerPK13IUseDimensionf");
		#endif

		// Handle Metal Mario setup
		ActorActionKeeperUpdatePostHook::InstallAtSymbol("_ZN2al17ActorActionKeeper10updatePostEv");
		TryUpdateSeMaterialCodeHook::InstallAtSymbol("_ZN2al23tryUpdateSeMaterialCodeEPNS_15IUseAudioKeeperEPKc");

		// Handles Metal Mario walking in water
		JudgeInWater::InstallAtSymbol("_ZNK18PlayerJudgeInWater5judgeEv");

		// Handles Super Mario breathing in water
		ReduceOxygen::InstallAtSymbol("_ZN12PlayerOxygen6reduceEv");

		// Patch PlayerJointControlKeeper capacity from 7 to 12
		exl::patch::CodePatcher jointCapPatcher(0x454F20);
		jointCapPatcher.WriteInst(0x52800181); // MOV W1, #12

		// Disable invincibility music patches
		exl::patch::CodePatcher invincibleStartPatcher(0x4CC6FC);
		invincibleStartPatcher.WriteInst(0x1F2003D5); // NOP
		exl::patch::CodePatcher invinciblePatcher(0x43F4A8);
		invinciblePatcher.WriteInst(0x1F2003D5); // NOP
	}
}