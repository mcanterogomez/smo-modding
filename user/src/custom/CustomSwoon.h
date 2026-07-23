#pragma once
#include "custom/.Globals.h"

// Handle stacked enemies
inline bool handleStacked(al::LiveActor*& actor, al::HitSensor* target, al::HitSensor* source) {
	if (!actor->getNerveKeeper()) return false;
    if (isType(actor, "BreedaWanwan")) {
        static bool(*tryBlowCap)(al::LiveActor*, al::HitSensor*) = nullptr;
        if (!tryBlowCap) nn::ro::LookupSymbol(reinterpret_cast<uintptr_t*>(&tryBlowCap), "_ZN12BreedaWanwan10tryBlowCapEPN2al9HitSensorE");
        while (tryBlowCap(actor, source)) {}
        return true;
    }
	if (isType(actor, "KuriboHack")) {
		rs::sendMsgYoshiTongueEatBind(target, source, nullptr, nullptr, nullptr);
		al::setNerve(actor, getNerveAt(0x1C9D888));
		return true;
	}
	if (isType(actor, "StackerCap")) {
        if (!al::isNerve(actor, getNerveAt(0x1C7B7F0)) && !al::isNerve(actor, getNerveAt(0x1C7B7F8))) return false; // OnHead, OnHeadAttack
        al::LiveActor* host = *reinterpret_cast<al::LiveActor**>((char*)actor + 0x108);
        int count = host && al::isAlive(host) ? *reinterpret_cast<int*>((char*)host + 0x154) : 0;
        if (count <= 0) return false;
        actor = (*reinterpret_cast<al::LiveActor***>(*reinterpret_cast<char**>((char*)host + 0x108) + 0x18))[count - 1]; // topCap
        static bool(*blowCapOnHead)(al::LiveActor*, const sead::Vector3f&, const sead::Vector3f&) = nullptr;
        if (!blowCapOnHead) nn::ro::LookupSymbol(reinterpret_cast<uintptr_t*>(&blowCapOnHead), "_ZN7Stacker13blowCapOnHeadERKN4sead7Vector3IfEES4_");
        blowCapOnHead(host, al::getSensorPos(source), sead::Vector3f::zero);
        return true;
    }
	return false;
}

// Send Cap messages to all sensors on the actor
inline bool trySendCapMsg(const al::LiveActor* actor, al::HitSensor* source) {
	al::HitSensorKeeper* keeper = actor->getHitSensorKeeper();
	if (!keeper) return false;
	for (s32 i = 0; i < keeper->getSensorNum(); i++) {
		al::HitSensor* s = keeper->getSensor(i);
		if (rs::sendMsgCapAttack(s, source) || rs::sendMsgCapReflect(s, source)) return true;
	}
	return false;
}

// Set the swoon nerve for the actor
inline bool trySwoon(al::LiveActor* actor, bool isSwoon = true) {
	auto isExactType = [&](const char* name) {
		char buf[64];
		snprintf(buf, sizeof(buf), "%zu%s", strlen(name), name);
		return al::isEqualString(typeid(*actor).name(), buf);
	};

	if (isExactType("FireBros") || isExactType("HammerBros")) { if (isSwoon) al::setNerve(*reinterpret_cast<al::IUseNerve**>((char*)actor + 0x130), getNerveAt(0x1C85DD0)); return true; }
	if (isExactType("Imomu")) { if (isSwoon) al::setNerve(*reinterpret_cast<al::IUseNerve**>((char*)actor + 0x118), getNerveAt(0x1C94EB0)); return true; }
	if (isExactType("Senobi")) { if (isSwoon) al::setNerve(*reinterpret_cast<al::IUseNerve**>((char*)actor + 0x128), getNerveAt(0x1CA83A8)); return true; }
	if (isExactType("BreedaWanwan")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1C621D0)); return true; }
	if (isExactType("Bull")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1C87D18)); return true; }
	if (isExactType("Byugo")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1C887B0)); return true; }
	if (isExactType("Frog")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1D58028)); return true; }
	if (isExactType("Gamane")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1C8ED38)); return true; }
	if (isExactType("Hosui")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1C938E0)); return true; }
	if (isExactType("Kakku")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1C99D68)); return true; }
	if (isExactType("KaronWing")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1C9A920)); return true; }
	if (isExactType("Killer")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1C9B248)); return true; }
	if (isExactType("KuriboHack")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1C9D888)); return true; }
	if (isExactType("KuriboWing")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1C9F298)); return true; }
	if (isExactType("Megane")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1CA0D68)); return true; }
	if (isExactType("PackunFire")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1CA35F0)); return true; }
	if (isExactType("Pukupuku")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1CA6028)); return true; }
	if (isExactType("Tank")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1CA9430)); return true; }
	if (isExactType("Tsukkun")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1CAE360)); return true; }
	if (isExactType("Wanwan")) { if (isSwoon) al::setNerve(actor, getNerveAt(0x1CAFD68)); return true; }
	return false;
}