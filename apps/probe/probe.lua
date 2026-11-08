-- **A probe for the glass** (0.151.12, temporary): it does the same work the reader does when it opens, in stages, and
-- after every stage it paints a **small square** and presents it. The log then says two things that no theory of mine has
-- been able to say: which stage came last, and whether the panel was still answering after it.
--
-- The reader's start-up, which is what this repeats: three modules of its own (`require`, read from the flash), two faces
-- from `/fonts`, the list of the shelf (`epub.books`, the flash **and** the card), and then its first page. On the board
-- the panel has been found silent at that first page with no paint of ours in the air (measured 8/11, docs/50), so the
-- question is which of those four steps precedes it.
--
-- Run it with `run probe`. It ends by itself and the shell paints again.

local function mark(what, level)
  console.print('probe: ' .. what)
  ui.rect(4, 4, 40, 40, level, 'gc16')
  ui.present('gc16')
end

-- 0: nothing at all, the app has just started and the view is the system's.
mark('0 nothing', 0)

-- 1: the three modules of the reader. `require` reads `/apps/<app>/<name>.lua` from the flash.
local library = require('library')
local chapters = require('chapters')
local notice = require('notice')
mark('1 requires ok', 3)

-- 2: the first face, read from `/fonts` and turned into glyphs in the extra memory.
local face = ui.load_font('/fonts/dejavuserif-latin1.ttf')
mark('2 first face ' .. tostring(face ~= nil), 6)

-- 3: the second face.
local title_face = ui.load_font('/fonts/dejavusans-latin1.ttf')
mark('3 second face ' .. tostring(title_face ~= nil), 9)

-- 4: the shelf, which walks the flash and the card.
local books = library.shelf_of()
mark('4 books ' .. tostring(#books), 12)

console.print('probe: done, requires ' .. tostring(library ~= nil and chapters ~= nil and notice ~= nil) ..
                ', faces ' .. tostring(face ~= nil) .. ' ' .. tostring(title_face ~= nil) ..
                ', books ' .. tostring(#books))
