"""Give SimTower's private resource types the names the port asks for.

The port looks resources up by a four-character type name - find("PART", 1000),
find("WAVE", id) - but the generated table calls those types TYPE_32513 and so
on, so every one of those lookups returns nothing and the port stops at
"Truncated PART/1000 resource".

Neither side is wrong.  In a New Executable a type id with the high bit set is
an integer, and SimTower's private types really are 0xFF01..0xFF0B, so
inspect_ne.py is right to report 32513..32523.  The four-character names live in
the original's own source and the port carries them; the public tools do not
have the mapping between the two.

This is that mapping, and every entry is pinned by the bytes or by an id set
only one type can satisfy - not by which id range looked plausible, which is how
three of them came out wrong the first time:

  32513 ALRT  entry 1001 reads exactly as parse_original_alert expects: little
              endian 0x0003 button mode, 0x0001 preserved word, then the
              NUL-terminated message "Save t..."
  32514 CGPK  ids 2536, 2600, 2664, 4073 match the port's calls exactly
  32515 CLUT  the only type whose entries are 2048 bytes, which is the minimum
              native_main.cpp requires of CLUT/1000
  32516 DTMP  the only type containing id 3040; its entries also begin with
              their own id as text
  32517 PART  one entry, id 1000, and PART is only ever asked for at 1000
  32518 STRL  the only type containing every id the port asks STRL for
  32519 TABL  the port reads TABL at 1001..1006, and this is the only type whose
              1001..1006 all parse as big-endian word tables
  32520 TABM  read at 1000 + tabm_number with tabm_number >= 1, so 1001 up; all
              twenty-two entries parse as big-endian word tables
  32521 TEXT  one entry, id 128, and the port asks TEXT for exactly 128
  32522 WAVE  every entry begins "RIFF....WAVE"
  32523 YEN   exactly three entries, 1000-1002, matching the three ids used

Run against the generated header after build_resource_pack.py.
"""

import argparse
import re
import sys

NAMES = {
    32513: "ALRT",
    32514: "CGPK",
    32515: "CLUT",
    32516: "DTMP",
    32517: "PART",
    32518: "STRL",
    32519: "TABL",
    32520: "TABM",
    32521: "TEXT",
    32522: "WAVE",
    32523: "YEN",
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("header")
    args = parser.parse_args()

    with open(args.header, encoding="utf-8") as handle:
        text = handle.read()

    renamed = 0
    for number, name in sorted(NAMES.items()):
        pattern = '{"TYPE_%d"' % number
        count = text.count(pattern)
        if count:
            text = text.replace(pattern, '{"%s"' % name)
            renamed += count

    leftover = sorted(set(int(n) for n in re.findall(r'\{"TYPE_(\d+)"', text)))
    with open(args.header, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)

    print("%s: %d descriptor(s) renamed" % (args.header, renamed))
    if leftover:
        # Not fatal: a type the port never asks for by name does no harm.  Worth
        # saying, though, because a new one would mean the mapping is short.
        print("still numeric: %s" % ", ".join("TYPE_%d" % n for n in leftover))
    if renamed == 0:
        print("nothing to rename - already named, or the header is not the "
              "generated one", file=sys.stderr)


if __name__ == "__main__":
    main()
