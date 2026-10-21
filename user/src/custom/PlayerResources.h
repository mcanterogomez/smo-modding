#pragma once
#include "custom/.Globals.h"
#include "Library/Yaml/ByamlIter.h"
#include "Library/Yaml/Writer/ByamlWriter.h"
#include "Project/Anim/AnimInfo.h"
#include "Project/Anim/InitResourceDataAnim.h"
#include "stream/seadRamStream.h"
#include "Library/Audio/AudioDirector.h"
#include "Library/Resource/Resource.h"
#include "Library/LiveActor/ActorResourceFunction.h"
#include "Library/Audio/AudioInfo.h"
#include "Library/Audio/AudioEventController.h"
#include "Library/Audio/System/AudioSystemInfo.h"
#include "Library/Audio/System/AudioKeeperFunction.h"
#include "Library/Effect/EffectSystemInfo.h"
#include "Library/Se/Info/SeAudioInfo.h"
#include "Library/Se/Function/SeDbFunction.h"
#include "Library/Memory/HeapUtil.h"
#include "Library/Message/MessageHolder.h"
#include "Library/Message/LanguageUtil.h"
#include "Library/Resource/ResourceFunction.h"
#include "Library/System/SystemKit.h"
#include "Project/Resource/ResourceSystem.h"
#include "Project/File/FileLoader.h"
#include "Project/ArchiveHolder.h"
#include "Project/ArchiveEntry.h"
#include "System/ProjectInterface.h"
#include "heap/seadExpHeap.h"
#include "heap/seadHeapMgr.h"
#include "filedevice/seadFileDeviceMgr.h"
#include <algorithm>
#include <atomic>
#include <nn/os.h>
#include <cstring>
#include <span>

namespace nn::ui2d { class TextureInfo; }
namespace al { nn::ui2d::TextureInfo* createTextureInfo(const char* archive, const char* model, const char* texture); }

// The game's sound and effect lists (only what's used here)
namespace al {
	class SeDataBase {
	public:
		SeDataBase() = default; // empty; the game's own steps fill its users, on the game's wave list
		void createUserInfoList(const Resource* resource, const sead::ConstPtrArray<char>& names);
		void setResourceSpecInfoToResourceInfo();
		void createResourceSpecificInfoList(const Resource* resource, const sead::ConstPtrArray<char>& names, const AudioSoundArchiveInfo* packs); // makes a new wave list from the Category files named
		AudioInfoListWithParts<SeUserInfo>* mUserInfoList = nullptr;
		void* mCategoryInfoList = nullptr;
		AudioInfoListWithParts<SeResourceSpecificInfo>* mSpecificInfoList = nullptr; // the waves; a wave's id is its pack and its place in its list
		u8 _18[0x18] = {};
	};
	static_assert(sizeof(SeDataBase) == 0x30);

	class EffectDataBase {
	public:
		s32 mUserCount;
		void** mUsers;
		s32 mCategoryCount;
		void* mCategories;
	};
	static_assert(sizeof(EffectDataBase) == 0x20);
}
namespace alEffectDataBaseFunction {
	void* createEffectUserInfo(const al::EffectDataBase* dataBase, const al::Resource* resource, const sead::SafeString& fileName);
}

// How a character's and each mod's files load: a worn character's files in one slot (clips, model, sounds, effects), each mod's own files added on top of the game's
namespace PlayerRetarget {

	// =========================================================
	//                         MOD FILES
	// =========================================================

	// A file is there (named as the game names its resources, without ".szs")
	inline bool hasFile(const char* path) { return al::isExistFile(al::StringTmp<128>("%s.szs", path)); }

	// A character's body ships its converted clips as PlayerAnimation<body> (PlayerAnimationSonic); each body's answer is remembered (several suits can be on)
	struct ClipAnswer { char body[64]; bool has; };
	inline ClipAnswer sClipAnswers[8] = {};
	inline s32 sClipAnswerNext = 0;
	inline bool hasOwnClips(const char* body) {
		if (!body) return false;
		for (const auto& a : sClipAnswers) if (a.body[0] && al::isEqualString(a.body, body)) return a.has;
		ClipAnswer& a = sClipAnswers[sClipAnswerNext++ % 8];
		al::copyString(a.body, body, sizeof(a.body));
		a.has = hasFile(al::StringTmp<128>("ObjectData/PlayerAnimation%s", body).cstr());
		return a.has;
	}

	// Each mod's own files on top of the game's: each goes where the game keeps the file it adds to, and only what the mod adds or changes ships (a same-named clip, action, key or effect is the mod's); each added when its mod ships it
	// They're found in the folders by name, so a mod needs no line here: <game file><name> adds to that game file (PlayerAnimationKart, PlayerAnimationHeadDrill, KartEffects, ItemListSonic); a <name> that's a body's model (Sonic, Mario) is that body's own file, for the slot
	struct AddOn { char file[64]; const char* gameFile; const char* log; bool isWhole = false; bool exists = true; }; // isWhole: used in place of the game's file, nothing joins it; exists: false once it didn't fit (not tried again)
	constexpr const char* shopFile = "SystemData/ItemList"; // the game's shop and closet lists
	struct AddOnKind { const char* folder; const char* start; const char* end; const char* gameFile; const char* log; bool isWhole; bool hasBodyFiles; }; // hasBodyFiles: a body's own files are named the same way (PlayerAnimationSonic, SonicEffects)
	inline const AddOnKind sAddOnKinds[] = { // the game's files a mod adds to, the longest name first (PlayerAnimationHead before PlayerAnimation)
		{"ObjectData", "PlayerAnimationHandL", "", "ObjectData/PlayerAnimationHandL", "Anims", false, true},
		{"ObjectData", "PlayerAnimationHandR", "", "ObjectData/PlayerAnimationHandR", "Anims", false, true},
		{"ObjectData", "PlayerAnimationFace", "", "ObjectData/PlayerAnimationFace", "Anims", false, true},
		{"ObjectData", "PlayerAnimationHead", "", "ObjectData/PlayerAnimationHead", "Anims", false, true},
		{"ObjectData", "PlayerAnimationEye", "", "ObjectData/PlayerAnimationEye", "Anims", false, true},
		{"ObjectData", "PlayerAnimation2D", "", "ObjectData/PlayerAnimation2D", "Anims", true, true}, // the whole 2D file (CustomAnimation.h swaps it in)
		{"ObjectData", "PlayerAnimation", "", "ObjectData/PlayerAnimation", "Anims", false, true},
		{"ObjectData", "PlayerActorHakoniwa", "", "ObjectData/PlayerActorHakoniwa", "Actor", false, true},
		{"EffectData", "", "Effects", "EffectData/EffectDataBase", "Effects", false, true},
		{"SoundData", "", "Sounds", nullptr, "Sfx", false, true}, // its own sounds: <name>Sounds + <name>Se.aras (sSoundOwners)
		{"SystemData", "ItemList", "", shopFile, "Shop", false, false}, // a suit's shop and closet rows and its Names
	};
	struct SoundOwner { char name[32]; };
	inline AddOn sAddOns[64] = {};
	inline s32 sAddOnCount = 0;
	inline SoundOwner sSoundOwners[8] = {};
	inline s32 sSoundOwnerCount = 0;
	inline std::atomic<s32> sFoundAddOns = 0; // 0 not yet, 1 looking (the game loads on several threads), 2 done
	inline sead::DirectoryEntry sEntries[64];

	// Looked for once, the first time the list is asked for; sorted by file name (a same-named clip, line or effect in two mods: the first by name)
	inline void findAddOns() {
		s32 state = 0;
		if (!sFoundAddOns.compare_exchange_strong(state, 1)) { while (sFoundAddOns.load() != 2) nn::os::YieldThread(); return; }
		sead::FileDevice* device = sead::FileDeviceMgr::instance()->getMainFileDevice();
		for (const char* folder : {"ObjectData", "EffectData", "SoundData", "SystemData"}) {
			sead::DirectoryHandle dir;
			if (!device->tryOpenDirectory(&dir, folder)) continue;
			u32 read = 0;
			while (device->tryReadDirectory(&read, &dir, sEntries, 64) && read) for (u32 i = 0; i < read; i++) {
				const char* file = sEntries[i].name.cstr();
				if (!al::isEndWithString(file, ".szs")) continue;
				for (const AddOnKind& kind : sAddOnKinds) {
					if (!al::isEqualString(kind.folder, folder) || !al::isStartWithString(file, kind.start)) continue;
					const char* name = file + std::strlen(kind.start);
					s32 length = (s32)(std::strlen(name) - 4 - std::strlen(kind.end)); // "PlayerAnimationHeadDrill.szs" -> "Drill", "KartEffects.szs" -> "Kart"
					if (length < 0 || std::strncmp(name + length, kind.end, std::strlen(kind.end))) continue;
					if (!length) break; // the game's own file
					s32 bodyLength = length > 6 && !std::strncmp(name + length - 6, "Closet", 6) ? length - 6 : length; // a body's closet clips: PlayerAnimationMarioCloset
					if (kind.hasBodyFiles && hasFile(al::StringTmp<128>("ObjectData/%.*s", bodyLength, name).cstr())) break; // a body's own file
					if (!kind.gameFile) {
						if (sSoundOwnerCount == 8) { logLine("%s: %s skipped, the list is full", kind.log, file); break; }
						al::copyString(sSoundOwners[sSoundOwnerCount++].name, al::StringTmp<32>("%.*s", length, name).cstr(), 32);
						break;
					}
					if (sAddOnCount == 64) { logLine("%s: %s skipped, the list is full", kind.log, file); break; }
					AddOn& addOn = sAddOns[sAddOnCount++];
					al::copyString(addOn.file, al::StringTmp<64>("%s/%.*s", folder, (s32)std::strlen(file) - 4, file).cstr(), sizeof(addOn.file));
					addOn.gameFile = kind.gameFile;
					addOn.log = kind.log;
					addOn.isWhole = kind.isWhole;
					break;
				}
			}
			device->tryCloseDirectory(&dir);
		}
		std::sort(sAddOns, sAddOns + sAddOnCount, [](const AddOn& a, const AddOn& b) { return std::strcmp(a.file, b.file) < 0; });
		std::sort(sSoundOwners, sSoundOwners + sSoundOwnerCount, [](const SoundOwner& a, const SoundOwner& b) { return std::strcmp(a.name, b.name) < 0; });
		s32 shops = 0;
		for (const AddOn& addOn : std::span(sAddOns, sAddOnCount)) if (al::isEqualString(addOn.gameFile, shopFile)) shops++; else logLine("AddOns: %s adds to %s%s", addOn.file, addOn.gameFile, addOn.isWhole ? " (whole)" : "");
		for (const SoundOwner& owner : std::span(sSoundOwners, sSoundOwnerCount)) logLine("AddOns: SoundData/%sSounds, its own sounds", owner.name);
		logLine("Shop: %d suits found", shops);
		sFoundAddOns.store(2);
	}
	inline std::span<AddOn> addOns() {
		if (sFoundAddOns.load() != 2) findAddOns();
		return {sAddOns, (size_t)sAddOnCount};
	}
	inline std::span<SoundOwner> soundOwners() { // each mod's own sounds: <owner>Sounds.szs + <owner>Se.aras (Player = Main, Kart = Mario Kart)
		if (sFoundAddOns.load() != 2) findAddOns();
		return {sSoundOwners, (size_t)sSoundOwnerCount};
	}

	// A resource is that file (the game names some with ".szs")
	inline bool isFile(const al::Resource* resource, const char* path) {
		if (!resource) return false;
		const char* name = resource->getPath();
		u32 length = std::strlen(path);
		return al::isStartWithString(name, path) && (!name[length] || al::isEqualString(name + length, ".szs"));
	}

	// =========================================================
	//                         CLIP SLOT
	// =========================================================

	// A worn character's files (model, parts, clips) load into one slot, emptied for the next character, so nothing of a character stays loaded once it's taken off
	namespace ClipSlot {
		constexpr const char* category = "PlayerRetargetClips";
		constexpr u32 margin = 128 * 1024; // what each load takes beyond its unpacked size (30 KB measured)
		inline const char* sPartSuffixes[] = {"", "Face", "Eye", "HandL", "HandR", "HandTexture", "Head", "HeadTexture", "Cap", "2D", "Sounds", "Effects"}; // its model files ("Sonic", "SonicFace"...) and its sound and effect lists
		// Where a body's file lives: its sound and effect lists where every mod keeps them (SoundData/SonicSounds, EffectData/SonicEffects), the rest in ObjectData
		inline const char* partFolder(const char* part) { return al::isEqualString(part, "Sounds") ? "SoundData/" : al::isEqualString(part, "Effects") ? "EffectData/" : "ObjectData/"; }
		inline const char* sClipParts[] = {"", "HandL", "HandR", "Face"}; // its clips ("PlayerAnimationSonic", "PlayerAnimationHandLSonic"...)
		constexpr s32 files = sizeof(sPartSuffixes) / sizeof(*sPartSuffixes) + sizeof(sClipParts) / sizeof(*sClipParts); // the resources the slot's category must hold: one per file it can take
		inline u32 sSize = 0; // the slot's size, fit for the biggest character in the costume list
		inline sead::Heap* sHeap = nullptr; // made the first time a character is worn: in the costume memory (kept for good, reused by every character) or else the world's
		inline sead::Heap* sParent = nullptr; // the heap it came from
		inline uintptr_t sStart = 0, sEnd = 0; // where the slot sits in memory (kept after the slot is gone, for forget)
		inline bool sInCostumes = false;
		inline char sPath[128] = ""; // the clips in it, as the game names them ("ObjectData/PlayerAnimationSonic")
		inline s32 sStage = 0, sUsedStage = -1; // stages started, and the last one a body used its clips in
		inline al::SeDataBase* sSounds = nullptr; // the worn character's sound and effect lists, built in the slot
		inline al::EffectDataBase* sEffects = nullptr;
		inline al::AudioInfoListWithParts<al::SeResourceSpecificInfo>* sOwnWaves = nullptr; // the worn character's own waves, a part of the game's wave list
		inline al::AudioInfoListWithParts<al::SeResourceSpecificInfo>* sGameWaves = nullptr;
		inline bool sMadeWaveParts = false; // the game's wave list had no parts, so its parts holder is the slot's

		// The worn character's own waves leave the game's wave list (a parts holder in the slot is only let go: its memory may be gone already)
		inline void dropOwnWaves() {
			if (!sOwnWaves) return;
			if (sMadeWaveParts) sGameWaves->mParts = nullptr;
			else if (s32 at = sGameWaves->mParts->indexOf(sOwnWaves); at >= 0) sGameWaves->mParts->erase(at);
			sOwnWaves = nullptr;
			sMadeWaveParts = false;
			logLine("Sfx: own waves removed");
		}

		// An archive's unpacked size from its header (0 if unreadable): a failed allocation halts the game, so nothing goes anywhere it isn't known to fit
		inline u32 getUnpackedSize(const char* path) {
			sead::FileDevice* device = sead::FileDeviceMgr::instance()->getMainFileDevice();
			sead::FileHandle handle;
			u8 head[8] = {};
			u32 read = 0;
			if (!device->tryOpen(&handle, al::StringTmp<128>("%s.szs", path), sead::FileDevice::cFileOpenFlag_ReadOnly, 0) || !device->tryRead(&read, &handle, head, 8) || read != 8 || std::memcmp(head, "Yaz0", 4)) return 0;
			return head[4] << 24 | head[5] << 16 | head[6] << 8 | head[7];
		}

		// The slot's files: the worn body's clips and model files; an "as Mario" mod replaces Mario for good, so only its body clips
		inline bool isSlotFile(const char* name) {
			if (!sPath[0]) return false;
			const char* body = sPath + std::strlen("ObjectData/PlayerAnimation");
			if (al::isEqualString(body, "Mario")) return al::isEqualString(name, sPath);
			for (const char* part : sPartSuffixes) {
				const char* at = al::isStartWithString(name, partFolder(part)) ? name + std::strlen(partFolder(part)) : nullptr;
				if (at && al::isStartWithString(at, body) && al::isEqualString(at + std::strlen(body), part)) return true;
			}
			if (!al::isStartWithString(name, "ObjectData/PlayerAnimation")) return false;
			const char* clip = name + std::strlen("ObjectData/PlayerAnimation");
			for (const char* part : sClipParts) if (al::isStartWithString(clip, part) && al::isEqualString(clip + std::strlen(part), body)) return true;
			return false;
		}

		// What the slot's files unpack to (0 if the body ships no clips)
		inline u32 setSize(const char* body) {
			u32 need = getUnpackedSize(al::StringTmp<128>("ObjectData/PlayerAnimation%s", body).cstr());
			if (!need || al::isEqualString(body, "Mario")) return need;
			for (const char* part : sClipParts) if (part[0]) need += getUnpackedSize(al::StringTmp<128>("ObjectData/PlayerAnimation%s%s", part, body).cstr());
			for (const char* part : sPartSuffixes) need += getUnpackedSize(al::StringTmp<128>("%s%s%s", partFolder(part), body, part).cstr());
			return need;
		}

		// The biggest set among the costumes the closet can dress in (SystemData/ItemList), so the one slot fits whichever character is worn later
		inline u32 boxSize(const char* worn) {
			u32 most = setSize(worn);
			if (const u8* list = al::tryGetBymlFromArcName(shopFile, "ItemCloth")) {
				al::ByamlIter clothes(list), item;
				for (s32 i = 0; i < clothes.getSize(); i++) {
					const char* name = nullptr;
					if (clothes.tryGetIterByIndex(&item, i) && item.tryGetStringByKey(&name, "ItemName") && hasOwnClips(name)) most = std::max(most, setSize(name));
				}
			}
			return most + margin * files;
		}

		inline void logHeaps(const char* when) {
			const sead::Heap* world = al::getWorldResourceHeap();
			const sead::Heap* costumes = al::tryFindNamedHeap("PlayerModel");
			logLine("Heaps %s: world free %zu largest %zu | costumes free %zu largest %zu | slot %s", when, world ? world->getFreeSize() : 0, world ? world->getMaxAllocatableSize(8) : 0, costumes ? costumes->getFreeSize() : 0, costumes ? costumes->getMaxAllocatableSize(8) : 0, sPath[0] ? sPath : "empty");
		}

		// The game remembers every file it opened until the world changes: the slot's files leave that list before their memory is freed, or wearing the character again opens the freed copy
		inline s32 sHoles = 0; // places on that list the slot's files left (the next files the game opens take them)
		inline void forget() {
			al::ArchiveHolder* opened = alProjectInterface::getSystemKit()->getFileLoader()->mArchiveHolder;
			s32 count = 0;
			for (s32 i = 0; i < opened->mSize; i++) if (al::ArchiveEntry* entry = opened->mArchiveEntries.get(i); (uintptr_t)entry->mArchiveRes >= sStart && (uintptr_t)entry->mArchiveRes < sEnd) { entry->clear(); count++; }
			sHoles += count;
			logLine("ClipSlot: forgot %d files, list %d/%d", count, opened->mSize, opened->mArchiveEntries.size());
		}

		// The worn character leaves everything that knows its files: the game's opened-files list and the slot's resources (its memory is then freed, destroyed or already gone)
		inline void drop() {
			forget();
			al::removeResourceCategory(category);
			sPath[0] = '\0';
			dropOwnWaves();
			sSounds = nullptr;
			sEffects = nullptr;
		}

		inline void empty() {
			if (!sPath[0]) return;
			logLine("ClipSlot: emptied of %s", sPath);
			drop();
			sHeap->freeAll();
			al::addResourceCategory(category, files, sHeap);
		}

		// A world change: a slot in the world's memory goes with it; one in the costume memory is only emptied
		inline void destroy() {
			if (!sHeap) return;
			if (sInCostumes) { empty(); return; }
			drop();
			sHeap->destroy();
			sHeap = nullptr;
		}

		// A body's clips take the slot unless they're loaded already or another body in this stage uses the slot (then the world keeps them, as before)
		inline void claim(const char* archive) {
			al::StringTmp<128> path("ObjectData/%s", archive);
			if (sPath[0] && al::isEqualString(sPath, path.cstr())) { sUsedStage = sStage; return; }
			if ((sPath[0] && sUsedStage == sStage) || al::findResource(path.cstr())) { logLine("ClipSlot: %s stays in the world (slot busy or already loaded)", path.cstr()); return; }
			u32 need = setSize(archive + std::strlen("PlayerAnimation"));
			if (!need) { logLine("ClipSlot: %s stays in the world (unreadable)", path.cstr()); return; }
			sead::Heap* costumes = al::tryFindNamedHeap("PlayerModel");
			if (sHeap && sInCostumes && costumes != sParent) { drop(); sHeap = nullptr; } // the costume memory was made again: the old slot went with it
			if (!sHeap) {
				const auto& categories = alProjectInterface::getSystemKit()->getResourceSystem()->mCategories;
				// the costume memory first (characters' closet copies are Mario's, so it keeps room the world doesn't); the largest block, as a failed allocation halts the game
				sSize = boxSize(archive + std::strlen("PlayerAnimation"));
				sInCostumes = costumes && costumes->getMaxAllocatableSize(8) >= sSize;
				sParent = sInCostumes ? costumes : al::getWorldResourceHeap();
				size_t room = sParent ? sParent->getMaxAllocatableSize(8) : 0;
				if (room >= sSize && categories.size() < categories.capacity() && (sHeap = sead::ExpHeap::create(sSize, category, sParent, 8))) { al::addResourceCategory(category, files, sHeap); sStart = sHeap->getStartAddress(); sEnd = sHeap->getEndAddress(); }
				logLine("ClipSlot: %s, %u (%s room %zu)", sHeap ? "made" : "no room, clips stay in the world", sSize, sInCostumes ? "costume" : "world", room);
				if (!sHeap) return;
			}
			if (need + margin * files > sSize) { logLine("ClipSlot: %s stays in the world (its files unpack to %u, the slot takes %u)", path.cstr(), need, sSize); return; }
			empty();
			al::copyString(sPath, path.cstr(), sizeof(sPath));
			sUsedStage = sStage;
			logLine("ClipSlot: %s takes the slot", path.cstr());
		}

		// A file the game opens takes a place forget() left, so the list never runs past its end (past it the game writes over its first file)
		struct ReuseHook : public mallow::hook::Trampoline<ReuseHook> {
			static al::ArchiveEntry* Callback(al::ArchiveHolder* opened, const sead::SafeString& name, sead::Heap* heap, sead::FileDevice* device) {
				if (sHoles > 0) {
					for (s32 i = 0; i < opened->mSize; i++) {
						al::ArchiveEntry* entry = opened->mArchiveEntries.get(i);
						if (entry->mArchiveRes || !entry->mFileName.isEmpty()) continue;
						entry->setLoadRequestInfo(name, heap, device);
						sHoles--;
						logLine("ClipSlot: %s reuses place %d", name.cstr(), i);
						return entry;
					}
					sHoles = 0; // none left (the world change cleared the list)
				}
				return Orig(opened, name, heap, device);
			}
		};

		// The game's table first; the worn character's files go to the slot, anything else to the current category (nothing of a character loads unless it's worn)
		struct CategoryHook : public mallow::hook::Trampoline<CategoryHook> {
			static const char* Callback(const al::ResourceSystem* system, const sead::SafeString& name) {
				if (isSlotFile(name.cstr())) { logLine("ClipSlot: %s (%u) into the slot, %zu free", name.cstr(), getUnpackedSize(name.cstr()), sHeap ? sHeap->getFreeSize() : 0); return category; }
				for (const AddOn& addOn : addOns()) if (al::isEqualString(name.cstr(), addOn.file)) return Orig(system, addOn.gameFile); // a mod's file goes where the game keeps the file it adds to
				return Orig(system, name);
			}
		};

		// Stages are counted; once one is set up (the last one's bodies are gone by then), clips none of its bodies used leave the slot
		struct StageHook : public mallow::hook::Trampoline<StageHook> {
			static void Callback(void* scene, const void* info) {
				sStage++;
				logHeaps("before stage");
				Orig(scene, info);
				if (sUsedStage != sStage) empty();
				else logLine("ClipSlot: %s, %zu bytes spare", sPath, sHeap->getFreeSize());
				logHeaps("after stage");
			}
		};

		// A world change: the slot is emptied first (destroyed if it came from the world's memory)
		struct WorldHook : public mallow::hook::Trampoline<WorldHook> {
			static void Callback(bool removeCategory) {
				logLine("ClipSlot: world memory destroyed (%d)", removeCategory);
				destroy();
				Orig(removeCategory);
			}
		};
	}

	// =========================================================
	//                      CHARACTER CLIPS
	// =========================================================

	// The leg hook asks every frame: the answer is kept per body until the next stage
	inline bool hasOwnClips(const al::LiveActor* body) {
		static const al::LiveActor* sBody = nullptr;
		static s32 sStage = -1;
		static bool sHas = false;
		if (body != sBody || sStage != ClipSlot::sStage) { sBody = body; sStage = ClipSlot::sStage; sHas = hasOwnClips(al::getModelName(body)); }
		return sHas;
	}

	// The player's body is a character port (it ships its own clips)
	inline bool isPort() { return isMarioModel && hasOwnClips(isMarioModel); }

	// Every body (the player's, cutscene and closet copies, all set up here) with its own clips loads them instead of Mario's
	// Closet copies (Mario) play Mario's Wait kept in the worn character's file (ClosetWait), already loaded; an "as Mario" body has its own closet file
	inline bool isClosetBody = false;
	inline const char* sClosetWorn = nullptr;
	inline char sBodyAnimation[96] = "";
	struct BodyAnimationArchiveHook : public mallow::hook::Inline<BodyAnimationArchiveHook> {
		static void Callback(exl::hook::InlineCtx* ctx) {
			const char* model = (const char*)ctx->X[28];
			bool isOwn = hasOwnClips(model);
			const char* body = isOwn ? model : isClosetBody && hasOwnClips(sClosetWorn) ? sClosetWorn : nullptr;
			if (!body) return;
			al::StringTmp<96> archive(isClosetBody && isOwn ? "PlayerAnimation%sCloset" : "PlayerAnimation%s", body);
			if (isClosetBody && isOwn && !hasFile(al::StringTmp<128>("ObjectData/%s", archive.cstr()).cstr())) return;
			al::copyString(sBodyAnimation, archive.cstr(), sizeof(sBodyAnimation));
			if (!isClosetBody || !isOwn) ClipSlot::claim(sBodyAnimation); // closet copies load before the player: whichever comes first puts the clips in the slot
			ctx->X[24] = (u64)sBodyAnimation;
		}
	};

	// The player reads footprints from PlayerAnimation by name; a character with its own clips reads its own copy, so Mario's big file never loads
	inline char sFootPrintArchive[96] = "";
	struct FootPrintArchiveHook : public mallow::hook::Trampoline<FootPrintArchiveHook> {
		static void Callback(void* thisPtr, al::LiveActor* actor, const char* archive, al::HitSensor* sensor, void* server) {
			if (archive && al::isEqualString(archive, "PlayerAnimation")) {
				const char* costume = GameDataFunction::getCurrentCostumeTypeName(actor);
				if (hasOwnClips(costume)) {
					al::copyString(sFootPrintArchive, al::StringTmp<96>("PlayerAnimation%s", costume).cstr(), sizeof(sFootPrintArchive));
					archive = sFootPrintArchive;
				}
			}
			Orig(thisPtr, actor, archive, sensor, server);
		}
	};

	// =========================================================
	//                    SOUNDS AND EFFECTS
	// =========================================================

	// A worn character's sounds and effects: its own users (<body>Sounds: Sonic, SonicFoot...; <body>Effects: Sonic) answer for Mario's while the player is set up
	// They're built the game's way on the game's own wave list and effect categories, so only the users ship
	inline bool isPlayerSetup = false;
	inline const char* sSetupBody = nullptr;

	// A character's own sound pack (SoundData/<body>Se.aras holding <body>Se.bfsar) joins the game's pack list at boot: the game makes a place for each listed pack then, and loads one only when a sound in use names it
	inline const al::AudioSoundArchiveInfo* sPacks = nullptr;
	inline s32 sGamePackCount = 0; // the game's own packs; character packs come after

	// A wave's id is its pack and its place: the game numbers a pack in a part by its place in that part, the player by its place after the game's packs
	inline void numberAfterGamePacks(al::AudioInfoListWithParts<al::SeResourceSpecificInfo>* waves) {
		for (s32 i = 0; i < waves->AudioInfoList<al::SeResourceSpecificInfo>::getSize(); i++) {
			u32* id = (u32*)((uintptr_t)waves->getInfoAt(i) + 0x60);
			if (*id >> 16) *id += sGamePackCount << 16;
		}
	}

	struct PackListHook : public mallow::hook::Trampoline<PackListHook> {
		static al::AudioResourceLoadingInfo* Callback(const al::Resource* resource) {
			al::AudioResourceLoadingInfo* info = Orig(resource);
			if (!info || !info->seInfo || !info->seInfo->addonInfo) return info;
			auto* own = new al::AudioInfoList<al::AudioAddonSoundArchiveInfo>();
			own->init(16);
			own->mIsLinearSearch = true;
			sead::FileDevice* device = sead::FileDeviceMgr::instance()->getMainFileDevice();
			sead::DirectoryHandle dir;
			sead::DirectoryEntry entries[8];
			u32 read = 0;
			if (device->tryOpenDirectory(&dir, "SoundData")) while (device->tryReadDirectory(&read, &dir, entries, 8) && read) for (u32 i = 0; i < read; i++) {
				const char* file = entries[i].name.cstr();
				size_t length = std::strlen(file) - 5; // "SonicSe.aras" -> "SonicSe"
				bool isMain = false; // a mod's own waves, for everyone
				for (const SoundOwner& owner : soundOwners()) if (al::isEqualString(file, al::StringTmp<64>("%sSe.aras", owner.name).cstr())) isMain = true;
				if (!isMain && (!al::isEndWithString(file, "Se.aras") || !hasOwnClips(al::StringTmp<128>("%.*s", (s32)length - 2, file).cstr()))) continue;
				char* name = new char[length + 1];
				std::memcpy(name, file, length);
				name[length] = '\0';
				auto* pack = static_cast<al::AudioAddonSoundArchiveInfo*>(operator new(sizeof(al::AudioAddonSoundArchiveInfo))); // the game's pack entry: only its name
				pack->name = name;
				own->setInfo(pack);
			}
			device->tryCloseDirectory(&dir);
			if (!own->getSize()) return info;
			al::AudioInfoListWithParts<al::AudioAddonSoundArchiveInfo>* list = info->seInfo->addonInfo;
			if (!list->mParts) {
				list->mParts = new sead::PtrArray<al::AudioInfoList<al::AudioAddonSoundArchiveInfo>>();
				list->mParts->allocBuffer(1, nullptr);
			}
			if (list->mParts->pushBack(own)) { sPacks = info->seInfo; sGamePackCount = list->AudioInfoList<al::AudioAddonSoundArchiveInfo>::getSize(); }
			logLine("Sfx: %d character sound packs listed after the game's %d%s", own->getSize(), list->AudioInfoList<al::AudioAddonSoundArchiveInfo>::getSize(), sPacks ? "" : " (no room, skipped)");
			return info;
		}
	};

	// A character's own waves (Category<body>.byml in <body>Sounds: Name, ArchiveName <body>Se, CategoryName, Volume) join the game's wave list as a part while it's worn, so its users can name them
	inline void addOwnWaves(const al::Resource* resource, const char* body, al::AudioInfoListWithParts<al::SeResourceSpecificInfo>* game) {
		al::StringTmp<128> list("Category%s", body);
		if (!sPacks || !resource->isExistFile(al::StringTmp<128>("%s.byml", list.cstr()))) return;
		sead::ConstPtrArray<char> names;
		names.allocBuffer(1, nullptr);
		names.pushBack(const_cast<char*>(list.cstr()));
		ClipSlot::sSounds->createResourceSpecificInfoList(resource, names, sPacks); // each wave's id from its pack's place in the boot list
		numberAfterGamePacks(ClipSlot::sSounds->mSpecificInfoList);
		ClipSlot::sGameWaves = game;
		ClipSlot::sMadeWaveParts = !game->mParts;
		if (ClipSlot::sMadeWaveParts) {
			game->mParts = new sead::PtrArray<al::AudioInfoList<al::SeResourceSpecificInfo>>();
			game->mParts->allocBuffer(1, nullptr);
		}
		if (!game->mParts->pushBack(ClipSlot::sSounds->mSpecificInfoList)) { logLine("Sfx: %s skipped, the wave list has no room", list.cstr()); return; }
		ClipSlot::sOwnWaves = ClipSlot::sSounds->mSpecificInfoList;
		logLine("Sfx: %s added, %d waves", list.cstr(), ClipSlot::sOwnWaves->AudioInfoList<al::SeResourceSpecificInfo>::getSize());
	}

	inline void beginPlayerSetup(const al::ActorInitInfo* info, const PlayerInitInfo* player) {
		const char* body = rs::getInitPlayerModelName(*player);
		if (!hasOwnClips(body)) return; // the closet scene's player (inside the Odyssey) is the one you play there, so it gets its sounds too
		ClipSlot::claim(al::StringTmp<96>("PlayerAnimation%s", body).cstr()); // the player's sounds are set up before its body, so the slot is claimed here
		al::StringTmp<128> sounds("SoundData/%sSounds", body), effects("EffectData/%sEffects", body);
		if (!ClipSlot::isSlotFile(sounds.cstr())) return;
		sead::ScopedCurrentHeapSetter setter(ClipSlot::sHeap);
		if (!ClipSlot::sSounds && hasFile(sounds.cstr())) {
			al::Resource* resource = al::findOrCreateResource(sounds, nullptr);
			al::ByamlIter root(resource->getByml(body)), parts;
			root.tryGetIterByKey(&parts, "PartsUserNameList");
			sead::ConstPtrArray<char> names;
			names.allocBuffer(parts.getSize() + 1, nullptr);
			char* user = new char[std::strlen(body) + 1]; // the user keeps its name, so it lives in the slot
			std::strcpy(user, body);
			names.pushBack(user);
			for (s32 i = 0; i < parts.getSize(); i++) {
				const char* part = nullptr;
				if (parts.tryGetStringByIndex(&part, i) && resource->isExistFile(al::StringTmp<128>("%s.byml", part))) names.pushBack(const_cast<char*>(part)); // parts it doesn't have stay Mario's
			}
			const al::SeDataBase* game = info->audioDirector->getAudioSystemInfo()->seDataBase;
			ClipSlot::sSounds = new al::SeDataBase();
			ClipSlot::sSounds->mCategoryInfoList = game->mCategoryInfoList;
			addOwnWaves(resource, body, game->mSpecificInfoList);
			ClipSlot::sSounds->mSpecificInfoList = game->mSpecificInfoList;
			ClipSlot::sSounds->createUserInfoList(resource, names);
			ClipSlot::sSounds->setResourceSpecInfoToResourceInfo();
			// parts it doesn't have (MarioFoot...) are Mario's: the game's users join its list while the parts are linked again
			al::AudioInfoListWithParts<al::SeUserInfo>* own = ClipSlot::sSounds->mUserInfoList;
			sead::PtrArray<al::AudioInfoList<al::SeUserInfo>> gameUsers;
			gameUsers.allocBuffer(1, nullptr);
			gameUsers.pushBack(game->mUserInfoList);
			own->mParts = &gameUsers;
			for (s32 i = 0; i < own->AudioInfoList<al::SeUserInfo>::getSize(); i++) if (!own->getInfoAt(i)->isPartsUser) alSeDbFunction::setPartsUserList(own->getInfoAt(i), own);
			own->mParts = nullptr;
			logLine("Sfx: %s built, %d users", sounds.cstr(), names.size());
		}
		if (!ClipSlot::sEffects && hasFile(effects.cstr())) {
			const al::EffectDataBase* game = static_cast<const al::EffectDataBase*>(info->effectSystemInfo->_10);
			void* user = alEffectDataBaseFunction::createEffectUserInfo(game, al::findOrCreateResource(effects, nullptr), al::StringTmp<128>("%s.byml", body));
			ClipSlot::sEffects = new al::EffectDataBase{1, new void*[1]{user}, game->mCategoryCount, game->mCategories};
			logLine("Sfx: %s built", effects.cstr());
		}
		sSetupBody = body;
		isPlayerSetup = ClipSlot::sSounds || ClipSlot::sEffects;
	}

	// Mario's user names asked during setup (the hooks only pass names starting with "Mario"), as the worn character's ("Mario" -> "Sonic", "MarioVoice" -> "SonicVoice")
	inline al::StringTmp<128> ownUserName(const char* name) {
		return al::StringTmp<128>("%s%s", sSetupBody, name + std::strlen("Mario"));
	}

	// The player's sound users come from the worn character's list when it has them
	struct SoundListHook : public mallow::hook::Trampoline<SoundListHook> {
		static const al::SeUserInfo* Callback(const al::AudioInfoListWithParts<al::SeUserInfo>* list, const char* name) {
			if (isPlayerSetup && ClipSlot::sSounds && al::isStartWithString(name, "Mario")) if (const al::SeUserInfo* own = Orig(ClipSlot::sSounds->mUserInfoList, ownUserName(name).cstr())) { logLine("Sfx: sounds %s -> %s", name, own->name); return own; }
			return Orig(list, name);
		}
	};

	// The player's effect user comes from the worn character's list when it has it
	struct EffectListHook : public mallow::hook::Trampoline<EffectListHook> {
		static void* Callback(const al::EffectSystemInfo* info, const char* name) {
			if (isPlayerSetup && ClipSlot::sEffects && al::isStartWithString(name, "Mario")) {
				al::EffectSystemInfo own = *info;
				own._10 = ClipSlot::sEffects;
				al::StringTmp<128> ownName = ownUserName(name);
				if (void* user = Orig(&own, ownName.cstr())) { logLine("Sfx: effects %s -> %s", name, ownName.cstr()); return user; }
			}
			return Orig(info, name);
		}
	};

	// A list's parts grow when full (the game sizes them for its own parts only)
	template <typename T>
	inline bool addPart(al::AudioInfoListWithParts<T>* list, al::AudioInfoList<T>* part) {
		if (!list->mParts || list->mParts->isFull()) {
			auto* parts = new sead::PtrArray<al::AudioInfoList<T>>();
			parts->allocBuffer(list->getPartsSize() + 4, nullptr); // room for a worn character's waves too
			for (s32 i = 0; i < list->getPartsSize(); i++) parts->pushBack(list->mParts->at(i));
			list->mParts = parts;
		}
		return list->mParts->pushBack(part);
	}

	// Main's own sounds (SoundData/PlayerSounds.szs + PlayerSe.aras; the names differ, the game keeps one file per name) join the game's lists at boot, for everyone: its waves, its users (PlayerHammer...) and the lines it adds to the game's users (Mario...)
	// PlayerSounds.szs: SeDbFileList (the game's format: ResourceSpecInfo = wave lists, UserInfo = users), CategoryPlayerSounds, one byml per user with only Main's lines
	inline al::SeDataBase* sGameSounds = nullptr;
	inline void addPlayerSounds(al::SeDataBase* game, const char* owner) {
		al::StringTmp<64> file("SoundData/%sSounds", owner);
		if (!hasFile(file.cstr())) return;
		if (!sPacks) { logLine("Sfx: %s skipped, the pack list wasn't made yet", file.cstr()); return; }
		al::Resource* resource = alAudioSystemFunction::tryGetAudioStationedResource(file.cstr(), nullptr); // the game's own way at boot (SeDataBase too): the usual loader isn't made yet
		if (!resource) { logLine("Sfx: %s skipped, the file didn't open", file.cstr()); return; }
		al::ByamlIter list(resource->getByml("SeDbFileList")), waveLists, userList;
		list.tryGetIterByKey(&waveLists, "ResourceSpecInfo");
		list.tryGetIterByKey(&userList, "UserInfo");
		sead::ConstPtrArray<char> categories, users;
		categories.allocBuffer(waveLists.getSize(), nullptr);
		users.allocBuffer(userList.getSize(), nullptr);
		for (s32 i = 0; i < waveLists.getSize(); i++) if (const char* name = nullptr; waveLists.tryGetStringByIndex(&name, i)) categories.pushBack(const_cast<char*>(name));
		for (s32 i = 0; i < userList.getSize(); i++) if (const char* name = nullptr; userList.tryGetStringByIndex(&name, i)) users.pushBack(const_cast<char*>(name));
		auto* own = new al::SeDataBase();
		own->mCategoryInfoList = game->mCategoryInfoList;
		own->createResourceSpecificInfoList(resource, categories, sPacks); // each wave's id from its pack's place in the boot list
		numberAfterGamePacks(own->mSpecificInfoList);
		s32 waves = own->mSpecificInfoList->AudioInfoList<al::SeResourceSpecificInfo>::getSize();
		if (!addPart(game->mSpecificInfoList, own->mSpecificInfoList)) { logLine("Sfx: %s skipped, the wave list has no room", file.cstr()); return; }
		own->mSpecificInfoList = game->mSpecificInfoList;
		own->createUserInfoList(resource, users);
		own->setResourceSpecInfoToResourceInfo();
		// a user the game has gets Main's lines as parts of its lists; a new user joins the game's user list
		auto* fresh = new al::AudioInfoList<al::SeUserInfo>();
		fresh->init(users.size());
		s32 added = 0;
		for (s32 i = 0; i < own->mUserInfoList->AudioInfoList<al::SeUserInfo>::getSize(); i++) {
			const al::SeUserInfo* user = own->mUserInfoList->getInfoAt(i);
			al::SeUserInfo* gameUser = const_cast<al::SeUserInfo*>(SoundListHook::Orig(game->mUserInfoList, user->name));
			if (!gameUser) { fresh->setInfo(user); continue; }
			if (user->playInfoList && user->playInfoList->getSize()) { if (gameUser->playInfoList) addPart(gameUser->playInfoList, user->playInfoList); else gameUser->playInfoList = user->playInfoList; }
			if (user->actionInfoList && user->actionInfoList->getSize()) { if (gameUser->actionInfoList) addPart(gameUser->actionInfoList, user->actionInfoList); else gameUser->actionInfoList = user->actionInfoList; }
			added++;
		}
		fresh->sort();
		if (fresh->getSize() && !addPart(game->mUserInfoList, fresh)) { logLine("Sfx: %s users skipped, the user list has no room", file.cstr()); return; }
		for (s32 i = 0; i < fresh->getSize(); i++) if (!fresh->getInfoAt(i)->isPartsUser) alSeDbFunction::setPartsUserList(fresh->getInfoAt(i), game->mUserInfoList);
		logLine("Sfx: %s added, %d waves, %d new users, lines on %d game users", file.cstr(), waves, fresh->getSize(), added);
	}

	// The game links its wave and user lists once at boot: Main's sounds join right after
	struct GameSoundsHook : public mallow::hook::Trampoline<GameSoundsHook> {
		static void Callback(al::SeDataBase* dataBase) {
			Orig(dataBase);
			if (sGameSounds) return; // the first is the game's; the later ones are Main's and the characters'
			sGameSounds = dataBase;
			for (const SoundOwner& owner : soundOwners()) addPlayerSounds(dataBase, owner.name);
		}
	};

	// =========================================================
	//                   MOD CLIPS AND EFFECTS
	// =========================================================

	// A mod's file opens the first time the game reads the file it adds to, in that file's memory (the player's files stay loaded for good), only if it fits there: a failed allocation halts the game
	inline al::Resource* loadAddOn(AddOn& addOn, const void* game) {
		if (al::Resource* own = al::findResource(addOn.file)) return own;
		if (!addOn.exists) return nullptr;
		sead::Heap* heap = sead::HeapMgr::instance()->findContainHeap(game);
		u32 need = ClipSlot::getUnpackedSize(addOn.file) + ClipSlot::margin;
		if (!heap || heap->getMaxAllocatableSize(8) < need) { logLine("%s: %s skipped, it needs %u and %s has %zu", addOn.log, addOn.file, need, heap ? heap->getName().cstr() : "no heap", heap ? heap->getMaxAllocatableSize(8) : 0); addOn.exists = false; return nullptr; }
		return al::findOrCreateResource(addOn.file, nullptr);
	}

	// Main has a clip of that name (the game's own finder hands back the clip, not yes/no)
	inline bool hasClip(const al::AnimInfoTable* table, const char* name) {
		for (s32 i = 0; i < table->getInfoCount(); i++) if (al::isEqualString(table->getResInfo(i).name, name)) return true;
		return false;
	}

	// A mod's clips into the game's clip list (a bigger list: the game sizes its own to fit); characters' own clips (PlayerAnimationSonic...) stay as they are
	struct NewClipsHook : public mallow::hook::Trampoline<NewClipsHook> {
		static al::InitResourceDataAnim* Callback(al::Resource* model, al::Resource* anim) {
			al::InitResourceDataAnim* data = Orig(model, anim);
			if (!data || !data->mInfoTable) return data;
			for (AddOn& addOn : addOns()) {
				al::Resource* game = isFile(anim, addOn.gameFile) ? anim : isFile(model, addOn.gameFile) ? model : nullptr;
				if (!game || addOn.isWhole) continue;
				al::Resource* own = loadAddOn(addOn, game);
				if (!own || !own->getResFile()) continue; // lines only (Eye's file, the actor's, the effect lists)
				al::InitResourceDataAnim* mine = Orig(own, nullptr); // the mod's clips, listed the game's way
				if (!mine || !mine->mInfoTable) continue;
				const al::AnimInfoTable* theirs = data->mInfoTable;
				const al::AnimInfoTable* add = mine->mInfoTable;
				auto* table = new al::AnimInfoTable(theirs->getInfoCount() + add->getInfoCount());
				s32 replaced = 0;
				for (s32 i = 0; i < theirs->getInfoCount(); i++) {
					const al::AnimResInfo& info = theirs->getResInfo(i);
					if (hasClip(add, info.name)) { replaced++; continue; }
					table->add(info.name, info.resMaterialAnim, info.frameMax, info.isLoop);
				}
				for (s32 i = 0; i < add->getInfoCount(); i++) {
					const al::AnimResInfo& info = add->getResInfo(i);
					table->add(info.name, info.resMaterialAnim, info.frameMax, info.isLoop);
				}
				table->sort();
				data->mInfoTable = table;
				logLine("Anims: %s added, %d clips (%d replace the game's)", own->getPath(), add->getInfoCount(), replaced);
			}
			return data;
		}
	};

	// An effect user's name (the game keeps it first)
	inline const char* effectUserName(void* user) { return *static_cast<const char**>(user); }

	// The game builds its effect list once: each mod's file is read first (its lines join the game's users as the game reads them), its new users join after, sorted by name as the game sorts the list; a name already there stays the first one's
	struct EffectListBuildHook : public mallow::hook::Trampoline<EffectListBuildHook> {
		static void Callback(al::EffectDataBase* dataBase, const char* path) {
			const al::Resource* game = al::findOrCreateResource(path, nullptr);
			s32 total = 0;
			for (AddOn& addOn : addOns()) if (isFile(game, addOn.gameFile)) if (const al::Resource* own = loadAddOn(addOn, game)) total += own->getEntryNum("/");
			Orig(dataBase, path);
			if (!total) return;
			s32 count = dataBase->mUserCount;
			void** users = new void*[count + total];
			for (s32 i = 0; i < count; i++) users[i] = dataBase->mUsers[i];
			for (const AddOn& addOn : addOns()) {
				al::Resource* own = isFile(game, addOn.gameFile) ? al::findResource(addOn.file) : nullptr;
				if (!own) continue;
				s32 added = 0, kept = 0;
				for (u32 e = 0; e < own->getEntryNum("/"); e++) {
					al::StringTmp<128> entry;
					own->getEntryName(&entry, "/", e);
					const char* dot = std::strchr(entry.cstr(), '.');
					al::StringTmp<128> name("%.*s", dot ? (s32)(dot - entry.cstr()) : entry.calcLength(), entry.cstr());
					bool isThere = false;
					for (s32 u = 0; u < count && !isThere; u++) isThere = al::isEqualString(effectUserName(users[u]), name.cstr());
					if (isThere) { kept++; continue; } // the game's (its lines joined it) or another mod's
					users[count++] = alEffectDataBaseFunction::createEffectUserInfo(dataBase, own, entry);
					added++;
				}
				logLine("Effects: %s added, %d new users (%d already there)", own->getPath(), added, kept);
			}
			std::sort(users, users + count, [](void* a, void* b) { return std::strcmp(effectUserName(a), effectUserName(b)) < 0; }); // the game finds users by halving the sorted list
			dataBase->mUsers = users;
			dataBase->mUserCount = count;
		}
	};
	// =========================================================
	//                           SUITS
	// =========================================================

	// A suit's names in the shop and closet come from its shop file's Names (language -> ItemCloth/ItemCap -> label -> text), in the game's language or else USen; all made once with the lists, as the game's UTF-16 text, so they don't need the file later
	struct ShopName { char file[16]; char label[64]; const char16* text; };
	inline ShopName sShopNames[128] = {};
	inline s32 sShopNameCount = 0;
	inline char16 sShopTexts[16384] = {};
	inline s32 sShopTextsUsed = 0;
	inline bool sMadeShopNames = false;
	inline const char16* toShopText(const char* text) {
		char16* out = sShopTexts + sShopTextsUsed;
		s32 room = (s32)(sizeof(sShopTexts) / sizeof(*sShopTexts)) - sShopTextsUsed - 1, length = 0; // one place kept for the end
		if (room < 0) return nullptr;
		for (const u8* at = (const u8*)text; *at;) {
			u32 c = *at++;
			s32 more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
			c &= 0x7F >> more;
			for (; more && (*at & 0xC0) == 0x80; more--) c = c << 6 | (*at++ & 0x3F);
			if (length + (c >= 0x10000 ? 2 : 1) > room) return nullptr;
			if (c >= 0x10000) { c -= 0x10000; out[length++] = 0xD800 | c >> 10; out[length++] = 0xDC00 | (c & 0x3FF); }
			else out[length++] = c;
		}
		out[length++] = 0;
		sShopTextsUsed += length;
		return out;
	}
	inline void makeShopNames() {
		if (sMadeShopNames) return;
		sMadeShopNames = true;
		const char* language = al::getLanguageString();
		s32 suits = 0;
		for (const AddOn& addOn : addOns()) {
			if (!al::isEqualString(addOn.gameFile, shopFile)) continue;
			const al::Resource* own = al::findResource(addOn.file);
			const u8* names = own ? own->tryGetByml("Names") : nullptr;
			if (!names) continue;
			al::ByamlIter all(names), texts;
			if (!(language && all.tryGetIterByKey(&texts, language)) && !all.tryGetIterByKey(&texts, "USen")) continue;
			suits++;
			for (const char* file : {"ItemCloth", "ItemCap"}) {
				al::ByamlIter kind;
				if (!texts.tryGetIterByKey(&kind, file)) continue;
				for (s32 i = 0; i < kind.getSize(); i++) {
					const char* label = nullptr;
					const char* text = nullptr;
					if (!kind.getKeyName(&label, i) || !kind.tryGetStringByKey(&text, label)) continue;
					const char16* own16 = sShopNameCount < 128 ? toShopText(text) : nullptr;
					if (!own16) { logLine("Shop: %s %s left to the game's text (no room)", file, label); continue; }
					ShopName& name = sShopNames[sShopNameCount++];
					al::copyString(name.file, file, sizeof(name.file));
					al::copyString(name.label, label, sizeof(name.label));
					name.text = own16;
				}
			}
		}
		logLine("Shop: %d names from %d suits in %s", sShopNameCount, suits, language ? language : "no language");
	}

	// A suit's name (made with the lists); null if it's the game's
	inline const char16* findShopName(const char* file, const char* label) {
		if (!sShopNameCount || !file || !label || !al::isStartWithString(file, "Item")) return nullptr; // the game asks for its texts all the time
		for (s32 i = 0; i < sShopNameCount; i++) if (al::isEqualString(sShopNames[i].file, file) && al::isEqualString(sShopNames[i].label, label)) return sShopNames[i].text;
		return nullptr;
	}
	struct ShopNameHook : public mallow::hook::Trampoline<ShopNameHook> {
		static const char16* Callback(const al::IUseMessageSystem* system, const char* file, const char* label) {
			const char16* own = findShopName(file, label);
			return own ? own : Orig(system, file, label);
		}
	};
	struct ShopNameExistHook : public mallow::hook::Trampoline<ShopNameExistHook> {
		static bool Callback(const al::IUseMessageSystem* system, const char* file, const char* label) { return findShopName(file, label) || Orig(system, file, label); }
	};

	// A suit's shop and closet icons come from its own file (ObjectData/TextureClothes<Item>: its <Item> and <Item>Cap pictures), there only when that suit is installed
	struct ShopIconHook : public mallow::hook::Trampoline<ShopIconHook> {
		static nn::ui2d::TextureInfo* Callback(const char* archive, const char* model, const char* texture) {
			if (archive && texture && al::isEqualString(archive, "ObjectData/TextureClothes")) {
				al::StringTmp<128> own("%s%s", archive, texture);
				if (!hasFile(own.cstr()) && al::isEndWithString(texture, "Cap")) own.format("%s%.*s", archive, (s32)std::strlen(texture) - 3, texture); // "SonicCap" is in TextureClothesSonic
				if (hasFile(own.cstr())) return Orig(own.cstr(), own.cstr() + std::strlen("ObjectData/"), texture); // its picture file is named after its archive, as the game sets up only that one
			}
			return Orig(archive, model, texture);
		}
	};

	// A suit's cap fits on the bodies that need their own (Mario64, MarioBone...): the game asks the body for InitPartsFixInfoHead<Cap>; a body without it reads <Body> from the cap's <Cap>HeadFix, there only when that suit is installed
	inline const u8* tryGetCapFit(const al::LiveActor* body, const char* name, const char* suffix) {
		if (suffix || !name || !al::isStartWithString(name, "InitPartsFixInfoHead")) return nullptr;
		const char* cap = name + std::strlen("InitPartsFixInfoHead");
		al::StringTmp<128> file("ObjectData/%sHeadFix", cap);
		if (!hasFile(file.cstr())) return nullptr;
		const al::Resource* own = al::findOrCreateResource(file.cstr(), nullptr);
		const u8* fit = own ? own->tryGetByml(al::StringTmp<128>("%s", al::getModelName(body))) : nullptr;
		if (fit) logLine("CapFit: %s on %s from %s", cap, al::getModelName(body), file.cstr());
		return fit;
	}
	struct CapFitExistHook : public mallow::hook::Trampoline<CapFitExistHook> {
		static bool Callback(const al::LiveActor* actor, const char* name, const char* suffix) { return Orig(actor, name, suffix) || tryGetCapFit(actor, name, suffix); }
	};
	struct CapFitHook : public mallow::hook::Trampoline<CapFitHook> {
		static const u8* Callback(const al::LiveActor* actor, const char* name, const char* suffix) {
			if (CapFitExistHook::Orig(actor, name, suffix)) return Orig(actor, name, suffix);
			const u8* fit = tryGetCapFit(actor, name, suffix);
			return fit ? fit : Orig(actor, name, suffix);
		}
	};

	// =========================================================
	//                          TABLES
	// =========================================================

	// One value of a table into the writer (lists and groups whole)
	inline void copyTableValue(al::ByamlWriter& writer, const al::ByamlIter& from, s32 index, bool isHash) {
		al::ByamlData data;
		const char* key = nullptr;
		if (isHash) from.getByamlDataAndKeyName(&data, &key, index);
		else from.getByamlDataByIndex(&data, index);
		al::ByamlIter inner;
		const char* text = nullptr;
		bool flag = false;
		s32 number = 0;
		u32 unsignedNumber = 0;
		f32 real = 0.0f;
		switch (data.getType()) {
		case al::ByamlDataType::Array:
		case al::ByamlDataType::Hash:
			if (isHash ? from.tryGetIterAndKeyNameByIndex(&inner, &key, index) : from.tryGetIterByIndex(&inner, index)) isHash ? writer.pushIter(key, inner) : writer.pushIter(inner);
			return;
		case al::ByamlDataType::String: from.tryConvertString(&text, &data); isHash ? writer.addString(key, text) : writer.addString(text); return;
		case al::ByamlDataType::Bool: from.tryConvertBool(&flag, &data); isHash ? writer.addBool(key, flag) : writer.addBool(flag); return;
		case al::ByamlDataType::Int: from.tryConvertInt(&number, &data); isHash ? writer.addInt(key, number) : writer.addInt(number); return;
		case al::ByamlDataType::UInt: from.tryConvertUInt(&unsignedNumber, &data); isHash ? writer.addUInt(key, unsignedNumber) : writer.addUInt(unsignedNumber); return;
		case al::ByamlDataType::Float: from.tryConvertFloat(&real, &data); isHash ? writer.addFloat(key, real) : writer.addFloat(real); return;
		default: isHash ? writer.addNull(key) : writer.addNull(); return;
		}
	}

	// A list line's name: an action's (PadAndCameraCtrl lists one action several times: Main's lines for an action replace all the game's) or an effect's
	inline const char* actionName(const al::ByamlIter& list, s32 index) {
		al::ByamlIter line;
		const char* name = nullptr;
		return list.tryGetIterByIndex(&line, index) && (line.tryGetStringByKey(&name, "ActionName") || line.tryGetStringByKey(&name, "EffectName")) ? name : nullptr;
	}
	inline bool hasAction(const al::ByamlIter& list, const char* name) {
		for (s32 i = 0; i < list.getSize(); i++) if (const char* own = actionName(list, i); own && al::isEqualString(own, name)) return true;
		return false;
	}

	// The game's group or list with the mod's into the writer: the mod's keys and named lines win; a list of named lines in a group joins the game's list the same way (an effect user's EffectList)
	inline void mergeInto(al::ByamlWriter& writer, const al::ByamlIter& theirs, const al::ByamlIter& mine, const char* name, s32 at = -1) { // at: where in the game's list the mod's lines go (else after it)
		bool isHash = theirs.isTypeHash();
		if (at < 0 || isHash) at = theirs.getSize();
		if (name) isHash ? writer.pushHash(name) : writer.pushArray(name);
		else isHash ? writer.pushHash() : writer.pushArray();
		auto copyTheirs = [&](s32 from, s32 to) {
			for (s32 i = from; i < to; i++) {
				const char* key = nullptr;
				if (isHash ? theirs.getKeyName(&key, i) && mine.isExistKey(key) : (key = actionName(theirs, i)) && hasAction(mine, key)) continue; // Main's is used
				copyTableValue(writer, theirs, i, isHash);
			}
		};
		copyTheirs(0, at);
		for (s32 i = 0; i < mine.getSize(); i++) {
			const char* key = nullptr;
			al::ByamlIter own, game;
			if (isHash && mine.tryGetIterAndKeyNameByIndex(&own, &key, i) && theirs.tryGetIterByKey(&game, key) && own.isTypeArray() && game.isTypeArray() && own.getSize() > 0 && actionName(own, 0)) { mergeInto(writer, game, own, key); continue; }
			copyTableValue(writer, mine, i, isHash);
		}
		copyTheirs(at, theirs.getSize());
		writer.pop();
	}

	// The game's table with Main's lines, written next to the game's file in its memory; null if it doesn't fit
	inline const u8* mergeTable(const u8* game, const u8* own, sead::Heap* heap, s32 at = -1) {
		al::ByamlIter theirs(game), mine(own);
		if (theirs.isTypeHash() != mine.isTypeHash()) return nullptr;
		al::ByamlWriter writer(heap, false);
		mergeInto(writer, theirs, mine, nullptr, at);
		u32 size = writer.calcPackSize();
		u8* table = (u8*)heap->tryAlloc(size, 8);
		if (!table) return nullptr;
		sead::RamWriteStream stream(table, size, sead::Stream::Modes::Binary);
		writer.write(&stream);
		return table;
	}

	// The lines a mod's table adds (an effect user's: its EffectList)
	inline s32 lineCount(const u8* lines) {
		al::ByamlIter added(lines), list;
		return added.tryGetIterByKey(&list, "EffectList") ? list.getSize() : added.getSize();
	}

	// The game's first costume after Mario's own clothes in a shop list (ItemCloth/ItemCap: the second line; ItemList: after the heart and Moon lines)
	inline s32 firstCostume(const u8* game) {
		al::ByamlIter list(game), line;
		for (s32 i = 0; i < list.getSize(); i++) {
			const char* item = nullptr;
			const char* type = nullptr;
			if (!list.tryGetIterByIndex(&line, i) || !line.tryGetStringByKey(&item, "ItemName") || al::isEqualString(item, "Mario")) continue;
			if (!line.tryGetStringByKey(&type, "ItemType") || al::isEqualString(type, "Cap") || al::isEqualString(type, "Clothes")) return i;
		}
		return list.getSize();
	}

	// Each joined table is made once (the player's files stay loaded); keyed by the game's own table
	struct NewTable { const u8* game; const u8* joined; };
	inline NewTable sNewTables[32] = {};
	inline s32 sNewTableCount = 0;
	inline const u8* withNewLines(const al::Resource* resource, const sead::SafeString& name, const u8* game) {
		if (!game || !resource) return game;
		bool isShop = isFile(resource, shopFile); // the shop and closet lists, read once at boot
		if (!isShop && !al::isStartWithString(resource->getPath(), "ObjectData/Player") && !al::isStartWithString(resource->getPath(), "EffectData/")) return game; // every game file in the table is one of these: the game reads tables all the time
		for (s32 i = 0; i < sNewTableCount; i++) if (sNewTables[i].game == game) return sNewTables[i].joined;
		const u8* joined = game;
		s32 first = isShop ? firstCostume(game) : -1, at = first; // after Mario's own clothes: a character's lines (it has its own clips) first, then a power-up suit's
		for (AddOn& addOn : addOns()) {
			if (addOn.isWhole || !isFile(resource, addOn.gameFile)) continue;
			const al::Resource* own = loadAddOn(addOn, game);
			const u8* lines = own ? own->tryGetByml(name) : nullptr;
			if (!lines) continue;
			bool isCharacter = isShop && hasOwnClips(addOn.file + std::strlen(shopFile));
			sead::Heap* heap = sead::HeapMgr::instance()->findContainHeap(game);
			const u8* next = heap && sNewTableCount < 32 ? mergeTable(joined, lines, heap, !isShop ? -1 : isCharacter ? first : at) : nullptr;
			if (!next) { logLine("%s: %s of %s kept as the game's (no room)", addOn.log, name.cstr(), resource->getPath()); return game; }
			if (isCharacter) first += lineCount(lines);
			if (isShop) at += lineCount(lines);
			joined = next;
			logLine("%s: %s of %s + %d lines from %s", addOn.log, name.cstr(), resource->getPath(), lineCount(lines), addOn.file);
		}
		if (isShop) makeShopNames(); // the suits' files are loaded now
		if (joined == game) return game;
		sNewTables[sNewTableCount++] = {game, joined};
		return joined;
	}
	struct NewTableHook : public mallow::hook::Trampoline<NewTableHook> {
		static const u8* Callback(const al::Resource* resource, const sead::SafeString& name) { return withNewLines(resource, name, Orig(resource, name)); }
	};
	struct TryNewTableHook : public mallow::hook::Trampoline<TryNewTableHook> {
		static const u8* Callback(const al::Resource* resource, const sead::SafeString& name) { return withNewLines(resource, name, Orig(resource, name)); }
	};

	// =========================================================
	//                           HOOKS
	// =========================================================

	// The file hooks (PlayerRetarget::Install installs them)
	inline void InstallResources() {
		BodyAnimationArchiveHook::InstallAtOffset(0x4440C0); // the shared body setup (initMarioModelActor, its Demo and Closet versions), where the "PlayerAnimation" archive name is passed on
		ClipSlot::CategoryHook::InstallAtSymbol("_ZNK2al14ResourceSystem25findCategoryNameFromTableERKN4sead14SafeStringBaseIcEE");
		ClipSlot::StageHook::InstallAtSymbol("_ZN10StageScene4initERKN2al13SceneInitInfoE");
		ClipSlot::WorldHook::InstallAtSymbol("_ZN2al24destroyWorldResourceHeapEb");
		ClipSlot::ReuseHook::InstallAtSymbol("_ZN2al13ArchiveHolder22addNewLoadRequestEntryERKN4sead14SafeStringBaseIcEEPNS1_4HeapEPNS1_10FileDeviceE");
		FootPrintArchiveHook::InstallAtSymbol("_ZN2al15FootPrintHolderC2EPNS_9LiveActorEPKcPNS_9HitSensorEPNS_15FootPrintServerE");
		PackListHook::InstallAtSymbol("_ZN2al24AudioResourceLoadingInfo10createInfoEPKNS_8ResourceE");
		SoundListHook::InstallAtSymbol("_ZN2al16tryFindAudioInfoINS_10SeUserInfoEEEPKT_PKNS_22AudioInfoListWithPartsIS2_EEPKc");
		EffectListHook::InstallAtSymbol("_ZN16alEffectFunction17tryFindEffectUserEPKN2al16EffectSystemInfoEPKc");
		GameSoundsHook::InstallAtSymbol("_ZN2al10SeDataBase33setResourceSpecInfoToResourceInfoEv");
		NewClipsHook::InstallAtSymbol("_ZN2al20InitResourceDataAnim9tryCreateEPNS_8ResourceES2_");
		NewTableHook::InstallAtSymbol("_ZNK2al8Resource7getBymlERKN4sead14SafeStringBaseIcEE");
		TryNewTableHook::InstallAtSymbol("_ZNK2al8Resource10tryGetBymlERKN4sead14SafeStringBaseIcEE");
		CapFitExistHook::InstallAtSymbol("_ZN2al24isExistModelResourceYamlEPKNS_9LiveActorEPKcS4_");
		CapFitHook::InstallAtSymbol("_ZN2al20getModelResourceYamlEPKNS_9LiveActorEPKcS4_");
		EffectListBuildHook::InstallAtSymbol("_ZN2al14EffectDataBaseC2EPKc");
		ShopNameHook::InstallAtSymbol("_ZN2al22getSystemMessageStringEPKNS_17IUseMessageSystemEPKcS4_");
		ShopNameExistHook::InstallAtSymbol("_ZN2al27isExistLabelInSystemMessageEPKNS_17IUseMessageSystemEPKcS4_");
		ShopIconHook::InstallAtSymbol("_ZN2al17createTextureInfoEPKcS1_S1_");
	}
}
