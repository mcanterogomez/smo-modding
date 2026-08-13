#pragma once
#include "Library/LiveActor/LiveActor.h"
#include "Player/IUsePlayerCollision.h"

class Motorcycle : public al::LiveActor, public IUsePlayerCollision {
public:
    Motorcycle(const char*);

    virtual void init(al::ActorInitInfo const &) override;
    virtual void initAfterPlacement() override;
    virtual void attackSensor(al::HitSensor *source, al::HitSensor *target) override;
    virtual bool receiveMsg(const al::SensorMsg *msg, al::HitSensor *sender, al::HitSensor *receiver) override;
    virtual void kill() override;
    virtual void movement() override;
    virtual void calcAnim() override;
    virtual void updateCollider() override;

    virtual PlayerCollider* getPlayerCollider() const override;

    void exeWait();
    void exeCreep();
    void exeJump();
    void endJump();
    void exeFall();
    void exeReset();
    void exeReaction();
    void exeRideWait();
    void exeRideWaitJump();
    void endRideWaitJump();
    void exeRideWaitLand();
    void exeRideStart();
    void exeRideStartOn();
    void exeRideParking();
    void exeRideParkingStart();
    void exeRideParkingSnap();
    void exeRideParkingAfter();
    void exeRideRun();
    void exeRideRunStart();
    void exeRideRunJump();
    void endRideRunJump();
    void exeRideRunLand();
    void exeRideRunFall();
    void exeRideRunBound();
    void exeRideRunBoundStart();
    void exeRideRunClash();
    void exeRideRunCollide();
    void exeRideRunWheelie();
    void endRideRunWheelie();

    char gap0[0x138 - sizeof(al::LiveActor) - sizeof(IUsePlayerCollision)];
    f32 mLean; // AllRoot Z-rotation input; al::initJointLocalZRotator(this, &mLean, "AllRoot") in Motorcycle::init, written each frame in exeRideRun
    f32 mSteer; // Handle X / FrontWheel Y / Cowl Y rotation input; normalizeAbs(mLean, 0, 55) * -32.5 at the end of Motorcycle::movement

private:
    char gap1[0x250 - 0x138 - sizeof(f32) * 2];
};

static_assert(sizeof(Motorcycle) == 0x250, "Motorcycle Size");