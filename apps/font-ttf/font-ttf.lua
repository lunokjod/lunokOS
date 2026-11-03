-- Font ttf: a face of **outlines** loaded from a TrueType file (docs/30).
--   run font-ttf
--
-- The file this app ships with was built out of the same eight by eight letters that `font-bdf` and `font-pcf`
-- draw, so the shape on the page is the shape of those two. What it has that they do not is that it is a
-- drawing and not a picture: the same file is drawn here at three sizes, and every one of them is the letter
-- drawn again and not a table of pixels stretched.
--
-- `scale` is the body in eighths of a pixel: 1 is eight pixels of body, 3 is twenty-four. The numbers go to
-- the console, which is where a glyph of outlines says what it did.
--
-- It ends by itself: a page, a present, wait for the ink, out.

local info = ui.caps()
local margin = 20
local word = 'ABCDEF'

local face, err = ui.load_font('/res/lunok-8x8.ttf')
if face == nil then
  console.print('no face: ' .. tostring(err and err.code) .. ' ' .. tostring(err and err.message))
  return
end
local w, h, glyphs = face:size()
console.print('ttf: ' .. tostring(face) .. ', a body of ' .. h .. ' px at scale 1, "' .. word .. '" is ' ..
                face:text_width(word, 1) .. ' px at 1, ' .. face:text_width(word, 3) .. ' at 3 and ' ..
                face:text_width(word, 6) .. ' at 6')

ui.clear(15)
ui.text(margin, margin, 'A FACE OF OUTLINES', 2, 0)

ui.font(face)
ui.text(margin, margin + 40, word, 2, 0)
ui.text(margin, margin + 60, word, 4, 0)
ui.text(margin, margin + 60 + 4 * h + 16, word, 6, 0)

-- The font of the system under it, for the same letters at the same place.
ui.font(nil)
ui.text(margin, info.height - 40, 'TTF AT 2, 4 AND 6', 2, 8)

face:release()
ui.present('gc16')
ui.idle()
