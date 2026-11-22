//
// Created by Administrator on 2026/11/11.
//

#include "ctasis.h"

#include <math.h>

const double TASIS_PI = 3.14159265358979323846;

void TASIS_CLOCK(struct TASIS_CHANNEL* tasisChannel, struct TASIS_CONFIG* tasisConfig,struct TASIS_WAVETABLE* tasisWaveTable) {
    tasisChannel->TASIS_BUFFER_OUT[0] = 0;
    tasisChannel->TASIS_BUFFER_OUT[1] = 0;
    for (int opid = 0; opid < OPER_NUMBER; ++opid) {
        for (int pitchid = 0; pitchid < OPER_PHASE_PITCHS; ++pitchid) {
            tasisChannel->OP_PHASE[opid][pitchid].VIEW += tasisChannel->OP_PITCH[opid][pitchid].VIEW;

            tasisChannel->TASIS_BUFFER_OUT[0] += (int16_t)(tasisWaveTable->TRI[tasisChannel->OP_PHASE[opid][pitchid].PAIR.H_REG]);
            tasisChannel->TASIS_BUFFER_OUT[1] += (int16_t)(tasisWaveTable->TRI[tasisChannel->OP_PHASE[opid][pitchid].PAIR.H_REG]);
        }
    }
}

void TASIS_WAVETABLE_RESET(struct TASIS_WAVETABLE* tasis_wavetable, struct TASIS_CONFIG* tasis_config) {
    for (int i = 0; i < TASIS_WAVE_POINTS; ++i) {
        tasis_wavetable->SINE[i] = (TASIS_REG)(sin(i*2*TASIS_PI/65535.0)*32767.0+32768.0);
        tasis_wavetable->TRI[i] = (TASIS_REG)(asin(sin(i*2*TASIS_PI/65535.0))/TASIS_PI*2*32767.0);
    }
}