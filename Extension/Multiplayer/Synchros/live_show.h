#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Skate Synchros, experimental: showing a received outfit with no level reload
// (contrib/outfit-sharing.md, "Showing a received item with no reload"). After a live apply
// without a reload has put the received outfit mod in the game's TOCs, three steps put it in the
// running game:
//
//   1. load the outfit mod's live bundle (synchros_outfit_mod.h) into the level's compartment, so
//      the received definitions, presets, textures and meshes are in memory;
//   2. add each received item to the cosmetics manager, so the game knows it is installed;
//   3. add the received presets' rows to the loaded bundle-reference table, so a recipe naming them
//      finds a bundle.
//
// Then the wearer is dressed again. Every step runs on the client update thread (the one that
// dresses peers) and reports what it did; none of them is undone: a loaded live bundle stays loaded
// for the rest of the game unless released by hand (`synchros live release`), so nothing the
// manager or a table points at is ever freed under it.
namespace dingosdk::synchros::live {

struct Row {
    std::string table, path;
    std::uint32_t bundle{};
};

// Whether this build has every function the steps call.
[[nodiscard]] bool supported(std::string& why);

// Step 1. A handle to hold, or nothing (with why).
[[nodiscard]] std::optional<std::uint64_t> begin_load(const std::string& bundle, std::string& problem);
enum class Load { running, loaded, failed };
[[nodiscard]] Load poll(std::uint64_t handle);
void release(std::uint64_t handle);

// Step 2. `definitions` are the items' EBX names; those already known are left as they are.
struct Registered {
    std::size_t added{}, known{};
    std::vector<std::string> not_loaded;   // no loaded asset by that name
};
[[nodiscard]] Registered register_items(const std::vector<std::string>& definitions, std::string& problem);

// Step 3. Rows already in the table are left as they are; returns how many were added.
[[nodiscard]] std::size_t add_rows(const std::vector<Row>& rows, std::string& problem);

} // namespace dingosdk::synchros::live
