// obj_model.h — minimal Wavefront OBJ loader for the game's models.
//
// The shipped models (USRDIR/data/models/care/*.obj) are small
// v/vt/vn triangle meshes with .mtl companions and DDS textures.
// This loader reads geometry only (positions + normals, faces
// triangulated by fan); texture coordinates are kept for the
// DDS-textured scene renderer.
// Indices in OBJ are 1-based (with negative = relative support).

#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace comet {

struct ObjModel {
    struct Triangle {
        // Indices into positions/normals (normals parallel to
        // positions when the file provides vn; otherwise face
        // normals are computed into faceNormals).
        int v[3];
        int t[3];
        int n[3];
    };
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> normals;
    std::vector<std::array<float, 2>> uvs;
    std::vector<Triangle> triangles;
    std::vector<std::array<float, 3>> faceNormals; // parallel to triangles
    float radius = 0.0f; // bounding sphere radius around the origin

    bool empty() const { return triangles.empty(); }
    bool loadFromText(const char* text, std::size_t size);
    bool loadFromFile(const char* path);
};

} // namespace comet
