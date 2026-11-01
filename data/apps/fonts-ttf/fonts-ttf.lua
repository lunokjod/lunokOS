-- Fonts ttf: **three real faces of outlines** on one page (docs/30).
--   run fonts-ttf
--
-- The three files are DejaVu Sans, DejaVu Serif and DejaVu Sans Mono, cut down to the printable ascii and
-- nothing else —`pyftsubset --unicodes=U+0020-007E`, about ten kilobytes each— so that what is being shown is
-- a real font and not a demo face. The licence of DejaVu travels with them (`/res/LICENCE.txt`).
--
-- Three faces is the whole of what the system keeps at once, and that is why there are three: the sans, the
-- serif and the mono at the same words, every one of them **drawn again at its size** and not a table of
-- pixels stretched. Scale is the body in eighths of a pixel: 2 is sixteen, 3 is twenty-four, 4 is thirty-two.
--
-- It ends by itself: a page, a present, wait for the ink, out.

local info = ui.caps()
local margin = 24
local line = 'Handgloves 0123'

local sans, err1 = ui.load_font('/res/dejavusans.ttf')
local serif, err2 = ui.load_font('/res/dejavuserif.ttf')
local mono, err3 = ui.load_font('/res/dejavusansmono.ttf')
if sans == nil or serif == nil or mono == nil then
  console.print('a face is missing: ' .. tostring(err1 and err1.message) .. ' ' ..
                  tostring(err2 and err2.message) .. ' ' .. tostring(err3 and err3.message))
  return
end
console.print('sans  "' .. line .. '": ' .. sans:text_width(line, 2) .. ' px at 2, ' ..
                sans:text_width(line, 4) .. ' at 4')
console.print('serif "' .. line .. '": ' .. serif:text_width(line, 2) .. ' px at 2, ' ..
                serif:text_width(line, 4) .. ' at 4')
console.print('mono  "' .. line .. '": ' .. mono:text_width(line, 2) .. ' px at 2, ' ..
                mono:text_width(line, 4) .. ' at 4')

ui.clear(15)
ui.text(margin, margin, 'THREE FACES OF OUTLINES', 2, 0)

-- The sans, big and then small.
ui.font(sans)
ui.text(margin, margin + 44, 'DejaVu Sans', 4, 0)
ui.text(margin, margin + 44 + 32 + 10, line, 2, 0)
ui.text(margin, margin + 44 + 32 + 10 + 16 + 6, 'the same words, twice the size', 1, 8)

-- The serif, which is the one that looks like a book.
local y = margin + 44 + 32 + 10 + 16 + 6 + 8 + 24
ui.font(serif)
ui.text(margin, y, 'DejaVu Serif', 4, 0)
ui.text(margin, y + 32 + 10, line, 2, 0)

-- The mono, where every letter is as wide as every other.
y = y + 32 + 10 + 16 + 30
ui.font(mono)
ui.text(margin, y, 'DejaVu Sans Mono', 4, 0)
ui.text(margin, y + 32 + 10, line, 2, 0)

ui.font(nil)
ui.text(margin, info.height - 60, 'DEJAVU, CUT TO THE ASCII, DRAWN AT EVERY SIZE', 1, 8)

-- **A face given back is a slot free**: the mono goes, the bold comes, and the page carries four faces of the
-- same family while the system never holds more than three (docs/30).
mono:release()
local bold, err4 = ui.load_font('/res/dejavusans-bold.ttf')
if bold ~= nil then
  ui.font(bold)
  ui.text(margin, info.height - 36, 'and Bold after a release', 2, 0)
  console.print('bold  "and Bold after a release": ' .. bold:text_width('and Bold after a release', 2) ..
                  ' px at 2')
  bold:release()
else
  console.print('no bold: ' .. tostring(err4 and err4.message))
end

ui.font(nil)
sans:release()
serif:release()
ui.present('gc16')
ui.idle()
