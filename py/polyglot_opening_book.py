# -*- coding: utf-8 -*-
#
# This file is part of the python-chess library.
# Copyright (C) 2012 Niklas Fiekas <niklas.fiekas@tu-clausthal.de>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import struct

# TODO: Also allow writing to opening books and document the class.

class PolyglotOpeningBook(object):
    def __init__(self, path):
        self._entry_struct = struct.Struct(">QHHI")

        self._stream = open(path, "rb")

        self.seek_entry(0, 2)
        self._entry_count = self._stream.tell() / 16

        self.seek_entry(0)

    def __len__(self):
        return self._entry_count

    def __getitem__(self, key):
        if key >= self._entry_count:
            raise IndexError()
        self.seek_entry(key)
        return self.next()

    def __iter__(self):
        self.seek_entry(0)
        return self

    def __reversed__(self):
        for i in xrange(len(self) - 1, -1, -1):
            self.seek_entry(i)
            yield self.next()

    def seek_entry(self, offset, whence=0):
        self._stream.seek(offset * 16, whence)

    def seek_position(self, key):
        # Calculate the position hash.
        # key = position.__hash__()

        # Do a binary search.
        start = 0
        end = len(self)
        while end >= start:
            middle = (start + end) / 2

            self.seek_entry(middle)
            raw_entry = self.next_raw()

            if raw_entry[0] < key:
                start = middle + 1
            elif raw_entry[0] > key:
                end = middle - 1
            else:
                # Position found. Move back to the first occurence.
                self.seek_entry(-1, 1)
                while raw_entry[0] == key and middle > start:
                    middle -= 1
                    self.seek_entry(middle)
                    raw_entry = self.next_raw()

                    if middle == start and raw_entry[0] == key:
                        self.seek_entry(-1, 1)
                return

        raise KeyError()

    def next_raw(self):
        try:
            return self._entry_struct.unpack(self._stream.read(16))
        except struct.error:
            raise StopIteration()

    def getTextMoveFromRaw(self, fx, fy, tx, ty):
        _file =  ['a','b','c','d','e','f','g','h']
        _rank = ['1','2','3','4','5','6','7','8']

        return _file[fx]+_rank[fy]+_file[tx]+_rank[ty]

    def next(self):
        raw_entry = self.next_raw()

        source_x = ((raw_entry[1] >> 6) & 077) & 0x7
        source_y = (((raw_entry[1] >> 6) & 077) >> 3) & 0x7

        target_x = (raw_entry[1] & 077) & 0x7
        target_y = ((raw_entry[1] & 077) >> 3) & 0x7

        promote = (raw_entry[1] >> 12) & 0x7
        promotion = "nbrq"[promote + 1] if promote else None

        move = self.getTextMoveFromRaw(source_x, source_y, target_x, target_y)

        if promotion:
            move += promotion

        return {
            "position_hash": raw_entry[0],
            "move": move,
            "weight": raw_entry[2],
            "learn": raw_entry[3]
        }

    def get_entries_for_position(self, key):
        # position_hash = position.__hash__()
        # Seek the position. Stop iteration if no entry exists.
        try:
            self.seek_position(key)
        except KeyError:
            raise StopIteration()
        # Iterate.
        while True:
            entry = self.next()
            if entry["position_hash"] == key:
                yield entry
            else:
                break
