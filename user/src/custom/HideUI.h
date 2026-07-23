#pragma once
#include "custom/.Globals.h"
#include "Library/Play/Layout/SimpleLayoutAppearWaitEnd.h"

struct CounterLifeCtrl;
struct ShineCounter;
struct MapMini;
struct CoinCounter;

extern "C" {
    void _ZN15CounterLifeCtrl3endEv(CounterLifeCtrl*);
    void _ZN15CounterLifeCtrl6appearEv(CounterLifeCtrl*);
    void _ZN7MapMini3endEv(MapMini*);
    void _ZN7MapMini13appearSlideInEv(MapMini*);
    void _ZN11CoinCounter6tryEndEv(CoinCounter*);
    void _ZN11CoinCounter8tryStartEv(CoinCounter*);
    void _ZN12ShineCounter6tryEndEv(ShineCounter*);
    void _ZN12ShineCounter8tryStartEv(ShineCounter*);
    void _ZN2al25SimpleLayoutAppearWaitEnd3endEv(al::SimpleLayoutAppearWaitEnd*);
    void _ZN2al18appearLayoutIfDeadEPNS_11LayoutActorE(al::LayoutActor*);
}

namespace HideUI {
    inline bool isHide = false;
    inline bool isPending = false;

    template <typename T>
    inline T* memberAt(void* base, ptrdiff_t offset) { return *reinterpret_cast<T**>(reinterpret_cast<u8*>(base) + offset); }

    inline void hideAll(void* l) {
        _ZN2al25SimpleLayoutAppearWaitEnd3endEv(memberAt<al::SimpleLayoutAppearWaitEnd>(l, 0x68));
        _ZN15CounterLifeCtrl3endEv(memberAt<CounterLifeCtrl>(l, 0x20));
        _ZN7MapMini3endEv(memberAt<MapMini>(l, 0x50));
        _ZN12ShineCounter6tryEndEv(memberAt<ShineCounter>(l, 0x28));
        _ZN11CoinCounter6tryEndEv(memberAt<CoinCounter>(l, 0x18));
        _ZN11CoinCounter6tryEndEv(memberAt<CoinCounter>(l, 0x30));
    }

    inline void showAll(void* l) {
        _ZN15CounterLifeCtrl6appearEv(memberAt<CounterLifeCtrl>(l, 0x20));
        _ZN7MapMini13appearSlideInEv(memberAt<MapMini>(l, 0x50));
        _ZN11CoinCounter8tryStartEv(memberAt<CoinCounter>(l, 0x18));
        _ZN11CoinCounter8tryStartEv(memberAt<CoinCounter>(l, 0x30));
        _ZN12ShineCounter8tryStartEv(memberAt<ShineCounter>(l, 0x28));
        _ZN2al18appearLayoutIfDeadEPNS_11LayoutActorE(memberAt<al::LayoutActor>(l, 0x68));
    }

    struct StartHook : public mallow::hook::Trampoline<StartHook> {
        static void Callback(void* layout) {
            isPending = isConfig()->isHide;
            Orig(layout);
        }
    };

    struct NrvWaitHook : public mallow::hook::Trampoline<NrvWaitHook> {
        static void Callback(const void* nerve, void* keeper) {
            Orig(nerve, keeper);
            void* layout = *reinterpret_cast<void**>(keeper);

            if (al::isPadTriggerPressLeftStick(-1) && !al::isPadHoldL(-1)) {
                if (isPending) {
                    isPending = false;
                    hideAll(layout);
                } else {
                    isConfig()->isHide = !isConfig()->isHide;
                    if (isConfig()->isHide) hideAll(layout);
                    else showAll(layout);
                    mallow::config::saveConfig();
                }
                if (isHakoniwa) al::tryStartSe(isHakoniwa, isConfig()->isHide ? "CameraSubjectiveOff" : "CameraSubjectiveOn");
                return;
            }
            if (isPending && al::isPadHoldAny(-1)) { isPending = false; hideAll(layout); }
        }
    };

    inline void Install() {
        StartHook::InstallAtSymbol("_ZN16StageSceneLayout5startEv");
        NrvWaitHook::InstallAtOffset(0x20D438);
    }
}