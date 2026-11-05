#pragma once
#include "Engine/Game/Build/fingerprint.h"
#include <cstdint>

// Loading a bundle while a level runs (Skate Synchros' live show, Extension/Multiplayer/Synchros/
// live_show.cpp; research in contrib/live-apply-without-reload.md). Read from Skate.exe 20260929.
//
// The resource manager loads bundles in operations, each into a compartment; a bundle stays loaded
// while its operation's handle is held. The engine's own synchronous load (0x4648a00, "Loading
// bundle %s...") calls begin(&handle, 1, &name, 1, 0, 0, 0), then done(handle, 5) in a loop
// (pumping the loader between), then succeeded(handle); on failure it releases the handle.
//
// A handle is a 64-bit value; one whose top byte is 8 is an operation already finished.
namespace dingosdk::game::build::v20260929::bundle_loading {
// bool begin(std::uint64_t* handle, std::uint16_t compartment, const char* const* names,
//            std::uint32_t count, std::uint64_t, std::uint32_t, std::uint32_t)
inline constexpr Fingerprint begin{0x41b48d0, {
    0x48,0x8b,0xc4,0x53,0x56,0x57,0x48,0x83,0xec,0x50,0x48,0x89,0x68,0x08,0x48,0x8b,
    0xf1,0x48,0x8b,0x2d,0x70,0x87,0x59,0x03,0xb9,0x88,0x00,0x00,0x00,0x4c,0x89,0x60}};
// bool done(std::uint64_t handle, std::uint32_t wait_ms)
inline constexpr Fingerprint done{0x41b6600, {
    0x48,0x8b,0xc1,0x48,0xc1,0xe8,0x38,0x3c,0x08,0x75,0x03,0xb0,0x01,0xc3,0x48,0x8b,
    0x05,0x4b,0x77,0x59,0x03,0x44,0x8b,0xc2,0x48,0x8b,0xd1,0x48,0x8b,0xc8,0x4c,0x8b}};
// bool succeeded(std::uint64_t handle)
inline constexpr Fingerprint succeeded{0x41b4f60, {
    0x48,0x8b,0xc1,0x48,0xc1,0xe8,0x38,0x3c,0x08,0x75,0x03,0xb0,0x01,0xc3,0x48,0x8b,
    0x05,0xeb,0x8d,0x59,0x03,0x48,0x8b,0xd1,0x48,0x8b,0xc8,0x4c,0x8b,0x00,0x49,0xff}};
// void release(std::uint64_t handle): cancels a running operation, unloads a finished one's bundles
inline constexpr Fingerprint release{0x41b3970, {
    0x48,0x8b,0x05,0xe9,0xa3,0x59,0x03,0x48,0x8b,0xd1,0x48,0x8b,0xc8,0x4c,0x8b,0x00,
    0x49,0xff,0xa0,0xa0,0x00,0x00,0x00,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc}};

// The root level's bundles (cas_main_sharedbundle, where item definitions and lists live) load
// into compartment 5 (Level::startLoadLevelResources, 0x4649bf0).
inline constexpr std::uint16_t level_compartment = 5;

// The cosmetics manager (engine.h cosmetics_manager) takes in item lists with
// register(manager, lists, ready) at 0x557c40 (reference_lists.h register_named, which ReSkate
// hooks): `lists` points at an array of list pointers whose count is the dword before it (bit 31 a
// flag); each list's items are the array at list + 0x28 (same count rule, bit 2 of each pointer a
// flag); each item's hash at + 0x48 goes into the manager's map (+ 0x30) with the item; the list is
// appended to + 0x58; the manager's ready byte (+ 0xa8) is set to `ready`; the UI is told.
// insert(map, &out, flag, const std::uint32_t* key, std::uint64_t hash) at 0x41fe40: finds or makes
// the node for `key` in a hash map {buckets*, u32 bucket count, ...}; out's first field is the node
// {u32 key, item* at + 8, next at + 0x10}. 0x557c40 calls it with the manager's map (+ 0x30) and then
// sets the node's item.
inline constexpr Fingerprint map_insert{0x41fe40, {
    0x40,0x53,0x55,0x56,0x57,0x41,0x55,0x48,0x83,0xec,0x30,0x48,0x8b,0xac,0x24,0x80,
    0x00,0x00,0x00,0x48,0x8b,0xf2,0x44,0x8b,0x51,0x08,0x33,0xd2,0x48,0x8b,0xc5,0x49}};
inline constexpr std::uint32_t manager_map = 0x30;      // buckets*, then u32 bucket count at + 0x38
inline constexpr std::uint32_t node_item = 0x8;
inline constexpr std::uint32_t node_next = 0x10;
inline constexpr std::uint32_t manager_items = 0x3c;    // u32, items in the map
inline constexpr std::uint32_t manager_ready = 0xa8;    // u8
inline constexpr std::uint32_t list_items = 0x28;       // the list's item array
inline constexpr std::uint32_t list_name = 0x18;        // Asset.Name, a char*
inline constexpr std::uint32_t item_hash = 0x48;        // u32

// A bundle-reference table asset (characters/customization/configs/cas_main_bundlereftable, in
// compartment 5): its data at + 0x48 (pointer, low 2 bits tags); the data's + 0x18 points at the
// table resource as loaded, its five header pointers made absolute (Engine/Resource/
// bundle_ref_table.cpp's layout: lookups + 8, pool + 16, bundles + 24, lookup count + 72, bundle
// count + 76). A lookup (0x16fe710 -> 0x16fe160, 0x16fe070) reads those each time.
// The registered tables (0x54c050 returns the registry, kept at 0x71ee620): an array at registry + 8
// (count in the dword before it, bit 31 a flag) of table assets (bit 2 of each pointer a flag), each
// named at + 0x18 (Asset.Name). The composite asks each in turn (0x518d70).
inline constexpr std::uintptr_t table_registry = 0x71ee620;
inline constexpr std::uint32_t registry_tables = 0x8;
inline constexpr std::uint32_t table_name = 0x18;
inline constexpr std::uint32_t table_data = 0x48;
inline constexpr std::uint32_t data_resource = 0x18;
}
