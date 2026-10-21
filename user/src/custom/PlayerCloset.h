#pragma once
#include "custom/PlayerResources.h"
#include "Library/Layout/LayoutActor.h"
#include "Library/Layout/LayoutActorUtil.h"
#include "Layout/ShopLayoutInfo.h"
#include "Util/ClothUtil.h"

class CommonHorizontalList { public: void setSelectedIdx(s32 idx); }; // one of the closet's outfit/cap rows
class StageSceneStateCloset { public: void appearMarioModel(); void hideMarioModel(); };
class PlayerJointControlPartsDynamics { public: void resetDynamics(); }; // a preview's springs

// The closet opens on the suits list (Mario and the suits), or on the characters list (Mario and the suits with their own clips) while a character is worn; L + R there swaps the lists; in the characters list the outfit and cap rows move together; a worn outfit or cap the list hasn't (worn from before) starts on Mario's; random never picks a character
namespace PlayerCloset {

	// A field of the closet or one of its rows (closet: +0x28 row layouts, +0x30 rows, +0x38 their columns, +0x50 their texts, +0x70 the row in use, +0x80 picked cap, +0x84 picked outfit, +0x1A0 one entry per column (0x4B0 each: name at +8, outfit text +0x68, cap text +0x288, springs +0x4A8), +0x1AC preview bump frames; a row: +0x8C places filled)
	template <typename T>
	inline T& at(const void* base, s32 offset) { return *(T*)((const u8*)base + offset); }

	inline s32& picked(void* closet, s32 row) { return at<s32>(closet, 0x84 - row * 4); } // the outfit's at +0x84, the cap's at +0x80

	// The place of a column in a row, or -1 if the row hasn't it
	inline s32 placeOf(void* closet, s32 row, s32 column) {
		s32* columns = at<s32**>(closet, 0x38)[row];
		for (s32 i = 0; i < at<s32>(at<void**>(closet, 0x30)[row], 0x8C); i++) if (columns[i] == column) return i;
		return -1;
	}

	// A row's cursor, pick and texts on a column, set as the game sets them when that row's cursor moves (texts, then the text panel's appear); false if the row hasn't that column
	inline bool pick(void* closet, s32 row, s32 column) {
		s32 place = placeOf(closet, row, column);
		if (place < 0) return false;
		at<CommonHorizontalList**>(closet, 0x30)[row]->setSelectedIdx(place);
		picked(closet, row) = column;
		const u8* entry = at<u8*>(closet, 0x1A0) + column * 0x4B0;
		al::LayoutActor* text = at<al::LayoutActor**>(closet, 0x50)[row];
		al::setPaneString(text, "TxtContent00", at<const char16*>(entry, row ? 0x288 : 0x68));
		al::setPaneString(text, "TxtContent01", al::getSystemMessageString(at<al::LayoutActor**>(closet, 0x28)[row], row ? "ItemCap" : "ItemCloth", al::StringTmp<128>("%s_Explain", at<const char*>(entry, 8)).cstr()));
		text->appear();
		return true;
	}

	// The preview wears the picks, bumps and its springs restart, as the game does after a pick changes (hideMarioModel hides the picks' models, so it goes before the picks change)
	inline void wearPicks(void* closet) {
		static_cast<StageSceneStateCloset*>(closet)->appearMarioModel();
		at<s32>(closet, 0x1AC) = 3;
		const u8* entries = at<u8*>(closet, 0x1A0);
		s32 outfit = picked(closet, 0), cap = picked(closet, 1);
		at<PlayerJointControlPartsDynamics*>(entries + outfit * 0x4B0, 0x4A8)->resetDynamics();
		if (cap != outfit) at<PlayerJointControlPartsDynamics*>(entries + cap * 0x4B0, 0x4A8)->resetDynamics();
	}

	inline bool isPickGone(void* closet) { return placeOf(closet, 0, picked(closet, 0)) < 0 || placeOf(closet, 1, picked(closet, 1)) < 0; } // the outfit or cap picked isn't in the list up

	// Each row's cursor on its pick, or on Mario's when the list up hasn't it (Mario's is always first); the preview changes only when a pick does
	inline void settle(void* closet) {
		bool isGone = isPickGone(closet);
		if (isGone) static_cast<StageSceneStateCloset*>(closet)->hideMarioModel();
		for (s32 row = 0; row < 2; row++) if (!pick(closet, row, picked(closet, row))) pick(closet, row, 0);
		if (isGone) wearPicks(closet);
	}

	inline bool sIsClosetHiding = false;
	inline bool sIsClosetShown = false; // the characters list is up
	inline bool sIsClosetOpened = false; // the closet just filled its rows; its picks are set after that, so the first select step checks them

	template <bool isCap>
	struct ClosetHaveHook : public mallow::hook::Trampoline<ClosetHaveHook<isCap>> {
		using Base = mallow::hook::Trampoline<ClosetHaveHook<isCap>>;
		static bool Callback(GameDataHolderAccessor accessor, const char* name) { return !(sIsClosetHiding && PlayerRetarget::hasOwnClips(name) != sIsClosetShown) && Base::Orig(accessor, name); }
	};
	// The random outfit and cap (the only ones that ask by place in the list) skip the characters; what they'd have rolled goes to Mario's
	template <bool isCap>
	struct ClosetRandomHook : public mallow::hook::Trampoline<ClosetRandomHook<isCap>> {
		using Base = mallow::hook::Trampoline<ClosetRandomHook<isCap>>;
		static bool Callback(GameDataHolderAccessor accessor, s32 index) { return !PlayerRetarget::hasOwnClips((isCap ? rs::getCapList(accessor) : rs::getClothList(accessor))[index]->name) && Base::Orig(accessor, index); }
	};
	struct ClosetListHook : public mallow::hook::Trampoline<ClosetListHook> {
		// The rows of the list that's up (the game always puts Mario's first)
		static void rows(void* closet) {
			sIsClosetHiding = true;
			Orig(closet);
			sIsClosetHiding = false;
		}
		static void Callback(void* closet) {
			GameDataHolderAccessor accessor(at<al::LiveActor*>(at<u8*>(closet, 0x1A0), 0x58)); // the game's own, from Mario's column
			sIsClosetShown = PlayerRetarget::hasOwnClips(GameDataFunction::getCurrentCostumeTypeName(accessor)) || PlayerRetarget::hasOwnClips(GameDataFunction::getCurrentCapTypeName(accessor)); // the closet's cursor and pick start on what's worn
			rows(closet);
			sIsClosetOpened = true;
		}
	};
	struct ClosetSelectHook : public mallow::hook::Trampoline<ClosetSelectHook> {
		static void Callback(void* closet) {
			if (sIsClosetOpened && isPickGone(closet)) settle(closet);
			sIsClosetOpened = false;
			if ((al::isPadTriggerL(-1) && al::isPadHoldR(-1)) || (al::isPadTriggerR(-1) && al::isPadHoldL(-1))) {
				sIsClosetShown = !sIsClosetShown;
				ClosetListHook::rows(closet);
				settle(closet);
				if (isHakoniwa) al::tryStartSe(isHakoniwa, "DemoReturnHomeJingle"); // the jingle of the L + left stick options
			}
			s32 outfit = picked(closet, 0), cap = picked(closet, 1);
			Orig(closet);
			// In the characters list a cursor that moved takes the other row with it: to the same item, or Mario's when that row hasn't it
			s32 row = at<s32>(closet, 0x70), moved = picked(closet, row), other = 1 - row;
			if (!sIsClosetShown || moved == (row ? cap : outfit) || moved == picked(closet, other)) return;
			static_cast<StageSceneStateCloset*>(closet)->hideMarioModel();
			if (!pick(closet, other, moved)) pick(closet, other, 0);
			wearPicks(closet);
		}
	};

	inline void Install() {
		ClosetHaveHook<false>::InstallAtSymbol("_ZN2rs11isHaveClothE22GameDataHolderAccessorPKc");
		ClosetHaveHook<true>::InstallAtSymbol("_ZN2rs9isHaveCapE22GameDataHolderAccessorPKc");
		ClosetRandomHook<false>::InstallAtSymbol("_ZN2rs11isHaveClothE22GameDataHolderAccessori");
		ClosetRandomHook<true>::InstallAtSymbol("_ZN2rs9isHaveCapE22GameDataHolderAccessori");
		ClosetListHook::InstallAtSymbol("_ZN21StageSceneStateCloset15updateExistItemEv");
		ClosetSelectHook::InstallAtSymbol("_ZN21StageSceneStateCloset9exeSelectEv");	}
}
