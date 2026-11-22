-- bt HID: this board, as the mouse of another device (0.190.259).
--
-- **Què és aquesta pàgina** (l'amo: *«fem una app lua, una app per aixicar un bluetooth HID device amb un teclat i un mouse. En quan
-- algú es conecti es passa a la seguent vista, una panalla totalment buida serà el mouse i es trasmitrà amb el dit…»*, i després:
-- *«la idea es usar el panell com a mouse. Fes que funcioni un dels dispositius, o el mouse o el teclat. Quan funcioni ja els
-- ajuntarem»*). **Un aparell i prou**: el ratolí. El teclat hi era (la 0.190.204 el va fer, amb el mode `both` que porta els dos
-- reports en un sol mapa) i no arribava a funcionar de debò; el que fa aquesta pàgina és el que l'amo va demanar: **una cosa que
-- funcioni**, i el teclat torna quan el ratolí estigui comprovat a la seva mà (docs/30, docs/50).
--
-- Dues vistes i prou:
--
--   - **Esperant**: què és aquesta placa, el nom amb què s'anuncia i el que una persona ha de fer a l'altre aparell. El commutador
--     del ràdio és l'únic botó, i **no s'encén res tot sol** (l'amo: *«un botó per encendre i apagar el bluetooth. Que no ho faci
--     l'app automátic a l'arrencar»*): obtenir el ràdio costa trenta kilobytes de heap i atura les becaines, o sigui que és una
--     decisió d'una persona i no un efecte d'obrir una pàgina.
--   - **Viva**: la pàgina és el ratolí. El dit que hi camina mou el cursor de l'altre aparell i el que hi toca i s'aixeca sense
--     caminar és un clic.
--
-- **Res es pinta mentre el dit camina**: el cursor és a l'altre aparell, o sigui que el ratolí d'aquesta pàgina no costa ni una
-- passada de tinta. L'únic que es repinta és el que diu què ha passat —el ràdio que s'ha encès, l'aparell que ha arribat o que ha
-- marxat— i això és una línia.
--
-- **I el report és el de l'exemple d'Espressif** (`esp_hid_device`, IDF 5.2.1, el que l'amo va passar): quatre bytes —botons, x, y i
-- la roda— sobre un mapa de tres eixos, o sigui que aquesta placa s'anuncia com el ratolí que tothom prova primer. El moviment d'un
-- dit que salta més de cent vint-i-set píxels el parteix el sistema en tants reports com calgui (`bt.move`, docs/30).

-- **Els tres tocs del vidre són d'aquest ratolí i no del sistema** (0.190.266, l'amo: *«Desactiva els 3 tocs per tancar l'app al
-- programa del ratolí.»*). Aquesta pàgina llegeix cada dit que hi cau —no el gest cuinat, els dits—, i a l'altre cap de l'enllaç **un
-- toc és un clic del botó esquerre**: tres tocs seguits són tres clics, o sigui que el tercer no pot ser el sistema tancant la pàgina
-- al mig d'una sessió amb el telèfon ja aparellat. El gest queda a la cua i aquí no el llegeix ningú (`poll_the_finger` només mira
-- dits); quan l'app s'acaba, el sistema recupera els tres tocs tot sol, que és la mà de `ui::give_back_the_glass` i no una promesa
-- d'aquesta pàgina (docs/30, docs/46).
if (touch.triple_tap_close ~= nil) then touch.triple_tap_close(false) end

local caps = ui.caps()
local view = { width = caps.width, height = caps.height }
if view.width <= 0 or view.height <= 0 then
  console.print('bt hid: this board has no view')
  return
end

local kMargin = 8
-- **Els botons, una mica més alts i més separats** (0.190.209, l'amo: *«fes els botons una mica mes alts, tots i separa'ls una mica mes
-- entre ells»*): the row is the height of a control of this page, and the gap is the same inside the row and between rows.
local kRowH = 58
-- **El botó fa tota l'amplada**: és l'únic control de la pàgina d'espera, i una paraula que no hi cap («Turn Blu…») és un botó que una
-- persona ha d'endevinar (trobat a l'emulador el 22/11).
local kButtonW = 520
-- **El forat entre els tres botons del ratolí** (0.190.267): the same 12 px the old row of this page used, o sigui que la fila es llegeix
-- com tres caixes i no com una sola caixa partida.
local kGap = 12

-- **El text es demana en píxels i la font respon amb els passos que té** (l'amo: *«quan obris bluetooth i tota la pesca espero que ho
-- notifiquis al log o a la vista de l'app (amb text a tamany decent!)»*): the size is what this page wants a line to take, and the
-- same file stays readable on a small board and on a big one (docs/45).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end

  local scale = math.floor(line_px / step + 0.5)
  if scale < 1 then scale = 1 end
  if scale > 16 then scale = 16 end

  return scale
end

-- **Els dos números de cada mida, i no un de sol**: `draw_block` demana **píxels** —la lletra més gran que hi cap en una línia d'aquella
-- alçada— i un control demana **passos** (`scale`), i confondre'ls és una pàgina que es pinta a la mida més petita que té (trobat a
-- l'emulador el 22/11: el títol sortia de set píxels amb `want=5` al registre, que era una escala i no una alçada).
local kSmallPx, kBodyPx, kBigPx = 20, 30, 52
-- I el commutador del ràdio és un control i vol passos, o sigui la mateixa alçada dita de l'altra manera.
local kBody = scale_for(kBodyPx)

-- **La forma de la vista**: quina de les dues. Res més —l'aparell connectat i el ràdio són el món, no la forma—.
local kStateBytes = 1024
local kLayout = 'v3'

-- **El que aquest fitxer declara abans de fer-lo servir**: Lua resol els noms en el moment de la crida i no de la definició, o sigui
-- que el que una funció tocarà s'ha d'haver dit aquí a dalt (docs/45).
local page = 1          -- 1 esperant, 2 viva
local note = ''         -- l'última cosa que ha passat, en una línia
local state = nil
local radio_button = nil
local the_buttons = {}  -- els tres botons del ratolí: esquerre, mig i dret
local drag = nil        -- { id, x, y, travelled }
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

local function save_state()
  local block = the_state()
  if block == nil then return end
  block:write(0, string.format('%s v%d %d\n', kLayout, page, #note))
end

local function read_state()
  local block = the_state()
  if block == nil then return nil end
  local raw = block:read(0, kStateBytes)
  if raw == nil or #raw == 0 then return nil end
  local layout_word, rest = raw:match('^(%S+) (.*)$')
  if layout_word ~= kLayout then return nil end  -- a shape from before is dropped, never migrated
  local v = rest:match('^v(%d+) %d+$')
  local which = tonumber(v or '')
  if which ~= 1 and which ~= 2 then which = 1 end
  return which
end

-- **El que passa es diu als dos llocs**: la línia de la pàgina és per a qui mira la placa, i el log és per a qui mira la consola i per
-- a qui, demà, haurà de saber on es va quedar el ràdio si es va quedar en algun lloc.
local function announce(text)
  note = text
  console.print('bt hid: ' .. text)
end

-- I el que la pàgina ja diu tota sola —la línia d'estat— només va al log.
local function inform(text)
  console.print('bt hid: ' .. text)
end

-- **El pad comença sota la fila dels tres botons** (0.190.267, l'amo: *«afegeix 2 o 3 botons a l'app “btHID Mouse” a la part de dalt
-- del panell que fa de mouse, sota la barra de sistema per fer de botons del mouse»*): the row is at the top of the view —which already
-- begins under the bar of the system— and everything below it is the pointer. The finger polls this rectangle, o sigui que un dit que
-- cau en un botó no és un moviment ni un clic del pad, i els dos camins no es barregen (docs/30).
local function pad_rect()
  return kMargin, kMargin * 2 + kRowH, view.width - kMargin * 2, view.height - kRowH - kMargin * 3
end

-- ================================================================ the radio

-- **La ràdio no s'encén tota sola** (0.190.209) i ara tampoc no hi ha res a triar: aquesta placa és un ratolí, i això es desa a `cfg`
-- abans d'encendre res, perquè el nucli arrenca el dispositiu amb el que hi ha desat i una placa que es va deixar com a teclat tornaria
-- com a teclat (docs/30, docs/50).
local function radio_on()
  if not bt.available() then
    announce('this board has no radio')
    return false
  end
  announce('bringing the radio up as a mouse')
  local mode, why = cfg.btmode('mouse')
  if mode == nil then
    announce(string.format('this board cannot keep the choice (%s)', tostring(why ~= nil and why.code or 'no reason')))
    return false
  end
  if not cfg.bt(true) then
    announce('the radio did not come up')
    return false
  end
  local got, dev_why = bt.device('mouse')
  if got == nil then
    announce(string.format('this board cannot be a mouse (%s)', tostring(dev_why ~= nil and dev_why.code or 'no reason')))
    return false
  end
  announce(string.format('radio up: this board is a %s, offered as \'%s\'', got, bt.name()))
  return true
end

local function radio_off()
  cfg.bt(false)
  drag = nil
  announce('bluetooth is off: nothing is offered to anybody')
  return true
end

-- **Connectat i escoltat són dues coses** (0.190.208): `bt.connected()` diu que hi ha un aparell a l'altre costat; `bt.ready()` diu que
-- a més a més ha encès els reports, que és el que fa que un moviment hi arribi. Entremig hi ha un moment en què la placa envia i l'altre
-- cap encara no rep res —el que una persona veu com «connecta però no envia»— i aquesta línia és la que ho diu (docs/30, docs/50).
local function state_words()
  if not cfg.bt() then return 'Bluetooth is off.' end
  if not bt.connected() then return 'Waiting for a device...' end
  if bt.ready ~= nil and not bt.ready() then return 'Connected. The other device is switching the reports on.' end
  return 'A device is connected: this page is its pointer.'
end

-- ================================================================ the drawing

local function a_line(scale)
  local h = ui.line_height(scale)
  if h <= 0 then h = 8 * scale end
  return h
end

-- **La mida més gran que hi cap** (i no una de triada a ull): the page asks for a size in pixels and the face answers with the largest
-- step that still fits the width of this board (docs/45).
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
    y = draw_block(kMargin, y, 'btHID Mouse', kBigPx, 1, room, 0) + 18
    y = draw_block(kMargin, y, 'This board is a mouse: the empty page is the pointer of the other device.', kBodyPx, 2, room, 0) + 10
    y = draw_block(kMargin, y, 'Pair with this name:', kBodyPx, 1, room, 0) + 6
    y = draw_block(kMargin, y, bt.available() and bt.name() or '(no radio)', kBodyPx, 1, room, 0) + 12
    y = draw_block(kMargin, y, state_words(), kBodyPx, 2, room, 0)
    if #note > 0 then draw_block(kMargin, y + 10, note, kSmallPx, 2, room, 0) end
  else
    -- **La pàgina viva és el pad i prou**: the frame is what says a finger has somewhere to land, and the two lines at the top say what
    -- it does —a hint that costs no repaint, because it never changes while the pad is alive— (docs/30).
    local x, y, w, h = pad_rect()
    ui.frame(x, y, w, h, 8)
    local inside = w - 24
    local top = draw_block(x + 12, y + 12, 'Drag here to move the pointer - tap to click', kSmallPx, 1, inside, 8) + 6
    top = draw_block(x + 12, top, state_words(), kBodyPx, 2, inside, 8) + 8
    draw_block(x + 12, top, note, kSmallPx, 2, inside, 8)
  end
  ui.present(clean and 'gc16' or 'du')
end

-- **El que no toca no és a la pàgina**: off the glass the control is not drawn and the finger does not find it.
layout = function()
  if radio_button == nil then return end
  if page == 1 then
    radio_button.title = cfg.bt() and 'Turn Bluetooth off' or 'Turn Bluetooth on'
    radio_button:resize(view.width - kMargin * 2, kRowH)
    radio_button:place(kMargin, view.height - kMargin - kRowH)
  else
    radio_button:place(0, view.height + 400)
  end
  -- **Els tres botons, en una fila que es reparteix l'amplada** (l'amo: *«a la part de dalt del panell … sota la barra de sistema»*):
  -- three equal boxes with the same gap, and they are off the glass while the page is waiting, on the page the switch is. The width is
  -- the room left after the margins and the two gaps, split in three, i el que no hi cap no és un botó que s'encongeixi a res.
  local room = view.width - kMargin * 2 - kGap * 2
  local each = math.floor(room / 3)
  for i, b in ipairs(the_buttons) do
    b:resize(each, kRowH)
    if page == 2 then
      b:place(kMargin + (i - 1) * (each + kGap), kMargin)
    else
      b:place(0, view.height + 400)
    end
  end
end

-- ================================================================ the mouse

-- **El dit que dona el moviment**: a finger that starts on the pad is tracked by its id and every frame is a relative movement; the one
-- that lifts where it fell, without walking, is a click. **El moviment el parteix el sistema** si passa de cent vint-i-set píxels, o
-- sigui que el que aquí es compta és el que el dit ha caminat de debò (docs/30, docs/50).
local function poll_the_finger()
  if page ~= 2 or not bt.connected() then
    drag = nil
    return
  end
  local fingers = touch.read()
  if drag == nil then
    local x, y, w, h = pad_rect()
    for _, f in ipairs(fingers) do
      if f.x >= x and f.x < x + w and f.y >= y and f.y < y + h then
        drag = { id = f.id, x = f.x, y = f.y, travelled = 0 }
        break
      end
    end
    return
  end
  local seen = nil
  for _, f in ipairs(fingers) do
    if f.id == drag.id then
      seen = f
      break
    end
  end
  if seen ~= nil then
    local dx = seen.x - drag.x
    local dy = seen.y - drag.y
    if dx ~= 0 or dy ~= 0 then
      bt.move(dx, dy)
      drag.travelled = drag.travelled + math.abs(dx) + math.abs(dy)
      drag.x = seen.x
      drag.y = seen.y
    end
    return
  end
  -- The finger has gone: one that did not walk was a click, and one that walked already sent what it moved. **Un toc no repinta res**:
  -- the click happens on the other device, i this page is the same page (docs/50).
  if drag.travelled < 24 then bt.click('left') end
  drag = nil
end

-- ================================================================ the page

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
  console.print('bt hid: there is no room for the switch of the radio')
end

-- **Els tres botons del ratolí** (0.190.267): esquerre, mig i dret, en una fila sota la barra del sistema. Un botó que el dit prem és
-- un `bt.click` de la paraula que porta, i **el pad no el veu** perquè el rectangle del punter comença sota la fila. El que no s'ha
-- d'amagar és un clic que no surt: quan no hi ha ningú a l'altre cap es diu a la línia i al registre, i si surt no es diu res —un
-- ratolí que escriu al registre cada clic és un registre que no es pot llegir (docs/30, docs/50).
for _, one in ipairs({ { 'Left', 'left' }, { 'Middle', 'middle' }, { 'Right', 'right' } }) do
  local b = ui.button(kButtonW, kRowH)
  if b ~= nil then
    b.scale = kBody
    b.title = one[1]
    local word = one[2]
    b.on_click = function()
      if not bt.click(word) then inform('the ' .. word .. ' button of the mouse did not go out') end
    end
    the_buttons[#the_buttons + 1] = b
  end
end
if #the_buttons == 0 then console.print('bt hid: there is no room for the buttons of the mouse') end

-- **El que s'ha deixat la persona**: la vista. L'aparell connectat no hi és —és el món i no la forma— i no hi ha res més a recordar,
-- perquè aquesta placa és un ratolí i prou (docs/30).
local restored = read_state()
if restored == nil then restored = 1 end
page = (restored == 2 and bt.connected() and cfg.bt()) and 2 or 1
if cfg.bt() then inform('the radio is already up, as it was left') end
if page == 2 then inform('back to the pad: a device is connected') end

layout()
paint(true)

-- **La vista canvia de mida quan el sistema en dona una altra** (docs/30): the pad is what the view leaves it, o sigui que el que s'ha
-- de fer és tornar a col·locar el botó i tornar a pintar.
function on_resize()
  local fresh = ui.caps()
  view.width = fresh.width
  view.height = fresh.height
  layout()
  paint(true)
end

function on_pause() save_state() end
function on_save() save_state() end
function on_close() save_state() end

-- **El bucle de la pàgina**: vint-i-cinc mil·lisegons és el que el vidre d'aquesta placa triga a donar un fotograma del dit, o sigui que
-- el cursor de l'altre aparell es mou tan fi com el d'aquí. El que mira cada volta és el dit i, quan s'espera, si algú s'ha connectat.
local waited = 0
-- **El que la pàgina ha de veure canviar sense que ella ho hagi fet** (0.190.209): la ràdio la pot encendre o apagar una altra cosa —el
-- commutador de `cfg`, el prompt, una pausa— i un aparell pot arribar o marxar. Es mira cada dos-cents mil·lisegons i només es repinta
-- quan el que hi ha pintat ja no és cert, perquè aquest vidre cobra cada passada.
local was_on = cfg.bt()
local was_connected = bt.connected()
while true do
  sys.delay(25):await()
  if page == 1 then
    waited = waited + 25
    if waited >= 200 then
      waited = 0
      local now_on = cfg.bt()
      local now_connected = bt.connected()
      if now_on ~= was_on or now_connected ~= was_connected then
        was_on = now_on
        was_connected = now_connected
        layout()
        paint(false)
      end
      if now_connected and now_on then
        inform('a device is connected, the page is the pad')
        note = ''
        page = 2
        drag = nil
        layout()
        paint(true)
        save_state()
      end
    end
  elseif not bt.connected() then
    -- **L'altre aparell ha marxat**: es torna a esperar i es diu, perquè una pàgina que segueix essent un ratolí sense ningú a l'altre
    -- cap és una pàgina que no fa res i no ho diu. **I si el que s'ha apagat és el Bluetooth, també es torna enrere**.
    drag = nil
    page = 1
    was_on = cfg.bt()
    was_connected = false
    announce(cfg.bt() and 'the device went away, waiting again' or 'bluetooth is off, so the pad is not sending anything')
    layout()
    paint(true)
    save_state()
  else
    poll_the_finger()
  end
end
