-- **Device Lock**: la pantalla que amaga el que hi ha al vidre, amb el candau i els tres tocs.
--
-- It is a port of `OLD/PaperOS/src/apps/lock_app.cpp` and it has to be **the same picture**: the padlock is drawn with
-- the same percentages —a body as tall as it is wide with a stroke and not a slab of ink, a shackle of two thirds of
-- the body whose legs go into it, the plate of the keyhole and the two rivets at the foot— and the two sentences and
-- the three dots sit in the same places, with the same 3 seconds between one tap and the next (l'amo: *«vull una còpia
-- exacta tret de les crides a sistema»*).
--
-- What changes is only what the system calls are: PaperOS asked its host for the whole glass and for the way out and
-- hid an app that was still alive under it; here it is `ui.full_screen` and `app.exit`, which are this system's own
-- doors. And the taps: PaperOS counted the UP of a finger that had not travelled, which is what this system already
-- calls a **tap** and hands over in `touch.gesture()` (docs/30, docs/90).
--
-- **El dibuix no porta ni un gris**: paper i tinta, i la tinta només pel forat i els detalls. Això és el que fa que un
-- passi barat (`du`) pugui portar-lo i que el panell no s'hagi de rentar per ensenyar-lo (docs/50).

-- **Els tres tocs i la finestra**, que és el número de l'amo: tres tocs i no un altre número, i tres segons entre un i el
-- següent —no des del primer—, perquè el que separa és una persona que toca d'una mà que descansa al vidre.
local kTapCount = 3
local kTapWindowMs = 3000

-- **Els tres cercles del compte**: el radi, la distància entre ells i el gruix de l'anell del que encara es deu.
local kDotRadius = 9
local kDotStep = 34
local kRingWidth = 3

-- **Les dues frases**, amb les mides de la vista de PaperOS: el títol fa 57 píxels de línia i la indicació 33, que en
-- aquesta cara d'onze píxels per pas són cinc passos i tres.
local kTitleText = 'Device locked'
local kHintText = 'Tap three times to unlock'
local kTitleScale = 5
local kHintScale = 3

-- **Els forats de la vista de PaperOS** (`padLarge` 18, `gap` 10), que són el que separa el candau de la frase, la frase
-- de la indicació i la indicació dels cercles.
local kGapIcon = 48
local kGapTitle = 16
local kGapHint = 24

-- **Les proporcions del candau**, les de `lock_app.cpp` i les de `tools/make_lock_icon.py`: un sol joc de números per a la
-- vista i per a la icona de l'app.
local kBodyWPercent = 34
local kBodyHPercent = 95
local kLiftPercent = 45
local kShacklePercent = 62
local kRingPercent = 12
local kStrokePercent = 6
local kCornerPercent = 14
local kPlatePercent = 26
local kPlateThick = 4
local kKeyPercent = 8
local kRivetPercent = 4

-- **El compte**, que és l'única cosa que aquesta pàgina té per dir dins de l'app.
local taps = 0
local lastTapMs = 0

-- **On va cada cosa**, en píxels de la vista: es torna a fer quan el vidre canvia de mida o de gir.
local box = {}

-- ------------------------------------------------------------------ el dibuix

-- **Un rectangle amb els cantons rodons**, fet de dues barres i quatre discs: és el que aquest sistema no té com a
-- primitiva i el que el candau necessita per al cos i per a la nansa.
local function round_fill(x, y, w, h, radius, level)
  if radius <= 0 then
    ui.rect(x, y, w, h, level)
    return
  end
  ui.rect(x + radius, y, w - 2 * radius, h, level)
  ui.rect(x, y + radius, w, h - 2 * radius, level)
  ui.disc(x + radius, y + radius, radius, level)
  ui.disc(x + w - radius - 1, y + radius, radius, level)
  ui.disc(x + radius, y + h - radius - 1, radius, level)
  ui.disc(x + w - radius - 1, y + h - radius - 1, radius, level)
end

-- **El candau, amb les proporcions de la vista de PaperOS**: el cos és un contorn —el mateix requadre rodó dues vegades,
-- la segona en paper—, la nansa és un anell amb les cames dins del cos, i el negre massís és només la placa, el forat,
-- la ranura i els dos reblons.
local function paint_padlock(x, y, body_w)
  local body_h = body_w * kBodyHPercent // 100
  local lift = body_w * kLiftPercent // 100
  local body_top = y + lift
  local stroke = body_w * kStrokePercent // 100
  local corner = body_w * kCornerPercent // 100

  local hoop_w = body_w * kShacklePercent // 100
  local hoop_t = body_w * kRingPercent // 100
  local hoop_r = hoop_w // 2
  local hoop_x = x + (body_w - hoop_w) // 2
  round_fill(hoop_x, y, hoop_w, lift + hoop_r, hoop_r, 0)
  round_fill(hoop_x + hoop_t, y + hoop_t, hoop_w - 2 * hoop_t, lift + hoop_r - hoop_t, hoop_r - hoop_t, 15)

  round_fill(x, body_top, body_w, body_h, corner, 0)
  round_fill(x + stroke, body_top + stroke, body_w - 2 * stroke, body_h - 2 * stroke, corner - stroke, 15)

  local plate_r = body_w * kPlatePercent // 100
  local plate_t = body_w * kPlateThick // 100
  local key_r = body_w * kKeyPercent // 100
  local cx = x + body_w // 2
  local face_y = body_top + body_h // 2
  local hole_y = face_y - plate_r * 30 // 100
  local slot_w = key_r * 14 // 10
  local slot_y = face_y + plate_r * 45 // 100

  ui.disc(cx, face_y, plate_r, 0)
  ui.disc(cx, face_y, plate_r - plate_t, 15)
  ui.disc(cx, hole_y, key_r, 0)
  ui.rect(cx - slot_w // 2, hole_y, slot_w, slot_y - hole_y, 0)
  ui.disc(cx, slot_y, slot_w // 2, 0)

  local rivet_r = body_w * kRivetPercent // 100
  local rivet_y = body_top + body_h - stroke - rivet_r * 2
  ui.disc(x + body_w * 22 // 100, rivet_y, rivet_r, 0)
  ui.disc(x + body_w * 78 // 100, rivet_y, rivet_r, 0)
end

-- **Els tres cercles i prou**, que és tot el que canvia un toc: el que ha caigut és ple i el que encara es deu és un anell
-- de la mateixa mida, o sigui que la fila no canvia d'amplada mentre algú toca.
local function paint_dots()
  for i = 1, kTapCount do
    local cx = box.dots_x + (i - 1) * kDotStep + kDotRadius
    local cy = box.dots_y + kDotRadius
    ui.disc(cx, cy, kDotRadius, 0)
    if i > taps then ui.disc(cx, cy, kDotRadius - kRingWidth, 15) end
  end
end

-- **On va cada cosa**, amb les mateixes aritmètiques que la vista de PaperOS: tot surt del costat curt de la vista, o
-- sigui que el mateix candau és el mateix dibuix amb el panell tombat i només es mouen les frases del voltant.
local function arrange(width, height)
  local unit = math.min(width, height)
  local title_h = ui.line_height(kTitleScale)
  local hint_h = ui.line_height(kHintScale)

  box.body_w = unit * kBodyWPercent // 100
  box.icon_h = box.body_w * kLiftPercent // 100 + box.body_w * kBodyHPercent // 100
  box.title_h = title_h
  box.hint_h = hint_h
  box.dots_h = kDotRadius * 2

  local block = box.icon_h + kGapIcon + title_h + kGapTitle + hint_h + kGapHint + box.dots_h
  local y = (height - block) // 2
  -- **El terra**: un bloc que no hi cap comença on comença el marge, o sigui que el que es perd és el blanc de sota i
  -- mai la frase.
  if y < 12 then y = 12 end

  box.width = width
  box.height = height
  box.icon_x = (width - box.body_w) // 2
  box.icon_y = y
  y = y + box.icon_h + kGapIcon
  box.title_y = y
  y = y + title_h + kGapTitle
  box.hint_y = y
  y = y + hint_h + kGapHint
  box.dots_y = y
  box.dots_x = (width - kTapCount * kDotStep) // 2
end

-- **Una lletra centrada**, que és com les posa la vista de PaperOS (`TextAlign::Center`).
local function centered(text, y, scale, level)
  local w = ui.text_width(text, scale)
  local x = (box.width - w) // 2
  if x < 0 then x = 0 end
  ui.text(x, y, text, scale, level)
end

-- **La pàgina sencera**: el paper, el candau, les dues frases i els cercles. Es pinta quan l'app arriba i quan el vidre
-- canvia, i no en un toc —un toc només torna a fer la fila dels cercles—.
local function paint_page()
  ui.clear(15)
  paint_padlock(box.icon_x, box.icon_y, box.body_w)
  centered(kTitleText, box.title_y, kTitleScale, 0)
  centered(kHintText, box.hint_y, kHintScale, 0)
  paint_dots()
  -- **El rentat més fort que no fa flaix** (0.179.5): la pantalla de bloqueig es pinta sobre el que hi hagués al vidre, i
  -- el que hi hagués pot ser qualsevol cosa, o sigui que la pàgina demana els setze nivells sense parpelleig.
  ui.present('gl16')
end

-- ------------------------------------------------------------------ els tocs

-- **Un compte que s'ha refredat es buida sol**: la finestra és entre un toc i el següent, i quan passa el comptador torna
-- a zero i la fila es torna a pintar, perquè un punt ple que s'hi queda per sempre és una pantalla que recorda un toc
-- d'ahir.
local function window_gone(now)
  if taps == 0 then return false end
  return now < lastTapMs or (now - lastTapMs) > kTapWindowMs
end

local function paint_the_dots_only()
  ui.rect(box.dots_x - 2, box.dots_y - 2, kTapCount * kDotStep + 4, box.dots_h + 4, 15)
  paint_dots()
  -- **El passi barat i només la seva caixa**: tres punts de dos tons són el que `du` porta, i enviar la pantalla sencera
  -- al panell per un toc és el que l'amo va trobar fora de lloc als jocs (0.189.x, docs/50).
  ui.present('du')
end

local function unlock()
  -- **El camí de tornada és del sistema i no d'aquesta pàgina** (docs/30): la barra torna i l'app es tanca, i el que hi
  -- havia darrere torna a la seva pantalla. **I la barra no torna fins aquí**: aquesta app és d'administrador
  -- (`"admin": true` al manifest) i la pantalla està **bloquejada**, o sigui que el sistema no la deixa recuperar amb tres
  -- tocs ni amb cap `ui.full_screen(false)` d'una altra pàgina; el pany és aquesta línia (0.189.99, docs/30).
  admin.LockScreen(false)
  app.exit()
end

-- **Un gest pot ser més d'un toc** (0.189.99): el tàctil compta els tocs seguits i el tercer dins de la seva finestra surt com
-- un sol gest `triple` —i dos com un `double`—, o sigui que una pàgina que només miri `tap` compta dos tocs d'una persona que
-- n'ha fet tres i no s'obre mai. És el que feia aquesta app fins avui, i només s'obria amb els tocs separats més de la
-- finestra del tàctil (docs/30).
local function taps_of(kind)
  if kind == 'tap' then return 1 end
  if kind == 'double' then return 2 end
  if kind == 'triple' then return 3 end
  return 0
end

local function count_a_tap(now, how_many)
  if window_gone(now) then taps = 0 end
  lastTapMs = now
  taps = taps + how_many
  if taps > kTapCount then taps = kTapCount end
  paint_the_dots_only()
  if taps < kTapCount then return end
  taps = 0
  unlock()
end

-- --------------------------------------------------------------------- l'app

-- **El vidre sencer, i amb el pany posat** (0.189.99): `ui.full_screen(true)` treu la barra però el sistema la torna amb
-- tres tocs a qualsevol lloc, i una pantalla de bloqueig que s'obre amb tres tocs no és una porta. `admin.LockScreen(true)`
-- és el mateix vidre sencer amb la clau girada: la barra no torna, i amb ella no hi ha el toc que obre la targeta del
-- sistema —l'única cosa d'aquest vidre que pot tancar l'app de sota— (docs/30, docs/46 D1).
admin.LockScreen(true)

local view = ui.caps()
local width, height = view.width, view.height
-- **La primera pàgina es pinta sempre** i no quan el vidre canvia de mida: la vista que el sistema dona aquí ja pot ser
-- la del vidre sencer, i un bucle que només pinta en veure una mida diferent es quedaria una pàgina en blanc per sempre
-- (vist a l'emulador, 15/11).
arrange(width, height)
paint_page()

while true do
  view = ui.caps()
  if view.width ~= width or view.height ~= height then
    width, height = view.width, view.height
    arrange(width, height)
    paint_page()
  end

  -- **Els tocs, que són el gest del sistema**: un `tap` és un dit que no ha caminat, que és exactament el que PaperOS
  -- comptava a mà amb el seu `TapSlopPx`, i la finestra de tres segons la porta aquesta pàgina perquè el sistema no en
  -- sap res, de comptes.
  local gesture = touch.gesture()
  while gesture do
    local counted = taps_of(gesture.kind)
    if counted > 0 then count_a_tap(sys.time_ms(), counted) end
    gesture = touch.gesture()
  end

  -- **I el compte que es refreda es veu**: sense això, un punt ple s'hi quedaria fins al toc següent.
  if window_gone(sys.time_ms()) then
    taps = 0
    paint_the_dots_only()
  end

  sys.delay(20):await()
end
