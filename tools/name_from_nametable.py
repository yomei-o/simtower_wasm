"""Take the resource names from SimTower's own name table.

SimTower carries a Win16 resource-name table - resource type 15, one entry -
and it holds exactly what the port needs and the generated header lacks: the
four-character names of the private resource types, and the string names of the
handful of resources the port looks up by name (TOWER_MENU, TOWER_TITLE,
TOWER_APPICON, TOWER_FILEICON).

That makes the mapping a fact to be read rather than a thing to be derived.  An
earlier pass inferred the private type names from id ranges and content and got
eight of eleven right; this replaces that guesswork, and cross-checks it.

Each entry is a length, a type id, a resource id, and then names as
NUL-terminated ASCII inside the remainder.  A type id or resource id with the
high bit set is an ordinal; the strings that follow name whichever of the two
was not an ordinal.

    python tools/name_from_nametable.py NAMETABLE_1.nametable header.hpp
"""

import argparse
import re
import struct
import sys

STANDARD = {
    1: "CURSOR", 2: "BITMAP", 3: "ICON", 4: "MENU", 5: "DIALOG",
    6: "STRING", 9: "ACCELERATOR", 10: "RCDATA", 12: "GROUP_CURSOR",
    14: "GROUP_ICON", 15: "NAMETABLE",
}


def parse(data):
    """(type_names, resource_names) - {type_number: name}, {(type, id): name}."""
    type_names = {}
    resource_names = {}

    position = 0
    while position + 6 <= len(data):
        length = struct.unpack_from("<H", data, position)[0]
        if length == 0 or position + length > len(data):
            break
        raw_type, raw_id = struct.unpack_from("<HH", data, position + 2)

        # Whatever is left in the entry is names, NUL-terminated.  Read them
        # that way rather than assuming a field layout: the entry carries one
        # name when only one of the two ids is an ordinal, and two when neither
        # is, and going by the NULs is right in both cases.
        tail = data[position + 6:position + length]
        names = [part.decode("ascii", "replace")
                 for part in tail.split(b"\x00")
                 if part and all(32 <= byte < 127 for byte in part)]

        type_number = raw_type & 0x7FFF
        id_number = raw_id & 0x7FFF

        # Which of the two the name belongs to is decided by the type, not by
        # the ordinal bits: both ids are ordinals on a private type, and the
        # name it carries is the type's own - every ALRT entry says "ALRT".
        # On a standard type the name is the resource's, which is where
        # TOWER_MENU and TOWER_APPICON come from.
        if names:
            if type_number >= 0x7F00:
                type_names.setdefault(type_number, names[0])
            else:
                type_names.setdefault(type_number, STANDARD.get(type_number, ""))
                resource_names[(type_number, id_number)] = names[0]

        position += length

    return type_names, resource_names


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("nametable")
    parser.add_argument("header")
    parser.add_argument("--report", action="store_true",
                        help="print the mapping and change nothing")
    args = parser.parse_args()

    with open(args.nametable, "rb") as handle:
        type_names, resource_names = parse(handle.read())

    private = {number: name for number, name in sorted(type_names.items())
               if number >= 0x7F00 and name}

    if args.report:
        print("private types:")
        for number, name in sorted(private.items()):
            print("  %d (0x%04X)  %s" % (number, number, name))
        print("named resources:")
        for (type_number, id_number), name in sorted(resource_names.items()):
            print("  type %-6d id %-6d %s" % (type_number, id_number, name))
        return

    with open(args.header, encoding="utf-8") as handle:
        text = handle.read()

    # Types first: the generated header calls them TYPE_<n>.
    renamed_types = 0
    for number, name in sorted(private.items()):
        pattern = '{"TYPE_%d"' % number
        count = text.count(pattern)
        if count:
            text = text.replace(pattern, '{"%s"' % name)
            renamed_types += count

    # Then the string ids, which the header leaves empty.  The port looks these
    # up by name, so without them every such lookup returns nothing.
    named = 0

    def fill(match):
        nonlocal named
        type_name, id_text, string_id, rest = match.groups()
        if string_id:
            return match.group(0)
        number = next((n for n, label in {**STANDARD, **private}.items()
                       if label == type_name), None)
        if number is None:
            return match.group(0)
        name = resource_names.get((number, int(id_text)))
        if not name:
            return match.group(0)
        named += 1
        return '{"%s", %s, "%s",%s' % (type_name, id_text, name, rest)

    text = re.sub(r'\{"([A-Z_0-9]+)", (-?\d+), "([^"]*)",(.*)', fill, text)

    with open(args.header, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)

    print("%s: %d type(s) named, %d string id(s) filled"
          % (args.header, renamed_types, named))

    leftover = sorted({int(n) for n in re.findall(r'\{"TYPE_(\d+)"', text)})
    if leftover:
        print("still numeric: %s"
              % ", ".join("TYPE_%d" % number for number in leftover))
    if renamed_types == 0 and named == 0:
        print("nothing changed - already named, or the wrong header",
              file=sys.stderr)


if __name__ == "__main__":
    main()
