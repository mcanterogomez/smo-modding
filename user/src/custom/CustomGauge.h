#pragma once
#include "Library/Layout/LayoutInitInfo.h"
#include "Layout/GaugeAir.h"

class CustomGauge {
public:
    static constexpr float DRAIN_RATE  = 0.001f;
    static constexpr int   EMPTY_DELAY = 120;

    CustomGauge(const al::LayoutInitInfo& info) {
        mGauge = new GaugeAir("GaugeAir", info);
    }

    // GaugeAir forwards
    void start()           { if (!mGauge->isAlive()) mGauge->start(); }
    void endMax()          { mGauge->endMax(); }
    bool isAlive() const   { return mGauge->isAlive(); }
    void setRate(float r) {
        mRate = sead::Mathf::clamp(r, 0.0f, 1.0f);
        mGauge->setRate(mRate);
    }

    // Custom logic
    float getRate() const  { return mRate; }
    bool  isEmpty() const  { return mRate <= 0.0f; }
    void  drain()          { setRate(mRate - DRAIN_RATE); }
    void  refill()         { setRate(1.0f); mTimer = -1; }

    void startTimer()      { if (mTimer < 0) mTimer = 0; }
    bool canUse() const    { return mTimer < 0; }
    bool tickTimer()       { return (mTimer >= 0) ? (++mTimer == EMPTY_DELAY) : false; }

private:
    GaugeAir* mGauge = nullptr;
    float mRate = 1.0f;
    int mTimer = -1;
};