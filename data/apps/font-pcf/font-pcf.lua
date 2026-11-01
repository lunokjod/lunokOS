-- Font pcf: a face loaded from a **PCF**, which is a BDF compiled (docs/30).
--   run font-pcf
--
-- The file this app ships with was made from the same BDF the app `font-bdf` draws, with `bdftopcf`, so the
-- two faces are **the same face**: the same eight by eight, the same six codes, the same ink. What the page
-- shows is one above the other —the PCF and then the font of the system— and the numbers go to the console.
--
-- It ends by itself: a page, a present, wait for the ink, out.

local info = ui.caps()
local margin = 20
local word = 'ABCDEF'

local face, err = ui.load_font('/res/abcdef-8x8.pcf')
if face == nil then
  console.print('no face: ' .. tostring(err and err.code) .. ' ' .. tostring(err and err.message))
  return
end
local w, h, glyphs = face:size()
console.print('pcf: ' .. tostring(face) .. ', a glyph is ' .. w .. 'x' .. h .. ', "' .. word .. '" is ' ..
                face:text_width(word, 1) .. ' px at 1')

ui.clear(15)
ui.text(margin, margin, 'A FACE FROM A PCF', 2, 0)

ui.font(face)
ui.text(margin, margin + 40, word, 1, 0)
ui.text(margin, margin + 60, word, 2, 0)
ui.text(margin, margin + 60 + h * 2 + 10, 'FACE BED', 2, 0)

ui.font(nil)
ui.text(margin, margin + 60 + h * 2 + 10 + 7 * 2 + 16, word, 2, 8)
ui.text(margin, info.height - 24, 'PCF ' .. w .. 'X' .. h .. ' WITH ' .. glyphs .. ' CODES', 1, 0)

face:release()
ui.present('gc16')
ui.idle()
