-- btnstates: **el banc dels tons d'un botó** (0.190.146, l'amo: *«Fes una app de test amb 9 botons»*).
--
-- Nine buttons in three rows, and each one says a different thing about the state of a control, because «a disabled button is not
-- drawn grey» is a sentence that can be about the state, about the tone, about the draw or about the page, i una pàgina d'una altra
-- app no diu quin dels nou era:
--
--   1 `plain`        enabled, no fields touched                  → the control of the test
--   2 `off build`    `disabled` before the first paint           → the case of the button that is born off
--   3 `off dim8`     `disabled` and `dim = 8` written by the app  → the tone the page expects
--   4 `off dim0`     `disabled` and `dim = 0`                     → black on purpose, to see if `dim` is read at all
--   5 `gray`         enabled and `color = 8`                      → the grey without any `disabled`
--   6 `off prim`     `disabled` and `primary = true`              → the ink face and the off face at once
--   7 `off later`    `disabled` **after the first present**      → the redraw of a control already on the glass
--   8 `off later2`   the same, after the second present          → two redraws in a row
--   9 `off nodim`    `disabled` and no `dim` at all                → the default of the system
--
-- **What the page does not do**: it does not press anything, it does not bring a card and it does not close itself. It is a picture
-- with nine words on it, and what a person reads is which of the words went grey (docs/30, docs/45).

local caps = sys.caps()
if not caps.display then
  console.print('btnstates: this board has no glass')
  return
end

local view = ui.caps()
local kGap = 8
local kButtonH = 64
local kWidth = (view.width - 4 * kGap) // 3

local kLabels = { 'plain', 'off build', 'off dim8', 'off dim0', 'gray', 'off prim', 'off later', 'off later2',
                  'off nodim' }
local buttons = {}
for i = 1, 9 do
  local one, why = ui.button(kWidth, kButtonH)
  if one == nil then
    console.print('btnstates: no button ' .. i .. ' (' .. tostring(why and why.code or 'the memory') .. ')')
    return
  end
  one.title = kLabels[i]
  buttons[i] = one
end

local function place_all()
  for i = 1, 9 do
    local column = (i - 1) % 3
    local row = (i - 1) // 3
    buttons[i]:place(kGap + column * (kWidth + kGap), kGap + row * (kButtonH + kGap))
  end
end

-- **Un camp que el control no accepta no ha de tombar el banc**: `dim` is a tone of some controls and not of all, and a pcall here
-- answers which of the nine cannot be written instead of a page that never opens (docs/30).
local function try(setting, value)
  local ok, err = pcall(function() setting(value) end)
  if not ok then console.print('btnstates: the field refused it (' .. tostring(err) .. ')') end
end

ui.clear(15)
place_all()

try(function(v) buttons[2].disabled = v end, true)
try(function(v) buttons[3].disabled = v end, true)
try(function(v) buttons[3].dim = v end, 8)
try(function(v) buttons[4].disabled = v end, true)
try(function(v) buttons[4].dim = v end, 0)
try(function(v) buttons[5].color = v end, 8)
try(function(v) buttons[6].disabled = v end, true)
try(function(v) buttons[6].primary = v end, true)
try(function(v) buttons[9].disabled = v end, true)

ui.present('gc16')

-- **I els dos que s'apaguen amb la pàgina ja al vidre** (7 i 8): this is the case of a control that is on the glass and has to come
-- back in another tone, which is the one the button of the settings was in (docs/30, docs/45).
try(function(v) buttons[7].disabled = v end, true)
ui.present('du')
try(function(v) buttons[8].disabled = v end, true)
ui.present('du')

console.print('btnstates: nine buttons, and the ones that went grey are the ones that were off')
