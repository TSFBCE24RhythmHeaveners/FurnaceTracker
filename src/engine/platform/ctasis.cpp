#include "ctasis.h"
#include "sound/cse1/utils.h"
#include "../engine.h"

#define CHIP_FREQBASE (1 << 27)
#define MEMORY_SIZE (16 * 1024 * 1024)

DivPlatformTasis::DivPlatformTasis() = default;
DivPlatformTasis::~DivPlatformTasis() = default;

// ---------- 输出 ----------

void DivPlatformTasis::acquire(short** buf, size_t len) {
  if (chans < 1) return;

  for (int i = 0; i < chans; i++) oscBuf[i]->begin(len);

  for (size_t i = 0; i < len; i++) {
    newVolume[0] = newVolume[1] = 0;
    for (int j = 0; j < enable_chans; j++) {
      TASIS_CLOCK(&chip_base[j], &config, waveTable);
      const auto l = bit_cast<int32_t>(chip_base[j].TASIS_BUFFER_OUT[0]);
      const auto r = bit_cast<int32_t>(chip_base[j].TASIS_BUFFER_OUT[1]);
      if (!isMuted[j]) {
        oscBuf[j]->putSample(i, (l + r) >> 1);
        newVolume[0] += l;
        newVolume[1] += r;
      } else {
        oscBuf[j]->putSample(i, 0);
      }
    }
    buf[0][i] = MAX(MIN(newVolume[0], 32767), -32768);
    buf[1][i] = MAX(MIN(newVolume[1], 32767), -32768);
    oldVolume[0] = newVolume[0];
    oldVolume[1] = newVolume[1];
  }

  for (int i = 0; i < chans; i++) oscBuf[i]->end(len);
}

void DivPlatformTasis::acquireDirect(blip_buffer_t** bb, size_t len) {
  if (chans < 1 || oscBuf[0] == nullptr) return;

  unsigned int sharedNeedlePos = oscBuf[0]->needle;
  for (int i = 0; i < chans; i++) oscBuf[i]->begin(len);

  for (size_t h = 0; h < len; h++) {
    newVolume[0] = newVolume[1] = 0;
    const unsigned int shiftedNeedlePos = sharedNeedlePos >> OSCBUF_PREC;

    for (int c = 0; c < enable_chans; c++) {
      TASIS_CLOCK(&chip_base[c], &config, waveTable);
      const auto l = bit_cast<int32_t>(chip_base[c].TASIS_BUFFER_OUT[0])>>3;
      const auto r = bit_cast<int32_t>(chip_base[c].TASIS_BUFFER_OUT[1])>>3;
      putSampleIKnowWhatIAmDoing(oscBuf[c], shiftedNeedlePos, isMuted[c] ? 0 : ((l + r) >> 1));
      if (!isMuted[c]) {
        newVolume[0] += l;
        newVolume[1] += r;
      }
    }

    // 手动 DAC（输出级）
    blip_add_delta(bb[0], h, newVolume[0] - oldVolume[0]);
    blip_add_delta(bb[1], h, newVolume[1] - oldVolume[1]);
    sharedNeedlePos += oscBuf[0]->rateMul;
    oldVolume[0] = newVolume[0];
    oldVolume[1] = newVolume[1];
  }

  for (int i = 0; i < chans; i++) oscBuf[i]->end(len);
}

bool DivPlatformTasis::hasAcquireDirect() { return true; }
bool DivPlatformTasis::hasSoftPan(int ch) { return true; }
int DivPlatformTasis::getOutputCount() { return 2; }

void DivPlatformTasis::muteChannel(int ch, bool mute) {
  if (ch >= 0 && ch < TASIS_MAX_CHANNEL) isMuted[ch] = mute;
}

// ---------- 命令 ----------

void DivPlatformTasis::tick(bool sysTick) {
  for (int i = 0; i < chans; i++) {
    chan[i].std.next();

    if (chan[i].freqChanged || chan[i].keyOn || chan[i].keyOff) {
      chan[i].freqChanged = false;
      chan[i].freq = chan[i].calcFreq();
      if (chan[i].keyOn) {
        for (int j = 0; j < 3; ++j) {
          chip_base[i].OP_PHASE[0][j].VIEW = rand()*(1<<30);
        }
      }
      chan[i].keyOn = false;
      chan[i].keyOff = false;
      chip_base[i].OP_PITCH[0][0].VIEW = chan[i].freq;
      chip_base[i].OP_PITCH[0][1].VIEW = chan[i].freq*pow(2.0, 10/1200.0);
      chip_base[i].OP_PITCH[0][2].VIEW = chan[i].freq*pow(2.0, -10/1200.0);
      chip_base[i].OP_PITCH[0][3].VIEW = chan[i].freq*pow(2.0, 20/1200.0);
      chip_base[i].OP_PITCH[0][4].VIEW = chan[i].freq*pow(2.0, -20/1200.0);
      chip_base[i].OP_PITCH[0][5].VIEW = chan[i].freq*pow(2.0, 30/1200.0);
      chip_base[i].OP_PITCH[0][6].VIEW = chan[i].freq*pow(2.0, -30/1200.0);
      chip_base[i].OP_PITCH[0][7].VIEW = chan[i].freq*pow(2.0, 40/1200.0);
      chip_base[i].OP_PITCH[0][8].VIEW = chan[i].freq*pow(2.0, -40/1200.0);
    }
    if (NEW_ARP_STRAT) {
      chan[i].handleArp();
    } else if (chan[i].std.arp.had && !chan[i].rawFreq) {
      if (!chan[i].inPorta) {
        chan[i].baseFreq = chan[i].calcBaseFreq(
            parent->calcArp(chan[i].note, chan[i].std.arp.val));
      }
      chan[i].freqChanged = true;
    }
    if (chan[i].std.pitch.had) {
      if (chan[i].std.pitch.mode) {
        chan[i].pitch2 += chan[i].std.pitch.val;
        CLAMP_VAR(chan[i].pitch2, -32768, 32767);
      } else {
        chan[i].pitch2 = chan[i].std.pitch.val;
      }
      chan[i].freqChanged = true;
    }
  }
}

int DivPlatformTasis::dispatch(DivCommand c) {
  if (c.chan < 0 || c.chan >= chans) return 1;
  switch (c.cmd) {
    case DIV_CMD_NOTE_ON: {
      DivInstrument* ins = parent->getIns(chan[c.chan].ins, DIV_INS_OPL);
      chan[c.chan].active = true;
      chan[c.chan].keyOn = true;
      if (c.value != DIV_NOTE_NULL) {
        chan[c.chan].baseFreq = chan[c.chan].calcBaseFreq(c.value);
        chan[c.chan].freqChanged = true;
        chan[c.chan].note = c.value;
      }
      chan[c.chan].macroInit(ins);
      break;
    }
    case DIV_CMD_NOTE_OFF:
      chan[c.chan].active = false;
      chan[c.chan].keyOff = true;
      chan[c.chan].macroInit(nullptr);
      break;
    case DIV_CMD_NOTE_OFF_ENV:
    case DIV_CMD_ENV_RELEASE:
      chan[c.chan].std.release();
      break;
    case DIV_CMD_INSTRUMENT:
      chan[c.chan].ins = c.value;
      break;
    case DIV_CMD_VOLUME:
      chan[c.chan].vol = std::min<int>(c.value, 255);
      break;
    case DIV_CMD_GET_VOLUME:
      return chan[c.chan].vol;
    case DIV_CMD_PITCH:
      chan[c.chan].pitch = c.value;
      chan[c.chan].freqChanged = true;
      break;
    case DIV_CMD_NOTE_PORTA: {
      int destFreq = chan[c.chan].calcBaseFreq(c.value2);
      bool done = false;
      if (destFreq > chan[c.chan].baseFreq) {
        chan[c.chan].baseFreq += c.value;
        if (chan[c.chan].baseFreq >= destFreq) {
          chan[c.chan].baseFreq = destFreq;
          done = true;
        }
      } else {
        chan[c.chan].baseFreq -= c.value;
        if (chan[c.chan].baseFreq <= destFreq) {
          chan[c.chan].baseFreq = destFreq;
          done = true;
        }
      }
      chan[c.chan].freqChanged = true;
      if (done) return 2;
      break;
    }
    case DIV_CMD_LEGATO:
      chan[c.chan].baseFreq = chan[c.chan].calcBaseFreq(c.value);
      chan[c.chan].freqChanged = true;
      break;
    case DIV_CMD_GET_VOLMAX:
      return amigaVolumeLike ? 64 : 255;
    case DIV_CMD_MACRO_OFF:
      chan[c.chan].std.mask(c.value, true);
      break;
    case DIV_CMD_MACRO_ON:
      chan[c.chan].std.mask(c.value, false);
      break;
    case DIV_CMD_MACRO_RESTART:
      chan[c.chan].std.restart(c.value);
      break;
    default:
      break;
  }
  return 1;
}

// ---------- 查询 ----------

SharedChannel* DivPlatformTasis::getChanState(int ch) {
  if (ch < 0 || ch >= chans) return nullptr;
  return &chan[ch];
}
DivDispatchOscBuffer* DivPlatformTasis::getOscBuffer(int ch) {
  if (ch < 0 || ch >= chans) return nullptr;
  return oscBuf[ch];
}
DivMacroInt* DivPlatformTasis::getChanMacroInt(int ch) {
  if (ch < 0 || ch >= chans) return nullptr;
  return &chan[ch].std;
}
unsigned int DivPlatformTasis::getMaxFreq(int ch) { return 0xffffffff; }

void DivPlatformTasis::notifyInsDeletion(void* ins) {
  for (int i = 0; i < chans; i++)
    chan[i].std.notifyInsDeletion(static_cast<DivInstrument*>(ins));
}
void DivPlatformTasis::notifyInsChange(int ins) {
  for (int i = 0; i < chans; i++)
    if (chan[i].ins == ins) chan[i].insChanged = true;
}
void DivPlatformTasis::forceIns() {
  for (int i = 0; i < chans; i++) {
    chan[i].insChanged = true;
    chan[i].freqChanged = true;
    chan[i].sample = -1;
  }
}

// ---------- 采样内存 ----------

const void* DivPlatformTasis::getSampleMem(int index) {
  return index == 0 ? pcmMem : nullptr;
}
size_t DivPlatformTasis::getSampleMemCapacity(int index) {
  return index == 0 ? MEMORY_SIZE : 0;
}
size_t DivPlatformTasis::getSampleMemUsage(int index) {
  return index == 0 ? sampleMemLen : 0;
}
bool DivPlatformTasis::isSampleLoaded(int index, int sample) {
  if (index != 0) return false;
  if (sample < 0 || sample > 32767) return false;
  return sampleLoaded[sample];
}
const DivMemoryComposition* DivPlatformTasis::getMemCompo(int index) {
  if (index != 0) return nullptr;
  return &memCompo;
}

void DivPlatformTasis::renderSamples(int sysID) {
  memset(pcmMem, 0, MEMORY_SIZE * sizeof(TASIS_REG));
  memset(sampleOff, 0, 32768 * sizeof(uint32_t));
  memset(sampleLoaded, 0, 32768 * sizeof(bool));

  memCompo = DivMemoryComposition();
  memCompo.name = "Tasis RAM (1 Byte = 16 Bit)";

  size_t memPos = 0;
  for (int i = 0; i < parent->song.sampleLen; i++) {
    DivSample* s = parent->song.sample[i];
    if (!s->renderOn[0][sysID]) {
      sampleOff[i] = 0;
      continue;
    }
    const uint32_t lengthByte = s->getCurBufLen();
    const uint32_t lengthREG = lengthByte >> 1;
    const uint8_t* originData = static_cast<uint8_t*>(s->getCurBuf());
    const uint32_t length =
        s->depth == DIV_SAMPLE_DEPTH_16BIT ? lengthREG : lengthByte;
    const size_t maxSize = getSampleMemCapacity(0);
    if (memPos + length > maxSize) {
      sampleLoaded[i] = false;
      continue;
    }
    if (length > 0) {
      if (s->depth == DIV_SAMPLE_DEPTH_16BIT) {
        memcpy(&pcmMem[memPos], originData, lengthByte);
      } else {
        for (unsigned int si = 0; si < length; si++) {
          pcmMem[memPos + si] = (static_cast<uint16_t>(originData[si]) << 8) |
                                static_cast<uint16_t>(originData[si]);
        }
      }
      sampleOff[i] = memPos;
      memCompo.entries.push_back(
          DivMemoryEntry(DIV_MEMORY_SAMPLE, "Sample", i, memPos, memPos + length));
      memPos += length;
    }
    sampleLoaded[i] = true;
  }

  sysIDCache = sysID;
  sampleMemLen = memPos;
  memCompo.used = sampleMemLen;
  memCompo.capacity = getSampleMemCapacity(0);
}

// ---------- 生命周期 ----------

void DivPlatformTasis::reset() {
  TASIS_WAVETABLE_RESET(waveTable, &config);
  oldVolume[0] = oldVolume[1] = 0;
  newVolume[0] = newVolume[1] = 0;
  for (int i = 0; i < chans; i++) {
    chan[i] = Channel(parent->song.compatFlags.linearPitch);
    chan[i].pitchTable = &pitchTable;
    chan[i].vol = 255;
    chan[i].std.setEngine(parent);
  }
}

void DivPlatformTasis::notifyPitchTable(int sample) {
  pitchTable.init(parent->song.tuning, chipClock, CHIP_FREQBASE, 0x7fffffff,
                  false, parent->song.compatFlags.linearPitch);
  samplePitchTable.update<Channel>(
      chan, chans, parent->song.tuning, chipClock, 1 << 16, 0x7fffffff, false,
      parent->song.compatFlags.linearPitch, sample);
}

void DivPlatformTasis::setFlags(const DivConfig& flags) {
  chipClock = 192000;
  CHECK_CUSTOM_CLOCK
  chipClock = flags.getBool("quarterClock", false) ? chipClock / 4 : chipClock;
  rate = chipClock;

  for (int i = 0; i < chans; i++) oscBuf[i]->setRate(rate);

  amigaVolumeLike = flags.getBool("amiga_volume_like", false);

  // 逻辑启用数：<= chans，<= TASIS_MAX_CHANNEL
  int want = (flags.getInt("channels", 7) & 7) + 1;
  if (want > chans) want = chans;
  if (want > TASIS_MAX_CHANNEL) want = TASIS_MAX_CHANNEL;
  if (want < 1) want = 1;
  enable_chans = static_cast<uint8_t>(want);

  notifyPitchTable();
}

int DivPlatformTasis::init(DivEngine* p, int channels, int sugRate,
                           const DivConfig& flags) {
  parent = p;
  samplePitchTable.init(parent);
  waveTable = new TASIS_WAVETABLE;

  if (channels < 1) channels = 1;
  if (channels > TASIS_MAX_CHANNEL) channels = TASIS_MAX_CHANNEL;
  chans = static_cast<unsigned char>(channels);

  for (bool & i : isMuted) i = false;
  for (int i = 0; i < chans; i++) {
    oscBuf[i] = new DivDispatchOscBuffer;
    oscBuf[i]->setRate(192000);
  }

  rate = chipClock = 192000;

  sampleOff = new unsigned int[32768]();
  sampleLoaded = new bool[32768]();
  pcmMem = new TASIS_REG[getSampleMemCapacity(0)]();
  sampleMemLen = 0;
  config.RAM = pcmMem;

  notifyPitchTable();
  setFlags(flags);
  reset();

  oldVolume[0] = oldVolume[1] = 0;
  return chans;
}

void DivPlatformTasis::quit() {
  for (int i = 0; i < chans; i++) {
    delete oscBuf[i];
    oscBuf[i] = nullptr;
  }
  delete[] pcmMem;
  pcmMem = nullptr;
  config.RAM = nullptr;
  delete[] sampleOff;
  sampleOff = nullptr;
  delete[] sampleLoaded;
  sampleLoaded = nullptr;
  delete waveTable;
  samplePitchTable.destroy<Channel>(chan, chans);
}