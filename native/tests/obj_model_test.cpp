// obj_model_test.cpp — parses a tiny inline OBJ (the game's format:
// v/vt/vn triangle faces) and checks counts, indices, and normals.

#include "../src/obj_model.h"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

const char* kObj =
    "# tiny fighter stand-in\n"
    "v 0 0 0\n"
    "v 1 0 0\n"
    "v 0 1 0\n"
    "v 0 0 1\n"
    "vt 0 0\n"
    "vn 0 0 1\n"
    "f 1/1/1 2/1/1 3/1/1\n"
    "f 1/1/1 4/1/1 2/1/1\n";

} // namespace

int main() {
    comet::ObjModel m;
    check(m.loadFromText(kObj, std::strlen(kObj)), "parses");
    check(m.positions.size() == 4, "4 vertices");
    check(m.triangles.size() == 2, "2 triangles");
    if (m.triangles.size() == 2) {
        check(m.triangles[0].v[0] == 0 && m.triangles[0].v[2] == 2,
              "indices are 0-based");
        // First face lies in z=0 plane: face normal is +Z or -Z.
        check(m.faceNormals[0][2] == 1.0f || m.faceNormals[0][2] == -1.0f,
              "face normal along Z");
    }
    check(m.radius == 1.0f, "bounding radius");

    comet::ObjModel empty;
    check(!empty.loadFromText("v 0 0 0\n", 8), "no faces = failure");

    if (failures == 0) {
        std::printf("obj_model_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "obj_model_test: %d failure(s)\n", failures);
    return 1;
}
