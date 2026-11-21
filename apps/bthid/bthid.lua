-- bthid: this board, as a Bluetooth keyboard and a mouse for another device (0.190.204).
--
-- **Què és aquesta pàgina** (l'amo: *«fem una app lua, una app per aixicar un bluetooth HID device amb un teclat i un mouse. En quan
-- algú es conecti es passa a la seguent vista, una panalla totalment buida serà el mouse i es trasmitrà amb el dit… El teclat gracies
-- a un Textbox»*). Dues vistes i prou:
--
--   - **Esperant**: el nom amb què aquesta placa s'anuncia i el que una persona ha de fer a l'altre aparell. Surt de la pàgina
--     quan `bt.connected()` diu que sí, i no abans.
--   - **Viva**: la pàgina buida és el ratolí —el dit que hi camina mou el cursor de l'altre aparell, i un toc és un clic— i la
--     caixa de text de baix és el teclat: el que s'hi escriu marxa com a tecles premudes, lletra per lletra.
--
-- **La placa ha de ser les dues coses alhora**, i això és el mode `both` del HID (0.190.204): el mapa del report porta dos report
-- IDs —el teclat l'1 i el ratolí el 2— i el report de cada cosa va per la seva característica. **El preu és el boot protocol**, o sigui
-- que aquesta pàgina és per a un mòbil o un ordinador i no per a la BIOS d'un PC; l'altre aparell ho ha de tornar a aparellar un cop,
-- perquè va llegir el mapa de quan aquesta placa era només un teclat (docs/50).
--
-- **Res es pinta mentre el dit camina**: el cursor és a l'altre aparell, o sigui que el ratolí d'aquesta pàgina no costa ni una passada
-- de tinta. L'únic que es repinta és el que diu què ha passat —les tecles que han sortit, l'aparell que ha marxat— i això és una línia.

local caps = ui.caps()
local view = { width = caps.width, height = caps.height }
if view.width <= 0 or view.height <= 0 then
  console.print('bthid: this board has no view')
  return
end

local kMargin = 8
-- **Els botons, una mica més alts i més separats** (0.190.209, l'amo: *«fes els botons una mica mes alts, tots i separa'ls una mica mes
-- entre ells»*): the row is the height of a control of this page —the box, the Enter, the switch of the radio and the three modes— i el forat
-- és el mateix a la fila i entre files, o sigui que la pàgina es llegeix com una graella i no com una pila de caixes que es toquen.
local kRowH = 58
-- **«Enter» hi ha de cabre de debò**: a 58 la paraula no hi era i el botó deia «…»; a 76 encara no hi era, i la lletra és la del camí que
-- dibuixa, o sigui que l'amplada es demana amb la paraula i no amb un número triat a ull (0.190.208).
local kButtonW = 96
local kGap = 12

-- **El text es demana en píxels i la font respon amb els passos que té** (l'amo: *«quan obris bluetooth i tota la pesca espero que ho
-- notifiquis al log o a la vista de l'app (amb text a tamany decent!)»*). O sigui que la mida no és un número triat a ull per a una
-- pantalla: és el que aquesta pàgina vol que ocupi una línia, i el mateix fitxer queda llegible en una placa petita i en una de gran
-- (docs/45, el mateix truc que el rellotge).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end

  local scale = math.floor(line_px / step + 0.5)
  if scale < 1 then scale = 1 end
  if scale > 16 then scale = 16 end

  return scale
end

-- Three sizes are all this page needs, and the state of the radio is the middle one: it is the line a person has to read from a
-- step away, o sigui que no pot ser la lletra petita.
local kSmall = scale_for(20)
local kBody = scale_for(30)
local kBig = scale_for(52)

-- **La forma de la vista** (docs/30 § «La sessió `state`»): quina de les dues i què hi ha a la caixa. Res més —l'aparell connectat i
-- el mode de la ràdio són el món, no la forma—.
local kStateBytes = 1024
-- **v2 i no v1** (0.190.208): el que la persona ha deixat és la vista, el que hi ha a la caixa **i què és aquesta placa** —un teclat,
-- un ratolí o les dues coses—, perquè tornar d'una pausa essent un ratolí quan se n'havia anat com a teclat seria desfer una decisió
-- que no és de la vista sinó d'ella. Una forma vella es descarta i no es migra, que és la regla (docs/30).
local kLayout = 'v2'

-- **El que aquest fitxer declara abans de fer-lo servir**: Lua resol els noms en el moment de la crida i no de la definició, o sigui
-- que el que una funció tocarà s'ha de dir aquí a dalt o la funció estarà escrivint en un global que no existeix (docs/45).
local page = 1         -- 1 esperant, 2 viva
local mode = 'both'    -- 'keyboard', 'mouse' o 'both': què és aquesta placa al Bluetooth
local note = ''
local typed = ''       -- el que hi ha a la caixa ara
local sent = ''        -- el que ja ha sortit cap a l'altre aparell
local state = nil
local box = nil
local enter = nil
local drag = nil       -- { id, x, y, travelled }
local paint = nil
local layout = nil
-- **Els tres botons de què és aquesta placa, declarats aquí i no més avall** (0.190.208): `layout` els col·loca, i una funció que llegeix un
-- nom que encara no és un `local` llegeix un global… que no existeix. És el parany que el comentari del capdamunt del fitxer avisa i que ja va
-- fer petar aquesta pàgina una vegada (docs/45).
local choices = {}
local radio_button = nil   -- **el botó d'encendre i apagar el Bluetooth** (0.190.209)
local kChoices = { { word = 'keyboard', title = 'Keyboard' }, { word = 'mouse', title = 'Mouse' },
                   { word = 'both', title = 'Both' } }

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
  block:write(0, string.format('%s v%d %d %s\n%s', kLayout, page, #typed, mode or '-', typed))
end

local function read_state()
  local block = the_state()
  if block == nil then return nil end
  local raw = block:read(0, kStateBytes)
  if raw == nil or #raw == 0 then return nil end
  local layout_word, rest = raw:match('^(%S+) (.*)$')
  if layout_word ~= kLayout then return nil end  -- a shape from before is dropped, never migrated
  local v, count, which_mode, text = rest:match('^v(%d+) (%d+) (%S+)\n(.*)$')
  if v == nil then return nil end
  -- **Un guió és «encara no s'ha triat»** (0.190.209) i no un mode per defecte: el que la persona no ha triat no ho tria la pàgina.
  if which_mode ~= 'keyboard' and which_mode ~= 'mouse' and which_mode ~= 'both' then which_mode = nil end
  local wanted = tonumber(count) or 0
  text = text or ''
  if #text > wanted then text = text:sub(1, wanted) end
  local which = tonumber(v) or 1
  if which ~= 1 and which ~= 2 then which = 1 end
  return which, text, which_mode
end

-- **El que passa es diu, i es diu als dos llocs**: la línia de la pàgina és per a qui mira la placa, i el log és per a qui mira la
-- consola i per a qui, demà, haurà de saber on es va quedar la ràdio si es va quedar en algun lloc (0.190.206).
local function announce(text)
  note = text
  console.print('bthid: ' .. text)
end

-- I el que passa molts cops —una tecla, un retrocés— només és de la pàgina: el log és per als passos, no per al compte de les lletres.
local function say(text)
  note = text
end

-- I el que la pàgina ja diu tota sola —la línia d'estat— només va al log: repetir-ho a sota com a nota és dir dues vegades el mateix.
local function inform(text)
  console.print('bthid: ' .. text)
end

-- The pad is everything above the row of the box. A finger that starts in it is a mouse; one that starts on the box is a person
-- typing, and the two never get mixed (docs/30).
-- **I el pad s'acaba on comença la fila dels controls** (0.190.209): the old sum left the frame crossing the row of the box and the box's own
-- white hid it —a line that is not seen is a line that is not there, but the day the two move apart the frame would be drawn over the
-- control—. A mouse-only page has no box, o sigui que el pad s'ho queda tot fins al marge.
local function pad_rect()
  local bottom = (mode == 'mouse') and (kMargin) or (kRowH + kGap * 2)
  return kMargin, kRowH + kGap, view.width - kMargin * 2, view.height - kRowH - kGap - bottom
end

-- **El que s'ha enviat i el que hi ha**: the box is the keyboard, o sigui que el que compta és la diferència amb el que ja ha sortit
-- —el que s'ha esborrat són retrocessos i el que s'ha afegit són les tecles, en aquest ordre— i no el text sencer un altre cop.
local function send_the_difference()
  if mode == 'mouse' then return end  -- a mouse has no box, and a box that sends nothing would only look like one (docs/30)
  if not bt.connected() then return end
  local common = 0
  local shortest = math.min(#typed, #sent)
  while common < shortest and typed:byte(common + 1) == sent:byte(common + 1) do common = common + 1 end
  local erased = #sent - common
  for _ = 1, erased do bt.type('\b') end
  local added = typed:sub(common + 1)
  local went = 0
  if #added > 0 then went = bt.type(added) end
  sent = typed
  if erased > 0 or went > 0 then
    say(string.format('%d key(s) out, %d back', went, erased))
    paint(false)
  end
end

-- **El ratolí és la pàgina buida**: the finger that starts on the pad is tracked by its id and every frame is a relative movement; the
-- one that lifts where it fell, without walking, is a click.
local function poll_the_finger()
  if page ~= 2 or mode == 'keyboard' or not bt.connected() then
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
  -- The finger has gone: one that did not walk was a click, and one that walked already sent what it moved.
  if drag.travelled < 24 then bt.click('left') end
  drag = nil
end

-- **Res no s'encén sol** (0.190.209, l'amo: *«un botó per encendre i apagar el bluetooth. Que no ho faci l'app automátic a l'arrencar. I
-- configurar-se com a dev tampoc sense que hagi escollit si un o l'altre o ambdós»*). Obtenir la ràdio i dir què és aquesta placa costa
-- trenta kilobytes de heap i canvia el que l'altre aparell veu, o sigui que és una decisió d'una persona i no un efecte d'obrir una pàgina:
-- la pàgina s'obre, diu com està el món, i no toca res fins que un dit prem un botó. Cada pas es diu al log abans de fer-lo, perquè un pas
-- que es queda a mitges ha de deixar la línia que diu on es va quedar.
local function radio_on()
  if not bt.available() then
    announce('this board has no radio')
    return false
  end
  if mode == nil then
    announce('choose what this board is first: a keyboard, a mouse, or both')
    return false
  end
  announce('bringing the radio up')
  if not cfg.bt(true) then
    announce('the radio did not come up')
    return false
  end
  announce(string.format('radio up, this board is \'%s\'', bt.name()))
  local got, why = bt.device(mode)
  if got == nil then
    announce(string.format('this board cannot be a %s (%s)', mode, tostring(why ~= nil and why.code or 'no reason')))
    return false
  end
  mode = got
  -- **Un «pair it again» només si hi havia algú abans**: acabar d'encendre la ràdio i demanar que es torni a aparellar seria demanar una
  -- cosa que encara no s'ha fet mai; el que canvia el mapa d'un vincle vell és canviar de mode (docs/50).
  if bt.connected() then
    announce(string.format('this board is a %s now: pair it again from the other side', got))
  else
    announce(string.format('this board is a %s, offered to the other devices as \'%s\'', got, bt.name()))
  end
  return true
end

local function radio_off()
  cfg.bt(false)
  drag = nil
  announce('bluetooth is off: nothing is offered to anybody')
  return true
end

-- **El que diu la pàgina, de dalt a baix i amb la mida de cada cosa** (0.190.206): the title, what this board is, the name a person
-- looks for in the list of the other device, and the state of the radio. The state is the line that has to be read from a step away,
-- so it wears the body and not the small face — and the note, when there is one, sits at the foot of the page where a line that
-- changes does not push the rest around.
local function a_line(scale)
  local h = ui.line_height(scale)
  if h <= 0 then h = 8 * scale end
  return h
end

-- **La mida més gran que hi cap** (i no una de triada a ull): the page asks for a size in pixels and the face answers with the largest
-- step that still fits the width of this board. A board narrower than another says the same thing, a size smaller, and never a
-- sentence cut in half by the edge of the paper (docs/45 — the same «ask the face» the clock does).
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

-- The biggest step whose wrapped text still takes `max_lines` lines of the width there is.
local function block_that_fits(text, want_px, max_lines, room)
  for s = scale_for(want_px), 1, -1 do
    local lines = wrap_at(text, s, room)
    if #lines <= max_lines then return s, lines end
  end
  return 1, wrap_at(text, 1, room)
end

-- **Una línia, o dues, i el que queda per sota**: draws and answers where the next thing goes.
local function draw_block(x, y, text, want_px, max_lines, room, ink)
  if text == nil or #text == 0 then return y end
  local s, lines = block_that_fits(text, want_px, max_lines or 1, room)
  for i, l in ipairs(lines) do
    ui.text(x, y + (i - 1) * a_line(s), l, s, ink)
  end
  return y + #lines * a_line(s)
end

-- **Connectat i escoltat són dues coses** (0.190.208): `bt.connected()` diu que hi ha un aparell a l'altre costat; `bt.ready()` diu que a
-- més a més ha encès els reports, que és el que fa que una tecla hi arribi. Entremig hi ha un segon en què la placa envia i l'altre cap
-- encara no rep res —el que una persona veu com «connecta però no envia»— i aquesta línia és la que ho diu.
local function state_words()
  -- **Una ràdio encesa sense tria no és una pàgina que espera algú**: `cfg.bt` és el commutador del teclat i el que ofereix és el teclat de
  -- sèrie de la ràdio; si aquesta pàgina no ha triat res, ho diu en comptes de dir «esperant» sobre una cosa que no ha decidit (0.190.209).
  if mode == nil then
    return cfg.bt() and 'Bluetooth is on, and nothing has been chosen.' or 'Bluetooth is off.'
  end
  if not cfg.bt() then return 'Bluetooth is off.' end
  if not bt.connected() then return 'Waiting for a device...' end
  if bt.ready ~= nil and not bt.ready() then return 'Connected. The other device is switching the reports on.' end
  return 'A device is connected.'
end

paint = function(clean)
  ui.clear(15)
  if page == 1 then
    local room = view.width - kMargin * 2
    local y = kMargin
    y = draw_block(kMargin, y, 'Bluetooth', 52, 1, room, 0) + 14
    y = draw_block(kMargin, y, 'What this board is:', 30, 1, room, 0) + 8
    local what = 'Not chosen yet'
    if mode == 'mouse' then
      what = 'A mouse only'
    elseif mode == 'keyboard' then
      what = 'A keyboard only'
    elseif mode == 'both' then
      what = 'A keyboard and a mouse'
    end
    y = draw_block(kMargin, y, what, 40, 1, room, 0) + 12
    y = draw_block(kMargin, y, 'Pair with this name:', 30, 1, room, 0) + 8
    y = draw_block(kMargin, y, bt.available() and bt.name() or '(no radio)', 44, 1, room, 0) + 12
    y = draw_block(kMargin, y, state_words(), 30, 2, room, 0)
    if #note > 0 then
      local s, lines = block_that_fits(note, 26, 2, room)
      draw_block(kMargin, y + 10, note, 26, 2, room, 0)
    end
  elseif mode ~= 'keyboard' then
    local x, y, w, h = pad_rect()
    ui.frame(x, y, w, h, 8)
    local room = w - 24
    -- **El que diu la pàgina va a dalt, en una columna, i el que queda és el ratolí**: a note at the foot of the pad would sit on the
    -- row of the box and lose its second line behind it —the pad ends where that row begins— o sigui que el que canvia es posa sota
    -- l'estat, i el pad sencer de sota segueix essent el dit (0.190.206).
    local top = draw_block(x + 12, y + 12, 'Drag here to move the pointer - tap to click', 22, 2, room, 8) + 10
    top = draw_block(x + 12, top, state_words(), 34, 2, room, 8) + 12
    draw_block(x + 12, top, note, 28, 2, room, 8)
  else
    -- **Un teclat sol no té ratolí**: the page says it instead of leaving a pad that a finger walks on and nothing happens.
    local room = view.width - kMargin * 2
    local y = kMargin
    y = draw_block(kMargin, y, 'A keyboard only', 44, 1, room, 0) + 12
    y = draw_block(kMargin, y, 'What you type in the box goes to the other device.', 30, 2, room, 0) + 10
    y = draw_block(kMargin, y, state_words(), 30, 2, room, 0)
    draw_block(kMargin, y + 10, note, 26, 2, room, 0)
  end
  ui.present(clean and 'gc16' or 'du')
end

-- **El que no toca no és a la pàgina**: off the glass the control is not drawn and the finger does not find it, que és el que fa que
-- la vista d'espera no tingui cap caixa de text que no hi pinta res (el mateix truc que el rellotge amb la seva).
layout = function()
  if box ~= nil then
    if page == 2 and mode ~= 'mouse' then
      box:resize(view.width - kMargin * 2 - kButtonW - kGap, kRowH)
      box:place(kMargin, view.height - kMargin - kRowH)
    else
      box:place(0, view.height + 400)
    end
  end
  if enter ~= nil then
    if page == 2 and mode ~= 'mouse' then
      enter:place(view.width - kMargin - kButtonW, view.height - kMargin - kRowH)
    else
      enter:place(0, view.height + 400)
    end
  end
  -- **Els tres botons de què és aquesta placa, a la pàgina d'espera i només allà**: a la vista viva el que hi ha és el dit i la caixa, i
  -- canviar de mode amb una mà al mig del ratolí seria canviar el mapa del report sota el cursor de l'altre cap (docs/30, docs/50).
  if radio_button ~= nil then
    if page == 1 then
      radio_button.title = cfg.bt() and 'Turn Bluetooth off' or 'Turn Bluetooth on'
      radio_button:resize(view.width - kMargin * 2, kRowH)
      radio_button:place(kMargin, view.height - kMargin - kRowH * 2 - kGap)
    else
      radio_button:place(0, view.height + 400)
    end
  end
  local each = math.floor((view.width - kMargin * 2 - kGap * (#choices - 1)) / #choices)
  for i, c in ipairs(choices) do
    if page == 1 then
      c.button:resize(each, kRowH)
      c.button:place(kMargin + (i - 1) * (each + kGap), view.height - kMargin - kRowH)
    else
      c.button:place(0, view.height + 400)
    end
  end
end

box = ui.textbox(2, 2)
if box == nil then
  console.print('bthid: there is no room for the box')
  return
end
box.scale = kBody
box.keyboard = 'alpha'
box.text = ''
-- **Cada canvi és el que s'envia**: the box is only the keyboard, o sigui que el que s'escriu marxa de seguida i el text que queda a la
-- caixa és el mirall del que ha sortit (docs/30).
box.on_change = function(self, text)
  typed = text or ''
  sent = sent or ''
  send_the_difference()
  save_state()
end

-- **Els tres botons de què és aquesta placa** (0.190.208, l'amo: *«com escullo el mouse?»*): keyboard, mouse or both, i el que està triat
-- es veu pel títol. Canviar-ho és tornar a anunciar-se amb el mapa nou, o sigui que el que diu la nota és que l'altre cap s'ha de tornar
-- a aparellar —un amfitrió que va llegir el mapa d'un teclat no endevina el d'un ratolí (docs/50)—.
local function choose(what)
  local got, why = cfg.btmode(what)
  if got == nil then
    -- **El segon valor d'un error és una taula** (`{code, message}`) i no una cadena: concatenar-la peta la pàgina, que és el que va passar
    -- amb `attempt to concatenate a table value` (0.190.210). El que es diu és el que aquesta pàgina sap, no el que la crida ha tornat.
    announce(string.format('this board cannot be a %s (%s)', what, tostring(why ~= nil and why.code or 'no reason')))
    paint(false)
    return
  end
  mode = got
  if mode == 'mouse' then
    -- **Un ratolí no té caixa**: el que hi havia escrit ja ha sortit o no sortirà, i deixar-ho a la vista d'una pàgina que no envia tecles
    -- seria mentir sobre el que fa (docs/30).
    typed = ''
    sent = ''
    if box ~= nil then box.text = '' end
  end
  -- **Triar no encén res**: si el Bluetooth és apagat, el que s'ha triat queda desat i la pàgina ho diu; encendre'l és el botó del costat
  -- (0.190.209: *«configurar-se com a dev tampoc sense que hagi escollit»*).
  if cfg.bt() then
    announce(string.format('this board is a %s now: pair it again from the other side', got))
  else
    announce(string.format('this board will be a %s; turn Bluetooth on when you want it', got))
  end
  layout()
  paint(false)
  save_state()
end

-- **El botó de la ràdio**: encendre i apagar, i res més. Encendre sense haver triat no fa res i ho diu, perquè una placa que s'ofereix
-- sense que ningú hagi dit què és és una placa que ha decidit per la persona.
local function flip_the_radio()
  if cfg.bt() then
    radio_off()
  else
    radio_on()
  end
  layout()
  paint(false)
  save_state()
end

enter = ui.button(kButtonW, kRowH)
if enter ~= nil then
  enter.title = 'Enter'
  enter.scale = kSmall
  enter.on_click = function()
    if not bt.connected() then
      say('nobody is connected')
      paint(false)
      return
    end
    bt.type('\n')
    say('Enter out')
    paint(false)
  end
end

-- **El botó de la ràdio, a sobre dels tres modes**: el títol diu què farà, no què passa, perquè un botó que diu «Bluetooth off» i que apaga
-- el Bluetooth és un botó que s'ha de llegir dues vegades (0.190.209).
radio_button = ui.button(view.width - kMargin * 2, kRowH)
if radio_button ~= nil then
  radio_button.scale = kSmall
  radio_button.title = 'Turn Bluetooth on'
  radio_button.on_click = function() flip_the_radio() end
else
  console.print('bthid: there is no room for the switch of the radio')
end

for _, c in ipairs(kChoices) do
  local button = ui.button(80, kRowH)
  if button ~= nil then
    button.title = c.title
    button.scale = kSmall
    button.on_click = function() choose(c.word) end
    choices[#choices + 1] = { word = c.word, button = button }
  end
end

-- **El que s'ha deixat la persona**: the view and what the box held. The device that is connected is not here —that is the world and
-- not the shape— i el que ja ha sortit és el text restaurat, o sigui que una pausa no reenvia res.
-- **Una sessió que no hi és no és una pausa que s'ha perdut** (0.190.205, i la placa ho va dir bé: *«on_pause: …: attempt to get length
-- of a nil value (upvalue 'typed')»*). `read_state` torna `nil` quan el bloc és buit o d'una altra forma, i un `nil` que arriba a
-- `typed` trenca el `#typed` del desament i, amb ell, tots els ganxos de sortida. El que aquesta pàgina llegeix es mira abans de
-- fer-lo servir, i el que no hi és és la primera vegada (docs/30, docs/45).
local restored_page, restored_text, restored_mode = read_state()
if restored_page == nil then
  restored_page = 1
  restored_text = ''
  restored_mode = nil
end
-- **La tria és de la placa i no de la pàgina** (0.190.208): `cfg.btmode` la desa, i el nucli esborra les sessions a cada arrencada —o sigui
-- que una tria que només visqués a la sessió s'hauria de fer cada cop que es reinicia la placa. El que la persona va triar fa un moment
-- (`restored_mode`) mana sobre el que hi ha desat, perquè és el més recent. **I si no ha triat mai res, no hi ha mode**: `cfg.btmode()`
-- torna la cadena buida quan la clau no hi és, i aquesta pàgina la llegeix com «encara no» en comptes de triar per ella (0.190.209).
mode = restored_mode
if mode == nil or mode == '' then
  local stored = cfg.btmode()
  if stored ~= nil and stored ~= '' then mode = stored end
end
typed = restored_text
sent = restored_text

-- **Res no s'encén aquí** (0.190.209): the page opens and says how the world is —the radio on or off, what this board would be, the name— and
-- it does not obtain the radio, and it does not make this board a device, until a finger presses one of the buttons. *«Que no ho faci l'app
-- automátic a l'arrencar»*, and the reason is that both things cost something a person did not ask for.
if cfg.bt() and mode ~= nil then
  inform('the radio is already up, as it was left')
end
page = (restored_page == 2 and bt.connected() and mode ~= nil and cfg.bt()) and 2 or 1
if page == 2 then
  box.text = restored_text
  note = ''
  inform('back to the pad: a device is connected')
end

layout()
paint(true)

-- **La vista canvia de mida quan la banda del teclat puja o baixa** (docs/30): the pad is what the view leaves it, o sigui que el que
-- s'ha de fer és tornar a col·locar la caixa i el botó i tornar a pintar. No es desa res: la forma no ha canviat.
function on_resize()
  local fresh = ui.caps()
  view.width = fresh.width
  view.height = fresh.height
  layout()
  paint(true)
end

-- **I el que una pausa no ha de fer perdre** (docs/30, docs/45): la vista i el que hi ha a la caixa. El que ja ha sortit és el mateix
-- text, o sigui que tornar no reenvia res.
function on_pause() save_state() end
function on_save() save_state() end
function on_close() save_state() end

-- **El bucle de la pàgina**: vint-i-cinc mil·lisegons és el que el vidre d'aquesta placa triga a donar un fotograma del dit, o sigui que
-- el cursor de l'altre aparell es mou tan fi com el d'aquí. El que mira cada volta és el dit i, quan s'espera, si algú s'ha connectat.
local waited = 0
-- **El que la pàgina ha de veure canviar sense que ella ho hagi fet** (0.190.209): la ràdio la pot encendre o apagar una altra cosa —el
-- commutador de `cfg`, el prompt, una pausa— i un aparell pot arribar o marxar. Es mira cada dos-cents mil·lisegons i només es repinta quan
-- el que hi ha pintat ja no és cert, perquè aquest vidre cobra cada passada.
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
      if now_connected and now_on and mode ~= nil then
        inform('a device is connected, the page is the pad')
        note = ''  -- the status line of the pad says it; l'avís de l'espera ja no hi fa res
        page = 2
        typed = ''
        sent = ''
        box.text = ''
        layout()
        paint(true)
        save_state()
      end
    end
  elseif not bt.connected() then
    -- **L'altre aparell ha marxat**: es torna a esperar i es diu, perquè una pàgina que segueix essent un ratolí sense ningú a l'altre
    -- cap és una pàgina que no fa res i no ho diu. **I si el que s'ha apagat és el Bluetooth, també es torna enrere**: la vista viva sense
    -- ràdio és una pàgina que envia al buit.
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
