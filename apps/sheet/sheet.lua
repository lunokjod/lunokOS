-- Sheet: every glyph of the font of the system in its cell, which is what says whether the bits of a face
-- land where the face says they do (docs/30).
--   run sheet
--
-- The system's font carries the codes 32 to 90 —space, punctuation, digits and capitals— in a glyph of five
-- by seven. This app draws each one inside a box of its own, so a column that is off by one or a glyph that
-- is drawn with somebody else's rows is seen at once instead of read past. It ends by itself.

local info = ui.caps()
local cell = 18
local margin = 12
local step = cell + 4
local per_row = math.max(1, math.floor((info.width - 2 * margin + 4) / step))

ui.clear(15)
local code = 32
local x = margin
local y = margin
while code <= 90 do
  ui.frame(x, y, cell, cell, 8)
  ui.text(x + 4, y + 4, string.char(code), 1, 0)
  if code % 10 == 0 then
    -- **The code under every tenth glyph**, because a sheet without numbers is a sheet nobody can check.
    ui.text(x, y + cell + 1, tostring(code), 1, 0)
  end
  code = code + 1
  x = x + step
  if x + cell > info.width - margin then
    x = margin
    y = y + step + 8
  end
end
ui.text(margin, y + step + 8, 'CODES 32 TO 90, FIVE BY SEVEN', 1, 0)
console.print('a sheet of ' .. (90 - 32 + 1) .. ' glyphs in cells of ' .. cell)
ui.present('gld16')
ui.idle()
