#include "configuration.h"
#include "Nrf52BatterySense.h"

#if defined(ARCH_NRF52) && defined(HAS_NRF52_DUAL_BATTERY_SENSE)

#include "DebugConfiguration.h"
#include "Nrf52SaadcLock.h"
#include "concurrency/LockGuard.h"
#include "power/PowerHAL.h"
#include <nrf.h>
#include <nrf52_erratas.h>
#include <nrf_power.h>
#include <wiring_analog.h>

namespace meshtastic::nrf52battery
{

namespace
{

void configureExternalAdc()
{
    analogReadResolution(BATTERY_SENSE_RESOLUTION_BITS);
#ifdef VBAT_AR_INTERNAL
    analogReference(VBAT_AR_INTERNAL);
#else
    analogReference(AR_INTERNAL);
#endif
}

uint32_t averageExternalRaw()
{
    uint32_t raw = 0;
    for (uint8_t i = 0; i < SAMPLE_COUNT; ++i) {
        raw += analogRead(BATTERY_PIN);
    }
    return raw / SAMPLE_COUNT;
}

bool isHighVoltageMode()
{
#if NRF_POWER_HAS_MAINREGSTATUS
    return nrf_power_mainregstatus_get(NRF_POWER) == NRF_POWER_MAINREGSTATUS_HIGH;
#else
    return false;
#endif
}

bool canReadVddh()
{
#ifdef SAADC_CH_PSELP_PSELP_VDDHDIV5
    return !nrf52_errata_160() && isHighVoltageMode() && !powerHAL_isVBUSConnected();
#else
    return false;
#endif
}

const char *sourceName(Source source)
{
    switch (source) {
    case Source::EXTERNAL_DIVIDER:
        return "P0.31 divider";
    case Source::VDDH_DIV5:
        return "VDDH/5";
    default:
        return "unknown";
    }
}

} // namespace

Source Nrf52BatterySense::detectSource()
{
    pinMode(BATTERY_PIN, INPUT_PULLDOWN);
    delay(DIVIDER_PROBE_PULLDOWN_MS);

    uint32_t raw = 0;
    {
        concurrency::LockGuard guard(concurrency::nrf52SaadcLock);
        configureExternalAdc();
        raw = averageExternalRaw();
    }

    pinMode(BATTERY_PIN, INPUT);
    delay(DIVIDER_RECOVERY_MS);

    const uint16_t probeMillivolts = rawToMillivolts(Source::EXTERNAL_DIVIDER, raw, 1.0f);
    const bool vddhAvailable = !nrf52_errata_160() && isHighVoltageMode();
    const Source source = sourceLatch.resolve(probeMillivolts, vddhAvailable);
    LOG_INFO("Battery sense: %s selected (P0.31 pulldown probe %u mV)", sourceName(source), probeMillivolts);
    return source;
}

Reading Nrf52BatterySense::readExternal(float externalMultiplier)
{
    uint32_t raw = 0;
    {
        concurrency::LockGuard guard(concurrency::nrf52SaadcLock);
        configureExternalAdc();
        raw = averageExternalRaw();
    }

    const uint16_t millivolts = rawToMillivolts(Source::EXTERNAL_DIVIDER, raw, externalMultiplier);
    return isPlausibleBatteryVoltage(millivolts) ? Reading{millivolts, Source::EXTERNAL_DIVIDER} : Reading{};
}

Reading Nrf52BatterySense::readVddh()
{
    if (!canReadVddh()) {
        return {};
    }

    uint32_t raw = 0;
    {
        concurrency::LockGuard guard(concurrency::nrf52SaadcLock);
        analogReadResolution(BATTERY_SENSE_RESOLUTION_BITS);
        analogReference(AR_INTERNAL_1_2);
        for (uint8_t i = 0; i < SAMPLE_COUNT; ++i) {
            raw += analogReadVDDHDIV5();
        }
#ifdef VBAT_AR_INTERNAL
        analogReference(VBAT_AR_INTERNAL);
#else
        analogReference(AR_INTERNAL);
#endif
    }

    if (powerHAL_isVBUSConnected() || !isHighVoltageMode()) {
        return {};
    }

    raw /= SAMPLE_COUNT;
    const uint16_t millivolts = rawToMillivolts(Source::VDDH_DIV5, raw);
    return isPlausibleBatteryVoltage(millivolts) ? Reading{millivolts, Source::VDDH_DIV5} : Reading{};
}

Reading Nrf52BatterySense::read(float externalMultiplier)
{
    const Source selected = sourceLatch.isResolved() ? sourceLatch.source() : detectSource();
    if (selected == Source::EXTERNAL_DIVIDER) {
        const Reading external = readExternal(externalMultiplier);
        if (external.isValid()) {
            return external;
        }

        const Reading vddh = readVddh();
        if (selectReadingSource(selected, false, vddh.isValid(), powerHAL_isVBUSConnected(), isHighVoltageMode()) ==
            Source::VDDH_DIV5) {
            return vddh;
        }
        return {};
    }

    if (selected == Source::VDDH_DIV5) {
        return readVddh();
    }
    return {};
}

} // namespace meshtastic::nrf52battery

#endif
