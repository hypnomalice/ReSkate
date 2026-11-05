#include "live_show.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Abi/native_data.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/bundle_loading.h"
#include "Engine/Game/Build/20260929/engine.h"
#include "Engine/Game/Build/20260929/reference_lists.h"
#include "Engine/Resource/bundle_ref_table.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <map>

namespace dingosdk::synchros::live {
namespace {
namespace bl = addr::bundle_loading;

using Begin = bool (*)(std::uint64_t*, std::uint16_t, const char* const*, std::uint32_t, std::uint64_t, std::uint32_t, std::uint32_t);
using Done = bool (*)(std::uint64_t, std::uint32_t);
using Succeeded = bool (*)(std::uint64_t);
using Release = void (*)(std::uint64_t);
using Insert = void* (*)(std::uintptr_t, void*, std::uint8_t, const std::uint32_t*, std::uint64_t);
using Find = std::uintptr_t (*)(std::uint16_t, const char*);

std::uintptr_t base() { return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)); }
bool matches(const game::build::Fingerprint& f) {
    std::array<unsigned char, 32> head{};
    return memory::peek(base() + f.rva, head) && head == f.bytes;
}
std::string lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
template <class T> bool get(std::uintptr_t at, T& value) { return memory::peek(at, value); }
std::uintptr_t untag(std::uintptr_t p, std::uintptr_t bits) { return p & ~bits; }
// A Frostbite array: its count is the dword before the first element, bit 31 a flag.
bool array(std::uintptr_t first, std::uint32_t& count) {
    if (!first) { count = 0; return true; }
    if (!get(first - 4, count)) return false;
    count &= 0x7fffffffU;
    return count < 1'000'000;
}

// Native calls, each behind SEH: a fault is a failed step, not the end of the game.
bool call_begin(Begin f, std::uint64_t* handle, const char* const* names) {
    __try { return f(handle, bl::level_compartment, names, 1, 0, 0, 0); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
int call_done(Done done, Succeeded ok, std::uint64_t handle) {
    __try {
        if (!done(handle, 0)) return 0;
        return ok(handle) ? 1 : 2;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 2; }
}
void call_release(Release f, std::uint64_t handle) {
    __try { f(handle); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
std::uintptr_t call_find(Find f, const char* name) {
    __try { return f(bl::level_compartment, name); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool call_insert(Insert f, std::uintptr_t map, std::uint32_t hash, std::uintptr_t item) {
    __try {
        std::uintptr_t out[2]{};
        const std::uint32_t key = hash;
        f(map, out, 0, &key, hash);
        if (!out[0]) return false;
        *reinterpret_cast<std::uintptr_t*>(out[0] + bl::node_item) = item;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool swap_pointer(std::uintptr_t at, std::uintptr_t value) {
    __try {
        InterlockedExchange64(reinterpret_cast<volatile LONG64*>(at), static_cast<LONG64>(value));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The manager's node for an item hash, or 0.
std::uintptr_t node_for(std::uintptr_t manager, std::uint32_t hash) {
    std::uintptr_t buckets{};
    std::uint32_t count{};
    if (!get(manager + bl::manager_map, buckets) || !get(manager + bl::manager_map + 8, count) || !buckets || !count) return 0;
    std::uintptr_t node{};
    if (!get(buckets + 8 * (hash % count), node)) return 0;
    for (int guard = 0; node && guard < 4096; ++guard) {
        std::uint32_t key{};
        if (!get(node, key)) return 0;
        if (key == hash) return node;
        if (!get(node + bl::node_next, node)) return 0;
    }
    return 0;
}

// The registered table named `name` (its resource name), or 0.
std::uintptr_t table_named(const std::string& name) {
    std::uintptr_t registry{}, first{};
    std::uint32_t count{};
    if (!get(base() + bl::table_registry, registry) || !registry || !get(registry + bl::registry_tables, first) ||
        !array(first, count))
        return 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uintptr_t table{}, text{};
        if (!get(first + 8 * i, table)) return 0;
        table = untag(table, 4);
        if (!table || !get(table + bl::table_name, text) || !text) continue;
        std::array<char, 256> buffer{};
        if (!memory::peek_bytes(text, buffer.data(), buffer.size() - 1)) continue;
        if (lower(buffer.data()) == name) return table;
    }
    return 0;
}

std::map<std::uint64_t, bool> held;   // handles begun here; true once loaded
} // namespace

bool supported(std::string& why) {
    for (const auto* f : {&bl::begin, &bl::done, &bl::succeeded, &bl::release, &bl::map_insert})
        if (!matches(*f)) {
            why = std::format("this game build differs at {:#x}", f->rva);
            return false;
        }
    if (!game::native_data().find_asset) {
        why = "the asset lookup is not available";
        return false;
    }
    return true;
}

std::optional<std::uint64_t> begin_load(const std::string& bundle, std::string& problem) {
    if (!supported(problem)) return std::nullopt;
    std::uint64_t handle{};
    const char* names[] = {bundle.c_str()};
    if (!call_begin(reinterpret_cast<Begin>(base() + bl::begin.rva), &handle, names) || !handle) {
        problem = "the game would not start loading " + bundle;
        return std::nullopt;
    }
    held[handle] = false;
    return handle;
}

Load poll(std::uint64_t handle) {
    const auto state = call_done(reinterpret_cast<Done>(base() + bl::done.rva),
                                 reinterpret_cast<Succeeded>(base() + bl::succeeded.rva), handle);
    if (state == 1) held[handle] = true;
    return state == 0 ? Load::running : state == 1 ? Load::loaded : Load::failed;
}

void release(std::uint64_t handle) {
    if (!handle || !matches(bl::release)) return;
    call_release(reinterpret_cast<Release>(base() + bl::release.rva), handle);
    held.erase(handle);
}

std::size_t already_loaded(const std::vector<std::string>& definitions) {
    const auto find = reinterpret_cast<Find>(game::native_data().find_asset);
    if (!find) return 0;
    std::size_t n = 0;
    for (const auto& name : definitions)
        if (call_find(find, name.c_str())) ++n;
    return n;
}

Registered register_items(const std::vector<std::string>& definitions, std::string& problem) {
    Registered out;
    if (!supported(problem)) return out;
    std::uintptr_t manager{};
    if (!get(base() + addr::engine::cosmetics_manager, manager) || !manager) {
        problem = "the cosmetics manager is not there";
        return out;
    }
    const auto find = reinterpret_cast<Find>(game::native_data().find_asset);
    const auto insert = reinterpret_cast<Insert>(base() + bl::map_insert.rva);
    for (const auto& name : definitions) {
        const auto item = call_find(find, name.c_str());
        std::uint32_t hash{};
        if (!item || !get(item + bl::item_hash, hash)) {
            out.not_loaded.push_back(name);
            continue;
        }
        if (const auto node = node_for(manager, hash)) {
            std::uintptr_t current{};
            if (get(node + bl::node_item, current) && current) {
                ++out.known;
                continue;
            }
        }
        if (call_insert(insert, manager + bl::manager_map, hash, item)) ++out.added;
        else problem = "the cosmetics manager would not take " + name;
    }
    return out;
}

std::size_t add_rows(const std::vector<Row>& rows, std::string& problem) {
    std::map<std::string, std::vector<frostbite::bundle_ref::Preset>> by_table;
    for (const auto& row : rows) by_table[lower(row.table)].push_back({row.path, row.bundle});
    std::size_t added = 0;
    for (const auto& [name, presets] : by_table) {
        const auto table = table_named(name);
        std::uintptr_t data{}, header{};
        if (!table || !get(table + bl::table_data, data) || !(data = untag(data, 3)) ||
            !get(data + bl::data_resource, header) || !header) {
            problem = name + " is not loaded";
            continue;
        }
        // The table as loaded, made a resource again, the presets added.
        std::array<std::byte, 96> head{};
        if (!memory::peek_bytes(header, head.data(), head.size())) {
            problem = name + " could not be read";
            continue;
        }
        const auto payload = frostbite::bundle_ref::loaded_size(head, header);
        std::vector<std::byte> loaded(payload);
        if (!payload || !memory::peek_bytes(header, loaded.data(), payload)) {
            problem = name + " is not laid out as expected";
            continue;
        }
        frostbite::bundle_ref::MergedTable merged;
        try {
            std::vector<std::byte> resource, meta;
            merged = frostbite::bundle_ref::with_presets(frostbite::bundle_ref::unload(loaded, header, resource, meta), presets);
        } catch (const std::exception& failure) {
            problem = name + ": " + failure.what();
            continue;
        }
        if (!merged.added) continue;
        // The new table, its pointers made absolute, takes the old one's place; the old one stays
        // allocated for any lookup reading it now.
        const auto size = merged.resource.size() - 20;
        auto* copy = static_cast<std::byte*>(VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!copy) {
            problem = "no memory for " + name;
            continue;
        }
        const auto start = reinterpret_cast<std::uintptr_t>(copy);
        const auto placed = frostbite::bundle_ref::load_at(merged.resource, start);
        std::memcpy(copy, placed.data(), placed.size());
        if (!swap_pointer(data + bl::data_resource, start)) {
            VirtualFree(copy, 0, MEM_RELEASE);
            problem = name + " could not be replaced";
            continue;
        }
        added += merged.added;
    }
    return added;
}

} // namespace dingosdk::synchros::live
