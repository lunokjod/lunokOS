-- btHID Keyboard: this board, as the keyboard of another device (0.190.267).
--
-- **Què és aquesta pàgina** (l'amo: *«Ara fes la versió teclat de l'app “btHID Keyboard”, revisa els requisits
-- https://novelbits.io/bluetooth-hid-device-design-example-project/ . Te una dificultat afegida... Hauràs de posar un TextBox sota la
-- barra de sistema a dalt de tot... Si no hi ha focus, el teclat per software no surt ni es manté»*). Dues vistes i prou:
--
--   - **Esperant**: què és aquesta placa, el nom amb què s'anuncia i el que una persona ha de fer a l'altre aparell. El commutador del
--     ràdio és l'únic botó, i **no s'encén res tot sol**: obtenir el ràdio costa trenta kilobytes de heap i atura les becaines.
--   - **Viva**: una caixa de text a dalt de tot —sota la barra del sistema— i la banda del teclat a baix. **El que s'hi escriu marxa
--     com a tecles premudes**, lletra per lletra, i la caixa és el mirall del que ha sortit.
--
-- **La dificultat que l'amo va assenyalar, i per què la caixa va a dalt**: la banda del teclat és del sistema i **segueix el focus** —
-- surt quan una caixa que pren tecles el té i marxa quan el focus se'n va a un botó o a la pàgina buida (docs/30, docs/90). Una caixa
-- no enfocada és una pàgina on el teclat no surt, i una caixa a baix de tot és una caixa **tapada per la banda** que ella mateixa fa
-- sortir (256 px al Paper). O sigui que la caixa va **a dalt de la vista, a tocar de la barra**, i la pàgina li dona el focus i li
-- aixeca la banda tot just entrar (`kb.target` i `kb:show`) en comptes d'esperar que el dit la trobi.
--
-- **El que aquesta pàgina no fa**: no és el ratolí. El mode es desa a `cfg btmode` abans d'encendre res —el nucli arrenca el
-- dispositiu amb el que hi ha desat— i el mapa del report del teclat és el de sempre (vuit bytes: modificadors, reservat i sis
-- tecles). **L'altre aparell s'ha de tornar a aparellar** quan es passa d'una app a l'altra: un amfitrió que va llegir el mapa d'un
-- ratolí no endevina el d'un teclat (docs/50).

local caps = ui.caps()
local view = { width = caps.width, height = caps.height }
if view.width <= 0 or view.height <= 0 then
  console.print('bt kbd: this board has no view')
  return
end

local kMargin = 8
local kRowH = 58
local kButtonW = 520

-- **El text es demana en píxels i la font respon amb els passos que té**: the size is what this page wants a line to take, and the same
-- file stays readable on a small board and on a big one (docs/45).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end

  local scale = math.floor(line_px / step + 0.5)
  if scale < 1 then scale = 1 end
  if scale > 16 then scale = 16 end

  return scale
end

-- **Els dos números de cada mida, i no un de sol**: `draw_block` demana **píxels** i un control demana **passos** (`scale`), i
-- confondre'ls és una pàgina que es pinta a la mida més petita que té (docs/45).
local kSmallPx, kBodyPx, kBigPx = 20, 30, 52
local kBody = scale_for(kBodyPx)

-- **La forma de la vista** (docs/30 § «La sessió `state`»): quina de les dues i què hi havia a la caixa. Res més —l'aparell connectat i
-- el ràdio són el món, no la forma—. El text **sí** que hi és: una caixa de text és part de la forma que una persona deixa, i el que
-- ha sortit no es torna a enviar (el `sent` d'aquesta pàgina comença on comença la caixa).
local kStateBytes = 1024
local kLayout = 'v1'

-- **El que aquest fitxer declara abans de fer-lo servir**: Lua resol els noms en el moment de la crida i no de la definició.
local page = 1          -- 1 esperant, 2 viva
local note = ''         -- l'última cosa que ha passat, en una línia
local state = nil
local radio_button = nil
local box = nil         -- la caixa que és el teclat
local kb = nil          -- la banda del sistema
local typed = ''        -- el que hi ha a la caixa
local sent = ''         -- el que ja ha sortit cap a l'altre aparell
local paint = nil
local layout = nil

local function the_state()
  if state == nil then
    local block = sys.session('state', kStateBytes)
    if block == nil then return nil end
    state = block
  end
  return state
end

-- **La forma, en dues línies i prou**: `v1 <pàgina>` i el que hi havia a la caixa a sota. El text no es neteja de res, o sigui que el
-- que es llegeix és exactament el que una persona va deixar (docs/30).
local function save_state()
  local block = the_state()
  if block == nil then return end
  block:write(0, string.format('%s v%d\n%s', kLayout, page, typed))
end

local function read_state()
  local block = the_state()
  if block == nil then return nil end
  local raw = block:read(0, kStateBytes)
  if raw == nil or #raw == 0 then return nil end
  local layout_word, rest = raw:match('^(%S+) (.*)$')
  if layout_word ~= kLayout then return nil end  -- a shape from before is dropped, never migrated
  local number, text = rest:match('^v(%d+)\n(.*)$')
  local which = tonumber(number or '')
  if which ~= 1 and which ~= 2 then which = 1 end
  return which, text or ''
end

-- **El que passa es diu als dos llocs**: la línia de la pàgina és per a qui mira la placa, i el log per a qui mira la consola.
local function announce(text)
  note = text
  console.print('bt kbd: ' .. text)
end

-- I el que la pàgina ja diu tota sola —la línia d'estat— només va al log.
local function inform(text)
  console.print('bt kbd: ' .. text)
end

-- ================================================================ the radio

-- **La ràdio no s'encén tota sola** (0.190.209): obtenir-la costa trenta kilobytes i atura les becaines, o sigui que és una decisió
-- d'una persona. El mode es desa **abans** d'encendre res, perquè el nucli arrenca el dispositiu amb el que hi ha desat (docs/30).
local function radio_on()
  if not bt.available() then
    announce('this board has no radio')
    return false
  end
  announce('bringing the radio up as a keyboard')
  local mode, why = cfg.btmode('keyboard')
  if mode == nil then
    announce(string.format('this board cannot keep the choice (%s)', tostring(why ~= nil and why.code or 'no reason')))
    return false
  end
  if not cfg.bt(true) then
    announce('the radio did not come up')
    return false
  end
  local got, dev_why = bt.device('keyboard')
  if got == nil then
    announce(string.format('this board cannot be a keyboard (%s)', tostring(dev_why ~= nil and dev_why.code or 'no reason')))
    return false
  end
  announce(string.format('radio up: this board is a %s, offered as \'%s\'', got, bt.name()))
  return true
end

local function radio_off()
  cfg.bt(false)
  if kb ~= nil then kb:hide() end
  announce('bluetooth is off: nothing is offered to anybody')
  return true
end

-- **Connectat i escoltat són dues coses** (0.190.208): `bt.connected()` diu que hi ha un aparell a l'altre costat; `bt.ready()` diu que
-- a més a més ha encès els reports. Entremig hi ha un moment en què la placa envia i l'altre cap encara no rep res, i aquesta línia és
-- la que ho diu (docs/30, docs/50).
local function state_words()
  if not cfg.bt() then return 'Bluetooth is off.' end
  if not bt.connected() then return 'Waiting for a device...' end
  if bt.ready ~= nil and not bt.ready() then return 'Connected. The other device is switching the reports on.' end
  return 'A device is connected: what you type here goes to it.'
end

-- ================================================================ the drawing

local function a_line(scale)
  local h = ui.line_height(scale)
  if h <= 0 then h = 8 * scale end
  return h
end

local function wrap_at(text, s, room)
  local lines, current = {}, ''
  for word in text:gmatch('%S+') do
    local trial = (#current == 0) and word or (current .. ' ' .. word)
    if #current > 0 and ui.text_width(trial, s) > room then
      lines[#lines + 1] = current
      current = word
    else
      current = trial
    end
  end
  if #current > 0 then lines[#lines + 1] = current end
  return lines
end

local function block_that_fits(text, want_px, max_lines, room)
  for s = scale_for(want_px), 1, -1 do
    local lines = wrap_at(text, s, room)
    if #lines <= max_lines then return s, lines end
  end
  return 1, wrap_at(text, 1, room)
end

local function draw_block(x, y, text, want_px, max_lines, room, ink)
  if text == nil or #text == 0 then return y end
  local s, lines = block_that_fits(text, want_px, max_lines or 1, room)
  for i, l in ipairs(lines) do
    ui.text(x, y + (i - 1) * a_line(s), l, s, ink)
  end
  return y + #lines * a_line(s)
end

paint = function(clean)
  ui.clear(15)
  local room = view.width - kMargin * 2
  if page == 1 then
    local y = kMargin
    y = draw_block(kMargin, y, 'btHID Keyboard', kBigPx, 1, room, 0) + 18
    y = draw_block(kMargin, y, 'This board is a keyboard: what you type in the box goes to the other device.', kBodyPx, 2, room, 0) + 10
    y = draw_block(kMargin, y, 'Pair with this name:', kBodyPx, 1, room, 0) + 6
    y = draw_block(kMargin, y, bt.available() and bt.name() or '(no radio)', kBodyPx, 1, room, 0) + 12
    y = draw_block(kMargin, y, state_words(), kBodyPx, 2, room, 0)
    if #note > 0 then draw_block(kMargin, y + 10, note, kSmallPx, 2, room, 0) end
  else
    -- **El que hi ha sota la caixa és el que no s'ha de tapar**: la caixa és a dalt i la banda a baix, o sigui que aquestes dues línies
    -- cauen al mig i es veuen sempre. El que no es repinta mai sol és la caixa: els controls es pinten ells (docs/30, docs/90).
    local y = kRowH + kMargin
    y = draw_block(kMargin, y, 'Type here: every letter goes to the other device.', kSmallPx, 2, room, 0) + 10
    y = draw_block(kMargin, y, state_words(), kBodyPx, 2, room, 0) + 8
    draw_block(kMargin, y, note, kSmallPx, 2, room, 0)
  end
  ui.present(clean and 'gc16' or 'du')
end

-- **El que no toca no és a la pàgina**: off the glass the control is not drawn and the finger does not find it. La caixa només hi és a
-- la vista viva —a l'espera no hi ha res a escriure i una caixa buida que no envia res seria mentir— i el commutador al revés.
layout = function()
  if radio_button ~= nil then
    if page == 1 then
      radio_button.title = cfg.bt() and 'Turn Bluetooth off' or 'Turn Bluetooth on'
      radio_button:resize(view.width - kMargin * 2, kRowH)
      radio_button:place(kMargin, view.height - kMargin - kRowH)
    else
      radio_button:place(0, view.height + 400)
    end
  end
  if box ~= nil then
    if page == 2 then
      -- **A dalt de tot i de tota l'amplada**: l'amo ho va dir així —«un TextBox sota la barra de sistema a dalt de tot»— i no és un
      -- gust: la banda ocupa els 256 px de baix, i una caixa allà seria una caixa tapada pel teclat que ella fa sortir (docs/30, docs/90).
      box:resize(view.width - kMargin * 2, kRowH)
      box:place(kMargin, 0)
    else
      box:place(0, view.height + 400)
    end
  end
end

-- ================================================================ the keys

-- **El que s'ha enviat i el que hi ha**: the box is the keyboard, o sigui que el que compta és la diferència amb el que ja ha sortit
-- —el que s'ha esborrat són retrocessos i el que s'ha afegit són les tecles, en aquest ordre— i no el text sencer un altre cop. Un text
-- que s'arregla pel mig torna a escriure la cua, que és el que fa qualsevol teclat de debò (docs/30).
local function send_the_difference(text)
  if not bt.connected() then return end  -- el que no s'ha pogut enviar queda a la diferència i marxa quan algú es connecti
  if text == sent then return end
  local common = 0
  local shortest = math.min(#text, #sent)
  while common < shortest and text:byte(common + 1) == sent:byte(common + 1) do common = common + 1 end
  local erased = #sent - common
  for _ = 1, erased do bt.type('\b') end
  local added = text:sub(common + 1)
  local went = 0
  if #added > 0 then went = bt.type(added) end
  sent = text
  -- **El que ha sortit no es repinta**: la caixa ja ensenya el text i aquest vidre cobra cada passada; el que sí que no s'ha de callar és
  -- un caràcter que no ha viatjat, perquè la caixa el tindria i l'altre aparell no (docs/30, docs/50).
  if went < #added then inform('a character of what you typed does not travel on this keyboard') end
end

-- ================================================================ the page

-- **La caixa de dalt, que és el teclat sencer**: la creu de dins (el `clear` del sistema) buida la caixa i això és exactament el que ha
-- de passar —cada lletra que marxa és un retrocés a l'altre aparell, perquè la caixa és el mirall del que ha sortit— (docs/30).
box = ui.textbox(view.width, kRowH)
if box == nil then
  console.print('bt kbd: there is no room for the box')
  return
end
box.scale = kBody
box.keyboard = 'alpha'

box.on_change = function(self, text)
  local what = text or ''
  -- **El retorn de carro de la banda és la tecla Enter** (docs/90): viatja com a tecla i la caixa no se'l queda, perquè una caixa d'una
  -- línia amb un salt a dins no és el que una persona ha escrit. Es fa després d'enviar-lo, i la segona passada —la que entra aquí amb el
  -- text net— no troba cap diferència i no envia res.
  typed = what
  send_the_difference(what)
  local clean = (what:gsub('[\r\n]', ''))
  if clean ~= what then
    typed = clean
    sent = clean
    self.text = clean
  end
  save_state()
end

kb = ui.keyboard()

radio_button = ui.button(kButtonW, kRowH)
if radio_button ~= nil then
  radio_button.scale = kBody
  radio_button.title = 'Turn Bluetooth on'
  radio_button.on_click = function()
    if cfg.bt() then radio_off() else radio_on() end
    layout()
    paint(false)
    save_state()
  end
else
  console.print('bt kbd: there is no room for the switch of the radio')
end

-- **El que s'ha deixat la persona**: la vista i el que hi havia a la caixa. L'aparell connectat no hi és —és el món i no la forma—.
local restored, restored_text = read_state()
if restored == nil then restored = 1 end
typed = restored_text or ''
sent = typed
box.text = typed
page = (restored == 2 and bt.connected() and cfg.bt()) and 2 or 1
if cfg.bt() then inform('the radio is already up, as it was left') end
if page == 2 then inform('back to the box: a device is connected') end

-- **I la banda, si la vista viva és la que toca** (docs/30, docs/90): `kb:show` exigeix una caixa que prengui tecles i és el que fa que el
-- teclat del sistema surti sense que cap dit hagi de trobar la caixa. Sense això la pàgina seria una caixa morta en una placa que no té
-- tecles físiques.
local function show_or_hide_the_band()
  if kb == nil then return end
  if page == 2 then
    kb.target = box
    kb:show('alpha')
  else
    kb:hide()
  end
end

layout()
show_or_hide_the_band()
paint(true)

-- **La vista canvia de mida quan la banda surt o marxa** (docs/30): la caixa torna a la seva plaça i la pàgina es pinta de nou, perquè un
-- control que es mou deixa la tinta d'on era.
function on_resize(width, height)
  local fresh = ui.caps()
  view.width = fresh.width
  view.height = fresh.height
  layout()
  paint(true)
end

function on_pause() save_state() end
function on_save() save_state() end
function on_close() save_state() end

-- **El bucle de la pàgina**: el que mira cada volta és el món —la ràdio i l'aparell—, perquè les tecles les porta el sistema i no cal
-- preguntar res. Cent vint mil·lisegons és prou perquè una pàgina que espera algú es giri abans que una persona aixequi el dit.
local was_on = cfg.bt()
local was_connected = bt.connected()
while true do
  sys.delay(120):await()
  local now_on = cfg.bt()
  local now_connected = bt.connected()
  if page == 1 then
    if now_on ~= was_on or now_connected ~= was_connected then
      was_on = now_on
      was_connected = now_connected
      layout()
      paint(false)
    end
    if now_connected and now_on then
      inform('a device is connected, the box is its keyboard')
      note = ''
      page = 2
      layout()
      show_or_hide_the_band()
      paint(true)
      save_state()
    end
  elseif not now_connected then
    -- **L'altre aparell ha marxat**: es torna a esperar i es diu. **I si el que s'ha apagat és el Bluetooth, també es torna enrere**.
    page = 1
    was_on = now_on
    was_connected = false
    announce(now_on and 'the device went away, waiting again' or 'bluetooth is off, so nothing you type goes out')
    layout()
    show_or_hide_the_band()
    paint(true)
    save_state()
  end
end
