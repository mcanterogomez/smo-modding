#pragma once
#include "custom/PlayerResources.h"
#include "Library/Joint/JointControllerBase.h"
#include "Library/Joint/JointSpringControllerHolder.h"
#include "Library/LiveActor/ActorResourceFunction.h"
#include "Library/Model/ModelCtrl.h"
#include "Library/Model/ModelKeeper.h"
#include "nn/g3d/ModelObj.h"
#include "nn/g3d/ResSkeleton.h"

// A character on Mario's skeleton: its body, glove and face clips converted at build time (PlayerAnimation<body>, PlayerAnimationHandL<body>...)
namespace PlayerRetarget {

	// =========================================================
	//                       SKELETON MATH
	// =========================================================

	using Mtx = sead::Matrix34f;
	using Vec = sead::Vector3f;
	static_assert(sizeof(nn::g3d::ResBone) == 0x60);

	// g3d SkeletonObj: +0x10 ResBone array, +0x20 world array (stride 0x40, four xyz_ columns)
	inline u8* getSkeleton(const al::LiveActor* model) { return (u8*)model->getModelKeeper()->getModelCtrl()->getModelObj()->GetSkeleton(); }
	inline const nn::g3d::ResBone* resBones(u8* skel) { return *(const nn::g3d::ResBone**)(skel + 0x10); }
	inline s32 parentOf(const nn::g3d::ResBone& bone) { return bone.GetParentIndex() == nn::g3d::ResBone::InvalidBoneIndex ? -1 : bone.GetParentIndex(); }

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

	// =========================================================
	//                        CONTROLLERS
	// =========================================================

	// The game's joint controllers, named for its debug lists
	class Controller : public al::JointControllerBase {
	public:
		Controller(s32 count, const char* name) : al::JointControllerBase(count), mName(name) {}
		const char* getCtrlTypeName() const override { return mName; }
		// The lib header's (u32) signature isn't what the game exports, so these stay local instead of importing missing symbols
		bool tryValidateConstraints(u32) override { return false; }
		bool tryInvalidateConstraints(u32) override { return false; }
		const char* mName;
	};

	// Controllers come from a fixed free list per keeper and a full one crashes on register; a part without a keeper gets its own
	inline bool tryReserveControllerSlot(al::LiveActor* model) {
		if (!al::isExistJointControllerKeeper(model)) { al::initJointControllerKeeper(model, 1); return true; }
		u8* keeper = *(u8**)((u8*)model->getModelKeeper()->getModelCtrl() + 0x3C0);
		return keeper && *(void**)(keeper + 0x18); // free list head
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

	// =========================================================
	//                         RIDE GRIP
	// =========================================================

	// Scooter and kart mix their L/C/R clips by steering, and a mix of converted arms leaves the fists off the handles: each arm reaches its fist to Mario's spot mixed the same way
	namespace RideGrip {
		constexpr s32 vehicleCount = 2, familyCount = 5;
		inline const char* sVehicles[vehicleCount] = {"Motorcycle", "Kart"};
		inline const char* sFamilies[familyCount] = {"Ride", "RideClash", "RideJump", "RideLand", "RideRunStart"};
		inline const char* sSides[3] = {"L", "C", "R"}; // the game's weight order
		struct Clip { s32 frameMax = -1; f32* pos = nullptr; }; // per frame: left fist xyz, right fist xyz, seen from nw4f_root
		inline Vec readVec(const al::ByamlIter& root, const char* key) {
			al::ByamlIter it;
			f32 x = 0, y = 0, z = 0;
			if (root.tryGetIterByKey(&it, key)) { it.tryGetFloatByIndex(&x, 0); it.tryGetFloatByIndex(&y, 1); it.tryGetFloatByIndex(&z, 2); }
			return Vec(x, y, z);
		}
	}

	class RideGripController : public Controller {
	public:
		RideGripController(al::LiveActor* model) : Controller(2, "PlayerRideGrip"), mModel(model) {}

		// Inside the skeleton pass, at the hand (its arm already placed): the mixed spot, reached by the two arm bones with the elbow kept on its side; the hand keeps its turn
		void calcJointCallback(s32 id, sead::Matrix34f* mtx) override {
			s32 s = id == mHand[0] ? 0 : 1;
			if (s == 0 || !mIsUpdated) update();   // both hands share the move, its weights and the fade: worked out once a frame, at the first hand
			mIsUpdated = s == 0;
			if (mInfluence <= 0.0f) return;

			u8* skel = getSkeleton(mModel);
			Vec T = transformed(worldAt(skel, 0), mTarget[s]);
			Vec S = worldAt(skel, mArm1[s]).getTranslation(), E = worldAt(skel, mArm2[s]).getTranslation(), W = mtx->getTranslation();
			Vec Wd = W + (T - transformed(*mtx, mGrip[s])) * mInfluence;
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
			if (!std::isfinite(Wd.x + Wd.y + Wd.z + E2.x + E2.y + E2.z)) return;   // bad numbers: the clip's pose stays
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
				const RideGrip::Clip& c = mClips[v][family][k];
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

		al::LiveActor* mModel;
		s32 mHand[2], mArm1[2], mArm2[2];
		s32* mUpper[2] = {}; // joints turned with the upper arm, and with the forearm (computed before the hand)
		s32* mLower[2] = {};
		s32 mUpperCount[2] = {}, mLowerCount[2] = {};
		s32 mMove = -1;
		bool mIsUpdated = false;
		f32 mInfluence = 0.0f, mBlend = 1.0f;
		Vec mTarget[2] = {Vec::zero, Vec::zero}, mFrom[2] = {Vec::zero, Vec::zero}; // spots seen from nw4f_root
		Vec mGrip[2]; // the character's fist in its hand joint's space
		RideGrip::Clip mClips[RideGrip::vehicleCount][RideGrip::familyCount][3];
	};

	// The table ships in the body's archive (RideGrip.byml); each body reads its own, since the level's memory goes with the level
	inline void tryInitRideGrip(al::LiveActor* model) {
		if (model != isMarioModel || !al::isExistModelResourceYaml(model, "RideGrip", nullptr)) return;
		u8* skel = getSkeleton(model);
		const auto* bones = resBones(skel);
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
		auto isUnder = [&](s32 j, s32 root) { for (s32 k = j; k >= 0; k = parentOf(bones[k])) if (k == root) return true; return false; };
		for (s32 s = 0; s < 2; s++) {
			ctrl->mArm1[s] = arm1[s]; ctrl->mArm2[s] = arm2[s]; ctrl->mHand[s] = hand[s];
			ctrl->mUpper[s] = new s32[hand[s]]; ctrl->mLower[s] = new s32[hand[s]]; // only joints before the hand are listed
			for (s32 j = 0; j < ctrl->mHand[s]; j++) {
				if (isUnder(j, ctrl->mArm2[s])) ctrl->mLower[s][ctrl->mLowerCount[s]++] = j;
				else if (isUnder(j, ctrl->mArm1[s])) ctrl->mUpper[s][ctrl->mUpperCount[s]++] = j;
			}
		}
		al::ByamlIter root(al::getModelResourceYaml(model, "RideGrip", nullptr)), clips, it;
		ctrl->mGrip[0] = RideGrip::readVec(root, "GripL");
		ctrl->mGrip[1] = RideGrip::readVec(root, "GripR");
		if (root.tryGetIterByKey(&clips, "Clips")) for (s32 v = 0; v < RideGrip::vehicleCount; v++) for (s32 f = 0; f < RideGrip::familyCount; f++) for (s32 k = 0; k < 3; k++) {
			if (!clips.tryGetIterByKey(&it, al::StringTmp<64>("%s%s%s", RideGrip::sVehicles[v], RideGrip::sFamilies[f], RideGrip::sSides[k]).cstr())) continue;
			s32 n = it.getSize();
			if (n < 6) continue;
			RideGrip::Clip& c = ctrl->mClips[v][f][k];
			c.pos = new f32[n];
			for (s32 i = 0; i < n; i++) it.tryGetFloatByIndex(&c.pos[i], i);
			c.frameMax = n / 6 - 1;
		}
		ctrl->appendJointId(ctrl->mHand[0]);
		ctrl->appendJointId(ctrl->mHand[1]);
		al::registerJointController(model, ctrl);
	}

	// =========================================================
	//                        FACE BONES
	// =========================================================

	// A face on its own bones (FaceBones.byml in the face's archive): its clips only switch Face00..08 on, so each frame the face bones ease to that face's pose and the lids shut as the eye part blinks
	class FaceController : public Controller {
	public:
		static constexpr const char* sFaces[9] = {"Face00", "Face01", "Face02", "Face03", "Face04", "Face05", "Face06", "Face07", "Face08"};
		static constexpr const char* sShut[2] = {"EyelidCloseLeft", "EyelidCloseRight"};
		static constexpr const char* sHalf[2] = {"EyelidLeft", "EyelidRight"};
		struct Pose { Vec t; sead::Quatf q; f32 s; };
		FaceController(al::LiveActor* face, al::LiveActor* eye) : Controller(1, "PlayerFaceBones"), mFace(face), mEye(eye) {}

		// At the last face bone, every face bone set on Face00: a face change eases over mEase frames, a blink eases the lids shut over 3 (half lid: halfway)
		void calcJointCallback(s32 id, sead::Matrix34f* mtx) override {
			s32 want = mCur;
			for (s32 f = 0; f < 9; f++) if (al::isJointVisibility(mFace, sFaces[f])) { want = f; break; }
			if (want != mCur) { std::copy(mShown, mShown + mCount, mFrom); mCur = want; mStep = 0.0f; }
			if (mStep < 1.0f) {
				mStep = std::min(mStep + 1.0f / mEase, 1.0f);
				for (s32 k = 0; k < mCount; k++) mShown[k] = mix(mFrom[k], mPoses[mCur * mCount + k], smooth(mStep));
			}
			for (s32 s = 0; s < 2 && mEye; s++) {
				f32 shut = al::isJointVisibility(mEye, sShut[s]) ? 1.0f : al::isJointVisibility(mEye, sHalf[s]) ? 0.5f : 0.0f;
				mBlink[s] += std::clamp(shut - mBlink[s], -0.34f, 0.34f);
			}
			u8* skel = getSkeleton(mFace);
			Mtx root = worldAt(skel, mRoot), local, world;
			for (s32 k = 0; k < mCount; k++) {
				s32 s = mSide[k];
				f32 lid = s < 0 ? 0.0f : smooth(mBlink[s]);
				Pose p = lid > 0.0f ? mix(mShown[k], mPoses[9 * mCount + k], lid) : mShown[k];
				local.fromQuat(p.q);
				local.scaleBases(p.s, p.s, p.s);
				local.setTranslation(p.t);
				world.setMul(root, local);
				if (mJoint[k] == id) *mtx = world;
				else setWorldAt(skel, mJoint[k], world);
			}
		}

		static Pose mix(const Pose& a, const Pose& b, f32 x) { Pose o; o.t = a.t + (b.t - a.t) * x; o.q.slerpTo(a.q, b.q, x); o.s = a.s + (b.s - a.s) * x; return o; }

		al::LiveActor* mFace;
		al::LiveActor* mEye;
		s32 mRoot = 0, mCount = 0, mCur = 0;
		s32* mJoint = nullptr;
		s8* mSide = nullptr; // the eye a lid blinks with (0 left, 1 right, -1 not a lid)
		f32 mEase = 6.0f, mStep = 1.0f, mBlink[2] = {};
		Pose* mPoses = nullptr; // faces 0..8 then the blink, each face bone under Face00
		Pose* mShown = nullptr;
		Pose* mFrom = nullptr;
	};

	inline void tryInitFaceBones(al::LiveActor* face, al::LiveActor* eye) {
		al::ByamlIter root(al::getModelResourceYaml(face, "FaceBones", nullptr)), bones, poses, row;
		bool isValid = root.tryGetIterByKey(&bones, "Bones") && root.tryGetIterByKey(&poses, "Poses") && bones.getSize() > 0 && poses.getSize() == 10 && al::getJointIndex(face, "Face00") >= 0;
		for (s32 k = 0; isValid && k < bones.getSize(); k++) { const char* name = ""; isValid = bones.tryGetStringByIndex(&name, k) && al::getJointIndex(face, name) >= 0; } // a missing bone would be written outside the skeleton
		if (!isValid || !tryReserveControllerSlot(face)) { logLine("FaceBones: not set up"); return; }
		auto* c = new FaceController(face, eye);
		c->mRoot = al::getJointIndex(face, "Face00");
		c->mCount = bones.getSize();
		root.tryGetFloatByKey(&c->mEase, "Ease");
		c->mPoses = new FaceController::Pose[10 * c->mCount];
		c->mJoint = new s32[c->mCount];
		c->mSide = new s8[c->mCount];
		c->mShown = new FaceController::Pose[c->mCount];
		c->mFrom = new FaceController::Pose[c->mCount];
		s32 last = c->mRoot;
		for (s32 k = 0; k < c->mCount; k++) {
			const char* name = "";
			bones.tryGetStringByIndex(&name, k);
			c->mJoint[k] = al::getJointIndex(face, name);
			c->mSide[k] = al::isStartWithString(name, "eyeLid") ? al::isEndWithString(name, "_r") : -1;
			last = std::max(last, c->mJoint[k]);
		}
		for (s32 f = 0; f < 10; f++) {
			poses.tryGetIterByIndex(&row, f);
			s32 n = row.getSize() == c->mCount * 8 ? 8 : 7; // tx ty tz qx qy qz qw, then an even scale when a face scales a bone
			for (s32 k = 0; k < c->mCount; k++) {
				f32 v[8] = {0, 0, 0, 0, 0, 0, 1, 1};
				for (s32 j = 0; j < n; j++) row.tryGetFloatByIndex(&v[j], k * n + j);
				c->mPoses[f * c->mCount + k] = {Vec(v[0], v[1], v[2]), sead::Quatf(v[6], v[3], v[4], v[5]), v[7]};
			}
		}
		std::copy(c->mPoses, c->mPoses + c->mCount, c->mShown);
		c->appendJointId(last);
		al::registerJointController(face, c);
	}

	// =========================================================
	//                           SETUP
	// =========================================================

	// The body's and its parts' springs (a head or face part ships the springs of the skin it holds) and ride grip; a face part on its own bones blinks with the eye part (parts sharing the body's model, the silhouette, are skipped)
	inline void executeInitPlayer(al::LiveActor* model) {
		if (!model || !model->getModelKeeper()) return;
		tryInitSprings(model);
		tryInitRideGrip(model);
		al::LiveActor* face = nullptr;
		al::LiveActor* eye = nullptr;
		for (s32 i = 0; i < al::getSubActorNum(model); i++) {
			al::LiveActor* part = al::getSubActor(model, i);
			if (!part || !part->getModelKeeper() || part->getModelKeeper() == model->getModelKeeper()) continue;
			tryInitSprings(part);
			if (al::isExistModelResourceYaml(part, "FaceBones", nullptr)) face = part;
			else if (al::getJointIndex(part, FaceController::sShut[0]) >= 0) eye = part;
		}
		if (face) tryInitFaceBones(face, eye);
	}

	// Controllers a model adds to the game's own: its springs, ride grips and face bones (only characters ship those)
	inline s32 extraControllerCount(const al::LiveActor* actor) {
		return getSpringCount(actor) + al::isExistModelResourceYaml(actor, "RideGrip", nullptr) + al::isExistModelResourceYaml(actor, "FaceBones", nullptr);
	}

	// A body's controller list is sized when it is made and a full one crashes on register, so it gets room for the ones it adds
	struct KeeperSizeHook : public mallow::hook::Trampoline<KeeperSizeHook> {
		static void Callback(const al::LiveActor* actor, s32 count) {
			if (actor) count += extraControllerCount(actor);
			Orig(actor, count);
		}
	};

	// =========================================================
	//                           HOOKS
	// =========================================================

	// The game fits Mario's clips to each body's bone lengths; converted clips already fit their body, so it's skipped
	struct BodyRetargetHook : public mallow::hook::Trampoline<BodyRetargetHook> {
		static void* Callback(al::LiveActor* body, const sead::Vector3f& scale) {
			if (hasOwnClips(al::getModelName(body))) return nullptr;
			return Orig(body, scale);
		}
	};

	// Cutscene copies of the body: the same springs and face as the player's
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
			isClosetBody = true;
			const auto* holder = ((const al::ActorInitInfo*)info)->actorSceneInfo.sceneObjHolder;
			sClosetWorn = holder ? GameDataFunction::getCurrentCostumeTypeName(GameDataHolderAccessor(holder)) : nullptr; // from the scene: the copy isn't set up yet
			// a character's closet copy is Mario's: the closet makes one per costume at once, and every character's model at once doesn't fit
			if (hasOwnClips(bodyName) && !al::isEqualString(bodyName, "Mario")) { bodyName = "Mario"; capName = "Mario"; }
			void* costume = Orig(parts, body, info, bodyName, capName, pConst, noseScale, earScale, follow, followMtx);
			isClosetBody = false;
			executeInitPlayer(body);
			return costume;
		}
	};

	// The closet starts Wait on its Mario copies (and their parts); one playing from a character's file switches its body to Mario's own Wait kept there
	struct ClosetActionHook : public mallow::hook::Trampoline<ClosetActionHook> {
		static void Callback(void* state, al::LiveActor* body, const char* action) {
			Orig(state, body, action);
			if (!al::isEqualString(action, "Wait") || !al::isExistAction(body, "ClosetWait")) return;
			f32 frame = al::getActionFrame(body);
			al::startAction(body, "ClosetWait");
			al::setActionFrame(body, frame);
		}
	};

	// The game's leg IK (ground, slopes, landings, crouching) puts the knee on the thigh's +Y side: on a long shin a deep bend turns that axis past the knee, so it goes backward or inward
	struct GroundPoseLegBottomHook : public mallow::hook::Trampoline<GroundPoseLegBottomHook> {
		static void Callback(u8* thisPtr, Mtx* foot, Mtx* legs, const s32* joints) {
			Mtx thigh = legs[0], knee = legs[1];
			Vec H = thigh.getTranslation(), K = knee.getTranslation(), F = foot->getTranslation(), y;
			f32 l1 = (K - H).length(), l2 = (F - K).length(), al = (F - H).length();
			if (!hasOwnClips(*(const al::LiveActor**)(thisPtr + 0x28)) || al < 1e-4f || l1 < 1e-4f) { Orig(thisPtr, foot, legs, joints); return; }
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
		BodyRetargetHook::InstallAtSymbol("_ZN2rs31createPlayerSklRetargettingInfoEPN2al9LiveActorERKN4sead7Vector3IfEE");
		DemoBodyHook::InstallAtSymbol("_ZN14PlayerFunction23initMarioModelActorDemoEPP31PlayerJointControlPartsDynamicsPN2al9LiveActorERKNS3_13ActorInitInfoEPKcSA_PK11PlayerConstPN4sead7Vector3IfEESH_b");
		ClosetBodyHook::InstallAtSymbol("_ZN14PlayerFunction25initMarioModelActorClosetEPP31PlayerJointControlPartsDynamicsPN2al9LiveActorERKNS3_13ActorInitInfoEPKcSA_PK11PlayerConstPN4sead7Vector3IfEESH_PP30PlayerJointControlFollowMtxPtrPNSE_8Matrix34IfEE");
		ClosetActionHook::InstallAtSymbol("_ZN21StageSceneStateCloset16startActionMarioEPN2al9LiveActorEPKc");
		InstallResources();
	}
}
