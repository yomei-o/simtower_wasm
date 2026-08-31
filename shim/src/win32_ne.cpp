// Resources straight out of a New Executable, in the browser.
//
// The build can make a resource pack with the upstream Python tools, but a pack
// cannot be published: it is the original game's artwork, and it belongs to its
// rights holders.  Parsing the executable here instead means the page asks the
// player for their own SIMTOWER.EXE and nothing copyrighted is ever committed or
// served.
//
// The NE resource table is small and rigid: a shift count, then a run of type
// blocks, each with a run of name entries carrying an offset and a length in
// shifted units.

#include "win32_internal.h"

#include <cstdio>
#include <cstring>

namespace shim {

namespace {

struct Entry {
    uint16_t type;      // 1 CURSOR, 2 BITMAP, ... 10 RCDATA
    uint16_t id;
    uint32_t offset;
    uint32_t size;
};

std::vector<BYTE> g_image;
std::vector<Entry> g_entries;

uint16_t rd16(const BYTE * p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

const char * typeNameFor(uint16_t type) {
    switch (type) {
        case 1:  return "CURSOR";
        case 2:  return "BITMAP";
        case 3:  return "ICON";
        case 4:  return "MENU";
        case 5:  return "DIALOG";
        case 6:  return "STRING";
        case 9:  return "ACCELERATOR";
        case 10: return "RCDATA";
        case 12: return "GROUP_CURSOR";
        case 14: return "GROUP_ICON";
        default: return "OTHER";
    }
}

}   // namespace

// Returns false with a reason on stderr rather than throwing: the caller is a
// file the player chose, and being told which way it is wrong is the point.
bool loadResourcesFromExecutable(const BYTE * data, size_t size) {
    g_entries.clear();

    if (size < 0x40 || data[0] != 'M' || data[1] != 'Z') {
        fprintf(stderr, "not an executable (no MZ header)\n");
        return false;
    }
    const uint32_t neOffset = rd16(data + 0x3c) | ((uint32_t)rd16(data + 0x3e) << 16);
    if (neOffset + 0x40 > size) {
        fprintf(stderr, "truncated: the new-executable header is past the end\n");
        return false;
    }
    if (data[neOffset] != 'N' || data[neOffset + 1] != 'E') {
        // A 32-bit repackaging says PE here, and its resources are laid out
        // completely differently.  Saying so is more use than failing quietly.
        fprintf(stderr, "not a 16-bit Windows executable: expected NE, found %c%c."
                        "  This needs SIMTOWER.EXE from the Windows 3.1 release.\n",
                data[neOffset], data[neOffset + 1]);
        return false;
    }

    const uint32_t tableOffset = neOffset + rd16(data + neOffset + 0x24);
    if (tableOffset + 2 > size) {
        fprintf(stderr, "truncated: no resource table\n");
        return false;
    }

    const uint16_t shift = rd16(data + tableOffset);
    size_t p = tableOffset + 2;

    while (p + 8 <= size) {
        const uint16_t typeId = rd16(data + p);
        const uint16_t count = rd16(data + p + 2);
        if (typeId == 0) break;
        p += 8;

        for (uint16_t i = 0; i < count && p + 12 <= size; i++, p += 12) {
            Entry e{};
            // The high bit marks an integer rather than a string, for both the
            // type and the name; only integers are used here.
            e.type = typeId & 0x7fff;
            e.id = rd16(data + p + 6) & 0x7fff;
            e.offset = (uint32_t)rd16(data + p) << shift;
            e.size = (uint32_t)rd16(data + p + 2) << shift;
            if (e.offset + e.size > size) continue;
            g_entries.push_back(e);
        }
    }

    if (g_entries.empty()) {
        fprintf(stderr, "no resources found in the executable\n");
        return false;
    }

    g_image.assign(data, data + size);
    state().pack = g_image.data();
    state().packSize = g_image.size();
    printf("SIMTOWER.EXE: %d resources\n", (int)g_entries.size());
    return true;
}

size_t resourceCount() { return g_entries.size(); }

const BYTE * resourceAt(size_t index, size_t * size) {
    if (index >= g_entries.size() || g_image.empty()) return nullptr;
    if (size) *size = g_entries[index].size;
    return g_image.data() + g_entries[index].offset;
}

const char * resourceType(size_t index) {
    if (index >= g_entries.size()) return "";
    return typeNameFor(g_entries[index].type);
}

int resourceId(size_t index) {
    if (index >= g_entries.size()) return 0;
    return g_entries[index].id;
}

// The type as the resource table numbers it, which is what FindResource is given
// once MAKEINTRESOURCE has been undone.
size_t findResourceIndex(uint16_t type, uint16_t id) {
    for (size_t i = 0; i < g_entries.size(); i++)
        if (g_entries[i].type == type && g_entries[i].id == id)
            return i + 1;
    return 0;
}

}   // namespace shim
