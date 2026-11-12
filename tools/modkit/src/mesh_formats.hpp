// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "rpk.hpp"
#include <map>

namespace dcmod {

// Decoder-only catalogue. Never merges resources into the writable target pack.
// Keep only descriptors so dependency geometry/textures can be released after parsing.
class MeshFormats {
public:
    void Add(const RpkPack& pack, const std::string& source) {
        for (const RpkResource& r : pack.resources) {
            if (r.Type() != unsigned(RpkTypeFromName("stream_format"))) continue;
            auto found = entries_.find(r.id);
            if (found != entries_.end()) {
                const RpkResource& old = found->second.resource;
                if (old.data != r.data || old.deps != r.deps)
                    throw Error("conflicting stream_format " + HexId(r.id) + " in " +
                                found->second.sources.front() + " and " + source);
                found->second.sources.push_back(source);
            } else {
                entries_.emplace(r.id, Entry{r, {source}});
            }
        }
    }

    const RpkResource* Find(uint64_t id) const {
        const auto it = entries_.find(id);
        return it == entries_.end() ? nullptr : &it->second.resource;
    }

    std::vector<std::string> Sources(uint64_t id) const {
        const auto it = entries_.find(id);
        return it == entries_.end() ? std::vector<std::string>{} : it->second.sources;
    }

private:
    struct Entry { RpkResource resource; std::vector<std::string> sources; };
    std::map<uint64_t, Entry> entries_;
};

// Report malformed descriptors without interpreting partial field layouts.
inline std::string MeshFormatProblem(const RpkResource* fmt) {
    if (!fmt) return "attribute_format_resource_missing_from_pack";
    if (fmt->data.size() < 21) return "attribute_format_descriptor_truncated";
    if (fmt->PayloadId() != fmt->id) return "attribute_format_header_invalid";
    if (size_t(fmt->data[20]) > (fmt->data.size() - 21) / 5)
        return "attribute_format_descriptor_truncated";
    return {};
}

} // namespace dcmod
