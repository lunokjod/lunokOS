-- Font sizes: the font of the system at every scale it accepts, each line with its number (docs/30).
--   run font-sizes
--
-- What this page answers: whether a scale is the system's business and nothing else. The app asks for the
-- same four words six times with a different scale and never touches a glyph; the heights are three, seven,
-- fourteen and so on, because the five by seven is multiplied and the system is the one that multiplies it.
-- It ends by itself.

local info = ui.caps()
local margin = 20
local line = 'FONT'
local y = margin

ui.clear(15)
ui.text(margin, y, 'THE SAME WORD AT SIX SIZES', 1, 0)
y = y + 7 + 14

for scale = 1, 6 do
  ui.text(margin, y, line, scale, 0)
  -- The number of the scale next to it, in the smallest size, so the label never is what takes the room.
  ui.text(margin + 6 * 5 * scale + 12, y + 7 * scale - 9, 'SCALE ' .. scale, 1, 8)
  y = y + 7 * scale + 12
  if y > info.height - 20 then break end
end

console.print('six scales of the font of the system, from 1 to 6')
ui.present('gc16')
ui.idle()
