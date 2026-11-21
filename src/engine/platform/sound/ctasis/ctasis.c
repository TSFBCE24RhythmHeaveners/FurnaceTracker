//
// Created by Administrator on 2026/11/11.
//

#include "ctasis.h"

void TASIS_CLOCK(struct TASIS_CHANNEL* tasisChannel, struct TASIS_CONFIG* tasisConfig,struct TASIS_WAVETABLE* tasisWaveTable) {
    tasisChannel->TASIS_BUFFER_OUT[0] = 0;
    tasisChannel->TASIS_BUFFER_OUT[1] = 0;
}

void TASIS_WAVETABLE_RESET(struct TASIS_WAVETABLE* tasis_wavetable, struct TASIS_CONFIG* tasis_config) {
    for (int i = 0; i < TASIS_WAVE_POINTS; ++i) {
        tasis_wavetable->SINE[i] = 0;
        tasis_wavetable->TRI[i] = 0;
    }
}