#pragma once
#include "ModConfig.h"
#include "custom/_Globals.h"

inline bool isDefinitve() {
    static bool value = al::isExistFile("ObjectData/MarioDefinitve.txt");
    return value;
}

inline bool isTempWait() {
    for (const char* name : {"WaitHot", "WaitCold", "WaitVeryCold"})
        if (isHakoniwa->mAnimator->isAnim(name)) return true;
    return false;
}

namespace CustomAnimation {

    inline const char* remapAnim(const char* name, PlayerAnimator* anim = nullptr) {
        if (!isHakoniwa || (anim && anim != isHakoniwa->mAnimator)
            || rs::isPlayer2D(isHakoniwa)) return nullptr;

        if (isWeaponOn) {
            if (al::isEqualString(name, "Wait")) return "BattleWait";
        }
        if (isTanooki) {
            if (al::isEqualString(name, "Glide")) return "GlideAlt";
            if (al::isEqualString(name, "JumpBroad8")) return "JumpBroad8Alt";
        }
        if (isFly) {
            if (al::isEqualString(name, "GlideFloat")) return "GlideFloatSuper";
            if (al::isEqualString(name, "Wait")) return "WaitSuper";
        }
        if (isMetal) {
            if (al::isEqualString(name, "Wait")) return "BattleWait";
            if (al::isEqualString(name, "JumpDashFast")) return "Jump";
            if (al::isEqualString(name, "WearEnd")) return "WearEndSuper";
        }
        if (isBrawl) {
            if (al::isEqualString(name, "BattleWait")) return "WaitBrawlFight";
            if (al::isEqualString(name, "JumpDashFast")) return "Jump";
            if (al::isEqualString(name, "Wait")) return "WaitBrawl";
            if (al::isEqualString(name, "WearEnd")) return "WearEndBrawl";
        }
        if (isSuper) {
            if (al::isEqualString(name, "BattleWait")) return "WaitSuperFight";
            if (al::isEqualString(name, "GlideFloat")) return "GlideFloatSuper";
            if (al::isEqualString(name, "Wait")) return "WaitSuper";
            if (al::isEqualString(name, "WearEnd")) return "WearEndSuper";
        }
        if (!isFeather && !isTanooki && !isFly && !isBrawl && !isSuper) {
            if (al::isEqualString(name, "JumpDashFast")) return "JumpDashFastClassic";
        }

        bool isSuit = (isMario && isCapeOn) || isFeather || isFly || isBrawl || isSuper;

        if (isSuit) {
            if (al::isEqualString(name, "HipDrop")) return "HipDropPunch";
            if (al::isEqualString(name, "HipDropLand")) return "HipDropPunchLand";
            if (al::isEqualString(name, "HipDropReaction")) return "HipDropPunchReaction";
            if (al::isEqualString(name, "HipDropStart")) return "HipDropPunchStart";
            if (al::isEqualString(name, "SwimDive")) return "SwimHipDropPunch";
            if (al::isEqualString(name, "SwimHipDrop")) return "SwimHipDropPunch";
            if (al::isEqualString(name, "SwimHipDropLand")) return "SwimHipDropPunchLand";
            if (al::isEqualString(name, "SwimHipDropStart")) return "SwimHipDropPunchStart";
        }
        if (isSuit || isMetal) {
            if (al::isEqualString(name, "LandStiffen")) return "LandSuper";
            if (al::isEqualString(name, "MofumofuDemoOpening2")) return "MofumofuDemoOpening2Super";
        }
        if (al::isEqualString(name, "BattleWait")) return "WaitBrawl";

        #ifdef ALLOW_DASH
            if (isMetal || isBrawl)
                for (const char* move : {"Move", "MoveMoon"})
                    if (al::isEqualString(name, move)) return "MoveBrawl";
            if (isSuper)
                for (const char* move : {"Move", "MoveMoon"})
                    if (al::isEqualString(name, move)) return "MoveSuper";
            if (!isFeather && !isTanooki && !isFly && !isBrawl && !isSuper)
                for (const char* move : {"Move", "MoveMoon"})
                    if (al::isEqualString(name, move)) return "MoveClassic";
        #endif

        return nullptr;
    }

    // Which idle-cycle anim the current suit uses, or nullptr if none — add new suits here
    inline const char* idleCycleAnim(bool alt, PlayerAnimator* anim = nullptr) {
        if (!isHakoniwa || (anim && anim != isHakoniwa->mAnimator)) return nullptr;
        if (isDefinitve()) return alt ? "AreaWaitView" : "AreaWaitStretch";
        //if (isBrawl) return alt ? "AreaWaitView" : "AreaWaitStretch"; // only one anim, ignores alt
        return nullptr;
    }

    // Every 720 idle frames, swaps Wait to the suit's cycle anim and back, flipping alt each time
    inline void updateIdleCycle(PlayerActorHakoniwa* thisPtr) {
        static int idleCycle = 0;
        static bool alt = false;
        const char* altAnim = idleCycleAnim(alt);
        if (!altAnim || isTempWait() || !al::isNerve(thisPtr, getNerveAt(nrvHakoniwaWait))) { idleCycle = 0; return; }

        if (idleCycle >= 0) {
            if (++idleCycle >= 720) { thisPtr->mAnimator->startAnim(altAnim); idleCycle = -1; }
        } else if (thisPtr->mAnimator->isAnimEnd()) { thisPtr->mAnimator->startAnim("Wait"); idleCycle = 0; alt = !alt; }
    }

    // Intercepts startAnim, remaps to suit-specific names
    struct PlayerAnimatorStartAnimHook : public mallow::hook::Trampoline<PlayerAnimatorStartAnimHook> {
        static void Callback(PlayerAnimator* thisPtr, const sead::SafeString& animName) {
            const char* swapped = remapAnim(animName.cstr(), thisPtr);
            Orig(thisPtr, swapped ? swapped : animName.cstr());
        }
    };

    // Makes isAnim match remapped names; "Wait" reads false while remap or the idle cycle owns it, so the engine never triggers WaitRelaxStart
    struct PlayerAnimatorIsAnimHook : public mallow::hook::Trampoline<PlayerAnimatorIsAnimHook> {
        static bool Callback(PlayerAnimator* thisPtr, const sead::SafeString& animName) {
            const char* swapped = remapAnim(animName.cstr(), thisPtr);
            if (al::isEqualString(animName.cstr(), "Wait") && (swapped || idleCycleAnim(false, thisPtr))) return false;
            return Orig(thisPtr, swapped ? swapped : animName.cstr());
        }
    };

    //Swap for new 2d animation archive
    struct PlayerAnimation2DArchiveHook : public mallow::hook::Inline<PlayerAnimation2DArchiveHook> {
        static void Callback(exl::hook::InlineCtx* ctx) {
            const char* model = (const char*)ctx->X[20];
            static bool isNew2D = al::isExistFile("ObjectData/MarioNew2D.txt");

            if ((al::isEqualString(model, "Mario2D") && isNew2D)
                #ifdef ALLOW_POWERUPS
                    || al::isEqualString(model, "MarioFeather2D")
                    || al::isEqualString(model, "MarioColorFire2D")
                    || al::isEqualString(model, "MarioColorIce2D")
                    || al::isEqualString(model, "MarioTanooki2D")
                    || al::isEqualString(model, "MarioDrill2D")
                    || al::isEqualString(model, "MarioColorMetal2D")
                    || al::isEqualString(model, "MarioColorFly2D")
                    || al::isEqualString(model, "MarioColorBrawl2D")
                    || al::isEqualString(model, "MarioColorSuper2D")
                #endif
            ) ctx->X[25] = (u64)"PlayerAnimationNew2D";
        }
    };

    inline void Install() {
        #ifdef ALLOW_POWERUPS
            PlayerAnimatorStartAnimHook::InstallAtSymbol("_ZN14PlayerAnimator9startAnimERKN4sead14SafeStringBaseIcEE");
            PlayerAnimatorIsAnimHook::InstallAtSymbol("_ZNK14PlayerAnimator6isAnimERKN4sead14SafeStringBaseIcEE");
        #endif
        PlayerAnimation2DArchiveHook::InstallAtOffset(0x445664);
    }
}