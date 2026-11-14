// SPDX-License-Identifier: GPL-2.0-or-later
// dcmod mesh ...: the geometry of resource packs as glTF 2.0 binary (.glb), for Blender and other editors.
//
// A model pack has "mesh" resources with position/attribute streams and an index buffer. Known layouts hold
// sub meshes: four 32 bit numbers (lowest vertex, highest vertex, first index, index count) after the bytes 02 03,
// plus matching draw-like records (3, lowest vertex, 0, first index, index count). Recognizing and updating those
// records does not establish that a native scene instance selects them; runtime draw evidence is still required.
// The indices are absolute positions in the vertex buffer and form a triangle list.
#include "cli.hpp"
#include "json.hpp"
#include "rpk.hpp"
#include "mesh_formats.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace dcmod {

namespace {

int Usage() {
    std::puts(
        "dcmod mesh: model geometry of resource packs as glTF 2.0 (.glb)\n"
        "\n"
        "  dcmod mesh scan PACK                        read-only JSON compatibility report and ranked candidates\n"
        "  dcmod mesh info PACK                        the sub meshes: name, vertices, triangles\n"
        "  dcmod mesh export PACK -o FILE.glb [NAME...]  one glTF mesh per sub mesh (names may use * and ?)\n"
        "  dcmod mesh materials PACK [SUBMESH]         the materials of the pack, or of the mesh of a sub mesh\n"
        "  dcmod mesh material PACK TARGET SOURCE -o OUT  TARGET sub mesh is drawn with the material of SOURCE (a sub mesh\n"
        "                                              or a material identifier)\n"
        "  dcmod mesh import PACK FILE.glb [--own-buffers] -o OUT.rpk  write the pack again with the positions (and triangles) of the\n"
        "                                              glTF objects that came from it\n"
        "  --dependency PACK                          repeat for explicit local read-only stream-format sources\n"
        "  --target NAME                              scan only: repeatable wildcard filter; all slots still checked\n"
        "  --isolate-buffers                            always relocate imported parts, including smaller ones\n"
        "\n"
        "PACK is a file or @path in the game's archive. Objects are named ID_N_NAME (the mesh's identifier and the\n"
        "sub mesh number); keep that prefix when renaming. An object may have more or fewer vertices and triangles\n"
        "than the game's: extra data goes to the end of the buffers (--own-buffers: new vertex buffers of its own for a part that grows). Normals and texture coordinates are exported and\n"
        "imported; skin weights are copied from the nearest old vertex. Axes are the game's own (Y up).");
    return 2;
}

struct Sub {
    uint64_t mesh = 0, pos = 0, ib = 0, attr = 0;
    uint32_t pos_stride = 12;
    bool half = false;  // positions are three half floats
    int normal_off = -1, tangent_off = -1;  // normal and tangent (three 10 bit signed numbers in 4 bytes) in a vertex of the attribute stream
    int uv_off = -1;  // where the texture coordinate (two floats) sits in a vertex of the attribute stream
    uint32_t stride = 0;
    uint64_t normal_buffer = 0, uv_buffer = 0, tangent_buffer = 0;
    uint32_t normal_stride = 0, uv_stride = 0, tangent_stride = 0;
    std::vector<uint64_t> streams; // explicitly bound non-position vertex streams
    std::vector<std::string> binding_problems;
    bool partial_layout = false;
    // skinning: four weights (half floats, kind 7) and four bone numbers (16 bit, kind 8), in the attribute stream or in a stream of their own
    uint64_t skin = 0;
    uint32_t skin_stride = 0;
    int w_off = -1, j_off = -1;
    uint32_t lo = 0, hi = 0, first = 0, count = 0, k = 0;
    std::vector<size_t> recs;  // where the mesh data holds this sub mesh's record (the bytes 02 03), often several times
    std::string name;
    uint32_t vertices() const { return hi - lo + 1; }
};

// Position streams are told by their stream format: three floats (12 bytes), three floats and 4 other bytes (16) or
// three half floats and 2 padding bytes (8), or the same with 4 other bytes after them (12).
int PositionKind(const RpkResource& vb) {
    if (vb.data.size() < 28 || vb.deps.empty()) return 0;
    switch (vb.deps[0]) {
        case 0x0002ea626d3b6d17ull: return 1;
        case 0x00021bb5417fb6e5ull: return 1;
        case 0x000234fef1a20d63ull: return 2;
        case 0x0002874b47ba4c22ull: return 2;
        default: return 0;
    }
}

float HalfToFloat(uint16_t h) {
    const int e = (h >> 10) & 31;
    const float m = float(h & 1023);
    float f = e == 0 ? std::ldexp(m, -24) : e == 31 ? (m ? NAN : INFINITY) : std::ldexp(m + 1024.f, e - 25);
    return h & 0x8000 ? -f : f;
}

uint16_t FloatToHalf(float f) {
    if (std::isnan(f)) return 0x7E00;
    const uint16_t sign = std::signbit(f) ? 0x8000 : 0;
    f = std::fabs(f);
    if (f >= 65520.f) return sign | 0x7C00;
    if (f < 6.1035156e-5f) return sign | uint16_t(std::lrint(std::ldexp(f, 24)));
    int e;
    const float m = std::frexp(f, &e);  // f = m * 2^e, m in [0.5, 1)
    uint32_t mant = uint32_t(std::lrint(std::ldexp(m, 11))) - 1024;  // 10 bits
    int exp = e + 14;
    if (mant >= 1024) mant = 0, ++exp;
    return sign | uint16_t(exp << 10) | uint16_t(mant);
}

uint32_t VbCount(const RpkResource& r) { return r.data.size() >= 28 ? Rd32(r.data.data() + 24) : 0; }

// The last place before `at` where the mesh data names the resource `id` (its identifier as 8 little endian bytes).
long LastMention(const Bytes& d, uint64_t id, size_t at) {
    if (d.size() < 8) return -1;
    uint8_t key[8];
    for (int i = 0; i < 8; ++i) key[i] = uint8_t(id >> (8 * i));
    for (size_t i = std::min(at, d.size() - 8) + 1; i-- > 0;) {
        if (i + 8 <= at && std::memcmp(&d[i], key, 8) == 0) return long(i);
    }
    return -1;
}

// The fields of a vertex stream by its stream format: a count, then five bytes per field (kind, type, index, 0, 0). The type
// gives the size for known encodings; unknown widths are not inferred.
struct Fields {
    int normal = -1, tangent = -1, uv = -1, weights = -1, joints = -1;
    bool whole = false;  // all sizes known and add up to stride
    bool partial = false; // only the known prefix has decodable offsets
    uint32_t opaque_at = 0;
};

Fields ParseFields(const RpkResource* fmt, uint32_t stride) {
    Fields f;
    if (!MeshFormatProblem(fmt).empty()) return f;
    const size_t n = fmt->data[20];
    if (n > (fmt->data.size() - 21) / 5) return f;
    uint32_t at = 0;
    for (size_t k = 0; k < n && 21 + k * 5 + 5 <= fmt->data.size(); ++k) {
        const uint8_t kind = fmt->data[21 + k * 5], type = fmt->data[22 + k * 5];
        if (!MeshFieldWidth(type)) {
            // No offsets are assigned at or beyond this field. A lower bound
            // rejects impossible suffixes without assuming any unknown width.
            uint32_t minimum = 0;
            for (size_t tail = k; tail < n; ++tail)
                minimum += std::max(1u, MeshFieldWidth(fmt->data[22 + tail * 5]));
            if (at > stride || minimum > stride - at) return Fields();
            f.partial = true; f.opaque_at = at;
            return f;
        }
        if (at > stride || MeshFieldWidth(type) > stride - at) return Fields();
        if (kind == 5 && type == 8 && f.uv < 0) f.uv = int(at);
        if (kind == 2 && type == 15 && f.normal < 0) f.normal = int(at);
        if (kind == 5 && type == 15 && fmt->data[23 + k * 5] == 6 && f.tangent < 0) f.tangent = int(at);
        if (kind == 7 && type == 6 && f.weights < 0) f.weights = int(at);
        if (kind == 8 && type == 24 && f.joints < 0) f.joints = int(at);
        at += MeshFieldWidth(type);
    }
    f.whole = at == stride;
    if (!f.whole) f = Fields();
    return f;
}

std::vector<Sub> FindSubs(const RpkPack& pack, const MeshFormats& formats) {
    const int vb_type = RpkTypeFromName("vertex_buffer"), ib_type = RpkTypeFromName("index_buffer"), mesh_type = RpkTypeFromName("mesh");
    std::vector<Sub> out;
    for (const RpkResource& m : pack.resources) {
        if (int(m.Type()) != mesh_type) continue;
        // Pack parsing validates the container, not stream-specific sizes. Refuse
        // malformed count/stride fields before FindSubs dereferences any stream.
        for (uint64_t id : m.deps) {
            const RpkResource* stream = pack.Find(id);
            if (!stream || (int(stream->Type()) != vb_type && int(stream->Type()) != ib_type)) continue;
            if (stream->data.size() < 28) throw Error("truncated mesh buffer " + HexId(id));
            const size_t stride = int(stream->Type()) == ib_type ? 2u : Rd16(stream->data.data() + 22);
            const size_t count = VbCount(*stream);
            if (!stride || count > (stream->data.size() - 28) / stride)
                throw Error("invalid mesh buffer count/stride " + HexId(id));
            const int position_kind=PositionKind(*stream);
            if (position_kind && stride < (position_kind==2 ? 6u : 12u))
                throw Error("position stride is smaller than XYZ encoding " + HexId(id));
            if (PositionKind(*stream) && stride < (PositionKind(*stream) == 2 ? 6u : 12u))
                throw Error("invalid position stride " + HexId(id));
        }
        std::vector<uint64_t> poss, ibs, attrs, skins;
        for (uint64_t d : m.deps) {
            const RpkResource* r = pack.Find(d);
            if (!r) continue;
            if (int(r->Type()) == vb_type && r->data.size() >= 28) {
                const Fields vf = ParseFields(r->deps.empty() ? nullptr : formats.Find(r->deps[0]), Rd16(r->data.data() + 22));
                if (PositionKind(*r)) poss.push_back(d);
                else if (vf.weights >= 0 && vf.normal < 0 && vf.uv < 0) skins.push_back(d);  // weights and bones only
                else attrs.push_back(d);
            }
            if (int(r->Type()) == ib_type) ibs.push_back(d);
        }
        if (poss.empty() || ibs.empty()) continue;
        // each sub mesh record is preceded by the identifiers of the buffers it uses; take the nearest of each kind
        auto nearest = [&](const std::vector<uint64_t>& list, size_t at) {
            long best = -1;
            uint64_t who = list.front();
            for (uint64_t d : list) {
                const long p = LastMention(m.data, d, at);
                if (p > best) best = p, who = d;
            }
            return who;
        };
        uint32_t k = 0;
        std::vector<std::array<uint64_t, 5>> seen;
        std::vector<size_t> seen_at;  // the entry of `out` that each key made
        for (size_t i = 0; i + 18 <= m.data.size(); ++i) {
            if (m.data[i] != 2 || m.data[i + 1] != 3) continue;
            const uint32_t lo = Rd32(&m.data[i + 2]), hi = Rd32(&m.data[i + 6]), first = Rd32(&m.data[i + 10]), cnt = Rd32(&m.data[i + 14]);
            const uint64_t pos = nearest(poss, i), ib = nearest(ibs, i);
            const uint32_t nv = VbCount(*pack.Find(pos)), ni = VbCount(*pack.Find(ib));
            if (!cnt || cnt % 3 || hi < lo || hi >= nv || uint64_t(first) + cnt > ni) continue;
            const uint8_t* ix = &pack.Find(ib)->data[28 + size_t(first) * 2];
            bool inside = true;  // a record whose indices leave its range is not a sub mesh
            for (uint32_t t = 0; t < cnt && inside; ++t) inside = Rd16(ix + size_t(t) * 2) >= lo && Rd16(ix + size_t(t) * 2) <= hi;
            if (!inside) continue;
            const std::array<uint64_t, 5> key{pos, ib, lo, hi, (uint64_t(first) << 32) | cnt};
            const auto dup = std::find(seen.begin(), seen.end(), key);
            if (dup != seen.end()) {
                out[seen_at[size_t(dup - seen.begin())]].recs.push_back(i);
                continue;
            }
            seen.push_back(key);
            seen_at.push_back(out.size());
            Sub s;
            s.mesh = m.id, s.pos = pos, s.pos_stride = Rd16(pack.Find(pos)->data.data() + 22), s.half = PositionKind(*pack.Find(pos)) == 2, s.ib = ib, s.lo = lo, s.hi = hi, s.first = first, s.count = cnt, s.k = k++;
            s.recs.push_back(i);
            // the attribute stream that has as many vertices as the position stream
            std::vector<uint64_t> same;
            for (uint64_t a : attrs) {
                if (VbCount(*pack.Find(a)) == nv) same.push_back(a);
            }
            if (!same.empty()) {
                s.attr = nearest(same, i);
                const RpkResource* av = pack.Find(s.attr);
                s.stride = Rd16(av->data.data() + 22);
                const Fields f = ParseFields(av->deps.empty() ? nullptr : formats.Find(av->deps[0]), s.stride);
                s.uv_off = f.uv, s.normal_off = f.normal, s.tangent_off = f.tangent;
                if (f.weights >= 0 && f.joints >= 0) s.skin = s.attr, s.skin_stride = s.stride, s.w_off = f.weights, s.j_off = f.joints;
            }
            if (!s.skin && !skins.empty()) {
                std::vector<uint64_t> same_skin;
                for (uint64_t a : skins) {
                    if (VbCount(*pack.Find(a)) == nv) same_skin.push_back(a);
                }
                if (!same_skin.empty()) {
                    s.skin = nearest(same_skin, i);
                    const RpkResource* sv = pack.Find(s.skin);
                    s.skin_stride = Rd16(sv->data.data() + 22);
                    const Fields f = ParseFields(sv->deps.empty() ? nullptr : formats.Find(sv->deps[0]), s.skin_stride);
                    if (f.weights >= 0 && f.joints >= 0) s.w_off = f.weights, s.j_off = f.joints;
                    else s.skin = 0;
                }
            }
            std::string n = m.DisplayName();
            const size_t bar = n.rfind('|');
            if (bar != std::string::npos && bar + 1 < n.size()) n = n.substr(bar + 1);
            s.name = HexId(m.id) + "_" + std::to_string(s.k) + "_" + n;
            out.push_back(s);
        }
    }
    // A resource somewhere in the mesh dependency list is not enough evidence
    // that it belongs to this draw. Use explicit mentions in each record block.
    for (Sub& s : out) {
        s.normal_buffer = s.normal_off >= 0 ? s.attr : 0;
        s.uv_buffer = s.uv_off >= 0 ? s.attr : 0;
        s.tangent_buffer = s.tangent_off >= 0 ? s.attr : 0;
        s.normal_stride = s.uv_stride = s.tangent_stride = s.stride;
        const RpkResource& mesh = *pack.Find(s.mesh);
        std::vector<uint64_t> active;
        bool first_record = true;
        for (size_t rec : s.recs) {
            size_t begin = 0;
            for (const Sub& other : out) if (other.mesh == s.mesh)
                for (size_t previous : other.recs) if (previous < rec) begin = std::max(begin, previous + 18);
            std::vector<uint64_t> current;
            for (uint64_t id : mesh.deps) {
                const RpkResource* vb = pack.Find(id);
                if (!vb || int(vb->Type()) != vb_type || LastMention(mesh.data, id, rec) < long(begin)) continue;
                if (id == s.pos) continue;
                if (PositionKind(*vb) || VbCount(*vb) != VbCount(*pack.Find(s.pos))) {
                    s.binding_problems.push_back("unmatched_vertex_stream_in_draw_block");
                    continue;
                }
                if (std::find(current.begin(), current.end(), id) == current.end()) current.push_back(id);
            }
            std::sort(current.begin(), current.end());
            if (first_record) active = current;
            else if (active != current) s.binding_problems.push_back("inconsistent_stream_bindings_across_records");
            first_record = false;
        }
        s.streams = active;
        // Keep legacy shared-reference export behavior when no explicit block
        // can be identified; relocation remains blocked by mention validation.
        std::vector<uint64_t> semantic_streams = active;
        if (active.empty() && s.attr) semantic_streams.push_back(s.attr);
        semantic_streams.push_back(s.pos); // normals can share the position record

        s.normal_off = s.uv_off = s.tangent_off = -1;
        s.normal_buffer = s.uv_buffer = s.tangent_buffer = 0;
        int normals = 0, uvs = 0, tangents = 0;
        for (uint64_t id : semantic_streams) {
            const RpkResource& vb = *pack.Find(id);
            const uint32_t stride = Rd16(vb.data.data() + 22);
            const RpkResource* fmt = vb.deps.empty() ? nullptr : formats.Find(vb.deps[0]);
            const Fields f = ParseFields(fmt, stride);
            s.partial_layout |= f.partial;
            if (id != s.pos && !f.whole && !f.partial) s.binding_problems.push_back("bound_attribute_layout_unresolved_or_invalid");
            if (id == s.pos && fmt && !MeshFormatProblem(fmt).empty())
                s.binding_problems.push_back("position_format_descriptor_invalid");
            if (f.normal >= 0) { ++normals; s.normal_buffer = id; s.normal_stride = stride; s.normal_off = f.normal; }
            if (f.uv >= 0) { ++uvs; s.uv_buffer = id; s.uv_stride = stride; s.uv_off = f.uv; }
            if (f.tangent >= 0) { ++tangents; s.tangent_buffer = id; s.tangent_stride = stride; s.tangent_off = f.tangent; }
            // Even an opaque encoding cannot hide a declared skin stream from
            // the static-geometry candidate gate.
            if (fmt && MeshFormatProblem(fmt).empty())
                for (size_t k = 0; k < fmt->data[20]; ++k)
                    if (fmt->data[21 + k*5] == 7 || fmt->data[21 + k*5] == 8) s.skin = s.skin ? s.skin : id;
        }
        if (normals > 1) { s.normal_off = -1; s.normal_buffer = 0; s.binding_problems.push_back("ambiguous_normal_streams"); }
        if (uvs > 1) { s.uv_off = -1; s.uv_buffer = 0; s.binding_problems.push_back("ambiguous_uv_streams"); }
        if (tangents > 1) { s.tangent_off = -1; s.tangent_buffer = 0; s.binding_problems.push_back("ambiguous_tangent_streams"); }
    }
    return out;
}

std::vector<uint64_t> RelocationStreams(const Sub& s) {
    std::vector<uint64_t> ids = s.streams;
    ids.push_back(s.pos);
    if (s.streams.empty() && s.attr) ids.push_back(s.attr);
    if (s.skin) ids.push_back(s.skin);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

bool IndependentMentions(const RpkPack& pack, const Sub& s, const std::vector<Sub>& all) {
    const Bytes& data = pack.Find(s.mesh)->data;
    for (size_t rec : s.recs) {
        size_t begin = 0;
        for (const Sub& other : all) if (other.mesh == s.mesh)
            for (size_t previous : other.recs) if (previous < rec) begin = std::max(begin, previous + 18);
        for (uint64_t id : RelocationStreams(s)) {
            const long at = LastMention(data, id, rec);
            if (at < long(begin)) return false;
            for (const Sub& other : all) {
                if (&other == &s || other.mesh != s.mesh) continue;
                const auto used = RelocationStreams(other);
                if (std::find(used.begin(), used.end(), id) == used.end()) continue;
                for (size_t r : other.recs) if (LastMention(data, id, r) == at) return false;
            }
        }
    }
    return true;
}

// Reports the same slot interpretation used by ImportSub; never mutates a pack.
Json ScanMeshes(const RpkPack& pack, const std::vector<Sub>& subs, const MeshFormats& formats,
                const std::vector<std::string>& targets) {
    Json report = Json::Object(), slots = Json::Array(), candidates = Json::Array();
    std::vector<const Sub*> ranked;
    std::map<uint64_t,std::vector<const RpkResource*>> consumers;
    for (const auto& resource:pack.resources)
        for (uint64_t id:resource.deps) consumers[id].push_back(&resource);
    for (const Sub& s : subs) {
        if (!targets.empty() && std::none_of(targets.begin(), targets.end(), [&](const std::string& pattern) {
                return WildMatch(Lower(pattern), Lower(s.name));
            })) continue;
        Json row = Json::Object(), blockers = Json::Array(), warnings = Json::Array(), streams = Json::Array();
        row.Set("target", s.name); row.Set("vertices", s.vertices()); row.Set("triangles", s.count / 3);
        row.Set("mesh_resource",HexId(s.mesh));
        Json owners=Json::Array();
        for(const auto* owner:consumers[s.mesh]) {
            Json ref=Json::Object(); ref.Set("id",HexId(owner->id));
            ref.Set("type",RpkTypeName(owner->Type())); ref.Set("name",owner->DisplayName()); owners.Push(ref);
        }
        row.Set("local_resource_consumers",owners);
        row.Set("consumer_scope","direct dependency IDs in this pack only; not decoded scene/LOD bindings");
        row.Set("normal_buffer", HexId(s.normal_buffer)); row.Set("uv_buffer", HexId(s.uv_buffer));
        row.Set("tangent_buffer", HexId(s.tangent_buffer)); row.Set("partial_layout", s.partial_layout);
        Json bound = Json::Array(); for (uint64_t id : RelocationStreams(s)) bound.Push(HexId(id));
        row.Set("relocation_buffers", bound);
        for (const auto& problem : s.binding_problems) blockers.Push(problem);
        row.Set("normal_decoded", s.normal_off >= 0); row.Set("tangent_decoded", s.tangent_off >= 0);
        row.Set("uv_decoded", s.uv_off >= 0); row.Set("skinned", s.skin != 0);
        row.Set("position_buffer", HexId(s.pos)); row.Set("attribute_buffer", HexId(s.attr));
        row.Set("index_buffer", HexId(s.ib)); row.Set("position_half_float", s.half);
        Json spans=Json::Array();
        for(uint64_t id : {s.mesh,s.pos,s.ib}) {
            const auto* resource=pack.Find(id);
            Json span=Json::Object(); span.Set("id",HexId(id));
            span.Set("type",RpkTypeName(resource->Type()));
            span.Set("offset",double(resource->source_offset));
            span.Set("size",double(resource->data.size())); spans.Push(span);
        }
        row.Set("source_file_spans",spans);
        row.Set("source_span_scope","parsed input file only; includes payload headers; not runtime allocation offsets");
        row.Set("vertex_first", s.lo); row.Set("vertex_last", s.hi);
        row.Set("index_first", s.first); row.Set("index_count", s.count);
        row.Set("position_stride", s.pos_stride);
        row.Set("position_resource_vertices",VbCount(*pack.Find(s.pos)));
        row.Set("mesh_search_target","t1:"+HexId(s.pos)+":"+std::to_string(VbCount(*pack.Find(s.pos)))+":"+std::to_string(s.vertices()));
        Json records=Json::Array(); for (size_t at:s.recs) records.Push(double(at));
        row.Set("mesh_record_offsets", records);
        // Measured mesh-local vertices, NOT decoded native draw/culling bounds.
        std::array<double, 3> low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
        bool finite = true, exportable = true;
        const Bytes& positions = pack.Find(s.pos)->data;
        for (uint32_t vertex = s.lo; vertex <= s.hi; ++vertex) {
            for (size_t axis = 0; axis < 3; ++axis) {
                const uint8_t* value = &positions[28 + size_t(vertex) * s.pos_stride + axis * (s.half ? 2 : 4)];
                float decoded;
                if (s.half) decoded = HalfToFloat(Rd16(value));
                else std::memcpy(&decoded, value, 4);
                finite &= std::isfinite(decoded);
                exportable &= std::isfinite(decoded) && std::fabs(decoded) < 1e6f;
                low[axis] = std::min(low[axis], double(decoded));
                high[axis] = std::max(high[axis], double(decoded));
            }
        }
        Json bounds;
        if (finite) {
            bounds = Json::Array();
            Json lo = Json::Array(), hi = Json::Array();
            for (size_t axis = 0; axis < 3; ++axis) { lo.Push(low[axis]); hi.Push(high[axis]); }
            bounds.Push(lo); bounds.Push(hi);
        }
        row.Set("vertex_bounds", bounds);
        row.Set("bounds_space", "mesh-local vertices; native scene transform and culling bounds unknown");
        if (!finite) blockers.Push("nonfinite_donor_positions");
        else if (!exportable) blockers.Push("donor_positions_exceed_legacy_export_range");
        const RpkResource& mesh = *pack.Find(s.mesh);
        std::string reason = "no_attribute_stream_with_matching_vertex_count";
        for (uint64_t id : mesh.deps) {
            const RpkResource* vb = pack.Find(id);
            if (!vb || vb->Type() != unsigned(RpkTypeFromName("vertex_buffer"))) continue;
            Json stream = Json::Object(), fields = Json::Array();
            stream.Set("id", HexId(id)); stream.Set("selected_attributes", id == s.attr);
            stream.Set("selected_normal", id == s.normal_buffer); stream.Set("selected_uv", id == s.uv_buffer);
            stream.Set("selected_tangent", id == s.tangent_buffer);
            const Fields decoded = ParseFields(vb->deps.empty() ? nullptr : formats.Find(vb->deps[0]), Rd16(vb->data.data()+22));
            stream.Set("known_prefix_decodable", decoded.partial);
            stream.Set("normal_offset", decoded.normal); stream.Set("uv_offset", decoded.uv);
            stream.Set("tangent_offset", decoded.tangent);
            if (decoded.partial) {
                stream.Set("opaque_suffix_offset", decoded.opaque_at);
                stream.Set("opaque_suffix_bytes", uint32_t(Rd16(vb->data.data()+22)) - decoded.opaque_at);
            }
            stream.Set("vertices", VbCount(*vb)); stream.Set("stride", Rd16(vb->data.data()+22));
            const RpkResource* fmt = vb->deps.empty() ? nullptr : formats.Find(vb->deps[0]);
            stream.Set("format_id", vb->deps.empty() ? "" : HexId(vb->deps[0]));
            Json sources = Json::Array();
            if (!vb->deps.empty()) for (const auto& source : formats.Sources(vb->deps[0])) sources.Push(source);
            stream.Set("format_sources", sources);
            const std::string problem = MeshFormatProblem(fmt);
            stream.Set("format_problem", problem);
            bool complete = problem.empty();
            bool declared = false, supported = false, known_widths = true;
            const bool records_present = fmt && fmt->data.size() >= 21 && size_t(fmt->data[20]) <= (fmt->data.size()-21)/5;
            if (records_present) for (size_t k=0; k<fmt->data[20]; ++k) {
                const size_t at=21+k*5;
                Json f=Json::Object(); f.Set("kind", int(fmt->data[at])); f.Set("type", int(fmt->data[at+1]));
                f.Set("index", int(fmt->data[at+2]));
                f.Set("metadata_byte_3", int(fmt->data[at+3])); f.Set("metadata_byte_4", int(fmt->data[at+4]));
                f.Set("byte_width_supported", MeshFieldWidth(fmt->data[at+1]) != 0); fields.Push(f);
                known_widths &= MeshFieldWidth(fmt->data[at+1]) != 0;
                if (fmt->data[at]==2) { declared=true; supported |= fmt->data[at+1]==15; }
            }
            stream.Set("format_complete", complete); stream.Set("fields", fields);
            stream.Set("decoder_stride_matches", complete && ParseFields(fmt,Rd16(vb->data.data()+22)).whole);
            if (id == s.attr) {
                if (!complete) reason=problem;
                else if (!declared) reason="no_normal_semantic_in_selected_descriptor";
                else if (!supported) reason="normal_semantic_present_but_type_unsupported";
                else if (!known_widths) reason="attribute_format_contains_unsupported_field_type";
                else if (!ParseFields(fmt,s.stride).whole) reason="normal_present_but_decoded_layout_does_not_match_stride";
                else reason="decoded";
            }
            streams.Push(stream);
        }
        if (s.normal_buffer && s.normal_off >= 0) reason = s.partial_layout ? "decoded_known_prefix" : "decoded";
        if (std::find(s.binding_problems.begin(), s.binding_problems.end(), "ambiguous_normal_streams") != s.binding_problems.end()) reason = "ambiguous_normal_streams";
        row.Set("normal_status", reason); row.Set("vertex_streams", streams);
        if (s.normal_off < 0) blockers.Push(reason);
        if (s.uv_off < 0) blockers.Push("no_decoded_uv_for_flat_loop");
        if (s.skin) blockers.Push("skinned_slot_not_a_static_geometry_candidate");
        Json shares=Json::Array(), index_shares=Json::Array();
        const auto used = RelocationStreams(s);
        for (const Sub& other:subs) {
            const auto theirs = RelocationStreams(other);
            bool shared = false;
            for (uint64_t id : used) shared |= std::find(theirs.begin(), theirs.end(), id) != theirs.end();
            if (&other != &s && shared) shares.Push(other.name);
            if (&other != &s && other.ib==s.ib) index_shares.Push(other.name);
        }
        const bool independent = s.attr != 0 && IndependentMentions(pack, s, subs);
        row.Set("shared_buffer_slots", shares); row.Set("shared_index_buffer_slots", index_shares);
        row.Set("independent_buffer_mentions", independent);
        if (!independent) blockers.Push("buffer_mentions_missing_or_shared_with_previous_slot");
        Bytes draw; Wr32(draw,3); Wr32(draw,s.lo); Wr32(draw,0); Wr32(draw,s.first); Wr32(draw,s.count);
        Json draws=Json::Array();
        for (size_t at=0;at+draw.size()<=mesh.data.size();++at)
            if (!std::memcmp(mesh.data.data()+at,draw.data(),draw.size())) draws.Push(double(at));
        bool has_draw=!draws.Items().empty();
        row.Set("draw_record_offsets",draws);
        row.Set("draw_table_found",has_draw);
        // Bounded raw-position/index signature for an opt-in renderer probe.
        // No claim that a matching resource is selected by the native scene.
        const auto hex=[](const uint8_t* data,size_t n) {
            const char* digits="0123456789abcdef"; std::string out;
            for(size_t i=0;i<n;++i) { out+=digits[data[i]>>4]; out+=digits[data[i]&15]; }
            return out;
        };
        const uint32_t samples=std::min(8u,s.vertices()), width=s.half ? 6 : 12;
        std::string prefix;
        for(uint32_t i=0;i<samples;++i)
            prefix+=hex(positions.data()+28+size_t(s.lo+i)*s.pos_stride,width);
        const auto& index_data=pack.Find(s.ib)->data;
        const auto index_prefix=hex(index_data.data()+28+size_t(s.first)*2,size_t(std::min(24u,s.count))*2);
        row.Set("draw_probe_signature", "v1:"+std::to_string(s.pos_stride)+":"+std::to_string(width)+":"+
                std::to_string(samples)+":"+std::to_string(s.count)+":"+std::to_string(s.lo)+":"+prefix+":"+index_prefix);
        row.Set("draw_probe_scope","raw position/index prefix; direct indexed draws; identical geometry can also match");
        if (!has_draw) blockers.Push("draw_table_entry_not_found");
        if (s.partial_layout) warnings.Push("unknown_suffix_not_decoded_full_donor_rows_preserved_shader_semantics_unverified");
        if (s.tangent_off<0) warnings.Push("tangent_not_decoded_material_compatibility_unverified");
        warnings.Push("scene_transform_culling_visibility_and_materials_unverified");
        warnings.Push("classification_is_for_supported_flat_loop_format_not_runtime_success");
        row.Set("blockers",blockers); row.Set("limitations",warnings);
        row.Set("compatible",blockers.Items().empty());
        row.Set("road_name_hint",Lower(s.name).find("road")!=std::string::npos);
        if (blockers.Items().empty()) ranked.push_back(&s);
        slots.Push(row);
    }
    std::sort(ranked.begin(),ranked.end(),[](const Sub* a,const Sub* b) {
        bool ar=Lower(a->name).find("road")!=std::string::npos, br=Lower(b->name).find("road")!=std::string::npos;
        if(ar!=br) return ar;
        if(a->vertices()!=b->vertices()) return a->vertices()<b->vertices();
        return a->name<b->name;
    });
    for(const Sub* s:ranked) candidates.Push(s->name);
    if (!targets.empty() && slots.Items().empty()) throw Error("scan target filters matched no recognized slots");
    report.Set("scanned_slots", subs.size()); report.Set("reported_slots", slots.Items().size());
    Json required=Json::Array(); for(const std::string& name:pack.required) required.Push(name);
    report.Set("required_packs",required);
    report.Set("schema","dc-mesh-compatibility-1"); report.Set("slots",slots); report.Set("candidates",candidates);
    report.Set("scope","recognized mesh slots only; missing external format dependencies are unresolved, not proof of absent normals");
    report.Set("ranking","compatible static slots: road name hint then vertex count; visibility is not inferred");
    return report;
}

// Normals and tangents are three signed 10 bit numbers in a 32 bit word (x in the lowest bits); checked on the Ferrari body
// (decoded normals agree with the triangles' normals, 1870 of 1872 vertices sampled).
std::array<float, 3> Dec10(uint32_t u) {
    std::array<float, 3> r{};
    for (size_t i = 0; i < 3; ++i) {
        int v = int((u >> (10 * i)) & 1023);
        if (v & 512) v -= 1024;
        r[i] = std::max(-1.f, float(v) / 511.f);
    }
    return r;
}

uint32_t Enc10(std::array<float, 3> v) {
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    uint32_t u = 0;
    for (size_t i = 0; i < 3; ++i) {
        const float f = l > 0 ? v[i] / l : (i == 2 ? 1.f : 0.f);
        u |= (uint32_t(int(std::lrint(std::max(-1.f, std::min(1.f, f)) * 511.f))) & 1023u) << (10 * i);
    }
    return u;
}

float Clean(float v) { return std::isfinite(v) && std::fabs(v) < 1e6f ? v : 0.f; }

void Put32(Bytes& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); }
void PadTo4(Bytes& b, uint8_t fill) { while (b.size() % 4) b.push_back(fill); }

Bytes MakeGlb(const RpkPack& pack, const std::vector<Sub>& subs) {
    Bytes bin;
    Json nodes = Json::Array(), meshes = Json::Array(), accessors = Json::Array(), views = Json::Array(), scene = Json::Array();
    for (const Sub& s : subs) {
        const Bytes& pv = pack.Find(s.pos)->data;
        const Bytes& iv = pack.Find(s.ib)->data;
        // positions
        PadTo4(bin, 0);
        const size_t pos_at = bin.size();
        float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
        for (uint32_t i = s.lo; i <= s.hi; ++i) {
            for (int c = 0; c < 3; ++c) {
                float f;
                if (s.half) f = HalfToFloat(Rd16(&pv[28 + size_t(i) * s.pos_stride + size_t(c) * 2]));
                else std::memcpy(&f, &pv[28 + size_t(i) * s.pos_stride + size_t(c) * 4], 4);
                f = Clean(f);
                mn[c] = std::min(mn[c], f), mx[c] = std::max(mx[c], f);
                uint32_t u;
                std::memcpy(&u, &f, 4);
                Put32(bin, u);
            }
        }
        // texture coordinates, when the attribute stream has them (glTF counts v from the top)
        size_t uv_at = 0;
        const bool has_uv = s.uv_off >= 0;
        if (has_uv) {
            PadTo4(bin, 0);
            uv_at = bin.size();
            const Bytes& av = pack.Find(s.uv_buffer)->data;
            for (uint32_t i = s.lo; i <= s.hi; ++i) {
                float uv[2];
                std::memcpy(uv, &av[28 + size_t(i) * s.uv_stride + size_t(s.uv_off)], 8);
                uv[1] = 1.f - uv[1];
                for (float f : uv) {
                    uint32_t u;
                    const float c = Clean(f);
                    std::memcpy(&u, &c, 4);
                    Put32(bin, u);
                }
            }
        }
        size_t nrm_at = 0;
        const bool has_nrm = s.normal_off >= 0 && s.normal_buffer;
        if (has_nrm) {
            PadTo4(bin, 0);
            nrm_at = bin.size();
            const Bytes& av = pack.Find(s.normal_buffer)->data;
            for (uint32_t i = s.lo; i <= s.hi; ++i) {
                for (float f : Dec10(Rd32(&av[28 + size_t(i) * s.normal_stride + size_t(s.normal_off)]))) {
                    uint32_t u;
                    std::memcpy(&u, &f, 4);
                    Put32(bin, u);
                }
            }
        }
        // bone numbers and weights, when the mesh is skinned
        size_t jn_at = 0, wt_at = 0;
        const bool has_skin = s.skin && s.w_off >= 0 && s.j_off >= 0;
        if (has_skin) {
            const Bytes& sv = pack.Find(s.skin)->data;
            PadTo4(bin, 0);
            jn_at = bin.size();
            for (uint32_t i = s.lo; i <= s.hi; ++i) {
                for (int c = 0; c < 4; ++c) {
                    const uint16_t j = Rd16(&sv[28 + size_t(i) * s.skin_stride + size_t(s.j_off) + size_t(c) * 2]);
                    bin.push_back(uint8_t(j)), bin.push_back(uint8_t(j >> 8));
                }
            }
            PadTo4(bin, 0);
            wt_at = bin.size();
            for (uint32_t i = s.lo; i <= s.hi; ++i) {
                for (int c = 0; c < 4; ++c) {
                    const float w = Clean(HalfToFloat(Rd16(&sv[28 + size_t(i) * s.skin_stride + size_t(s.w_off) + size_t(c) * 2])));
                    uint32_t u;
                    std::memcpy(&u, &w, 4);
                    Put32(bin, u);
                }
            }
        }
        // indices, relative to the first vertex
        PadTo4(bin, 0);
        const size_t idx_at = bin.size();
        for (uint32_t i = 0; i < s.count; ++i) {
            const uint32_t v = Rd16(&iv[28 + (size_t(s.first) + i) * 2]) - s.lo;
            bin.push_back(uint8_t(v)), bin.push_back(uint8_t(v >> 8));
        }
        const size_t idx_end = bin.size();
        auto view = [&](size_t at, size_t len, int target) {
            Json v = Json::Object();
            v.Set("buffer", 0), v.Set("byteOffset", int64_t(at)), v.Set("byteLength", int64_t(len)), v.Set("target", target);
            views.Push(v);
            return int(views.Items().size()) - 1;
        };
        const int pv_i = view(pos_at, s.vertices() * 12, 34962), iv_i = view(idx_at, idx_end - idx_at, 34963);
        const int uv_i = has_uv ? view(uv_at, s.vertices() * 8, 34962) : -1;
        const int nrm_i = has_nrm ? view(nrm_at, s.vertices() * 12, 34962) : -1;
        const int jn_i = has_skin ? view(jn_at, s.vertices() * 8, 34962) : -1;
        const int wt_i = has_skin ? view(wt_at, s.vertices() * 16, 34962) : -1;
        Json pa = Json::Object();
        pa.Set("bufferView", pv_i), pa.Set("componentType", 5126), pa.Set("count", int64_t(s.vertices())), pa.Set("type", "VEC3");
        Json jmn = Json::Array(), jmx = Json::Array();
        for (int c = 0; c < 3; ++c) jmn.Push(double(mn[c])), jmx.Push(double(mx[c]));
        pa.Set("min", jmn), pa.Set("max", jmx);
        accessors.Push(pa);
        Json ia = Json::Object();
        ia.Set("bufferView", iv_i), ia.Set("componentType", 5123), ia.Set("count", int64_t(s.count)), ia.Set("type", "SCALAR");
        accessors.Push(ia);
        const int pa_i = int(accessors.Items().size()) - 2;
        Json prim = Json::Object(), attrs = Json::Object();
        attrs.Set("POSITION", pa_i);
        if (has_uv) {
            Json ua = Json::Object();
            ua.Set("bufferView", uv_i), ua.Set("componentType", 5126), ua.Set("count", int64_t(s.vertices())), ua.Set("type", "VEC2");
            accessors.Push(ua);
            attrs.Set("TEXCOORD_0", int(accessors.Items().size()) - 1);
        }
        if (has_nrm) {
            Json na = Json::Object();
            na.Set("bufferView", nrm_i), na.Set("componentType", 5126), na.Set("count", int64_t(s.vertices())), na.Set("type", "VEC3");
            accessors.Push(na);
            attrs.Set("NORMAL", int(accessors.Items().size()) - 1);
        }
        if (has_skin) {
            Json ja = Json::Object(), wa = Json::Object();
            ja.Set("bufferView", jn_i), ja.Set("componentType", 5123), ja.Set("count", int64_t(s.vertices())), ja.Set("type", "VEC4");
            accessors.Push(ja);
            attrs.Set("JOINTS_0", int(accessors.Items().size()) - 1);
            wa.Set("bufferView", wt_i), wa.Set("componentType", 5126), wa.Set("count", int64_t(s.vertices())), wa.Set("type", "VEC4");
            accessors.Push(wa);
            attrs.Set("WEIGHTS_0", int(accessors.Items().size()) - 1);
        }
        prim.Set("attributes", attrs), prim.Set("indices", pa_i + 1), prim.Set("mode", 4);
        Json prims = Json::Array();
        prims.Push(prim);
        Json mesh = Json::Object();
        mesh.Set("name", s.name), mesh.Set("primitives", prims);
        meshes.Push(mesh);
        Json node = Json::Object();
        node.Set("name", s.name), node.Set("mesh", int(meshes.Items().size()) - 1);
        nodes.Push(node);
        scene.Push(int(nodes.Items().size()) - 1);
    }
    PadTo4(bin, 0);
    Json root = Json::Object(), asset = Json::Object(), sc = Json::Object(), scenes = Json::Array(), buffers = Json::Array(), buf = Json::Object();
    asset.Set("version", "2.0"), asset.Set("generator", "dcmod mesh");
    sc.Set("nodes", scene), scenes.Push(sc);
    buf.Set("byteLength", int64_t(bin.size())), buffers.Push(buf);
    root.Set("asset", asset), root.Set("scene", 0), root.Set("scenes", scenes), root.Set("nodes", nodes), root.Set("meshes", meshes);
    root.Set("accessors", accessors), root.Set("bufferViews", views), root.Set("buffers", buffers);
    std::string js = root.Dump(0);
    while (js.size() % 4) js.push_back(' ');
    Bytes out;
    Put32(out, 0x46546C67), Put32(out, 2), Put32(out, uint32_t(12 + 8 + js.size() + 8 + bin.size()));
    Put32(out, uint32_t(js.size())), Put32(out, 0x4E4F534A);
    out.insert(out.end(), js.begin(), js.end());
    Put32(out, uint32_t(bin.size())), Put32(out, 0x004E4942);
    out.insert(out.end(), bin.begin(), bin.end());
    return out;
}

struct Glb {
    Json json;
    Bytes bin;
};

Glb ReadGlb(const Bytes& d) {
    if (d.size() < 20 || Rd32(d.data()) != 0x46546C67) throw Error("not a .glb file");
    Glb g;
    size_t p = 12;
    while (p + 8 <= d.size()) {
        const uint32_t len = Rd32(&d[p]), type = Rd32(&d[p + 4]);
        if (p + 8 + len > d.size()) throw Error("damaged .glb");
        if (type == 0x4E4F534A) g.json = Json::Parse(std::string_view(reinterpret_cast<const char*>(&d[p + 8]), len));
        if (type == 0x004E4942) g.bin.assign(d.begin() + p + 8, d.begin() + p + 8 + len);
        p += 8 + len;
    }
    return g;
}

// GLB lengths/indices are bounded unsigned integers; never round untrusted JSON.
size_t GlbUnsigned(const Json& object, const char* key, bool required = true) {
    const Json* value = object.Find(key);
    if (!value && !required) return 0;
    if (!value || !value->IsNumber()) throw Error(std::string("GLB needs numeric ") + key);
    const double number = value->AsNumber();
    if (!std::isfinite(number) || number < 0 || number > UINT32_MAX || std::floor(number) != number)
        throw Error(std::string("invalid GLB unsigned field: ") + key);
    return size_t(number);
}

// Accessors must fit their declared view, not merely somewhere in the BIN chunk.
std::vector<double> Accessor(const Glb& g, size_t index, int comps) {
    const Json* accessors = g.json.Find("accessors"), *views = g.json.Find("bufferViews");
    if (!accessors || !views) throw Error("GLB needs accessors and bufferViews");
    const Json& acc = accessors->Items().at(index);
    const Json& view = views->Items().at(GlbUnsigned(acc, "bufferView"));
    const size_t offset = GlbUnsigned(view, "byteOffset", false), length = GlbUnsigned(view, "byteLength");
    const size_t relative = GlbUnsigned(acc, "byteOffset", false), n = GlbUnsigned(acc, "count");
    const size_t ct = GlbUnsigned(acc, "componentType");
    if (ct != 5126 && ct != 5125 && ct != 5123 && ct != 5121) throw Error("unsupported GLB component type");
    const char* shape = comps == 1 ? "SCALAR" : comps == 2 ? "VEC2" : comps == 3 ? "VEC3" : "VEC4";
    if (acc.GetString("type") != shape || acc.Has("sparse") || acc.GetBool("normalized"))
        throw Error("unsupported GLB accessor shape, sparse or normalized encoding");
    if (GlbUnsigned(view, "buffer") != 0) throw Error("GLB view must use embedded buffer zero");
    const size_t width = ct == 5126 || ct == 5125 ? 4 : ct == 5123 ? 2 : 1;
    const size_t element = width * size_t(comps);
    const size_t stride = view.Has("byteStride") ? GlbUnsigned(view, "byteStride") : element;
    if (offset > g.bin.size() || length > g.bin.size() - offset || relative > length ||
        !n || stride < element || stride % width || offset % width || relative % width ||
        length - relative < element || n - 1 > (length - relative - element) / stride)
        throw Error("GLB accessor outside its view or misaligned");
    const size_t base = offset + relative;
    std::vector<double> out;
    out.reserve(n * size_t(comps));
    for (size_t i = 0; i < n; ++i) {
        for (int c = 0; c < comps; ++c) {
            const uint8_t* q = &g.bin[base + i * stride + size_t(c) * width];
            if (ct == 5126) {
                float f;
                std::memcpy(&f, q, 4);
                out.push_back(f);
            } else {
                out.push_back(width == 4 ? Rd32(q) : width == 2 ? Rd16(q) : q[0]);
            }
        }
    }
    return out;
}

RpkPack LoadPack(const Args& args, const std::string& spec) {
    if (spec.empty() || spec[0] != '@') return RpkPack::Parse(ReadFile(spec));
    auto archive = OpenGame(args);
    const IndexEntry* e = archive->Find(GamePath(spec.substr(1)));
    if (!e) throw Error("not in the archive: " + spec);
    return RpkPack::Parse(archive->Read(*e));
}

// Writes the glTF object `prim` (positions `p`, texture coordinates, triangles) into the sub mesh. Vertices and triangles
// may be fewer or more than the game's. Extra vertices go to the end of the position and attribute buffers (the
// attribute data of the nearest old vertex is copied), extra triangles to the end of the index buffer.
// An identifier for a new vertex buffer: the type of vertex buffers and 48 bits taken from a hash of where it comes from.
uint64_t NewBufferId(const RpkPack& pack, uint64_t a, uint64_t b) {
    uint64_t h = 1469598103934665603ull;
    for (const uint64_t v : {a, b}) {
        for (int i = 0; i < 8; ++i) h = (h ^ ((v >> (8 * i)) & 255)) * 1099511628211ull;
    }
    for (;;) {
        const uint64_t id = (uint64_t(RpkTypeFromName("vertex_buffer")) << 48) | (h & 0xFFFFFFFFFFFFull);
        if (!pack.Find(id)) return id;
        h = h * 1099511628211ull + 1;
    }
}

// A vertex buffer without vertices that has the format of `src`. The payload carries its own identifier, which the game
// goes by: a copy that keeps the old one is taken for the old buffer.
RpkResource EmptyBufferLike(const RpkResource& src, uint64_t id) {
    RpkResource r;
    r.id = id, r.deps = src.deps, r.names = src.names, r.sources = src.sources;
    r.data.assign(src.data.begin(), src.data.begin() + 28);
    for (int i = 0; i < 8; ++i) r.data[8 + size_t(i)] = uint8_t(id >> (8 * i));
    std::memset(&r.data[24], 0, 4);
    return r;
}

void ImportSub(RpkPack& pack, const Sub& sub, const std::vector<Sub>& all, bool own_buffers, bool isolate_buffers, const Glb& g, const Json& prim, const std::vector<double>& p, const std::string& name) {
    const size_t nv_new = p.size() / 3;
    if (!nv_new || p.size() % 3 || nv_new > 65535) throw Error(name + ": needs 1..65535 complete vertices");
    for (double value : p) {
        if (!std::isfinite(value) || std::fabs(value) > (sub.half ? 65504.0 : double(std::numeric_limits<float>::max())))
            throw Error(name + ": position is non-finite or exceeds the destination format");
    }
    if (isolate_buffers && (!prim.Has("indices") || prim.GetInt("mode", 4) != 4))
        throw Error(name + ": isolated imports require an indexed triangle list");
    // Check indices before any floating-point-to-integer conversion or buffer edit.
    if (prim.Has("indices")) {
        const auto indices = Accessor(g, GlbUnsigned(prim, "indices"), 1);
        if (indices.empty() || indices.size() % 3) throw Error(name + ": indices are not a triangle list");
        for (double index : indices) {
            if (!std::isfinite(index) || index < 0 || std::floor(index) != index || index >= double(nv_new))
                throw Error(name + ": invalid triangle index");
        }
    }
    const auto stream_ids = RelocationStreams(sub);
    const bool extra_stream = std::any_of(sub.streams.begin(), sub.streams.end(), [&](uint64_t id) { return id != sub.attr && id != sub.skin; });
    if (!isolate_buffers && ((sub.normal_off >= 0 && sub.normal_buffer != sub.attr) ||
                            (sub.uv_off >= 0 && sub.uv_buffer != sub.attr) ||
                            (sub.tangent_off >= 0 && sub.tangent_buffer != sub.attr) || extra_stream || sub.partial_layout))
        throw Error(name + ": multi-stream or partial-layout import requires --isolate-buffers");
    if (isolate_buffers) {
        if (sub.normal_off < 0)
            throw Error(name + ": isolated imports require a decoded normal stream; run mesh scan with the same --dependency files");
        if (!sub.binding_problems.empty()) throw Error(name + ": " + sub.binding_problems.front());
        if (sub.skin && (sub.w_off < 0 || sub.j_off < 0)) throw Error(name + ": skin layout is not decoded");
        if (!IndependentMentions(pack, sub, all)) throw Error(name + ": buffer mentions are missing or shared with another slot");
        for (uint64_t id : stream_ids)
            if (VbCount(*pack.Find(id)) != VbCount(*pack.Find(sub.pos))) throw Error(name + ": stream vertex counts do not match");
    }
    std::vector<double> uvs;
    if (sub.uv_off >= 0 && prim.Find("attributes")->Has("TEXCOORD_0")) {
        uvs = Accessor(g, GlbUnsigned(*prim.Find("attributes"), "TEXCOORD_0"), 2);
        if (uvs.size() != nv_new * 2) uvs.clear();
    }
    RpkResource* vb = pack.Find(sub.pos);
    RpkResource* av = sub.attr ? pack.Find(sub.attr) : nullptr;
    RpkResource* sv = sub.skin && sub.skin != sub.attr ? pack.Find(sub.skin) : nullptr;  // a stream of weights and bones of its own
    const uint32_t nv = sub.vertices();
    // External geometry must not rewrite vertices shared by another scene part.
    const bool grow = nv_new > nv || isolate_buffers;
    if (isolate_buffers && !av) throw Error(name + ": cannot isolate without an attribute stream");
    if (isolate_buffers && sub.normal_off < 0)
        throw Error(name + ": isolated imports require a decoded normal stream; run mesh scan with the same --dependency files");
    if (isolate_buffers && prim.Find("attributes")->Has("TEXCOORD_0") && sub.uv_off < 0)
        throw Error(name + ": isolated import cannot preserve UVs; run mesh scan with the same --dependency files");
    // bone numbers and weights from the glTF object (JOINTS_0 and WEIGHTS_0)
    std::vector<double> jn, wt;
    if (sub.skin && prim.Find("attributes")->Has("JOINTS_0") && prim.Find("attributes")->Has("WEIGHTS_0")) {
        jn = Accessor(g, GlbUnsigned(*prim.Find("attributes"), "JOINTS_0"), 4);
        wt = Accessor(g, GlbUnsigned(*prim.Find("attributes"), "WEIGHTS_0"), 4);
        if (jn.size() != nv_new * 4 || wt.size() != nv_new * 4) jn.clear(), wt.clear();
    }
    const auto put_skin = [&](uint8_t* row, size_t i) {  // `row` is the vertex in the stream that holds the skin data
        if (jn.empty()) return;
        for (size_t c = 0; c < 4; ++c) {
            const uint16_t h = FloatToHalf(float(wt[i * 4 + c])), j = uint16_t(jn[i * 4 + c]);
            // a weight that is the same after rounding stays as it is
            if (HalfToFloat(h) != Clean(HalfToFloat(Rd16(row + sub.w_off + c * 2))) && h != Rd16(row + sub.w_off + c * 2)) std::memcpy(row + sub.w_off + c * 2, &h, 2);
            if (j != Rd16(row + sub.j_off + c * 2)) std::memcpy(row + sub.j_off + c * 2, &j, 2);
        }
    };
    const size_t csize = sub.half ? 2 : 4;
    const auto put_pos = [&](uint8_t* dst, size_t i) {
        for (size_t c = 0; c < 3; ++c) {
            const float f = float(p[i * 3 + c]);
            // a position that is the same after rounding stays as it is
            if (sub.half) {
                const uint16_t h = FloatToHalf(f);
                if (HalfToFloat(h) != Clean(HalfToFloat(Rd16(dst + c * 2))) && h != Rd16(dst + c * 2)) std::memcpy(dst + c * 2, &h, 2);
            } else {
                float old;
                std::memcpy(&old, dst + c * 4, 4);
                if (f != Clean(old)) std::memcpy(dst + c * 4, &f, 4);
            }
        }
    };
    // normals: from the glTF object when it has them, else (for vertices that are new) from its triangles
    std::vector<double> nrm;
    if (sub.normal_off >= 0 && prim.Find("attributes")->Has("NORMAL")) {
        nrm = Accessor(g, GlbUnsigned(*prim.Find("attributes"), "NORMAL"), 3);
        if (nrm.size() != nv_new * 3) nrm.clear();
    }
    std::vector<std::array<float, 3>> calc_n(nv_new, std::array<float, 3>{0, 0, 0}), calc_t(nv_new, std::array<float, 3>{0, 0, 0});
    if ((nv_new > nv || isolate_buffers) && prim.Has("indices")) {
        const std::vector<double> ix = Accessor(g, GlbUnsigned(prim, "indices"), 1);
        for (size_t t = 0; t + 2 < ix.size(); t += 3) {
            size_t v[3];
            for (size_t k = 0; k < 3; ++k) v[k] = size_t(ix[t + k]) < nv_new ? size_t(ix[t + k]) : 0;
            float e1[3], e2[3];
            for (size_t c = 0; c < 3; ++c) e1[c] = float(p[v[1] * 3 + c] - p[v[0] * 3 + c]), e2[c] = float(p[v[2] * 3 + c] - p[v[0] * 3 + c]);
            const float n[3] = {-(e1[1] * e2[2] - e1[2] * e2[1]), -(e1[2] * e2[0] - e1[0] * e2[2]), -(e1[0] * e2[1] - e1[1] * e2[0])};
            float tg[3] = {0, 0, 0};
            if (!uvs.empty()) {
                const float du1 = float(uvs[v[1] * 2] - uvs[v[0] * 2]), du2 = float(uvs[v[2] * 2] - uvs[v[0] * 2]);
                const float dv1 = -float(uvs[v[1] * 2 + 1] - uvs[v[0] * 2 + 1]), dv2 = -float(uvs[v[2] * 2 + 1] - uvs[v[0] * 2 + 1]);
                const float det = du1 * dv2 - du2 * dv1;
                if (std::fabs(det) > 1e-12f) {
                    for (size_t c = 0; c < 3; ++c) tg[c] = (e1[c] * dv2 - e2[c] * dv1) / det;
                }
            }
            for (size_t k = 0; k < 3; ++k) {
                for (size_t c = 0; c < 3; ++c) calc_n[v[k]][c] += n[c], calc_t[v[k]][c] += tg[c];
            }
        }
    }
    const auto put_frame = [&](uint8_t* dst, uint8_t* tangent_dst, size_t i) {
        if (sub.normal_off < 0) return;
        std::array<float, 3> n{};
        bool set = false;
        if (!nrm.empty()) {
            n = {float(nrm[i * 3]), float(nrm[i * 3 + 1]), float(nrm[i * 3 + 2])};
            // a normal that is the same after rounding stays as it is
            const auto old = Dec10(Rd32(dst + sub.normal_off));
            const float dot = old[0] * n[0] + old[1] * n[1] + old[2] * n[2];
            const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            const float lo = std::sqrt(old[0] * old[0] + old[1] * old[1] + old[2] * old[2]);
            if (l > 0 && (isolate_buffers || lo == 0 || dot / (l * lo) < 0.9995f)) set = true;
        } else if (i >= nv || isolate_buffers) {
            n = calc_n[i], set = true;
        }
        if (set) {
            const uint32_t u = (Rd32(dst + sub.normal_off) & 0xC0000000u) | Enc10(n);
            std::memcpy(dst + sub.normal_off, &u, 4);
        }
        if (sub.tangent_off >= 0 && (i >= nv || isolate_buffers) && !uvs.empty()) {
            std::array<float, 3> t = calc_t[i];
            const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (len > 0) {
                const float d = (t[0] * n[0] + t[1] * n[1] + t[2] * n[2]) / (len * len);
                for (size_t c = 0; c < 3; ++c) t[c] -= d * n[c];
                const uint32_t u = (Rd32(tangent_dst + sub.tangent_off) & 0xC0000000u) | Enc10(t);
                std::memcpy(tangent_dst + sub.tangent_off, &u, 4);
            }
        }
    };
    const auto put_uv = [&](uint8_t* dst, size_t i) {
        if (uvs.empty()) return;
        float old[2];
        const float now[2] = {float(uvs[i * 2]), 1.f - float(uvs[i * 2 + 1])};
        std::memcpy(old, dst + sub.uv_off, 8);
        old[0] = Clean(old[0]), old[1] = Clean(old[1]);  // a junk value (the export wrote 0 for it) counts as unchanged
        if (isolate_buffers || std::fabs(old[0] - now[0]) > 1e-6f * std::max(1.f, std::fabs(old[0])) || std::fabs(old[1] - now[1]) > 1e-6f * std::max(1.f, std::fabs(old[1]))) std::memcpy(dst + sub.uv_off, now, 8);
    };
    if (!grow) {
        for (uint32_t i = 0; i < nv_new; ++i) {
            put_pos(&vb->data[28 + size_t(sub.lo + i) * sub.pos_stride], i);
            if (av) put_uv(&av->data[28 + size_t(sub.lo + i) * sub.stride], i), put_frame(&av->data[28 + size_t(sub.lo + i) * sub.stride], &av->data[28 + size_t(sub.lo + i) * sub.stride], i);
            if (sv) put_skin(&sv->data[28 + size_t(sub.lo + i) * sub.skin_stride], i);
            else if (sub.skin && av) put_skin(&av->data[28 + size_t(sub.lo + i) * sub.stride], i);
        }
    }
    // A part that grows is written whole at the end of the buffers (its old vertices stay unused), so its vertices
    // stay one range. Vertices that are new get the other vertex data (colour, weights) of the old one that is nearest.
    // A part may have buffers of its own, so that it does not depend on room in the shared ones: always with --own-buffers,
    // and when the shared buffer is full. --isolate-buffers also relocates parts that do not grow.
    const bool own = grow && av && (own_buffers || isolate_buffers || uint64_t(VbCount(*vb)) + nv_new > 65535);
    std::map<uint64_t, uint64_t> destinations;
    for (uint64_t id : stream_ids) destinations[id] = id;
    if (own) {
        if (!IndependentMentions(pack, sub, all)) throw Error(name + ": cannot independently rebind every vertex stream");
        for (uint64_t id : stream_ids) {
            // Add sequentially so the allocator also checks earlier new IDs.
            const uint64_t fresh = NewBufferId(pack, id, sub.mesh ^ (uint64_t(sub.k) << 40));
            RpkResource copy = EmptyBufferLike(*pack.Find(id), fresh);
            pack.Add(std::move(copy));
            destinations[id] = fresh;
        }
        RpkResource* mesh = pack.Find(sub.mesh);
        for (uint64_t id : stream_ids) {
            // Find all locations before patching, including duplicate records
            // that may mention the same binding more than once.
            std::vector<size_t> spots;
            for (size_t rec : sub.recs) spots.push_back(size_t(LastMention(mesh->data, id, rec)));
            for (size_t at : spots) for (int byte = 0; byte < 8; ++byte)
                mesh->data[at + size_t(byte)] = uint8_t(destinations[id] >> (byte * 8));
            mesh->deps.push_back(destinations[id]);
        }
    }
    vb = pack.Find(sub.pos); // vector additions may have invalidated old resource pointers
    const uint32_t base = own ? 0 : VbCount(*vb);
    const uint32_t lo = grow ? base : sub.lo, hi = lo + uint32_t(nv_new) - 1;
    if (grow) {
        if (uint64_t(base) + nv_new > 65535) throw Error(name + ": the vertex buffer is full (16 bit indices)");
        for (uint64_t id : stream_ids)
            if (VbCount(*pack.Find(id)) != VbCount(*vb)) throw Error(name + ": cannot extend mismatched vertex streams");
        std::vector<std::array<float, 3>> old_pos(nv);
        for (uint32_t i = 0; i < nv; ++i) for (size_t c = 0; c < 3; ++c) {
            const uint8_t* q = &vb->data[28 + size_t(sub.lo + i) * sub.pos_stride + c * csize];
            if (sub.half) old_pos[i][c] = HalfToFloat(Rd16(q));
            else std::memcpy(&old_pos[i][c], q, 4);
            if (!std::isfinite(old_pos[i][c])) throw Error(name + ": nonfinite donor position");
        }
        for (size_t j = 0; j < nv_new; ++j) {
            uint32_t best = uint32_t(j);
            if (j >= nv) {
                double best_d = std::numeric_limits<double>::infinity();
                for (uint32_t i = 0; i < nv; ++i) {
                    double d = 0;
                    for (size_t c = 0; c < 3; ++c) { const double delta = old_pos[i][c] - p[j*3+c]; d += delta*delta; }
                    if (d < best_d) best_d = d, best = i;
                }
            }
            // Copy entire stride-sized records, preserving every opaque byte.
            for (uint64_t id : stream_ids) {
                const RpkResource* source = pack.Find(id);
                RpkResource* dest = pack.Find(destinations.at(id));
                const size_t stride = Rd16(source->data.data() + 22), at = 28 + size_t(sub.lo + best) * stride;
                Bytes row(source->data.begin() + at, source->data.begin() + at + stride);
                dest->data.insert(dest->data.end(), row.begin(), row.end());
            }
            const auto row = [&](uint64_t id) -> uint8_t* {
                if (!id) return nullptr;
                RpkResource* resource = pack.Find(destinations.at(id));
                const size_t stride = Rd16(resource->data.data() + 22);
                return &resource->data[resource->data.size() - stride];
            };
            put_pos(row(sub.pos), j);
            if (sub.uv_off >= 0) put_uv(row(sub.uv_buffer), j);
            if (sub.normal_off >= 0) put_frame(row(sub.normal_buffer), row(sub.tangent_buffer), j);
            if (sub.skin) put_skin(row(sub.skin), j);
        }
        const uint32_t total = base + uint32_t(nv_new);
        for (const auto& binding : destinations) std::memcpy(&pack.Find(binding.second)->data[24], &total, 4);
    }
    uint32_t first = sub.first, count = sub.count;
    if (prim.Has("indices")) {
        const std::vector<double> idx = Accessor(g, GlbUnsigned(prim, "indices"), 1);
        if (idx.empty() || idx.size() % 3) throw Error(name + ": the indices are not a triangle list");
        RpkResource* ib = pack.Find(sub.ib);
        Bytes raw;
        for (const double d : idx) {
            if (d >= double(nv_new)) throw Error(name + ": a triangle uses a vertex that does not exist");
            const uint32_t v = lo + uint32_t(d);
            raw.push_back(uint8_t(v)), raw.push_back(uint8_t(v >> 8));
        }
        count = uint32_t(idx.size());
        if (count > sub.count || grow) {
            first = VbCount(*ib);
            if (uint64_t(first) + count > 0xFFFFFFFFull / 2) throw Error(name + ": the index buffer is full");
            ib->data.insert(ib->data.end(), raw.begin(), raw.end());
            const uint32_t total = first + count;
            std::memcpy(&ib->data[24], &total, 4);
        } else {
            std::memcpy(&ib->data[28 + size_t(first) * 2], raw.data(), raw.size());
        }
    }
    if (first == sub.first && count == sub.count && lo == sub.lo && hi == sub.hi) return;
    // The mesh keeps the range twice: in its records (02 03, lowest vertex, highest vertex, first index, count) and in a
    // table at its end (03 00 00 00, lowest vertex, 0, first index, count) that the game draws from.
    RpkResource* mesh = pack.Find(sub.mesh);
    for (const size_t at : sub.recs) {
        std::memcpy(&mesh->data[at + 2], &lo, 4);
        std::memcpy(&mesh->data[at + 6], &hi, 4);
        std::memcpy(&mesh->data[at + 10], &first, 4);
        std::memcpy(&mesh->data[at + 14], &count, 4);
    }
    uint8_t pat[20] = {3, 0, 0, 0};
    const uint32_t fields[4] = {sub.lo, 0, sub.first, sub.count};
    std::memcpy(pat + 4, fields, 16);
    size_t tail = 0;
    for (size_t i = 0; i + 20 <= mesh->data.size(); ++i) {
        if (std::memcmp(&mesh->data[i], pat, 20) != 0) continue;
        std::memcpy(&mesh->data[i + 4], &lo, 4);
        std::memcpy(&mesh->data[i + 12], &first, 4);
        std::memcpy(&mesh->data[i + 16], &count, 4);
        ++tail;
    }
    if (!tail) throw Error(name + ": the mesh has no draw table entry for this sub mesh");
}

}  // namespace

int RunMesh(const std::vector<std::string>& argv) {
    if (argv.empty()) return Usage();
    const std::string cmd = argv[0];
    const Args args = ParseArgs(argv, {"--update", "--base", "--game", "-o", "--output", "--dependency", "--target"});
    if (args.Has("--target") && cmd != "scan") throw Error("--target is supported only by mesh scan");
    if (args.positional.size() < 2) return Usage();
    RpkPack pack = LoadPack(args, args.positional[1]);
    const auto dependencies = args.All("--dependency");
    // An output path must never alias the target or any supplied dependency,
    // including symlinks/hardlinks. No discovery or recursive loading by name.
    const std::string output = args.Get("-o", args.Get("--output"));
    const auto protect = [&](const std::string& input) {
        if (output.empty() || input.empty() || input[0] == '@') return;
        if (fs::weakly_canonical(output) == fs::weakly_canonical(input) ||
            (fs::exists(output) && fs::exists(input) && fs::equivalent(output, input)))
            throw Error("output aliases read-only input: " + input);
    };
    protect(args.positional[1]);
    MeshFormats formats;
    formats.Add(pack, args.positional[1]);
    Json dependency_report = Json::Array();
    for (const auto& path : dependencies) {
        if (path.empty() || path[0] == '@') throw Error("--dependency needs an explicit local RPK file");
        protect(path);
        try {
            const RpkPack dependency = RpkPack::Parse(ReadFile(path));
            formats.Add(dependency, path);
            Json entry = Json::Object(), required = Json::Array();
            entry.Set("path", path);
            for (const auto& name : dependency.required) required.Push(name);
            entry.Set("required_packs", required);
            dependency_report.Push(entry);
        } catch (const Error& e) {
            throw Error("dependency " + path + ": " + e.what());
        }
    }
    std::vector<Sub> subs = FindSubs(pack, formats);

    if (cmd == "scan") {
        if (args.Has("-o") || args.Has("--output")) throw Error("scan writes JSON to stdout only; redirect to a new report file");
        Json report = ScanMeshes(pack, subs, formats, args.All("--target"));
        report.Set("dependency_packs", dependency_report);
        report.Set("resolution_scope", "stream_format IDs only; explicit local files; conflicting payloads/dependencies rejected; no resource merging or recursive loading");
        const std::string json = report.Dump();
        std::fputs(json.c_str(), stdout);
        return 0;
    }
    if (cmd == "info") {
        size_t tris = 0;
        for (const Sub& s : subs) {
            std::printf("%-60s %6u vertices %7u triangles\n", s.name.c_str(), s.vertices(), s.count / 3);
            tris += s.count / 3;
        }
        std::printf("%zu sub meshes, %zu triangles\n", subs.size(), tris);
        return 0;
    }
    if (cmd == "export") {
        const std::string out = args.Get("-o", args.Get("--output"));
        if (out.empty()) throw Error("export needs -o FILE.glb");
        if (args.positional.size() > 2) {
            std::vector<Sub> keep;
            for (const Sub& s : subs) {
                for (size_t i = 2; i < args.positional.size(); ++i) {
                    if (WildMatch(Lower(args.positional[i]), Lower(s.name))) {
                        keep.push_back(s);
                        break;
                    }
                }
            }
            subs = keep;
        }
        if (subs.empty()) throw Error("no sub meshes to write");
        WriteFile(out, MakeGlb(pack, subs));
        std::printf("wrote %zu sub meshes to %s\n", subs.size(), out.c_str());
        return 0;
    }
    if (cmd == "materials") {
        // the materials of the pack, or those of the mesh of one sub mesh (the ones it can be given)
        const int mat_type = RpkTypeFromName("material");
        const std::string of = args.positional.size() > 2 ? args.positional[2] : "";
        const RpkResource* only = nullptr;
        if (!of.empty()) {
            for (const Sub& sb : subs) {
                if (sb.name == of || StartsWith(sb.name, of + "_")) only = pack.Find(sb.mesh);
            }
            if (!only) throw Error("no sub mesh " + of);
        }
        for (const RpkResource& r : pack.resources) {
            if (int(r.Type()) != mat_type) continue;
            if (only && std::find(only->deps.begin(), only->deps.end(), r.id) == only->deps.end()) continue;
            std::printf("%s  %s\n", HexId(r.id).c_str(), r.DisplayName().c_str());
        }
        return 0;
    }
    if (cmd == "material") {
        // dcmod mesh material PACK TARGET SOURCE -o OUT: the sub mesh TARGET is drawn with the material of the sub mesh SOURCE
        // (TARGET and SOURCE are names as `info` shows them, or only ID_N), or with a material of the pack given by its identifier
        const std::string out = args.Get("-o", args.Get("--output"));
        if (out.empty() || args.positional.size() < 4) throw Error("material needs TARGET SOURCE and -o OUT");
        const auto find_sub = [&](const std::string& n) -> const Sub* {
            for (const Sub& s : subs) {
                if (s.name == n || StartsWith(s.name, n + "_")) return &s;
            }
            return nullptr;
        };
        const Sub* target = find_sub(args.positional[2]);
        if (!target) throw Error("no sub mesh " + args.positional[2]);
        const int mat_type = RpkTypeFromName("material");
        const auto material_of = [&](const Sub& sb, size_t rec) -> std::pair<long, uint64_t> {
            const RpkResource* m = pack.Find(sb.mesh);
            long best = -1;
            uint64_t who = 0;
            for (const uint64_t d : m->deps) {
                const RpkResource* r = pack.Find(d);
                if (!r || int(r->Type()) != mat_type) continue;
                const long at = LastMention(m->data, d, rec);
                if (at > best) best = at, who = d;
            }
            return {best, who};
        };
        uint64_t wanted = 0;
        if (const Sub* source = find_sub(args.positional[3])) {
            wanted = material_of(*source, source->recs.front()).second;
        } else {
            const RpkResource* r = pack.Find(ParseHexId(args.positional[3]));
            if (!r || int(r->Type()) != mat_type) throw Error(args.positional[3] + " is neither a sub mesh nor a material of the pack");
            wanted = r->id;
        }
        if (!wanted) throw Error("the source has no material");
        size_t prev_end = 0;
        for (const Sub& o : subs) {
            if (o.mesh != target->mesh) continue;
            for (const size_t r : o.recs) {
                if (r < target->recs.front() && r + 18 > prev_end) prev_end = r + 18;
            }
        }
        RpkResource* mesh = pack.Find(target->mesh);
        for (const size_t rec : target->recs) {
            const long at = material_of(*target, rec).first;
            if (at < long(prev_end)) throw Error("the sub mesh shares its material with the sub mesh before it in the mesh, so it cannot get another");
            for (int i = 0; i < 8; ++i) mesh->data[size_t(at) + size_t(i)] = uint8_t(wanted >> (8 * i));
        }
        // A material that the mesh does not list does not work: adding it to the dependencies (with the count of materials in the
        // header raised) made the game hang while loading, so only materials the mesh already has are accepted.
        if (std::find(mesh->deps.begin(), mesh->deps.end(), wanted) == mesh->deps.end()) throw Error("the mesh of that sub mesh does not use the material " + HexId(wanted) + "; choose one of its own (dcmod mesh materials PACK SUBMESH)");
        WriteFile(out, pack.Write());
        std::printf("%s uses the material %s (%s); written to %s\n", target->name.c_str(), HexId(wanted).c_str(), pack.Find(wanted)->DisplayName().c_str(), out.c_str());
        return 0;
    }
    if (cmd == "import") {
        const std::string out = args.Get("-o", args.Get("--output"));
        if (out.empty() || args.positional.size() < 3) throw Error("import needs FILE.glb and -o OUT.rpk");
        const Glb g = ReadGlb(ReadFile(args.positional[2]));
        const Json* nodes = g.json.Find("nodes");
        const Json* meshes = g.json.Find("meshes");
        if (!nodes || !meshes) throw Error("the file has no objects");
        size_t used = 0;
        for (const Json& node : nodes->Items()) {
            const std::string name = node.GetString("name");
            if (name.size() < 18 || name[16] != '_' || !node.Has("mesh")) continue;
            uint64_t id = 0;
            try {
                id = ParseHexId(name.substr(0, 16));
            } catch (const Error&) {
                continue;
            }
            const size_t us = name.find('_', 17);
            const uint32_t k = uint32_t(std::strtoul(name.substr(17, us == std::string::npos ? std::string::npos : us - 17).c_str(), nullptr, 10));
            const Sub* sub = nullptr;
            for (const Sub& s : subs) {
                if (s.mesh == id && s.k == k) sub = &s;
            }
            if (!sub) continue;
            const Json* primitives = meshes->Items().at(GlbUnsigned(node, "mesh")).Find("primitives");
            if (!primitives) throw Error(name + ": GLB mesh needs primitives");
            if (primitives->Items().size() != 1) throw Error(name + ": GLB import needs exactly one primitive per mesh");
            const Json& prim = primitives->Items().at(0);
            if (!prim.Find("attributes") || !prim.Find("attributes")->Has("POSITION"))
                throw Error(name + ": GLB primitive needs POSITION attributes");
            const std::vector<double> p = Accessor(g, GlbUnsigned(*prim.Find("attributes"), "POSITION"), 3);
            ImportSub(pack, *sub, subs, args.Has("--own-buffers"), args.Has("--isolate-buffers"), g, prim, p, name);
            ++used;
        }
        if (!used) throw Error("none of the objects in the file came from this pack (their names start with the mesh identifier)");
        WriteFile(out, pack.Write());
        std::printf("updated %zu sub meshes, wrote %s\n", used, out.c_str());
        return 0;
    }
    return Usage();
}

}  // namespace dcmod
