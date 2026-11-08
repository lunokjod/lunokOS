-- **A probe for the glass** (0.151.12, temporary): it does the same work the reader does when it opens, in stages, and
-- after every stage it paints a **small square** and presents it. The log then says two things no theory of mine has been
-- able to say: which stage came last, and whether the panel was still answering after it.
--
-- The reader's start-up, which is what this repeats: three modules of its own (`require`, read from the flash), two faces
-- from `/fonts`, the list of the shelf (`epub.books`, the flash **and** the card), and then its first page. 0.151.11 showed
-- the panel already silent at that first page with no paint of ours in the air -- 5530 ms after the last one -- so the
-- question is which of those steps precedes it. `probe: 0 nothing` has already answered on the board: the panel was alive.
--
-- The three modules here are filler of the same size as the reader's (`one.lua` 11 007 B, `two.lua` 8 871 B,
-- `three.lua` 4 599 B against 10 137, 8 722 and 3 609): what is being measured is the read and the compile, not the text.
-- They are the app's own because `require` reads **the folder of the running app and nothing else** (the door says so: «is
-- not a file of this app»).
--
-- Run it with `run probe`. It ends by itself and the shell paints again.

local function mark(what, level)
  console.print('probe: ' .. what)
  ui.rect(4, 4, 40, 40, level, 'gc16')
  ui.present('gc16')
end

-- 0: nothing at all, the app has just started and the view is the system's.
mark('0 nothing', 0)

-- 1: three modules, read from the flash and compiled. The reader does this before it touches the glass at all.
local one = require('one')
local two = require('two')
local three = require('three')
mark('1 requires ' .. tostring(one ~= nil and two ~= nil and three ~= nil), 3)

-- 2: the first face, read from `/fonts` and turned into glyphs in the extra memory.
local face = ui.load_font('/fonts/dejavuserif-latin1.ttf')
mark('2 first face ' .. tostring(face ~= nil), 6)

-- 3: the second face.
local title_face = ui.load_font('/fonts/dejavusans-latin1.ttf')
mark('3 second face ' .. tostring(title_face ~= nil), 9)

-- 4: the shelf, which walks the flash and the card. `epub.books` is the system's door and not a module of the reader.
local books = {}
local ok, paths = pcall(epub.books)
if ok and type(paths) == 'table' then books = paths end
mark('4 books ' .. tostring(#books), 12)

console.print('probe: done, modules ' .. tostring(one ~= nil and two ~= nil and three ~= nil) ..
                ', faces ' .. tostring(face ~= nil) .. ' ' .. tostring(title_face ~= nil) ..
                ', books ' .. tostring(#books))
