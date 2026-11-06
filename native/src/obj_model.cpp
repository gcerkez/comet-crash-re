// obj_model.cpp — see obj_model.h.

#include "obj_model.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace comet {
namespace {

std::array<float, 3> sub(const std::array<float, 3>& a,
                         const std::array<float, 3>& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

std::array<float, 3> cross(const std::array<float, 3>& a,
                           const std::array<float, 3>& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]};
}

void normalize(std::array<float, 3>& v) {
    const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 0.0f) {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
}

// Resolves a 1-based (or negative relative) OBJ index.
int resolveIndex(int idx, std::size_t count) {
    if (idx > 0) {
        return idx - 1;
    }
    if (idx < 0) {
        return static_cast<int>(count) + idx;
    }
    return -1;
}

} // namespace

bool ObjModel::loadFromText(const char* text, std::size_t size) {
    positions.clear();
    normals.clear();
    triangles.clear();
    faceNormals.clear();
    radius = 0.0f;

    const std::string src(text, size);
    std::size_t pos = 0;
    while (pos < src.size()) {
        std::size_t eol = src.find('\n', pos);
        if (eol == std::string::npos) {
            eol = src.size();
        }
        const std::string line = src.substr(pos, eol - pos);
        pos = eol + 1;
        if (line.size() < 2) {
            continue;
        }
        if (line[0] == 'v' && line[1] == ' ') {
            float x, y, z;
            if (std::sscanf(line.c_str(), "v %f %f %f", &x, &y, &z) == 3) {
                positions.push_back({x, y, z});
                const float r = std::sqrt(x * x + y * y + z * z);
                if (r > radius) {
                    radius = r;
                }
            }
        } else if (line[0] == 'v' && line[1] == 'n') {
            float x, y, z;
            if (std::sscanf(line.c_str(), "vn %f %f %f", &x, &y, &z) == 3) {
                normals.push_back({x, y, z});
            }
        } else if (line[0] == 'v' && line[1] == 't') {
            float u, v;
            if (std::sscanf(line.c_str(), "vt %f %f", &u, &v) == 2) {
                uvs.push_back({u, v});
            }
        } else if (line[0] == 'f' && line[1] == ' ') {
            // Face corners in v, v/vt, v//vn, or v/vt/vn form.
            std::vector<int> vs;
            std::vector<int> ts;
            std::vector<int> ns;
            const char* p = line.c_str() + 2;
            while (*p != '\0') {
                while (*p == ' ' || *p == '\t' || *p == '\r') {
                    ++p;
                }
                if (*p == '\0') {
                    break;
                }
                char* end = nullptr;
                const long vi = std::strtol(p, &end, 10);
                if (end == p) {
                    break;
                }
                p = end;
                long ni = 0;
                long ti = 0;
                if (*p == '/') {
                    ++p;
                    if (*p != '/') {
                        ti = std::strtol(p, &end, 10);
                        p = end;
                    }
                    if (*p == '/') {
                        ++p;
                        ni = std::strtol(p, &end, 10);
                        p = end;
                    }
                }
                vs.push_back(resolveIndex(static_cast<int>(vi), positions.size()));
                ts.push_back(ti != 0
                                 ? resolveIndex(static_cast<int>(ti), uvs.size())
                                 : -1);
                ns.push_back(ni != 0
                                 ? resolveIndex(static_cast<int>(ni), normals.size())
                                 : -1);
            }
            for (std::size_t k = 1; k + 1 < vs.size(); ++k) {
                Triangle t;
                t.v[0] = vs[0];
                t.v[1] = vs[k];
                t.v[2] = vs[k + 1];
                t.t[0] = ts[0];
                t.t[1] = ts[k];
                t.t[2] = ts[k + 1];
                t.n[0] = ns[0];
                t.n[1] = ns[k];
                t.n[2] = ns[k + 1];
                if (t.v[0] < 0 || t.v[1] < 0 || t.v[2] < 0) {
                    continue;
                }
                triangles.push_back(t);
            }
        }
    }
    if (triangles.empty()) {
        return false;
    }
    faceNormals.reserve(triangles.size());
    for (const Triangle& t : triangles) {
        std::array<float, 3> n = cross(sub(positions[t.v[1]], positions[t.v[0]]),
                                       sub(positions[t.v[2]], positions[t.v[0]]));
        normalize(n);
        faceNormals.push_back(n);
    }
    return true;
}

bool ObjModel::loadFromFile(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (len <= 0) {
        std::fclose(f);
        return false;
    }
    std::string buf(static_cast<std::size_t>(len), '\0');
    const std::size_t got = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    return got == buf.size() && loadFromText(buf.data(), buf.size());
}

} // namespace comet
