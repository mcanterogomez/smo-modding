#pragma once
// C++ Standard Library
#include <cstddef>
#include <limits.h>

// External hooking & config libraries
#include <exl/hook/base.hpp>
#include <mallow/config.hpp>
#include <mallow/init/initLogging.hpp>
#include <mallow/logging/logger.hpp>
#include <mallow/mallow.hpp>

// Core game system
#include "System/GameDataFunction.h"

// Library headers
#include "Library/Base/StringUtil.h"
#include "Library/Camera/CameraUtil.h"
#include "Library/Controller/InputFunction.h"
#include "Library/Controller/SpinInputAnalyzer.h"
#include "Library/Effect/EffectKeeper.h"
#include "Library/Effect/EffectSystemInfo.h"
#include "Library/File/FileUtil.h"
#include "Library/HitSensor/HitSensorKeeper.h"
#include "Library/Joint/JointControllerKeeper.h"
#include "Library/LiveActor/ActorActionFunction.h"
#include "Library/LiveActor/ActorAnimFunction.h"
#include "Library/LiveActor/ActorCollisionFunction.h"
#include "Library/LiveActor/ActorClippingFunction.h"
#include "Library/LiveActor/ActorFlagFunction.h"
#include "Library/LiveActor/ActorInitInfo.h"
#include "Library/LiveActor/ActorModelFunction.h"
#include "Library/LiveActor/ActorMovementFunction.h"
#include "Library/LiveActor/ActorPoseUtil.h"
#include "Library/LiveActor/ActorSensorFunction.h"
#include "Library/LiveActor/ActorSensorUtil.h"
#include "Library/LiveActor/LiveActorFlag.h"
#include "Library/LiveActor/LiveActorFunction.h"
#include "Library/LiveActor/LiveActorGroup.h"
#include "Library/Math/MathUtil.h"
#include "Library/Nature/NatureUtil.h"
#include "Library/Nature/WaterSurfaceFinder.h"
#include "Library/Nerve/NerveSetupUtil.h"
#include "Library/Nerve/NerveUtil.h"
#include "Library/Placement/PlacementFunction.h"
#include "Library/Player/PlayerUtil.h"
#include "Library/Rail/RailUtil.h"
#include "Library/Se/SeFunction.h"
#include "Library/Shadow/ActorShadowUtil.h"

// Game-specific utilities
#include "Project/HitSensor/HitSensor.h"
#include "Util/ActorDimensionUtil.h"
#include "Util/DemoUtil.h"
#include "Util/ObjUtil.h"
#include "Util/PlayerCollisionUtil.h"
#include "Util/PlayerUtil.h"
#include "Util/SensorMsgFunction.h"

// Player actor & state headers
#include "Player/PlayerAnimator.h"
#include "Player/PlayerAnimFrameCtrl.h"
#include "Player/IUsePlayerCollision.h"
#include "Player/PlayerActionGroundMoveControl.h"
#include "Player/PlayerActorHakoniwa.h"
#include "Player/PlayerBindKeeper.h"
#include "Player/PlayerCarryKeeper.h"
#include "Player/PlayerColliderHakoniwa.h"
#include "Player/PlayerCounterForceRun.h"
#include "Player/PlayerDamageKeeper.h"
#include "Player/PlayerEquipmentUser.h"
#include "Player/PlayerFunction.h"
#include "Player/PlayerHackKeeper.h"
#include "Player/PlayerInfo.h"
#include "Player/PlayerInput.h"
#include "Player/PlayerJointControlKeeper.h"
#include "Player/PlayerJudgeStartWaterSurfaceRun.h"
#include "Player/PlayerJudgeWallHitDown.h"
#include "Player/PlayerJudgeWaterSurfaceRun.h"
#include "Player/PlayerModelHolder.h"
#include "Player/PlayerStateHeadSliding.h"
#include "Player/PlayerStateSpinCap.h"
#include "Player/PlayerStateSwim.h"
#include "Player/PlayerTrigger.h"
#include "Player/HackCap.h"

// Mod-specific & custom actors
#include "custom/CustomGauge.h"
#include "custom/CustomPlayerConst.h"
#include "headers/FireBall.h"
#include "headers/HammerBrosHammer.h"
#include "headers/Motorcycle.h"
#include "headers/PlayerIceCube.h"
#include "headers/PlayerStateJump.h"
#include "headers/PlayerStateWait.h"
#include "headers/PlayerStainControl.h"
#include "headers/TankBullet.h"
#include "ModConfig.h"
#include "math/seadVectorFwd.h"

// Namespaces
namespace PlayerEquipmentFunction {
	bool isEquipmentNoCapThrow(const PlayerEquipmentUser*);
	bool isEquipmentForceDash(const PlayerEquipmentUser*);
}

using mallow::log::logLine;

// =========================================================
//                        CORE
// =========================================================

// Helper: nerve pointer from binary offset
inline const al::Nerve* getNerveAt(uintptr_t offset) {
	return (const al::Nerve*)((((u64)malloc) - 0x00724b94) + offset);
}

inline ModConfig* isConfig() { return mallow::config::getConfg<ModConfig>(); }

// Spin button config
inline bool isPadTriggerGalaxySpin(int port) {
	switch (isConfig()->attackButton) {
		case 'X': return al::isPadTriggerX(port);
		default: return al::isPadTriggerY(port);
	}
}

// Galaxy SFX config
inline void isGalaxySfx(PlayerActorHakoniwa* player) {
	if (!isConfig()->galaxySfx) return;
	auto* model = player->mModelHolder->findModelActor("Normal");
	al::tryEmitEffect(model, "SpinAttack", nullptr);
	al::tryStartSe(model, "SpinAttack");
}

// =========================================================
//                    NERVE OFFSETS
// =========================================================

const uintptr_t spinCapNrvOffset = 0x1D78940;
const uintptr_t nrvSpinCapFall = 0x1D7ff70;

const uintptr_t nrvHakoniwaFall = 0x1D78910;
const uintptr_t nrvHakoniwaHipDrop = 0x1D78978;
const uintptr_t nrvHakoniwaJump = 0x1D78948;
const uintptr_t nrvHakoniwaRun = 0x1D78928;
const uintptr_t nrvHakoniwaSquat = 0x1D78920;
const uintptr_t nrvHakoniwaWait = 0x1D78918;

// =========================================================
//                        FLAGS
// =========================================================

// Suit flags
inline bool isMario = false;
inline bool isClassic = false;
inline bool isNoCap = false;
inline bool isFeather = false;
inline bool isFire = false;
inline bool isIce = false;
inline bool isTanooki = false;
inline bool isDrill = false;
inline bool isMetal = false;
inline bool isFly = false;
inline bool isBrawl = false;
inline bool isSuper = false;
inline bool isKnight = false;
inline bool isCapeOn = false;
inline bool isWeaponOn = false;

// Action flags
inline bool canAction = false;
inline bool nextThrowLeft = true;
inline bool prevIsCarry = false;

// Player state flags
inline bool isSpinActive = false;
inline bool isSpinRethrow = false;
inline bool isPunchRight = false;
inline bool isDoubleJump = false;
inline bool isDoubleJumpConsume = false;
inline bool isNotFloat = false;
inline bool isSuperRunningOnSurface = false;
inline bool isAntiGravity = false;

// Proximity flags
inline bool isNearCollectible = false;
inline bool isNearTreasure = false;
inline bool isNearSwoonedEnemy = false;
inline bool isSneaking = false;

// =========================================================
//                       ACTORS
// =========================================================

inline PlayerActorHakoniwa* isHakoniwa = nullptr;
inline al::LiveActor* isMarioModel = nullptr; // the player's 3D model ("Normal"), set with isHakoniwa in initPlayer
inline HammerBrosHammer* isHammer = nullptr;
inline HammerBrosHammer* isSmashHammer = nullptr;
inline CustomGauge* isGauge = nullptr;
inline Motorcycle* isKart = nullptr;
inline WorldEndBorderKeeper* kartBorder = nullptr;
inline al::LiveActor* isKoopa = nullptr;
inline al::LiveActor* isNearTarget = nullptr; // nearest homing target this frame

// Actor groups
inline al::LiveActorGroup* fireBalls = nullptr;
inline al::LiveActorGroup* iceBalls = nullptr;
inline al::LiveActorGroup* iceCubes = nullptr;
inline al::LiveActorGroup* tankBullets = nullptr;

// =========================================================
//                        STATE
// =========================================================

// Hit buffer
inline al::LiveActor* hitBuffer[0x40];
inline int hitBufferCount = 0;

// Attack counters (-1 = inactive)
inline float attackSensorRemaining = -1; // anim frames, counts down at the anim's speed
inline int fireStep = -1;
inline int drillStep = -1;
inline int drillSensorRemaining = -1; // hitbox lingers N frames after drill pop
inline int guardWindow = 0; // frames left to parry
inline int isCapeActive = -1;
inline int isDashDelay = -1; // -1 = idle, counts down while R is held and running, dash engages at 0
inline int isMarioActive = 0; // 0 = none, 1 = enabling, -1 = disabling
inline int battleStance = 0; // frames left in the battle stance, 0 = off

// Hit effect state — set by EmitEffectHook when "Hit" fires on any actor
inline bool isEffect = false;
inline sead::Vector3f isSpawnPos;

// Constants
const f32 MIN_SPEED_RUN_ON_WATER = 15.0f;
const sead::Color4u8 paintClear(0, 0, 0, 0);

// Pose modifiers
inline sead::Vector3f glideRot = {0.0f, 0.0f, 0.0f};
inline sead::Vector3f legScale = {1.0f, 1.0f, 1.0f};

// Kart modifiers
inline float hoverBlend = 0.0f; // 0 = grounded, 1 = hover
inline float wheelTilt = 0.0f;
inline float wheelSpin = 0.0f;
inline float propellerSpin = 0.0f;
inline float propellerSpeed = 0.0f;
inline float propellerOffset = 0.0f;
inline sead::Quatf wheelSteerQuat = sead::Quatf::unit;
inline sead::Vector3f propellerScale = {0.0f, 0.0f, 0.0f};

// =========================================================
//                      CUSTOM STATES
// =========================================================

struct SpinState {
	bool isGalaxy = false;
	bool canGalaxy = true;
	bool canStandard = true;
	bool trigger = false;
	bool isFakeQueued = false; // fake spin waiting for the Fall nerve to start it, the hit timer ends it

	void resetForNewSpin() {
		canGalaxy = true;
		canStandard = true;
	}
};

inline SpinState spin;

// Enters the cap spin; trigger false = plain cap throw
inline void startSpinCap(PlayerActorHakoniwa* player, bool trigger = true) {
	spin.trigger = trigger;
	al::setNerve(player, getNerveAt(spinCapNrvOffset));
}

// The attack playing: its anim, its hitbox and the anim frame the hitbox turns on (0 = right away)
struct AttackState {
	const char* anim = nullptr;
	const char* sensor = nullptr;
	float hitFrame = 0.0f;
};

inline AttackState attack;

// Starts an attack in the main and sub anim; frames = anim frames from the start until it ends, -1 = when the anim ends
inline void startAttack(PlayerActorHakoniwa* player, const char* anim, const char* sensor, float hitFrame, float frames) {
	auto* animator = player->mAnimator;
	for (const char* name : {"GalaxySpin", "DoubleSpin", "Punch"}) al::invalidateHitSensor(player, name); // a new attack ends every other hitbox
	animator->startSubAnim(anim);
	animator->startAnim(anim);
	attack = {anim, sensor, hitFrame};
	hitBufferCount = 0; // a new attack can hit everything again
	attackSensorRemaining = frames < 0 ? animator->getAnimFrameMax() : frames;
	if (hitFrame <= 0) al::validateHitSensor(player, sensor); // no windup: hits straight away
}

// Still winding up: the attack's anim hasn't reached its hit frame yet
inline bool isWindup(const PlayerAnimator* anim) {
	return attack.anim && attack.hitFrame > 0 && anim->isAnim(attack.anim) && anim->getAnimFrame() <= attack.hitFrame;
}

// Turns the attack's hitbox on the frame its anim passes the hit frame (ground, air and water)
inline void updateWindup(PlayerActorHakoniwa* player) {
	auto* anim = player->mAnimator;
	float frame = anim->getAnimFrame();
	if (!attack.sensor || attack.hitFrame <= 0 || !anim->isAnim(attack.anim) || frame < attack.hitFrame || frame - anim->getAnimFrameRate() >= attack.hitFrame) return;
	al::validateHitSensor(player, attack.sensor);
}

// =========================================================
//                     TYPE CHECKS
// =========================================================

template<typename... Models>
inline bool isType(const al::LiveActor* actor, const char* name, Models... models) {
	if (!al::isEqualSubString(typeid(*actor).name(), name)) return false;
	if constexpr (sizeof...(models) == 0) return true;
	bool matched = false, excluded = false;
	((models[0] == '!' ? excluded |= al::isModelName(actor, models + 1) : matched |= al::isModelName(actor, models)), ...);
	return matched && !excluded;
}

template<typename... Names>
inline bool isAnyType(const al::LiveActor* actor, Names... names) {
	const char* type = typeid(*actor).name();
	bool matched = false, excluded = false;
	((names[0] == '!' ? excluded |= al::isEqualSubString(type, names + 1) : matched |= al::isEqualSubString(type, names)), ...);
	return matched && !excluded;
}

// Free-moving enemies have a movement anim; rooted ones (Packun, chained Wanwan) don't
inline bool isMobile(const al::LiveActor* actor) {
	for (const char* anim : {"Walk", "Fall", "Move", "Swim", "Run", "Jump", "Fly"}) // most common first, so most enemies exit on the first check
		if (al::isExistAction(actor, anim)) return true;
	return false;
}

// Check if has sensor type(s), each checked independently across all sensors
template<typename... Fns>
inline bool hasSensor(const al::LiveActor* actor, Fns... checks) {
	al::HitSensorKeeper* keeper = actor->getHitSensorKeeper();
	auto any = [&](auto check) {
		for (s32 i = 0; keeper && i < keeper->getSensorNum(); i++)
			if (check(keeper->getSensor(i))) return true;
		return false;
	};
	return (any(checks) && ...);
}

// =========================================================
//                       HELPERS
// =========================================================

inline sead::Vector3f setupHitEffect(const al::HitSensor* a, const al::HitSensor* b) {
	isEffect = false;
	isSpawnPos = (al::getSensorPos(a) + al::getSensorPos(b)) * 0.5f;
	isSpawnPos.y += 20.0f;
	return isSpawnPos;
}

inline sead::Vector3f getFireDir(const al::LiveActor* from, const al::LiveActor* to) {
	sead::Vector3f dir = al::getTrans(to) - al::getTrans(from);
	dir.normalize();
	return dir;
}

// nullptr = guarded Hit, any string = forced emit with that effect
inline void isHitEffect(al::LiveActor* thisPtr, al::LiveActor* targetHost, const char* effect = nullptr) {
	bool handled = isEffect;
	isEffect = false;
	if (handled) return;
	al::tryEmitEffect(thisPtr, effect ? effect : "Hit", &isSpawnPos);
}

inline bool isInHitBuffer(const al::LiveActor* actor) {
	if (hitBufferCount >= 0x40) return true; // full: every writer checks here first, so nothing writes past the end
	for (int i = 0; i < hitBufferCount; i++) {
		if (hitBuffer[i] == actor) return true;
	}
	return false;
}

// Guard Mario against attacks
inline bool isValidAttackTarget(const al::HitSensor* target) {
	al::LiveActor* targetHost = al::getSensorHost(target);
	return targetHost && !al::isSensorPlayerAll(target);
}

// Validate/invalidate a hit sensor and reset the hit buffer on activation
inline void updateAttackSensor(al::LiveActor* actor, const char* name, bool active, bool& was) {
	if (active && !was) { al::validateHitSensor(actor, name); hitBufferCount = 0; }
	else if (!active && was) al::invalidateHitSensor(actor, name);
	was = active;
}

// Returns the nearest valid target within maxDist, skipping already-hit actors
inline al::LiveActor* findNearestTarget(const al::LiveActor* player, f32 maxDist) {
	al::HitSensor* eye = al::getHitSensor(player, "Eye");
	if (!eye) return nullptr;

	al::LiveActor* nearest = nullptr;
	f32 best = maxDist;

	for (int i = 0; i < eye->mSensorCount; i++) {
		al::HitSensor* s = eye->mSensors[i];
		al::LiveActor* actor = al::getSensorHost(s);
		if (!actor || actor == player
			|| (!al::isSensorNpc(s) && !al::isSensorEnemyBody(s) && !al::isSensorMapObj(s))
			|| !al::isAlive(actor) || isInHitBuffer(actor)) continue;

		f32 d = al::calcDistance(player, actor);
		if (d < best) { best = d; nearest = actor; }
	}
	return nearest;
}

// Any enemy the Eye sensor sees that can hurt Mario; rocks have no attack sensor, friendly ones like Shibo also have an Npc one
inline bool isEnemyNear(const al::LiveActor* player) {
	al::HitSensor* eye = al::getHitSensor(player, "Eye");
	for (int i = 0; eye && i < eye->mSensorCount; i++) {
		al::HitSensor* s = eye->mSensors[i];
		if (!s->mIsValid || !al::isSensorEnemyBody(s) || isAnyType(al::getSensorHost(s), "FireBall")
			|| hasSensor(al::getSensorHost(s), al::isSensorNpc)) continue; // a defeated one switches its body sensor off
		al::HitSensorKeeper* keeper = al::getSensorHost(s)->getHitSensorKeeper();
		for (int j = 0; j < keeper->getSensorNum(); j++)
			if (al::isSensorEnemyAttack(keeper->getSensor(j))) return true;
	}
	return false;
}

// =========================================================
//                   ANIMATION CHECKS
// =========================================================

// Skeletal anim table of an actor's model, cached per actor so stale pointers compare but never deref
template <typename T>
inline const al::AnimInfoTable* getAnimTable(const T* actor) {
	static const T* owner = nullptr;
	static const al::AnimInfoTable* table = nullptr;

	if (actor == owner) return table;	// stale pointers only ever compare, never deref
	owner = actor;
	table = nullptr;
	if (!actor) return nullptr;

	const al::LiveActor* model = actor;
	if constexpr (requires { actor->mModelHolder; }) model = actor->mModelHolder->findModelActor("Normal");

	if (auto* skl = model->getModelKeeper()->getAnimSkl()) table = skl->getAnimInfoTable();
	return table;
}

// endSubAnim restarts the main anim when nothing is attached, so always gate it
inline void tryEndSubAnim(PlayerAnimator* anim) {
	if (anim->isSubAnimPlaying()) anim->endSubAnim();
}

// Upper body calls crash without the 3D model's slot (2D models, or no model yet mid stage init), so always go through these
inline bool hasUpperBody(const PlayerAnimator* anim) {
	return anim && anim->mModelHolder->mCurrentModel && anim->mModelHolder->isCurrentModelLabel("Normal");
}
inline bool isUpperBodyAnim(const PlayerAnimator* anim, const char* name) { return hasUpperBody(anim) && anim->isUpperBodyAnim(name); }
inline bool isUpperBodyAnimEnd(const PlayerAnimator* anim) { return !hasUpperBody(anim) || anim->isUpperBodyAnimEnd(); }
inline void tryStartUpperBodyAnim(PlayerAnimator* anim, const char* name) { if (hasUpperBody(anim)) anim->startUpperBodyAnim(name); }
inline void tryClearUpperBodyAnim(PlayerAnimator* anim) { if (hasUpperBody(anim)) anim->clearUpperBodyAnim(); }

inline bool isSwingAnim(const PlayerAnimator* anim) {
	if (!anim) return false;
	return al::isEqualString(anim->mCurAnim, "SwingAttack")
		|| al::isEqualString(anim->mCurAnim, "SwingAirAttack");
}

inline bool isBaseSpinAnim(const PlayerAnimator* anim) {
	return isSwingAnim(anim)
		|| al::isEqualString(anim->mCurAnim, "SpinSeparate")
		|| al::isEqualString(anim->mCurAnim, "SpinSeparateSwim")
		|| al::isEqualString(anim->mCurAnim, "SpinLow")
		|| al::isEqualString(anim->mCurAnim, "CapeAttack")
		|| al::isEqualString(anim->mCurAnim, "TailAttack");
}

inline bool isDoubleSpinAnim(const PlayerAnimator* anim) {
	return al::isEqualString(anim->mCurAnim, "SpinAttackLeft")
		|| al::isEqualString(anim->mCurAnim, "SpinAttackRight")
		|| al::isEqualString(anim->mCurAnim, "SpinAttackAirLeft")
		|| al::isEqualString(anim->mCurAnim, "SpinAttackAirRight");
}

inline bool isSpinAnim(const PlayerAnimator* anim) {
	if (!anim) return false;
	return isBaseSpinAnim(anim) || isDoubleSpinAnim(anim);
}

inline bool isPunchAnim(const PlayerAnimator* anim) {
	if (!anim) return false;
	return al::isEqualString(anim->mCurAnim, "PunchL")
		|| al::isEqualString(anim->mCurAnim, "PunchR")
		|| al::isEqualString(anim->mCurAnim, "RabbitGet")
		|| al::isEqualString(anim->mCurAnim, "Kick");
}

inline bool isJumpPunchAnim(const PlayerAnimator* anim) {
	if (!anim) return false;
	return al::isEqualString(anim->mCurAnim, "JumpPunchEndL")
		|| al::isEqualString(anim->mCurAnim, "JumpPunchEndR")
		|| al::isEqualString(anim->mCurAnim, "JumpPunchL")
		|| al::isEqualString(anim->mCurAnim, "JumpPunchR");
}

inline bool isHipDropAnim(const PlayerAnimator* anim) {
	if (!anim) return false;
	return al::isEqualString(anim->mCurAnim, "HipDrop")
		|| al::isEqualString(anim->mCurAnim, "HipDropPunch")
		|| al::isEqualString(anim->mCurAnim, "HipDropReaction")
		|| al::isEqualString(anim->mCurAnim, "HipDropPunchReaction")
		|| al::isEqualString(anim->mCurAnim, "SpinJumpDownFallL")
		|| al::isEqualString(anim->mCurAnim, "SpinJumpDownFallR")
		|| al::isEqualString(anim->mCurAnim, "SwimHipDrop")
		|| al::isEqualString(anim->mCurAnim, "SwimHipDropPunch")
		|| al::isEqualString(anim->mCurAnim, "SwimDive");
}

inline bool isDrillAnim(const PlayerAnimator* anim) {
	if (drillSensorRemaining > 0) return true;
	if (!anim) return false;
	return anim->isSubAnim("DrillIn")
		|| anim->isSubAnim("DrillOut")
		|| anim->isSubAnim("DrillOutFast");
}

// =========================================================
//                     PLAYER CHECKS
// =========================================================

// Captured actors keep vanilla behaviour
inline bool isHacking() { return isHakoniwa && isHakoniwa->mHackKeeper && isHakoniwa->mHackKeeper->mHackActor; }

// Not flickering from a hit, captured or in a cutscene
inline bool isPlayerActive(const PlayerActorHakoniwa* player) {
	bool isFlicker = player->mDamageKeeper && player->mDamageKeeper->mDamageInvalidCount > 0;
	return !isFlicker && !isHacking() && !rs::isActiveDemo(player);
}

// Underwater and too deep for the surface to matter
inline bool isSubmerged(const PlayerActorHakoniwa* player) {
	auto* wsf = player->mWaterSurfaceFinder;
	return al::isInWater(player) && !(wsf && wsf->isFoundSurface() && wsf->getDistance() <= 80.0f);
}

// Handle Guard state
inline bool isGuarding() {
	return isHakoniwa && (isUpperBodyAnim(isHakoniwa->mAnimator, "HitGuard") || isHakoniwa->mAnimator->isAnim("HitGuard"));
}

// Handle active action state
inline bool isActionBusy() { return isGuarding() || fireStep >= 0 || drillStep >= 0; }

// Any attack or guard in progress: the attack's anim is still playing, a shot, a guard or the hammer
inline bool isAttacking(const PlayerActorHakoniwa* player) {
	return (attack.anim && player->mAnimator->isAnim(attack.anim)) || fireStep >= 0 || isGuarding() || (isHammer && al::isAlive(isHammer));
}