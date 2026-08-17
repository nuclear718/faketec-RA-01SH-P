#pragma once

#include <cstdint>

namespace meshtastic::nrf52battery
{

enum class Source : uint8_t { UNKNOWN, EXTERNAL_DIVIDER, VDDH_DIV5 };

constexpr uint8_t SAMPLE_COUNT = 15;
constexpr uint16_t DIVIDER_PROBE_THRESHOLD_MV = 15;
constexpr uint16_t MIN_VALID_BATTERY_MV = 2500;
constexpr uint16_t MAX_VALID_BATTERY_MV = 4500;
constexpr uint16_t EXTERNAL_ADC_FULL_SCALE_MV = 3000;
constexpr uint16_t VDDH_ADC_FULL_SCALE_MV = 1200;
constexpr uint16_t ADC_CODE_COUNT = 4096;
constexpr uint32_t DIVIDER_PROBE_PULLDOWN_MS = 10;
constexpr uint32_t DIVIDER_RECOVERY_MS = 250;

constexpr bool dividerDetected(uint16_t probeMillivolts)
{
    return probeMillivolts >= DIVIDER_PROBE_THRESHOLD_MV;
}

constexpr bool isPlausibleBatteryVoltage(uint16_t millivolts)
{
    return millivolts >= MIN_VALID_BATTERY_MV && millivolts <= MAX_VALID_BATTERY_MV;
}

constexpr Source selectSource(uint16_t probeMillivolts, bool highVoltageMode)
{
    if (dividerDetected(probeMillivolts)) {
        return Source::EXTERNAL_DIVIDER;
    }
    return highVoltageMode ? Source::VDDH_DIV5 : Source::UNKNOWN;
}

class SourceLatch
{
  public:
    Source resolve(uint16_t probeMillivolts, bool highVoltageMode)
    {
        if (!resolved) {
            selected = selectSource(probeMillivolts, highVoltageMode);
            resolved = true;
        }
        return selected;
    }

    bool isResolved() const { return resolved; }
    Source source() const { return selected; }

  private:
    bool resolved = false;
    Source selected = Source::UNKNOWN;
};

inline uint16_t rawToMillivolts(Source source, uint32_t raw, float externalMultiplier = 2.0f)
{
    float scale = 0.0f;
    switch (source) {
    case Source::EXTERNAL_DIVIDER:
        scale = EXTERNAL_ADC_FULL_SCALE_MV * externalMultiplier;
        break;
    case Source::VDDH_DIV5:
        scale = VDDH_ADC_FULL_SCALE_MV * 5.0f;
        break;
    default:
        return 0;
    }

    const float millivolts = raw * scale / ADC_CODE_COUNT;
    if (millivolts <= 0.0f || millivolts > UINT16_MAX) {
        return 0;
    }
    return static_cast<uint16_t>(millivolts + 0.5f);
}

constexpr Source selectReadingSource(Source selected, bool externalValid, bool vddhValid, bool usbConnected,
                                     bool highVoltageMode)
{
    if (selected == Source::EXTERNAL_DIVIDER && externalValid) {
        return Source::EXTERNAL_DIVIDER;
    }
    if ((selected == Source::EXTERNAL_DIVIDER || selected == Source::VDDH_DIV5) && !usbConnected && highVoltageMode &&
        vddhValid) {
        return Source::VDDH_DIV5;
    }
    return Source::UNKNOWN;
}

struct Reading {
    uint16_t millivolts = 0;
    Source source = Source::UNKNOWN;

    constexpr bool isValid() const { return source != Source::UNKNOWN && isPlausibleBatteryVoltage(millivolts); }
};

class VoltageFilter
{
  public:
    uint16_t update(Reading reading)
    {
        currentValid = reading.isValid();
        if (!currentValid) {
            return 0;
        }

        if (!hasFilteredValue) {
            filteredValue = reading.millivolts;
            hasFilteredValue = true;
        } else {
            filteredValue += (reading.millivolts - filteredValue) * 0.5f;
        }
        filteredSource = reading.source;
        return currentMillivolts();
    }

    void invalidate() { currentValid = false; }
    bool isValid() const { return currentValid; }
    Source lastValidSource() const { return hasFilteredValue ? filteredSource : Source::UNKNOWN; }
    uint16_t currentMillivolts() const { return currentValid ? filteredMillivolts() : 0; }
    uint16_t filteredMillivolts() const { return hasFilteredValue ? static_cast<uint16_t>(filteredValue + 0.5f) : 0; }

  private:
    bool currentValid = false;
    bool hasFilteredValue = false;
    float filteredValue = 0.0f;
    Source filteredSource = Source::UNKNOWN;
};

#if defined(ARCH_NRF52) && defined(HAS_NRF52_DUAL_BATTERY_SENSE)
class Nrf52BatterySense
{
  public:
    Reading read(float externalMultiplier);
    Source selectedSource() const { return sourceLatch.source(); }

  private:
    Source detectSource();
    Reading readExternal(float externalMultiplier);
    Reading readVddh();

    SourceLatch sourceLatch;
};
#endif

} // namespace meshtastic::nrf52battery
