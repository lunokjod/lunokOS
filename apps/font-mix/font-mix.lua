-- Font mix: the font of the system and a **partial** face in the same line, which is the case a font has to
-- survive (docs/30).
--   run font-mix
--
-- The face of this app carries three letters and nothing else (A, B and C). The lines below ask for
-- "ABCDEF" in it: what has to happen is that A, B and C arrive, D, E and F leave a hole, and **the line does
-- not shift** — a face that is missing a code is a gap and not a squeezed word. Under each line the same text
-- is drawn with the font of the system, so the two can be compared column by column.
--
-- The numbers go to the console: what the face says of itself and how wide the same line is with it and with
-- the system's, because those two numbers are what a page is laid out with.

local info = ui.caps()
local margin = 20
local line = 'ABCDEF'
local y = margin

local face, err = ui.load_font('/res/abc-6x7.lfont')
if face == nil then
  console.print('no face: ' .. tostring(err and err.message))
  return
end
local w, h, glyphs = face:size()
console.print('face: ' .. tostring(face) .. ', a glyph is ' .. w .. 'x' .. h)

ui.clear(15)
ui.text(margin, y, 'A PARTIAL FACE AND THE SYSTEM', 1, 0)
y = y + 27

-- One line with the face, one with the system, and the same text in both.
ui.font(face)
ui.text(margin, y, line, 2, 0)
y = y + h * 2 + 10
ui.font(nil)
ui.text(margin, y, line, 2, 0)
y = y + 7 * 2 + 20

-- And the same pair at size one, which is where a hole and a shift are easiest to count.
ui.font(face)
ui.text(margin, y, line, 1, 0)
y = y + h + 8
ui.font(nil)
ui.text(margin, y, line, 1, 0)
y = y + 7 + 20

ui.text(margin, y, 'SYSTEM ' .. ui.text_width(line, 1) .. ' PX', 1, 0)

console.print('the face is ' .. face:text_width(line, 1) .. ' px wide at 1 with "' .. line .. '"')
face:release()
ui.present('gc16')
ui.idle()
