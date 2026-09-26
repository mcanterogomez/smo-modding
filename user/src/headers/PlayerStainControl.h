#pragma once
#include <basis/seadTypes.h>

class PlayerStainControl {
public:
    // This wipes Mario clean (useful for ending the taunt)
    void clearStain();

    // These set the "Dirt Level" to max and apply the texture immediately
    void recordDamageFire(); // Applies the Soot/Burn texture
    void recordIceWater(); // Applies the Ice/Snow texture
    void recordPoison(); // Applies the Poison texture
    void recordBlackSmoke(); // Likely a lighter soot variant
};