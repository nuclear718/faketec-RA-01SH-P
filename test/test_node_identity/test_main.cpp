#include "MeshTypes.h"
#include "TestUtil.h"
#include <unity.h>

#if defined(ARCH_PORTDUINO)
#define IDENTITY_TEST_ENTRY extern "C"
#else
#define IDENTITY_TEST_ENTRY
#endif

#if !(MESHTASTIC_EXCLUDE_PKI_KEYGEN || MESHTASTIC_EXCLUDE_PKI)
#include "FSCommon.h"
#include "mesh/CryptoEngine.h"
#include "mesh/NodeDB.h"
#include "mesh/RadioInterface.h"
#include "mesh/TypeConversions.h"
#include "meshUtils.h"
#include "modules/AdminModule.h"
#include <ErriezCRC32.h>
#include <cstring>
#if ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#include "xmodem.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unistd.h>
#endif

class NodeDBTestShim : public NodeDB
{
  public:
    void bootIdentity() { initializeNodeIdentity(); }
    void chooseProvisional() { pickNewNodeNum(); }
    bool pendingSave() const { return identitySavePending; }
    bool repairMarker() { return readSelfRepairMarker(); }
    bool persist() { return saveToDiskNoRetry(SEGMENT_CONFIG); }
    void loadSavedIdentity()
    {
        meshNodes->clear(); // Match a cold boot, not an in-place append into the current vector.
        numMeshNodes = 0;
        loadFromDisk();
        cleanupMeshDB();
    }
    void repairPreviouslyCompleted() { legacySelfRepairComplete = true; }
    void degradedBoot() { configDecodeFailed = true; }
    void clear()
    {
        for (size_t i = 0; i < numMeshNodes; ++i)
            eraseNodeSatellites(meshNodes->at(i).num);
        meshNodes->assign(MAX_NUM_NODES, meshtastic_NodeInfoLite_init_zero);
        numMeshNodes = 0;
        identitySavePending = false;
        legacySelfRepairPending = false;
        legacySelfRepairComplete = false;
        configDecodeFailed = false;
    }
};

class AdminModuleTestShim : public AdminModule
{
  public:
    AdminModuleTestShim() { hasOpenEditTransaction = true; }
    using AdminModule::handleSetConfig;
};

static NodeDBTestShim *db;
static NodeNum provisional;
#if ARCH_PORTDUINO
static char fixtureRoot[] = "/tmp/meshlink-identity-test-XXXXXX";
#endif

static meshtastic_NodeInfoLite *addOwner(NodeNum number)
{
    auto *node = db->getOrCreateMeshNode(number);
    TypeConversions::CopyUserToNodeInfoLite(node, owner);
    return node;
}

static NodeNum prepareKey()
{
    config.has_security = true;
    config.security.private_key.size = 32;
    for (unsigned i = 0; i < 32; ++i)
        config.security.private_key.bytes[i] = i + 17;
    TEST_ASSERT_TRUE(crypto->ensurePkiKeys(config.security, owner));
    return crc32Buffer(owner.public_key.bytes, 32);
}

static NodeNum seedExistingDuplicate()
{
    const NodeNum keyed = prepareKey();
    myNodeInfo.my_node_num = keyed;
    snprintf(owner.id, sizeof(owner.id), "!%08x", keyed);
    addOwner(keyed);
    return keyed;
}

static unsigned visibleCount()
{
    unsigned count = 0;
    for (size_t i = 0; i < db->getNumMeshNodes(); ++i) {
        auto wire = TypeConversions::ConvertToNodeInfoThin(&db->meshNodes->at(i));
        if (!wire.is_ignored)
            ++count;
    }
    return count;
}

void setUp()
{
    db->clear();
#if ARCH_PORTDUINO
    std::filesystem::remove_all(std::filesystem::path(fixtureRoot) / "prefs");
    std::filesystem::create_directory(std::filesystem::path(fixtureRoot) / "prefs");
#endif
    devicestate = meshtastic_DeviceState_init_zero;
    config = meshtastic_LocalConfig_init_zero;
    moduleConfig = meshtastic_LocalModuleConfig_init_zero;
    devicestate.version = DEVICESTATE_CUR_VER;
    config.version = DEVICESTATE_CUR_VER;
    devicestate.has_owner = true;
    devicestate.has_my_node = true;
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_TW;
    owner.hw_model = HW_VENDOR;
    owner.role = config.device.role;
    getMacAddr(owner.macaddr);
    provisional = (uint32_t(owner.macaddr[2]) << 24) | (uint32_t(owner.macaddr[3]) << 16) | (uint32_t(owner.macaddr[4]) << 8) |
                  owner.macaddr[5];
    myNodeInfo.my_node_num = provisional;
    snprintf(owner.id, sizeof(owner.id), "!%08x", provisional);
    snprintf(owner.long_name, sizeof(owner.long_name), "Meshtastic %04x", provisional & 0xffff);
    snprintf(owner.short_name, sizeof(owner.short_name), "%04x", provisional & 0xffff);
    addOwner(provisional);
}

void tearDown() {}

static void test_first_region_keeps_open_session_and_synchronizes_self()
{
    TEST_ASSERT_TRUE(db->ensureLocalPkiKeys());
    TEST_ASSERT_EQUAL_HEX32(provisional, db->getNodeNum());
    TEST_ASSERT_EQUAL_UINT(32, db->getMeshNode(provisional)->public_key.size);
    TEST_ASSERT_EQUAL_MEMORY(owner.public_key.bytes, db->getMeshNode(provisional)->public_key.bytes, 32);
    TEST_ASSERT_TRUE(db->pendingSave());
    TEST_ASSERT_EQUAL_UINT(1, visibleCount());
}

static void test_first_region_admin_dispatch_updates_self_without_renumbering()
{
    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    initRegion();
    meshtastic_Config incoming = meshtastic_Config_init_zero;
    incoming.which_payload_variant = meshtastic_Config_lora_tag;
    incoming.payload_variant.lora.region = meshtastic_Config_LoRaConfig_RegionCode_TW;
    incoming.payload_variant.lora.use_preset = true;
    incoming.payload_variant.lora.modem_preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    AdminModuleTestShim admin;
    admin.handleSetConfig(incoming, false);
    TEST_ASSERT_EQUAL(meshtastic_Config_LoRaConfig_RegionCode_TW, config.lora.region);
    TEST_ASSERT_EQUAL_HEX32(provisional, db->getNodeNum());
    TEST_ASSERT_EQUAL_UINT(32, db->getMeshNode(provisional)->public_key.size);
    TEST_ASSERT_EQUAL_MEMORY(owner.public_key.bytes, db->getMeshNode(provisional)->public_key.bytes, 32);
    TEST_ASSERT_EQUAL_UINT(1, visibleCount());
    TEST_ASSERT_TRUE(db->pendingSave());
}

static void test_first_region_then_reboot_retires_actual_previous_identity()
{
    TEST_ASSERT_TRUE(db->ensureLocalPkiKeys());
    const auto security = config.security;
    const NodeNum keyed = crc32Buffer(owner.public_key.bytes, 32);
    db->bootIdentity();
    TEST_ASSERT_EQUAL_HEX32(keyed, db->getNodeNum());
    TEST_ASSERT_TRUE(nodeInfoLiteIsIgnored(db->getMeshNode(provisional)));
    TEST_ASSERT_FALSE(nodeInfoLiteIsIgnored(db->getMeshNode(keyed)));
    TEST_ASSERT_EQUAL_UINT(1, visibleCount());
    TEST_ASSERT_EQUAL_UINT(2, db->getNumMeshNodes());
    TEST_ASSERT_EQUAL_MEMORY(security.private_key.bytes, config.security.private_key.bytes, 32);
    db->bootIdentity();
    TEST_ASSERT_EQUAL_HEX32(keyed, db->getNodeNum());
    TEST_ASSERT_EQUAL_UINT(2, db->getNumMeshNodes());
    TEST_ASSERT_EQUAL_UINT(1, visibleCount());
}

static void test_old_firmware_partial_identity_state_recovers_without_random_id()
{
    strcpy(owner.long_name, "Custom local radio");
    strcpy(db->getMeshNode(provisional)->long_name, owner.long_name);
    const NodeNum keyed = prepareKey(); // Old region path updated owner but left
                                        // its NodeDB row keyless.
    TEST_ASSERT_EQUAL_UINT(0, db->getMeshNode(provisional)->public_key.size);
    db->bootIdentity();
    TEST_ASSERT_EQUAL_HEX32(keyed, db->getNodeNum());
    TEST_ASSERT_TRUE(nodeInfoLiteIsIgnored(db->getMeshNode(provisional)));
    TEST_ASSERT_EQUAL_UINT(1, visibleCount());
    char expectedId[sizeof(owner.id)];
    snprintf(expectedId, sizeof(expectedId), "!%08x", keyed);
    TEST_ASSERT_EQUAL_STRING(expectedId, owner.id);
    TEST_ASSERT_TRUE(db->pendingSave());
}

static void test_existing_default_ghost_is_quarantined_without_erasing_data()
{
    const NodeNum keyed = seedExistingDuplicate();
#if !MESHTASTIC_EXCLUDE_TELEMETRYDB
    db->nodeTelemetry[provisional].uptime_seconds = 140;
#endif
    db->bootIdentity();
    TEST_ASSERT_EQUAL_HEX32(keyed, db->getNodeNum());
    TEST_ASSERT_TRUE(nodeInfoLiteIsIgnored(db->getMeshNode(provisional)));
    TEST_ASSERT_EQUAL_UINT(2, db->getNumMeshNodes());
    TEST_ASSERT_EQUAL_UINT(1, visibleCount());
#if !MESHTASTIC_EXCLUDE_TELEMETRYDB
    meshtastic_DeviceMetrics metrics = meshtastic_DeviceMetrics_init_zero;
    TEST_ASSERT_TRUE(db->copyNodeTelemetry(provisional, metrics));
    TEST_ASSERT_EQUAL_UINT(140, metrics.uptime_seconds);
#endif
    TEST_ASSERT_TRUE(db->pendingSave());
}

static void test_same_name_other_number_is_preserved()
{
    seedExistingDuplicate();
    const NodeNum peerNum = provisional ^ 0x80000000;
    auto *peer = addOwner(peerNum);
    peer->public_key.size = 0;
    db->bootIdentity();
    TEST_ASSERT_FALSE(nodeInfoLiteIsIgnored(db->getMeshNode(peerNum)));
}

static void test_conflicting_key_at_old_hardware_number_is_preserved()
{
    seedExistingDuplicate();
    auto *old = db->getMeshNode(provisional);
    old->public_key.size = 32;
    memset(old->public_key.bytes, 0x55, 32);
    db->bootIdentity();
    TEST_ASSERT_FALSE(nodeInfoLiteIsIgnored(db->getMeshNode(provisional)));
    TEST_ASSERT_EQUAL_UINT8(0x55, db->getMeshNode(provisional)->public_key.bytes[0]);
}

static void test_favourite_verified_and_muted_candidates_are_preserved()
{
    const uint16_t masks[] = {NODEINFO_BITFIELD_IS_FAVORITE_MASK, NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK,
                              NODEINFO_BITFIELD_IS_MUTED_MASK};
    for (auto mask : masks) {
        setUp();
        seedExistingDuplicate();
        nodeInfoLiteSetBit(db->getMeshNode(provisional), mask, true);
        db->bootIdentity();
        TEST_ASSERT_FALSE(nodeInfoLiteIsIgnored(db->getMeshNode(provisional)));
    }
}

static void test_custom_names_without_keys_are_ambiguous_and_preserved()
{
    strcpy(owner.long_name, "Custom radio");
    strcpy(db->getMeshNode(provisional)->long_name, "Custom radio");
    seedExistingDuplicate();
    db->bootIdentity();
    TEST_ASSERT_FALSE(nodeInfoLiteIsIgnored(db->getMeshNode(provisional)));
}

static void test_peer_hop_or_mqtt_evidence_is_preserved()
{
    seedExistingDuplicate();
    db->getMeshNode(provisional)->has_hops_away = true;
    db->getMeshNode(provisional)->hops_away = 1;
    db->bootIdentity();
    TEST_ASSERT_FALSE(nodeInfoLiteIsIgnored(db->getMeshNode(provisional)));
    db->getMeshNode(provisional)->has_hops_away = false;
    nodeInfoLiteSetBit(db->getMeshNode(provisional), NODEINFO_BITFIELD_VIA_MQTT_MASK, true);
    db->bootIdentity();
    TEST_ASSERT_FALSE(nodeInfoLiteIsIgnored(db->getMeshNode(provisional)));
}

static void test_mismatched_hardware_or_name_is_preserved()
{
    seedExistingDuplicate();
    strcpy(db->getMeshNode(provisional)->short_name, "peer");
    db->bootIdentity();
    TEST_ASSERT_FALSE(nodeInfoLiteIsIgnored(db->getMeshNode(provisional)));
    strcpy(db->getMeshNode(provisional)->short_name, owner.short_name);
    db->getMeshNode(provisional)->hw_model = static_cast<meshtastic_HardwareModel>((owner.hw_model + 1) % 255);
    db->bootIdentity();
    TEST_ASSERT_FALSE(nodeInfoLiteIsIgnored(db->getMeshNode(provisional)));
}

static void test_conflicting_key_still_uses_collision_avoidance()
{
    prepareKey();
    auto *old = db->getMeshNode(provisional);
    old->public_key.size = 32;
    memset(old->public_key.bytes, 0x55, 32);
    db->chooseProvisional();
    TEST_ASSERT_NOT_EQUAL(provisional, db->getNodeNum());
    TEST_ASSERT_FALSE(nodeInfoLiteIsIgnored(old));
}

static void test_partial_save_with_stale_owner_key_keeps_the_real_previous_id()
{
    const NodeNum keyed = prepareKey();
    TypeConversions::CopyUserToNodeInfoLite(db->getMeshNode(provisional), owner);
    owner.public_key.size = 0;
    db->bootIdentity();
    TEST_ASSERT_EQUAL_HEX32(keyed, db->getNodeNum());
    TEST_ASSERT_TRUE(nodeInfoLiteIsIgnored(db->getMeshNode(provisional)));
    TEST_ASSERT_EQUAL_UINT(1, visibleCount());
}

static void test_foreign_device_state_does_not_claim_a_keyless_peer()
{
    owner.macaddr[0] ^= 0x10;
    db->chooseProvisional();
    TEST_ASSERT_NOT_EQUAL(provisional, db->getNodeNum());
    TEST_ASSERT_FALSE(nodeInfoLiteIsIgnored(db->getMeshNode(provisional)));
}

static void test_explicit_unignore_after_completed_repair_is_respected()
{
    seedExistingDuplicate();
    db->bootIdentity();
    auto *old = db->getMeshNode(provisional);
    db->repairPreviouslyCompleted();
    nodeInfoLiteSetBit(old, NODEINFO_BITFIELD_IS_IGNORED_MASK, false);
    db->bootIdentity();
    TEST_ASSERT_FALSE(nodeInfoLiteIsIgnored(db->getMeshNode(provisional)));
}

static void test_corrupt_config_does_not_migrate_or_quarantine()
{
    const NodeNum keyed = seedExistingDuplicate();
    db->degradedBoot();
    db->bootIdentity();
    TEST_ASSERT_EQUAL_HEX32(keyed, db->getNodeNum());
    TEST_ASSERT_FALSE(nodeInfoLiteIsIgnored(db->getMeshNode(provisional)));
    TEST_ASSERT_FALSE(db->pendingSave());
}

static void test_licensed_owner_is_not_migrated()
{
    owner.is_licensed = true;
    TypeConversions::CopyUserToNodeInfoLite(db->getMeshNode(provisional), owner);
    TEST_ASSERT_FALSE(db->ensureLocalPkiKeys());
    db->bootIdentity();
    TEST_ASSERT_EQUAL_HEX32(provisional, db->getNodeNum());
    TEST_ASSERT_EQUAL_UINT(1, visibleCount());
}

#if ARCH_PORTDUINO
static void test_config_only_save_persists_identity_and_repair_across_reload()
{
    const NodeNum keyed = seedExistingDuplicate();
    db->bootIdentity();
    const auto privateKey = config.security.private_key;
    TEST_ASSERT_TRUE(db->persist());
    TEST_ASSERT_TRUE(db->repairMarker());
    TEST_ASSERT_FALSE(db->pendingSave());
    db->loadSavedIdentity();
    db->bootIdentity();
    TEST_ASSERT_EQUAL_HEX32(keyed, db->getNodeNum());
    TEST_ASSERT_EQUAL_MEMORY(privateKey.bytes, config.security.private_key.bytes, 32);
    TEST_ASSERT_TRUE(nodeInfoLiteIsIgnored(db->getMeshNode(provisional)));
    nodeInfoLiteSetBit(db->getMeshNode(provisional), NODEINFO_BITFIELD_IS_IGNORED_MASK, false);
    TEST_ASSERT_TRUE(db->saveToDisk(SEGMENT_NODEDATABASE));
    db->loadSavedIdentity();
    db->bootIdentity();
    TEST_ASSERT_FALSE(nodeInfoLiteIsIgnored(db->getMeshNode(provisional)));
}

static void test_failed_identity_save_never_formats_preferences_or_commits_marker()
{
    TEST_ASSERT_TRUE(db->ensureLocalPkiKeys());
    TEST_ASSERT_TRUE(db->persist());
    const auto savedKey = config.security.private_key;
    db->getMeshNode(provisional)->public_key.size = 0;
    seedExistingDuplicate();
    db->bootIdentity();
    std::filesystem::create_directory(std::filesystem::path(fixtureRoot) / "prefs/config.proto.tmp");
    TEST_ASSERT_FALSE(db->saveToDisk(SEGMENT_CONFIG));
    TEST_ASSERT_TRUE(db->pendingSave());
    TEST_ASSERT_FALSE(db->repairMarker());
    meshtastic_LocalConfig saved = meshtastic_LocalConfig_init_zero;
    TEST_ASSERT_EQUAL(LoadFileResult::LOAD_SUCCESS, db->loadProto("/prefs/config.proto", meshtastic_LocalConfig_size,
                                                                  sizeof(saved), &meshtastic_LocalConfig_msg, &saved));
    TEST_ASSERT_EQUAL_MEMORY(savedKey.bytes, saved.security.private_key.bytes, 32);
    std::filesystem::remove_all(std::filesystem::path(fixtureRoot) / "prefs/config.proto.tmp");
    TEST_ASSERT_TRUE(db->saveToDisk(SEGMENT_CONFIG));
    TEST_ASSERT_TRUE(db->repairMarker());
}

static void test_failed_node_write_preserves_previous_database_and_retries()
{
    seedExistingDuplicate();
    TEST_ASSERT_TRUE(db->saveToDisk(SEGMENT_CONFIG | SEGMENT_DEVICESTATE | SEGMENT_NODEDATABASE));
    const auto path = std::filesystem::path(fixtureRoot) / "prefs/nodes.proto";
    const auto readBytes = [&]() {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };
    const auto previous = readBytes();
    TEST_ASSERT_FALSE(previous.empty());
    db->bootIdentity();
    const auto obstacle = std::filesystem::path(fixtureRoot) / "prefs/nodes.proto.tmp";
    std::filesystem::create_directory(obstacle);
    TEST_ASSERT_FALSE(db->saveToDisk(SEGMENT_CONFIG));
    TEST_ASSERT_TRUE(db->pendingSave());
    TEST_ASSERT_FALSE(db->repairMarker());
    TEST_ASSERT_TRUE(previous == readBytes());
    std::filesystem::remove_all(obstacle);
    TEST_ASSERT_TRUE(db->saveToDisk(SEGMENT_CONFIG));
    TEST_ASSERT_TRUE(db->repairMarker());
    db->loadSavedIdentity();
    TEST_ASSERT_TRUE(nodeInfoLiteIsIgnored(db->getMeshNode(provisional)));
}

static void test_file_transfer_defers_identity_save_and_repair_marker()
{
    seedExistingDuplicate();
    db->bootIdentity();
    meshtastic_XModem packet = meshtastic_XModem_init_zero;
    packet.control = meshtastic_XModem_Control_SOH;
    strcpy(reinterpret_cast<char *>(packet.buffer.bytes), "/prefs/test-transfer.bin");
    xModem.handlePacket(packet);
    TEST_ASSERT_TRUE(xModem.isBusy());
    const bool saved = db->saveToDisk(SEGMENT_CONFIG);
    packet.control = meshtastic_XModem_Control_EOT;
    xModem.handlePacket(packet);
    TEST_ASSERT_FALSE(saved);
    TEST_ASSERT_FALSE(xModem.isBusy());
    TEST_ASSERT_TRUE(db->pendingSave());
    TEST_ASSERT_FALSE(db->repairMarker());
    TEST_ASSERT_TRUE(db->saveToDisk(SEGMENT_CONFIG));
    TEST_ASSERT_TRUE(db->repairMarker());
}
#endif

IDENTITY_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
#if ARCH_PORTDUINO
    portduino_config.mac_address = "020001fb9239";
    if (!mkdtemp(fixtureRoot))
        exit(1);
    portduinoVFS->mountpoint(fixtureRoot);
#endif
    db = new NodeDBTestShim();
    nodeDB = db;
    UNITY_BEGIN();
    RUN_TEST(test_first_region_keeps_open_session_and_synchronizes_self);
    RUN_TEST(test_first_region_admin_dispatch_updates_self_without_renumbering);
    RUN_TEST(test_first_region_then_reboot_retires_actual_previous_identity);
    RUN_TEST(test_old_firmware_partial_identity_state_recovers_without_random_id);
    RUN_TEST(test_existing_default_ghost_is_quarantined_without_erasing_data);
    RUN_TEST(test_same_name_other_number_is_preserved);
    RUN_TEST(test_conflicting_key_at_old_hardware_number_is_preserved);
    RUN_TEST(test_favourite_verified_and_muted_candidates_are_preserved);
    RUN_TEST(test_custom_names_without_keys_are_ambiguous_and_preserved);
    RUN_TEST(test_peer_hop_or_mqtt_evidence_is_preserved);
    RUN_TEST(test_mismatched_hardware_or_name_is_preserved);
    RUN_TEST(test_conflicting_key_still_uses_collision_avoidance);
    RUN_TEST(test_partial_save_with_stale_owner_key_keeps_the_real_previous_id);
    RUN_TEST(test_foreign_device_state_does_not_claim_a_keyless_peer);
    RUN_TEST(test_explicit_unignore_after_completed_repair_is_respected);
    RUN_TEST(test_corrupt_config_does_not_migrate_or_quarantine);
    RUN_TEST(test_licensed_owner_is_not_migrated);
#if ARCH_PORTDUINO
    RUN_TEST(test_config_only_save_persists_identity_and_repair_across_reload);
    RUN_TEST(test_failed_identity_save_never_formats_preferences_or_commits_marker);
    RUN_TEST(test_failed_node_write_preserves_previous_database_and_retries);
    RUN_TEST(test_file_transfer_defers_identity_save_and_repair_marker);
#endif
    const int result = UNITY_END();
    nodeDB = nullptr;
    delete db;
#if ARCH_PORTDUINO
    std::filesystem::remove_all(fixtureRoot);
#endif
    exit(result);
}
#else
void setUp() {}
void tearDown() {}
IDENTITY_TEST_ENTRY void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    exit(UNITY_END());
}
#endif
IDENTITY_TEST_ENTRY void loop() {}
