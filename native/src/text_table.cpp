// text_table.cpp — see text_table.h.

#include "text_table.h"

#include "language1.inc"

namespace comet::text {

const char* text(int index) {
    if (index < 0 || index >= kLanguage1Count) {
        return "";
    }
    return kLanguage1[index];
}

} // namespace comet::text
