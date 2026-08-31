"""Turn a saved tower into a debugging one: every tool unlocked, money to burn.

Most of the game is behind its star rating - the whole transport catalogue
arrives at two stars, and the rest above that - and reaching those ratings takes
a real game.  For testing the renderer that is a wall.  This rewrites two
header fields of a `.TDT` so the palette opens up and construction never runs
out of money.

    python tools/make_debug_tower.py TOWER.TDT DEBUG.TDT [--rating 6] [--funds 1000000000]

Import the result from the page's "import .TDT" and open it from the game's
File menu.  The game recomputes the rating from the tower's own population, so
this unlocks the palette for the session rather than for ever.

The header, from parse_original_tdt: a little-endian version word, then the
rating word, then the balance as a signed dword.
"""
import argparse
import struct

RATING_OFFSET = 2
BALANCE_OFFSET = 4


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('source')
    parser.add_argument('destination')
    parser.add_argument('--rating', type=int, default=6)
    parser.add_argument('--funds', type=int, default=1000000000)
    args = parser.parse_args()

    data = bytearray(open(args.source, 'rb').read())
    if len(data) < 8:
        raise SystemExit('not a tower file')

    raw_version = struct.unpack_from('<H', data, 0)[0]
    low, high = raw_version & 0xFF, raw_version >> 8
    # 10d0:0b6a recognises the opposite-endian family only when the low byte is
    # nonzero and the high byte is zero.
    swapped = low != 0 and high == 0
    version = low if swapped else high
    if not 0x17 <= version <= 0x24:
        raise SystemExit('unsupported save version 0x%02x' % version)
    order = '>' if swapped else '<'

    before_rating = struct.unpack_from(order + 'H', data, RATING_OFFSET)[0]
    before_funds = struct.unpack_from(order + 'i', data, BALANCE_OFFSET)[0]

    if not 1 <= args.rating <= 6:
        raise SystemExit('rating must be 1..6')
    struct.pack_into(order + 'H', data, RATING_OFFSET, args.rating)
    struct.pack_into(order + 'i', data, BALANCE_OFFSET, args.funds)

    open(args.destination, 'wb').write(bytes(data))
    print('version 0x%02x, %s-endian' % (version, 'big' if swapped else 'little'))
    print('rating  %d -> %d' % (before_rating, args.rating))
    print('funds   %d -> %d' % (before_funds, args.funds))
    print('wrote', args.destination)


if __name__ == '__main__':
    main()
