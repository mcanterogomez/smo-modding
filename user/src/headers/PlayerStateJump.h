#pragma once

#include "Library/Nerve/NerveStateBase.h"
#include "math/seadVector.h"

namespace al {
class ActorStateBase;
class LiveActor;
}

class PlayerConst;
class IPlayerModelChanger;
class IUseDimension;
class PlayerInput;
struct PlayerJumpMessageRequest;
class IJudge;
class PlayerCounterForceRun;
class PlayerJointControlKeeper;
class IUsePlayerCollision;
class PlayerTrigger;
class PlayerAnimator;
class PlayerActionDiveInWater;
class PlayerActionAirMoveControl;
class HackCap;

class PlayerContinuousJump {
public:
    void clear();
};

class PlayerStateJump : public al::ActorStateBase {
public:
    PlayerStateJump(al::LiveActor*                   actor,
                    const PlayerConst*               pConst,
                    const IPlayerModelChanger*       modelChanger,
                    const IUseDimension*             dimension,
                    const PlayerInput*               input,
                    const PlayerJumpMessageRequest*  jumpRequest,
                    const IJudge*                    judgeCapCatch,
                    const PlayerCounterForceRun*     counterForceRun,
                    const PlayerJointControlKeeper*  jointControlKeeper,
                    const IUsePlayerCollision*       collision,
                    PlayerTrigger*                   trigger,
                    PlayerContinuousJump*            continuousJump,
                    PlayerAnimator*                  animator,
                    PlayerActionDiveInWater*         actionDiveInWater,
                    HackCap*                         hackCap,
                    IJudge*                          judgeHackCapHoldHoveringJump,
                    bool                             isMoon);
    ~PlayerStateJump() override;

    void exeJump();

public:
    const PlayerConst*                mConst;
    const IPlayerModelChanger*        mModelChanger;
    const IUseDimension*              mDimension;
    const PlayerJumpMessageRequest*   mJumpMessageRequest;
    const IJudge*                     mJudgeCapCatch;
    const PlayerCounterForceRun*      mCounterForceRun;
    const PlayerJointControlKeeper*   mJointControlKeeper;
    const IUsePlayerCollision*        mCollision;
    PlayerAnimator*                   mAnimator;
    PlayerContinuousJump*             mContinuousJump;
    PlayerTrigger*                    mTrigger;
    PlayerActionDiveInWater*          mActionDiveInWater;
    PlayerActionAirMoveControl*       mActionAirMoveControl = nullptr;
    HackCap*                          mHackCap;
    IJudge*                           mJudgeHackCapHoldHoveringJump;
    bool                              isMoon = false;
    bool                              pad0[3] = { false, false, false };
    int                               mExtendFrame = 0;
    float                             mJumpPower = 0.0f;
    float                             mMoveSpeedMax = 0.0f;
    float                             mJumpGravity = 0.0f;
    int                               mSpinFlowerJumpStayCounter = 0;
    int                               mContinuousJumpCount = 0;
    bool                              _B4 = false;
    bool                              _B5 = false;
    bool                              _B6 = false;
    bool                              _B7 = false;
    bool                              _B8 = false;
    bool                              _B9 = false;
    bool                              mIsHoldCapSeparateJump = false;
    bool                              pad1 = false;
    sead::Vector3f                    _BC = {0.0f, 0.0f, 0.0f};
    const char*                       _C8 = nullptr;
    const char*                       mJumpAnimName = nullptr;
    const PlayerInput*                mInput = nullptr;
    sead::Vector3f                    mTurnJumpAngle = {0.0f, 0.0f, 0.0f};
    int                               _EC = 0;
    bool                              _F0 = false;
    bool                              pad2[3] = { false, false, false };
    sead::Vector3f                    vec = {0.0f, 0.0f, 0.0f};
};