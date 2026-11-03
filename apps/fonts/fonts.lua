-- Fonts: the font of the system and a face loaded from a portable font file, on the same page (docs/30).
--   run fonts
--
-- The system's font is five by seven and lives in the flash; a face comes from `/res/fonts/*.lfont`, which
-- is plain text —one glyph per block of `#` and `.`— and is loaded with `ui.load_font`. This app loads
-- every font it finds there, says what each one says of itself, and draws the same line with each face at
-- three sizes, so a glance at the glass says whether the glyphs arrive where they should and how big they
-- are. It ends by itself: a page, a present, wait for the ink, out.
--
-- **What to look at**: the digits of a loaded face are eight by eight and the letters of the system are
-- five by seven, so the two lines have a different height for the same `scale`, and the width printed on
-- the console has to be the one that fits the line on the page.

-- **`/res` is the app's own folder and not the disk's**: from an app, `/res/x` is `apps/<id>/res/x` —the
-- files an app ships with—, which is where the face of this app travels (docs/70).
local FONT_DIR = '/res'

local info = ui.caps()
local margin = 24
local line = '0123456789'

-- The console is where the numbers go: what a face says of itself and how wide a line is with it.
console.print('the view is ' .. info.width .. 'x' .. info.height)

local faces = {}
local names = fs.list(FONT_DIR) or {}
for _, entry in ipairs(names) do
  local name = entry.name or ''
  if name:sub(-6) == '.lfont' then
    local face, err = ui.load_font(FONT_DIR .. '/' .. name)
    if face == nil then
      -- **A file that is not a face says why**, and the app says it too: a font is a text file somebody
      -- writes, and the reason it was refused is the only thing that helps them fix it.
      console.print('no  ' .. name .. ': ' .. tostring(err and err.code) .. ' ' .. tostring(err and err.message))
    else
      local w, h, glyphs = face:size()
      console.print('yes ' .. name .. ': ' .. tostring(face))
      console.print('    a glyph is ' .. w .. 'x' .. h .. ', ' .. glyphs .. ' codes, and "' .. line ..
                      '" at 1 is ' .. face:text_width(line, 1) .. ' px')
      faces[#faces + 1] = face
    end
  end
end

-- The page: the system's font first, then the same line with each loaded face at 1, 2 and 3.
ui.clear(15)
local y = margin
ui.text(margin, y, 'THE FONT OF THE SYSTEM', 2, 0)
y = y + 7 * 2 + 12
ui.text(margin, y, line, 1, 0)
y = y + 7 + 20

for index, face in ipairs(faces) do
  local w, h = face:size()
  ui.font(face)
  ui.text(margin, y, 'FACE ' .. index .. ' AT 1 AND 2', 1, 0)
  y = y + h + 6
  ui.text(margin, y, line, 1, 0)
  ui.text(margin, y + h + 4, line, 2, 0)
  y = y + h + 4 + h * 2 + 18
end
-- **Back to the font of the system**, so the rest of the page (and the bar) is drawn with it: a face is the
-- canvas's, and nil is the system's.
ui.font(nil)

if #faces == 0 then
  ui.text(margin, y, 'NO FONT IN ' .. FONT_DIR, 1, 0)
end
ui.text(margin, info.height - 20, 'RUN FONTS', 1, 0)

console.print('faces loaded: ' .. #faces)
ui.present('gc16')
ui.idle()
console.print('the page is on the glass')
