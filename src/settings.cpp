#include "settings.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Settings
{
    namespace
    {
        constexpr auto        kBaseFile = "Data/skse/plugins/EngineFixes.toml";
        constexpr auto        kUserFile = "Data/skse/plugins/EngineFixesCustom.toml";
        constexpr std::size_t kCommentColumn = 48;  // column the "# comment" is aligned to

        // Format a default literal as it should appear in the TOML.
        std::string DefToml(bool a_v) { return a_v ? "true" : "false"; }
        std::string DefToml(int a_v) { return std::to_string(a_v); }
        std::string DefToml(unsigned a_v) { return std::to_string(a_v); }
        std::string DefToml(float a_v)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(a_v));
            std::string s = buf;
            if (s.find_first_of(".eE") == std::string::npos) {
                s += ".0";  // TOML floats require a fractional part (1 -> 1.0)
            }
            return s;
        }

        struct Entry
        {
            std::string key;
            std::string value;  // default, unless an existing value overrides it
            std::string comment;
        };

        struct Section
        {
            std::string        name;
            std::string        preamble;  // optional comment line above the [Section] header
            std::vector<Entry> entries;
        };

        using ExistingMap = std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;

        std::string Trim(std::string a_s)
        {
            const auto b = a_s.find_first_not_of(" \t");
            if (b == std::string::npos) {
                return {};
            }
            const auto e = a_s.find_last_not_of(" \t\r");
            return a_s.substr(b, e - b + 1);
        }

        // Build the canonical schema (single source of truth: settings_schema.h).
        std::vector<Section> BuildSchema()
        {
            std::vector<Section> out;
            const auto           add = [&](const char* a_name, const char* a_preamble, std::vector<Entry> a_entries) {
                out.push_back(Section{ a_name, a_preamble, std::move(a_entries) });
            };

#define X(type, key, def, comment) entries.push_back(Entry{ #key, DefToml(def), comment });
            {
                std::vector<Entry> entries;
                EF_SETTINGS_GENERAL(X)
                add("General", "", std::move(entries));
            }
            {
                std::vector<Entry> entries;
                EF_SETTINGS_FIXES(X)
                add("Fixes", "contains bug fixes", std::move(entries));
            }
            {
                std::vector<Entry> entries;
                EF_SETTINGS_PATCHES(X)
                add("Patches", "contains optional game patches", std::move(entries));
            }
            {
                std::vector<Entry> entries;
                EF_SETTINGS_MEMORYMANAGER(X)
                add("MemoryManager", "patches to replace Skyrim's allocators with tbbmalloc", std::move(entries));
            }
            {
                std::vector<Entry> entries;
                EF_SETTINGS_WARNINGS(X)
                add("Warnings", "", std::move(entries));
            }
            {
                std::vector<Entry> entries;
                EF_SETTINGS_DEBUG(X)
                add("Debug", "", std::move(entries));
            }
#undef X
            return out;
        }

        // Parse the existing TOML into section -> key -> raw value, so user edits
        // are preserved when regenerating. Flat scalar settings only (no nested
        // tables/arrays/strings), which is all EngineFixes uses.
        ExistingMap ParseExisting(const std::string& a_path)
        {
            ExistingMap   m;
            std::ifstream in(a_path);
            if (!in) {
                return m;
            }
            std::string line;
            std::string section;
            while (std::getline(in, line)) {
                const auto a = line.find_first_not_of(" \t");
                if (a == std::string::npos || line[a] == '#') {
                    continue;
                }
                if (line[a] == '[') {
                    const auto e = line.find(']', a);
                    if (e != std::string::npos) {
                        section = Trim(line.substr(a + 1, e - a - 1));
                    }
                    continue;
                }
                const auto eq = line.find('=', a);
                if (eq == std::string::npos) {
                    continue;
                }
                const auto key = Trim(line.substr(a, eq - a));
                auto       val = line.substr(eq + 1);
                if (const auto h = val.find('#'); h != std::string::npos) {
                    val = val.substr(0, h);  // strip inline comment (EF has no '#' inside values)
                }
                val = Trim(val);
                if (!key.empty() && !section.empty()) {
                    m[section][key] = val;
                }
            }
            return m;
        }

        std::string Render(const std::vector<Section>& a_sections, const ExistingMap& a_existing)
        {
            std::string out;
            out += "# Engine Fixes - unified build for Skyrim SE 1.5.97, AE 1.6.1170, and VR 1.4.15\n";
            out += "# Settings marked (VR-only) are ignored on SE/AE; all others apply to all three runtimes.\n";
            out += "# User overrides go in EngineFixesCustom.toml (created alongside this file).\n";
            for (const auto& s : a_sections) {
                out += '\n';
                if (!s.preamble.empty()) {
                    out += "# " + s.preamble + '\n';
                }
                out += '[' + s.name + "]\n";
                for (const auto& e : s.entries) {
                    std::string value = e.value;
                    if (const auto sit = a_existing.find(s.name); sit != a_existing.end()) {
                        if (const auto kit = sit->second.find(e.key); kit != sit->second.end() && !kit->second.empty()) {
                            value = kit->second;  // preserve the user's current value
                        }
                    }
                    std::string out_line = e.key + " = " + value;
                    if (!e.comment.empty()) {
                        if (out_line.size() < kCommentColumn) {
                            out_line.append(kCommentColumn - out_line.size(), ' ');
                        } else {
                            out_line += ' ';
                        }
                        out_line += "# " + e.comment;
                    }
                    out += out_line + '\n';
                }
            }
            return out;
        }

        std::string ReadFile(const std::string& a_path)
        {
            std::ifstream in(a_path, std::ios::binary);
            if (!in) {
                return {};
            }
            std::ostringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }

        std::string StripCR(std::string a_s)
        {
            std::erase(a_s, '\r');
            return a_s;
        }

        // Self-healing: ensure EngineFixes.toml contains every declared setting.
        // Missing keys are added with defaults + descriptions; existing values are
        // preserved. Idempotent: only writes when content actually changes (line
        // endings ignored), so it does not churn the file every launch.
        void Generate()
        {
            namespace fs = std::filesystem;

            const auto sections = BuildSchema();
            const auto existing = ParseExisting(kBaseFile);
            const auto desired = Render(sections, existing);
            const auto current = ReadFile(kBaseFile);

            if (StripCR(desired) == StripCR(current)) {
                return;
            }

            std::size_t total = 0;
            std::size_t added = 0;
            for (const auto& s : sections) {
                const auto sit = existing.find(s.name);
                for (const auto& e : s.entries) {
                    ++total;
                    if (sit == existing.end() || !sit->second.contains(e.key)) {
                        ++added;
                    }
                }
            }

            std::error_code ec;
            fs::create_directories(fs::path(kBaseFile).parent_path(), ec);
            std::ofstream outFile(kBaseFile, std::ios::binary | std::ios::trunc);
            if (!outFile) {
                logger::warn("could not write {} (self-heal skipped)"sv, kBaseFile);
                return;
            }
            outFile << desired;
            logger::info("EngineFixes.toml regenerated: {} settings ensured, {} added"sv, total, added);
        }
    }

    void Load()
    {
        try {
            Generate();
        } catch (const std::exception& e) {
            logger::warn("EngineFixes.toml self-heal failed: {}"sv, e.what());
        }

        const auto toml = REX::TOML::SettingStore::GetSingleton();
        toml->Init(kBaseFile, kUserFile);
        toml->Load();
    }
}
