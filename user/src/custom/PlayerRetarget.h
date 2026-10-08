#pragma once
#include "custom/.Globals.h"
#include "Library/Joint/JointControllerBase.h"
#include "Library/Joint/JointSpringControllerHolder.h"
#include "Library/LiveActor/ActorResourceFunction.h"
#include "Library/Yaml/ByamlIter.h"
#include "Library/Model/ModelCtrl.h"
#include "Library/Model/ModelKeeper.h"
#include "nn/g3d/ModelObj.h"
#include "nn/g3d/ResSkeleton.h"
#include <algorithm>

// A character on Mario's skeleton: body clips converted at build time (PlayerAnimationRig), gloves converted live from Mario's rest values in bone user data ("MarioTrans", "MarioRot")
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
	inline Vec transformed(const Mtx& m, const Vec& v) { return rotated(m, v) + m.getTranslation(); }
	inline Mtx turnBetween(Vec from, Vec to) {   // the shortest turn from one direction to another
		from.normalize(); to.normalize();
		sead::Quatf q; q.makeVectorRotation(from, to);
		Mtx r; r.fromQuat(q); r.setTranslation(Vec::zero);
		return r;
	}
	inline Mtx swung(const Mtx& old, Vec from, Vec to, const Vec& at) {   // a bone turned only as much as needed to point from one direction to another: it keeps its own twist
		Mtx out; out.setMul(turnBetween(from, to), old); out.setTranslation(at);
		return out;
	}
	inline Mtx turnedAbout(const Mtx& m, const Mtx& turn, const Vec& from, const Vec& to) {   // a joint carried by a turn about one point that lands on another
		Mtx out;
		out.setMul(turn, m);
		out.setTranslation(to + rotated(turn, m.getTranslation() - from));
		return out;
	}

	class RetargetController : public al::JointControllerBase {
	public:
		RetargetController(al::LiveActor* model, s32 count) : al::JointControllerBase(count), mModel(model) {}

		// Inside the game's skeleton pass, joint by joint: Mario's turn of this joint under its parent, put on the part's own rest pose and bone length
		void calcJointCallback(s32 id, sead::Matrix34f* mtx) override {
			s32 p = mParent[id];
			if (p < 0) return;
			u8* skel = getSkeleton(mModel);
			Mtx parent = worldAt(skel, p), inv, rel, a, b;
			inv.setInverse(parent);
			rel.setMul(inv, *mtx);
			const f32* s = scaleOf(skel, p);   // the parent's scale moves its children (Maya scale), so the length difference grows with it
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
		static const f32* scaleOf(u8* skel, s32 id) { return (const f32*)(*(u8**)(skel + 0x18) + id * 0x60 + 0x10); }
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
		bool hasMario = false;
		for (s32 i = 0; i < count && !hasMario; i++) { s32 n; getUserFloats(bones[i], "MarioTrans", &n); hasMario = n == 3; }
		if (!hasMario) return;
		s32 parent[maxJoints];
		Vec offset[maxJoints];
		Mtx own[maxJoints], mario[maxJoints], turn[maxJoints]; // rest poses (the part's own, Mario's) and each joint's own rest frame seen from Mario's
		for (s32 i = 0; i < count; i++) {
			const auto& t = bones[i].GetTranslate();
			const auto& r = bones[i].GetRotateEuler();
			Vec body(t.x, t.y, t.z), rot(r.x, r.y, r.z), marioRest = body, marioRot = rot;
			s32 n;
			if (const f32* f = getUserFloats(bones[i], "MarioTrans", &n); n == 3) marioRest.set(f[0], f[1], f[2]);
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
		if (!tryReserveControllerSlot(model)) return;

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

	// Springs listed by the character's build (quills, nose): one game JointSpringController per entry of InitJointSpringCtrl in the model's archive
	inline s32 getSpringCount(const al::LiveActor* model) {
		if (!al::isExistModelResourceYaml(model, "InitJointSpringCtrl", nullptr)) return 0;
		return al::ByamlIter(al::getModelResourceYaml(model, "InitJointSpringCtrl", nullptr)).getSize();
	}

	inline void tryInitSprings(al::LiveActor* model) {
		if (getSpringCount(model) <= 0) return;
		if (!al::isExistJointControllerKeeper(model)) al::JointSpringControllerHolder::tryCreateAndInitJointControllerKeeper(model, "InitJointSpringCtrl");
		else (new al::JointSpringControllerHolder())->init(model, "InitJointSpringCtrl");
	}

	// Scooter and kart mix their L/C/R clips by steering, and a mix of converted arms leaves the fists off the handles: each arm reaches its fist to Mario's spot mixed the same way
	namespace RideGrip {
		constexpr s32 vehicleCount = 2, familyCount = 5, maxBones = 64;
		inline const char* sVehicles[vehicleCount] = {"Motorcycle", "Kart"};
		inline const char* sFamilies[familyCount] = {"Ride", "RideClash", "RideJump", "RideLand", "RideRunStart"};
		inline const char* sSides[3] = {"L", "C", "R"}; // the game's weight order
		struct Clip { s32 frameMax = -1; f32* pos = nullptr; }; // per frame: left fist xyz, right fist xyz, seen from nw4f_root
		inline Clip sClips[vehicleCount][familyCount][3];
		inline Vec sGrip[2]; // the character's fist in its hand joint's space
		inline bool sIsLoaded = false;
		inline Vec readVec(const al::ByamlIter& root, const char* key) {
			al::ByamlIter it;
			f32 x = 0, y = 0, z = 0;
			if (root.tryGetIterByKey(&it, key)) { it.tryGetFloatByIndex(&x, 0); it.tryGetFloatByIndex(&y, 1); it.tryGetFloatByIndex(&z, 2); }
			return Vec(x, y, z);
		}
	}

	class RideGripController : public al::JointControllerBase {
	public:
		RideGripController(al::LiveActor* model) : al::JointControllerBase(2), mModel(model) {}

		// Inside the skeleton pass, at the hand (its arm already placed): the mixed spot, reached by the two arm bones with the elbow kept on its side; the hand keeps its turn
		void calcJointCallback(s32 id, sead::Matrix34f* mtx) override {
			s32 s = id == mHand[0] ? 0 : 1;
			if (s == 0 || !mIsUpdated) update();   // both hands share the move, its weights and the fade: worked out once a frame, at the first hand
			mIsUpdated = s == 0;
			if (mInfluence <= 0.0f) return;

			u8* skel = getSkeleton(mModel);
			Vec T = transformed(worldAt(skel, 0), mTarget[s]);
			Vec S = worldAt(skel, mArm1[s]).getTranslation(), E = worldAt(skel, mArm2[s]).getTranslation(), W = mtx->getTranslation();
			Vec Wd = W + (T - transformed(*mtx, RideGrip::sGrip[s])) * mInfluence;
			f32 l1 = (E - S).length(), l2 = (W - E).length(), d = (Wd - S).length();
			if (d < 1e-4f || l1 < 1e-4f || l2 < 1e-4f) return;
			f32 g = std::max(1.0f, d / (l1 + l2));   // out of reach: both bones longer
			l1 *= g; l2 *= g;
			Vec u = (Wd - S) * (1.0f / d), sw = W - S;
			if (sw.length() < 1e-4f) return;
			sw.normalize();
			Vec p = perpendicular(perpendicular(E - S, sw), u);
			if (p.length() < 1e-6f) return;
			p.normalize();
			f32 a = std::clamp((l1 * l1 - l2 * l2 + d * d) / (2.0f * d), -l1, l1);
			Vec E2 = S + u * a + p * sqrtf(std::max(l1 * l1 - a * a, 0.0f));
			Mtx r1 = turnBetween(E - S, E2 - S), r2 = turnBetween(rotated(r1, W - E), Wd - E2), r21;
			r21.setMul(r2, r1);
			for (s32 i = 0; i < mUpperCount[s]; i++) setWorldAt(skel, mUpper[s][i], turnedAbout(worldAt(skel, mUpper[s][i]), r1, S, S));
			for (s32 i = 0; i < mLowerCount[s]; i++) setWorldAt(skel, mLower[s][i], turnedAbout(worldAt(skel, mLower[s][i]), r21, E, E2));
			mtx->setTranslation(Wd);
		}

		// This frame's spots: a new move slides over from the last spots, and off the ride moves the hands let go over 8 frames from them
		void update() {
			Vec target[2];
			s32 move = findTargets(target);
			if (move < 0) { mMove = -1; mInfluence = std::max(mInfluence - 0.125f, 0.0f); return; }
			if (move != mMove) { mFrom[0] = mTarget[0]; mFrom[1] = mTarget[1]; mBlend = mInfluence > 0.0f ? 0.0f : 1.0f; mMove = move; }
			mBlend = std::min(mBlend + 0.125f, 1.0f);
			for (s32 s = 0; s < 2; s++) mTarget[s] = mFrom[s] + (target[s] - mFrom[s]) * mBlend;
			mInfluence = std::min(mInfluence + 0.125f, 1.0f);
		}

		// Both spots from the clips the game is mixing (scooter or kart, by the move), at the move's frame, with its weights; -1 off the ride moves
		s32 findTargets(Vec* out) const {
			if (!isHakoniwa || !isHakoniwa->mAnimator || !isHakoniwa->mBindKeeper || !isHakoniwa->mBindKeeper->mBindSensor) return -1;
			PlayerAnimator* anim = isHakoniwa->mAnimator;
			const char* cur = anim->mCurAnim.cstr();
			if (!anim->mSklAnimBlendWeights || !al::isStartWithString(cur, "Motorcycle")) return -1;
			s32 family = -1;
			for (s32 i = 0; i < RideGrip::familyCount; i++) if (al::isEqualString(cur + 10, RideGrip::sFamilies[i])) family = i;   // past "Motorcycle"
			if (family < 0) return -1;
			al::LiveActor* host = al::getSensorHost(isHakoniwa->mBindKeeper->mBindSensor);   // the kart is the kart module's scooter with its own model (isKart lives in that module)
			if (!host || !host->getModelKeeper()) return -1;
			s32 v = al::isEqualString(host->getName(), "Kart") || al::isEqualString(al::getModelName(host), "PlayerKart") ? 1 : 0;
			f32 frame = anim->getAnimFrame(), total = 0.0f;
			out[0] = out[1] = Vec::zero;
			for (s32 k = 0; k < 3; k++) {
				const RideGrip::Clip& c = RideGrip::sClips[v][family][k];
				f32 w = anim->mSklAnimBlendWeights[k];
				if (!c.pos || w <= 0.0f) continue;
				f32 f = std::clamp(frame, 0.0f, (f32)c.frameMax);
				s32 i = (s32)f, j = std::min(i + 1, c.frameMax);
				for (s32 s = 0; s < 2; s++) {
					const f32* a = c.pos + i * 6 + s * 3;
					const f32* b = c.pos + j * 6 + s * 3;
					Vec pa(a[0], a[1], a[2]), pb(b[0], b[1], b[2]);
					out[s] += (pa + (pb - pa) * (f - i)) * w;
				}
				total += w;
			}
			if (total < 0.5f) return -1;
			out[0] *= 1.0f / total; out[1] *= 1.0f / total;
			return v * RideGrip::familyCount + family;
		}

		const char* getCtrlTypeName() const override { return "PlayerRideGrip"; }
		bool tryValidateConstraints(u32) override { return false; }
		bool tryInvalidateConstraints(u32) override { return false; }

		al::LiveActor* mModel;
		s32 mHand[2], mArm1[2], mArm2[2];
		s32 mUpper[2][RideGrip::maxBones], mLower[2][RideGrip::maxBones], mUpperCount[2] = {}, mLowerCount[2] = {}; // joints turned with the upper arm, and with the forearm (computed before the hand)
		s32 mMove = -1;
		bool mIsUpdated = false;
		f32 mInfluence = 0.0f, mBlend = 1.0f;
		Vec mTarget[2] = {Vec::zero, Vec::zero}, mFrom[2] = {Vec::zero, Vec::zero}; // spots seen from nw4f_root
	};

	// The table ships in the body's archive (RideGrip.byml, read once); the player's body gets the controller
	inline void tryInitRideGrip(al::LiveActor* model) {
		if (model != isMarioModel || !al::isExistModelResourceYaml(model, "RideGrip", nullptr)) return;
		if (!RideGrip::sIsLoaded) {
			al::ByamlIter root(al::getModelResourceYaml(model, "RideGrip", nullptr)), clips, it;
			RideGrip::sGrip[0] = RideGrip::readVec(root, "GripL");
			RideGrip::sGrip[1] = RideGrip::readVec(root, "GripR");
			if (!root.tryGetIterByKey(&clips, "Clips")) return;
			for (s32 v = 0; v < RideGrip::vehicleCount; v++) for (s32 f = 0; f < RideGrip::familyCount; f++) for (s32 k = 0; k < 3; k++) {
				if (!clips.tryGetIterByKey(&it, al::StringTmp<64>("%s%s%s", RideGrip::sVehicles[v], RideGrip::sFamilies[f], RideGrip::sSides[k]).cstr())) continue;
				s32 n = it.getSize();
				if (n < 6) continue;
				RideGrip::Clip& c = RideGrip::sClips[v][f][k];
				c.pos = new f32[n];
				for (s32 i = 0; i < n; i++) it.tryGetFloatByIndex(&c.pos[i], i);
				c.frameMax = n / 6 - 1;
			}
			RideGrip::sIsLoaded = true;
		}
		u8* skel = getSkeleton(model);
		s32 count = *(u16*)(skel + 0x50);
		const auto* bones = *(const nn::g3d::ResBone**)(skel + 0x10);
		if (count > RideGrip::maxBones) return;
		s32 arm1[2], arm2[2], hand[2];
		for (s32 s = 0; s < 2; s++) {
			const char* side = s ? "R" : "L";
			arm1[s] = al::getJointIndex(model, al::StringTmp<16>("Arm%s1", side).cstr());
			arm2[s] = al::getJointIndex(model, al::StringTmp<16>("Arm%s2", side).cstr());
			hand[s] = al::getJointIndex(model, al::StringTmp<16>("Hand%s", side).cstr());
			if (arm1[s] < 0 || arm2[s] < 0 || hand[s] < 0) return;
		}
		if (!tryReserveControllerSlot(model)) return;
		auto* ctrl = new RideGripController(model);
		auto isUnder = [&](s32 j, s32 root) { for (s32 k = j; k >= 0; k = bones[k].GetParentIndex() == nn::g3d::ResBone::InvalidBoneIndex ? -1 : bones[k].GetParentIndex()) if (k == root) return true; return false; };
		for (s32 s = 0; s < 2; s++) {
			ctrl->mArm1[s] = arm1[s]; ctrl->mArm2[s] = arm2[s]; ctrl->mHand[s] = hand[s];
			for (s32 j = 0; j < ctrl->mHand[s]; j++) {
				if (isUnder(j, ctrl->mArm2[s])) ctrl->mLower[s][ctrl->mLowerCount[s]++] = j;
				else if (isUnder(j, ctrl->mArm1[s])) ctrl->mUpper[s][ctrl->mUpperCount[s]++] = j;
			}
		}
		ctrl->appendJointId(ctrl->mHand[0]);
		ctrl->appendJointId(ctrl->mHand[1]);
		al::registerJointController(model, ctrl);
	}

	// The body and each of its parts decide from their own bones; parts sharing a model with one already done (the silhouette) are skipped
	inline void executeInitPlayer(al::LiveActor* model) {
		if (!model || !model->getModelKeeper()) return;
		tryRetarget(model);
		tryInitSprings(model);
		tryInitRideGrip(model);
		for (s32 i = 0; i < al::getSubActorNum(model); i++) {
			al::LiveActor* part = al::getSubActor(model, i);
			if (part && part->getModelKeeper() && part->getModelKeeper() != model->getModelKeeper()) tryRetarget(part);
		}
	}

	// A body's controller list is sized when it is made and a full one crashes on register: a body with springs or ride grips gets room for them
	struct KeeperSizeHook : public mallow::hook::Trampoline<KeeperSizeHook> {
		static void Callback(const al::LiveActor* actor, s32 count) {
			if (al::isEqualString(al::getModelName(actor), "Mario")) count += getSpringCount(actor) + (al::isExistModelResourceYaml(actor, "RideGrip", nullptr) ? 1 : 0);
			Orig(actor, count);
		}
	};

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

	// The game's leg IK (ground, slopes, landings, crouching) puts the knee on the thigh's +Y side: on a long shin a deep bend turns that axis past the knee, so it goes backward or inward
	struct GroundPoseLegBottomHook : public mallow::hook::Trampoline<GroundPoseLegBottomHook> {
		static void Callback(u8* thisPtr, Mtx* foot, Mtx* legs, const s32* joints) {
			Mtx thigh = legs[0], knee = legs[1];
			Vec H = thigh.getTranslation(), K = knee.getTranslation(), F = foot->getTranslation(), y;
			f32 l1 = (K - H).length(), l2 = (F - K).length(), al = (F - H).length();
			if (!isRigInstalled() || al < 1e-4f || l1 < 1e-4f) { Orig(thisPtr, foot, legs, joints); return; }
			// the side the knee is really on (the game's own +Y for a nearly straight leg)
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
			// the knee placed with the game's math (al::calcBetweenTwoLinkPos, same lengths and slack)
			if (d > 1e-3f) {
				Vec u = v * (1.0f / d), p = perpendicular(side, u);
				if (d + slack > l1 + l2) { f32 g = (d + slack) / (l1 + l2); l1 *= g; l2 *= g; }
				if (p.length() > 1e-6f) {
					p.normalize();
					f32 t = std::clamp((l2 * l2 - l1 * l1 + d * d) / (2.0f * d), -l2, l2);   // along the foot-to-hip line, from the foot; a foot closer than the leg folds stays folded flat (the game's knee jumps there)
					kn = Fg + u * t + p * sqrtf(std::max(l2 * l2 - t * t, 0.0f));
				}
			}
			setWorldAt(skel, joints[0], swung(thigh, K - H, kn - H, H));   // each leg bone keeps its own twist
			setWorldAt(skel, joints[1], swung(knee, F - K, Fg - kn, kn));
		}
	};

	inline void Install() {
		KeeperSizeHook::InstallAtSymbol("_ZN2al25initJointControllerKeeperEPKNS_9LiveActorEi");
		GroundPoseLegBottomHook::InstallAtSymbol("_ZN28PlayerJointControlGroundPose16calcLegBottomMtxEPN4sead8Matrix34IfEERKNS0_9SafeArrayIS2_Li2EEERKNS4_IiLi2EEE");
		BodyAnimationArchiveHook::InstallAtOffset(0x4440C0); // the shared body setup (initMarioModelActor, its Demo and Closet versions), where the "PlayerAnimation" archive name is passed on
		DemoBodyHook::InstallAtSymbol("_ZN14PlayerFunction23initMarioModelActorDemoEPP31PlayerJointControlPartsDynamicsPN2al9LiveActorERKNS3_13ActorInitInfoEPKcSA_PK11PlayerConstPN4sead7Vector3IfEESH_b");
		ClosetBodyHook::InstallAtSymbol("_ZN14PlayerFunction25initMarioModelActorClosetEPP31PlayerJointControlPartsDynamicsPN2al9LiveActorERKNS3_13ActorInitInfoEPKcSA_PK11PlayerConstPN4sead7Vector3IfEESH_PP30PlayerJointControlFollowMtxPtrPNSE_8Matrix34IfEE");
	}
}
