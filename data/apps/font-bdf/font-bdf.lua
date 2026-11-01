-- Font bdf: a face loaded from a **BDF**, the bitmap distribution format an X server read (docs/30).
--   run font-bdf
--
-- `ui.load_font` looks at what the file says of itself: `lunok-font 1` is this system's own text face and
-- `STARTFONT` is a BDF. This app loads the BDF it ships with —six letters, eight by eight— and draws the
-- same words with it and with the font of the system, so the two can be compared on the page. The numbers go
-- to the console: what the face says of itself and how wide a line is with it.
--
-- It ends by itself: a page, a present, wait for the ink, out.

local info = ui.caps()
local margin = 20
local word = 'ABCDEF'

local face, err = ui.load_font('/res/abcdef-8x8.bdf')
if face == nil then
  console.print('no face: ' .. tostring(err and err.code) .. ' ' .. tostring(err and err.message))
  return
end
local w, h, glyphs = face:size()
console.print('bdf: ' .. tostring(face) .. ', a glyph is ' .. w .. 'x' .. h .. ', "' .. word .. '" is ' ..
                face:text_width(word, 1) .. ' px at 1')

ui.clear(15)
ui.text(margin, margin, 'A FACE FROM A BDF', 2, 0)

-- The BDF at two sizes, and the same words with the font of the system under them.
ui.font(face)
ui.text(margin, margin + 40, word, 1, 0)
ui.text(margin, margin + 60, word, 2, 0)
ui.text(margin, margin + 60 + h * 2 + 10, 'FACE BED', 2, 0)

ui.font(nil)
ui.text(margin, margin + 60 + h * 2 + 10 + 7 * 2 + 16, word, 2, 8)
ui.text(margin, info.height - 24, 'BDF ' .. w .. 'X' .. h .. ' WITH ' .. glyphs .. ' CODES', 1, 0)

face:release()
ui.present('gc16')
ui.idle()
