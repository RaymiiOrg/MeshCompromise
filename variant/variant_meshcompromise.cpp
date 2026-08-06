#include "configuration.h"

#ifdef M5STACK_CARDPUTER_ADV

#define lateInitVariant upstreamLateInitVariant
#include "platform/extra_variants/m5stack_cardputer_adv/variant.cpp"
#undef lateInitVariant

#include "meshcompromise/bridge_thread.h"

void lateInitVariant()
{
    upstreamLateInitVariant();
    meshcompromise::setupBridge();
}

#endif
