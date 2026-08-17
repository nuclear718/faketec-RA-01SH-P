#include "TestUtil.h"
#include "platform/nrf52/Nrf52BatterySense.h"
#include <cstdlib>
#include <unity.h>

using meshtastic::nrf52battery::isPlausibleBatteryVoltage;
using meshtastic::nrf52battery::rawToMillivolts;
using meshtastic::nrf52battery::Reading;
using meshtastic::nrf52battery::selectReadingSource;
using meshtastic::nrf52battery::selectSource;
using meshtastic::nrf52battery::Source;
using meshtastic::nrf52battery::SourceLatch;
using meshtastic::nrf52battery::VoltageFilter;

static void test_probe_threshold_and_source_priority()
{
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Source::VDDH_DIV5), static_cast<uint8_t>(selectSource(14, true)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Source::EXTERNAL_DIVIDER),
                            static_cast<uint8_t>(selectSource(15, true)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Source::EXTERNAL_DIVIDER),
                            static_cast<uint8_t>(selectSource(15, false)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Source::UNKNOWN), static_cast<uint8_t>(selectSource(14, false)));
}

static void test_detection_is_latched()
{
    SourceLatch externalLatch;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Source::EXTERNAL_DIVIDER),
                            static_cast<uint8_t>(externalLatch.resolve(15, false)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Source::EXTERNAL_DIVIDER),
                            static_cast<uint8_t>(externalLatch.resolve(0, true)));
    TEST_ASSERT_TRUE(externalLatch.isResolved());

    SourceLatch vddhLatch;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Source::VDDH_DIV5), static_cast<uint8_t>(vddhLatch.resolve(0, true)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Source::VDDH_DIV5),
                            static_cast<uint8_t>(vddhLatch.resolve(15, false)));

    SourceLatch unknownLatch;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Source::UNKNOWN),
                            static_cast<uint8_t>(unknownLatch.resolve(0, false)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Source::UNKNOWN),
                            static_cast<uint8_t>(unknownLatch.resolve(15, true)));
}

static void test_nominal_voltage_conversions()
{
    const uint32_t rawValues[] = {2253, 2526, 2867};
    const uint16_t expectedMillivolts[] = {3300, 3700, 4200};

    for (uint8_t i = 0; i < 3; ++i) {
        TEST_ASSERT_UINT16_WITHIN(1, expectedMillivolts[i],
                                  rawToMillivolts(Source::EXTERNAL_DIVIDER, rawValues[i], 2.0f));
        TEST_ASSERT_UINT16_WITHIN(1, expectedMillivolts[i], rawToMillivolts(Source::VDDH_DIV5, rawValues[i], 9.0f));
    }
}

static void test_override_only_affects_external_divider()
{
    TEST_ASSERT_UINT16_WITHIN(1, 3201, rawToMillivolts(Source::EXTERNAL_DIVIDER, 2526, 1.73f));
    TEST_ASSERT_UINT16_WITHIN(1, 3700, rawToMillivolts(Source::VDDH_DIV5, 2526, 1.73f));
}

static void test_valid_voltage_boundaries()
{
    TEST_ASSERT_FALSE(isPlausibleBatteryVoltage(2499));
    TEST_ASSERT_TRUE(isPlausibleBatteryVoltage(2500));
    TEST_ASSERT_TRUE(isPlausibleBatteryVoltage(4500));
    TEST_ASSERT_FALSE(isPlausibleBatteryVoltage(4501));
}

static void test_usb_and_fallback_policy()
{
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Source::EXTERNAL_DIVIDER),
        static_cast<uint8_t>(selectReadingSource(Source::EXTERNAL_DIVIDER, true, true, true, true)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Source::VDDH_DIV5),
        static_cast<uint8_t>(selectReadingSource(Source::EXTERNAL_DIVIDER, false, true, false, true)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Source::UNKNOWN),
        static_cast<uint8_t>(selectReadingSource(Source::EXTERNAL_DIVIDER, false, false, false, true)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Source::UNKNOWN),
        static_cast<uint8_t>(selectReadingSource(Source::EXTERNAL_DIVIDER, false, true, true, true)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(Source::UNKNOWN),
        static_cast<uint8_t>(selectReadingSource(Source::EXTERNAL_DIVIDER, false, true, false, false)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Source::UNKNOWN),
                            static_cast<uint8_t>(selectReadingSource(Source::VDDH_DIV5, false, true, true, true)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Source::UNKNOWN),
                            static_cast<uint8_t>(selectReadingSource(Source::VDDH_DIV5, false, true, false, false)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Source::VDDH_DIV5),
                            static_cast<uint8_t>(selectReadingSource(Source::VDDH_DIV5, false, true, false, true)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Source::UNKNOWN),
                            static_cast<uint8_t>(selectReadingSource(Source::UNKNOWN, true, true, false, true)));
}

static void test_filter_rejects_invalid_samples()
{
    VoltageFilter filter;
    TEST_ASSERT_EQUAL_UINT16(0, filter.currentMillivolts());
    TEST_ASSERT_EQUAL_UINT16(0, filter.update({}));
    TEST_ASSERT_EQUAL_UINT16(0, filter.filteredMillivolts());

    TEST_ASSERT_EQUAL_UINT16(3700, filter.update(Reading{3700, Source::EXTERNAL_DIVIDER}));
    TEST_ASSERT_EQUAL_UINT16(3900, filter.update(Reading{4100, Source::EXTERNAL_DIVIDER}));
    TEST_ASSERT_EQUAL_UINT16(0, filter.update(Reading{2499, Source::EXTERNAL_DIVIDER}));
    TEST_ASSERT_EQUAL_UINT16(0, filter.update(Reading{4501, Source::VDDH_DIV5}));
    TEST_ASSERT_EQUAL_UINT16(0, filter.update({}));
    TEST_ASSERT_FALSE(filter.isValid());
    TEST_ASSERT_EQUAL_UINT16(0, filter.currentMillivolts());
    TEST_ASSERT_EQUAL_UINT16(3900, filter.filteredMillivolts());
}

static void test_hardware_policy_constants()
{
    TEST_ASSERT_EQUAL_UINT8(15, meshtastic::nrf52battery::SAMPLE_COUNT);
    TEST_ASSERT_EQUAL_UINT32(10, meshtastic::nrf52battery::DIVIDER_PROBE_PULLDOWN_MS);
    TEST_ASSERT_EQUAL_UINT32(250, meshtastic::nrf52battery::DIVIDER_RECOVERY_MS);
}

void setUp(void) {}
void tearDown(void) {}

extern "C" {
void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_probe_threshold_and_source_priority);
    RUN_TEST(test_detection_is_latched);
    RUN_TEST(test_nominal_voltage_conversions);
    RUN_TEST(test_override_only_affects_external_divider);
    RUN_TEST(test_valid_voltage_boundaries);
    RUN_TEST(test_usb_and_fallback_policy);
    RUN_TEST(test_filter_rejects_invalid_samples);
    RUN_TEST(test_hardware_policy_constants);
    exit(UNITY_END());
}

void loop() {}
}
