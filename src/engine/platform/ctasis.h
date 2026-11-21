#ifndef FURNACE_CTASIS_H
#define FURNACE_CTASIS_H

#include "../dispatch.h"
#include "sound/ctasis/ctasis.h"

#define TASIS_MAX_CHANNEL 8

class DivPlatformTasis : public DivDispatch {
    struct Channel : public SharedChannel {
        int sample{};
        explicit Channel(bool linear = true) : SharedChannel(0, linear) {}
    };

    Channel chan[TASIS_MAX_CHANNEL];
    TASIS_CHANNEL chip_base[TASIS_MAX_CHANNEL]{};
    TASIS_WAVETABLE waveTable{};
    TASIS_CONFIG config{};
    TASIS_REG* pcmMem{};

    DivDispatchOscBuffer* oscBuf[TASIS_MAX_CHANNEL]{};   // 未分配为 nullptr
    DivPitchTable pitchTable{};
    DivPitchTableManager samplePitchTable{};
    bool isMuted[TASIS_MAX_CHANNEL]{};
    unsigned char chans{};          // init 分配数（0..8）
    uint8_t enable_chans{};         // 逻辑启用数（<= chans）
    DivMemoryComposition memCompo{};
    unsigned int* sampleOff{};
    bool* sampleLoaded{};
    size_t sampleMemLen{};
    int sysIDCache{};
    int oldVolume[2]{};
    int newVolume[2]{};
    bool amigaVolumeLike{};

public:
    DivPlatformTasis();
    ~DivPlatformTasis() override;

    void acquire(short** buf, size_t len) override;
    void acquireDirect(blip_buffer_t** bb, size_t len) override;
    bool hasAcquireDirect() override;
    bool hasSoftPan(int ch) override;
    int getOutputCount() override;
    void muteChannel(int ch, bool mute) override;
    int dispatch(DivCommand c) override;

    void notifyInsDeletion(void* ins) override;
    void notifyInsChange(int ins) override;
    void forceIns() override;

    void renderSamples(int sysID) override;
    void notifyPitchTable(int sample = -1) override;
    unsigned int getMaxFreq(int ch) override;
    SharedChannel* getChanState(int chan) override;
    DivDispatchOscBuffer* getOscBuffer(int chan) override;
    DivMacroInt* getChanMacroInt(int ch) override;

    void setFlags(const DivConfig& flags) override;

    const void* getSampleMem(int index) override;
    size_t getSampleMemCapacity(int index) override;
    size_t getSampleMemUsage(int index) override;
    bool isSampleLoaded(int index, int sample) override;
    const DivMemoryComposition* getMemCompo(int index) override;

    void reset() override;
    void tick(bool sysTick = true) override;
    int init(DivEngine* parent, int channels, int sugRate, const DivConfig& flags) override;
    void quit() override;
};

#endif // FURNACE_CTASIS_H