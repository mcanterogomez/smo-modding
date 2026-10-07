#pragma once
#include "custom/.Globals.h"
#include "Library/Joint/JointControllerBase.h"
#include "Library/Model/ModelCtrl.h"
#include "Library/Model/ModelKeeper.h"
#include "nn/g3d/ModelObj.h"
#include "nn/g3d/ResSkeleton.h"
#include <algorithm>

// A character built on Mario's skeleton: its body's clips come converted at build time (PlayerAnimationRig, loaded here instead of Mario's PlayerAnimation);
// its parts (gloves) play Mario's animations converted live, each joint taking Mario's turn on the part's own rest pose and bone lengths.
// The build tools store each part bone's Mario rest values as bone user data ("MarioTrans", "MarioRot"); models without them are left alone.
namespace PlayerRetarget {

	using Mtx = sead::Matrix34f;
	using Vec = sead::Vector3f;
	constexpr s32 maxJoints = 32;
	static_assert(sizeof(nn::g3d::ResBone) == 0x60);

	// g3d SkeletonObj: +0x10 ResBone array, +0x18 local array (stride 0x60, scale at +0x10), +0x20 world array (stride 0x40, four xyz_ columns), +0x50 bone count
	inline u8* getSkeleton(const al::LiveActor* model) { return (u8*)model->getModelKeeper()->getModelCtrl()->getModelObj()->GetSkeleton(); }

	inline Vec rotated(const Mtx& m, const Vec& v) { Vec r; r.setRotated(m, v); return r; }
	inline Vec perpendicular(const Vec& v, const Vec& unit) { return v - unit * v.dot(unit); }
	inline f32 smooth(f32 x) { return x * x * (3.0f - 2.0f * x); }
	inline Mtx worldAt(u8* skel, s32 id) {   // a joint's world matrix in the skeleton's buffer (four xyz_ columns)
		const f32* e = (const f32*)(*(u8**)(skel + 0x20) + id * 0x40);
		Mtx w;
		for (s32 c = 0; c < 4; c++) for (s32 r = 0; r < 3; r++) w.m[r][c] = e[c * 4 + r];
		return w;
	}
	inline void setWorldAt(u8* skel, s32 id, const Mtx& w) {
		f32* e = (f32*)(*(u8**)(skel + 0x20) + id * 0x40);
		for (s32 c = 0; c < 4; c++) for (s32 r = 0; r < 3; r++) e[c * 4 + r] = w.m[r][c];
	}
	inline Mtx swung(const Mtx& old, Vec from, Vec to, const Vec& at) {   // a bone turned only as much as needed to point from one direction to another: it keeps its own twist
		from.normalize(); to.normalize();
		sead::Quatf q; q.makeVectorRotation(from, to);
		Mtx r, out; r.fromQuat(q); out.setMul(r, old); out.setTranslation(at);
		return out;
	}

	class RetargetController : public al::JointControllerBase {
	public:
		RetargetController(al::LiveActor* model, s32 count) : al::JointControllerBase(count), mModel(model) {}

		// Inside the game's skeleton pass, joint by joint: Mario's turn of this joint under its parent, put on the part's own rest pose and bone length
		void calcJointCallback(s32 id, sead::Matrix34f* mtx) override {
			s32 p = mParent[id];
			if (p < 0) return;
			Mtx parent = readWorld(p), inv, rel, a, b;
			inv.setInverse(parent);
			rel.setMul(inv, *mtx);
			const f32* s = scaleOf(p);   // the parent's scale moves its children (Maya scale), so the length difference grows with it
			rel.setTranslation(rel.getTranslation() + Vec(mOffset[id].x * s[0], mOffset[id].y * s[1], mOffset[id].z * s[2]));
			a.setMul(parent, mTurnInv[p]); b.setMul(a, rel); mtx->setMul(b, mTurn[id]);
		}

		const char* getCtrlTypeName() const override { return "PlayerRetarget"; }
		// The lib header's (u32) signature isn't what the game exports, so these stay local instead of importing missing symbols
		bool tryValidateConstraints(u32) override { return false; }
		bool tryInvalidateConstraints(u32) override { return false; }

		al::LiveActor* mModel;
		s32 mParent[maxJoints];
		Vec mOffset[maxJoints]; // the part's bone translation minus Mario's, in Mario's frame of the parent
		Mtx mTurn[maxJoints], mTurnInv[maxJoints]; // each joint's own rest frame seen from Mario's rest frame (rotation only)

	private:
		Mtx readWorld(s32 id) const { return worldAt(getSkeleton(mModel), id); }
		const f32* scaleOf(s32 id) const { return (const f32*)(*(u8**)(getSkeleton(mModel) + 0x18) + id * 0x60 + 0x10); }
	};

	// Controllers come from a fixed free list per keeper and a full one crashes on register; a part without a keeper gets its own
	inline bool tryReserveControllerSlot(al::LiveActor* model) {
		if (!al::isExistJointControllerKeeper(model)) { al::initJointControllerKeeper(model, 1); return true; }
		u8* keeper = *(u8**)((u8*)model->getModelKeeper()->getModelCtrl() + 0x3C0);
		return keeper && *(void**)(keeper + 0x18); // free list head
	}

	// Float array the build tools stored on the bone under this name
	inline const f32* getUserFloats(const nn::g3d::ResBone& bone, const char* name, s32* count) {
		for (s32 i = 0; i < bone.GetUserDataCount(); i++) {
			const auto* data = bone.GetUserData(i);
			if (!al::isEqualString(data->GetName(), name)) continue;
			*count = data->GetCount();
			return data->GetFloat();
		}
		*count = 0;
		return nullptr;
	}

	inline void tryRetarget(al::LiveActor* model) {
		u8* skel = getSkeleton(model);
		s32 count = *(u16*)(skel + 0x50);
		const auto* bones = *(const nn::g3d::ResBone**)(skel + 0x10);
		if (count > maxJoints) return;
		s32 parent[maxJoints];
		Vec offset[maxJoints];
		Mtx own[maxJoints], mario[maxJoints], turn[maxJoints]; // rest poses (the part's own, Mario's) and each joint's own rest frame seen from Mario's
		bool hasMario = false;
		for (s32 i = 0; i < count; i++) {
			const auto& t = bones[i].GetTranslate();
			const auto& r = bones[i].GetRotateEuler();
			Vec body(t.x, t.y, t.z), rot(r.x, r.y, r.z), marioRest = body, marioRot = rot;
			s32 n;
			if (const f32* f = getUserFloats(bones[i], "MarioTrans", &n); n == 3) { marioRest.set(f[0], f[1], f[2]); hasMario = true; }
			if (const f32* f = getUserFloats(bones[i], "MarioRot", &n); n == 3) marioRot.set(f[0], f[1], f[2]);
			s32 p = parent[i] = bones[i].GetParentIndex() == nn::g3d::ResBone::InvalidBoneIndex ? -1 : bones[i].GetParentIndex();
			Mtx lo, lm, inv;
			lo.makeRT(rot, body); lm.makeRT(marioRot, marioRest);
			if (p >= 0) { own[i].setMul(own[p], lo); mario[i].setMul(mario[p], lm); }
			else { own[i] = lo; mario[i] = lm; }
			inv.setInverse(mario[i]);
			turn[i].setMul(inv, own[i]);
			turn[i].setTranslation(Vec::zero);
			offset[i] = (p >= 0 ? rotated(turn[p], body) : body) - marioRest;   // the length difference, measured in Mario's frame of the parent
		}
		if (!hasMario || !tryReserveControllerSlot(model)) return;

		auto* ctrl = new RetargetController(model, count);
		for (s32 i = 0; i < count; i++) {
			ctrl->mParent[i] = parent[i];
			ctrl->mOffset[i] = offset[i];
			ctrl->mTurn[i] = turn[i];
			ctrl->mTurnInv[i].setInverse(turn[i]);
			ctrl->appendJointId(i);
		}
		al::registerJointController(model, ctrl);
	}

	// The body and each of its parts decide from their own bones; parts sharing a model with one already done (the silhouette) are skipped
	inline void executeInitPlayer(al::LiveActor* model) {
		if (!model || !model->getModelKeeper()) return;
		tryRetarget(model);
		for (s32 i = 0; i < al::getSubActorNum(model); i++) {
			al::LiveActor* part = al::getSubActor(model, i);
			if (part && part->getModelKeeper() && part->getModelKeeper() != model->getModelKeeper()) tryRetarget(part);
		}
	}

	// The enabled character ships its converted clips
	inline bool isRigInstalled() {
		static bool isRig = al::isExistFile("ObjectData/PlayerAnimationRig.szs");
		return isRig;
	}

	// Every body ("Mario": the player's, cutscene and closet copies, all set up here) loads the converted clips instead of Mario's; costumes keep his
	struct BodyAnimationArchiveHook : public mallow::hook::Inline<BodyAnimationArchiveHook> {
		static void Callback(exl::hook::InlineCtx* ctx) {
			const char* model = (const char*)ctx->X[28];
			if (isRigInstalled() && model && al::isEqualString(model, "Mario")) ctx->X[24] = (u64)"PlayerAnimationRig";
		}
	};

	// Cutscene copies of the body: their parts (gloves) get the same live retarget as the player's
	struct DemoBodyHook : public mallow::hook::Trampoline<DemoBodyHook> {
		static void* Callback(void** parts, al::LiveActor* body, const void* info, const char* bodyName, const char* capName, const void* pConst, void* noseScale, void* earScale, bool isCloset) {
			void* costume = Orig(parts, body, info, bodyName, capName, pConst, noseScale, earScale, isCloset);
			executeInitPlayer(body);
			return costume;
		}
	};

	// Closet copies of the body: the same
	struct ClosetBodyHook : public mallow::hook::Trampoline<ClosetBodyHook> {
		static void* Callback(void** parts, al::LiveActor* body, const void* info, const char* bodyName, const char* capName, const void* pConst, void* noseScale, void* earScale, void** follow, void* followMtx) {
			void* costume = Orig(parts, body, info, bodyName, capName, pConst, noseScale, earScale, follow, followMtx);
			executeInitPlayer(body);
			return costume;
		}
	};

	// The game's leg IK (ground, slopes, landings, crouching) puts the knee on the thigh's +Y side and rebuilds both leg bones around it. On a body whose shin is
	// long against its thigh, a deep bend turns that axis past the knee and the knee goes backward or inward. It is given the side the knee is really on (the game's
	// own for a nearly straight leg). It places the knee with the game's math (al::calcBetweenTwoLinkPos, same lengths and slack), but a foot closer than the leg
	// can fold keeps the leg folded flat on that side (the game drops the knee onto the hip-foot line there, so it jumps); each leg bone keeps its own twist
	struct GroundPoseLegBottomHook : public mallow::hook::Trampoline<GroundPoseLegBottomHook> {
		static void Callback(u8* thisPtr, Mtx* foot, Mtx* legs, const s32* joints) {
			Mtx thigh = legs[0], knee = legs[1];
			Vec H = thigh.getTranslation(), K = knee.getTranslation(), F = foot->getTranslation(), y;
			f32 l1 = (K - H).length(), l2 = (F - K).length(), al = (F - H).length();
			if (!isRigInstalled() || al < 1e-4f || l1 < 1e-4f) { Orig(thisPtr, foot, legs, joints); return; }
			Vec off = perpendicular(K - H, (F - H) * (1.0f / al));
			thigh.getBase(y, 1); y.normalize();
			f32 b = smooth(std::clamp(off.length() / (0.15f * l1), 0.0f, 1.0f));
			Vec side = off.length() > 1e-6f ? off * (b / off.length()) + y * (1.0f - b) : y;
			side.normalize();
			legs[0].setBase(1, side);
			Orig(thisPtr, foot, legs, joints);
			u8* skel = getSkeleton(*(al::LiveActor**)(thisPtr + 0x28));
			Vec Fg = foot->getTranslation(), v = H - Fg, kn = worldAt(skel, joints[1]).getTranslation();   // the game's foot target and knee
			f32 d = v.length(), slack = *(thisPtr + 492) ? 0.0f : 2.0f;   // the game's slack: a leg never quite straight
			if (d > 1e-3f) {
				Vec u = v * (1.0f / d), p = perpendicular(side, u);
				if (d + slack > l1 + l2) { f32 g = (d + slack) / (l1 + l2); l1 *= g; l2 *= g; }
				if (p.length() > 1e-6f) {
					p.normalize();
					f32 t = std::clamp((l2 * l2 - l1 * l1 + d * d) / (2.0f * d), -l2, l2);   // along the foot-to-hip line, from the foot
					kn = Fg + u * t + p * sqrtf(std::max(l2 * l2 - t * t, 0.0f));
				}
			}
			setWorldAt(skel, joints[0], swung(thigh, K - H, kn - H, H));
			setWorldAt(skel, joints[1], swung(knee, F - K, Fg - kn, kn));
		}
	};

	inline void Install() {
		GroundPoseLegBottomHook::InstallAtSymbol("_ZN28PlayerJointControlGroundPose16calcLegBottomMtxEPN4sead8Matrix34IfEERKNS0_9SafeArrayIS2_Li2EEERKNS4_IiLi2EEE");
		BodyAnimationArchiveHook::InstallAtOffset(0x4440C0); // the shared body setup (initMarioModelActor, its Demo and Closet versions), where the "PlayerAnimation" archive name is passed on
		DemoBodyHook::InstallAtSymbol("_ZN14PlayerFunction23initMarioModelActorDemoEPP31PlayerJointControlPartsDynamicsPN2al9LiveActorERKNS3_13ActorInitInfoEPKcSA_PK11PlayerConstPN4sead7Vector3IfEESH_b");
		ClosetBodyHook::InstallAtSymbol("_ZN14PlayerFunction25initMarioModelActorClosetEPP31PlayerJointControlPartsDynamicsPN2al9LiveActorERKNS3_13ActorInitInfoEPKcSA_PK11PlayerConstPN4sead7Vector3IfEESH_PP30PlayerJointControlFollowMtxPtrPNSE_8Matrix34IfEE");
	}
}
