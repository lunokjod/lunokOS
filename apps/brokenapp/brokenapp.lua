-- brokenapp: una app que peta de set maneres diferents, per mirar la pàgina d'error del sistema (0.189.162).
--
-- L'amo: *«Si peta una app, que menys que saber el perqué amb la pantalla i els dits. Un modal amb el missatge d'error? vull
-- detalls del perqué una app peta. Fes una app d'exemple que peti de diferents maneres al polsar uns botons. Diga-li "Broken App
-- Test"»*.
--
-- **Cada botó és una manera de trencar-se i el perquè és diferent**: a nil that is indexed, a global that is not a function, a
-- message of the app's own, an error thrown as a table, two tables compared, a recursion with no end and a string that does not fit
-- in the memory. The system paints the page of the error with what it said and the traceback of where, and it waits for its button
-- —the page is `kernel::app_error` and this app is only the way to call it— (docs/30, docs/50).
--
-- **I un bucle sense fi no és una d'aquestes set maneres** (0.190.2, l'amo: *«Perqué l'app de test no peta?»*): the turn budget of
-- the interpreter is **a slice and not a kill** (`docs/20`: «an app that never yields keeps running, but the console, the alarms, the
-- button, the glass and the radio get a turn between slices»), o sigui que una app dins d'un `while true` **no es mor**: el sistema
-- recupera el torn, la pàgina segueix congelada i la pàgina de l'error no arriba mai. Hi era com a setena manera de petar des de la
-- 0.189.162 i no podia petar; ara el setè botó compara dues taules, que sí que és un error, i el pressupost del torn queda explicat
-- on toca.
--
-- **El botó no peta, i això és la lliçó**: an error thrown inside `on_click` is a hook of the system and it is called in a protected
-- call of its own, so the app would go on as if nothing had happened. **El que sí que travessa el ganxo és `app.exit()`** des de la
-- 0.192.49 —el comiat és una intenció i no un error, i el torn del nucli el compleix quan el ganxo ha tornat—, però un error de
-- debò no: el botó escriu un desig i **el torn de la pàgina** és el que peta, que és el camí que un control d'aquest sistema fa per
-- a qualsevol cosa que acaba l'app (docs/30, docs/90).

local kTitle = 4
local kBody = 2
local kMargin = 16
local kGap = 8

local caps = sys.caps()
if not caps.display then
  console.print('brokenapp: this board has no glass')
  return
end

local view = ui.caps()
local width, height = view.width, view.height

-- **El que el dit ha demanat**, que la pàgina executa al seu torn.
local wanted = nil

local function label(text, scale, y)
  local words = ui.label(width - 2 * kMargin, ui.line_height(scale) + 4)
  if words == nil then return end
  words.text = text
  words.scale = scale
  words.align = 'center'
  words.valign = 'middle'
  words.color = 0
  words.background = 15
  words:place(kMargin, y)
  return words
end

-- **Les set maneres de petar**, amb el nom de la que és en el registre de l'app: una línia que digui què s'ha demanat és el que fa
-- que una pàgina d'error es pugui reproduir. **I el que es veu al vidre va en anglès**, com els botons i les files de tota
-- pàgina d'aquest sistema —el codi i els comentaris són una altra cosa— (docs/45).
local crashes = {
  { id = 'nil-index', text = 'An index into a nil',
    run = function()
      local nothing = nil
      return nothing.field
    end },
  { id = 'nil-call', text = 'Call what is not there',
    run = function()
      no_such_function_here()
    end },
  { id = 'message', text = 'An error with a message',
    run = function()
      error('the message this app wrote on purpose')
    end },
  { id = 'table', text = 'An error with a table',
    run = function()
      error({ code = 'E_BROKEN', message = 'an error thrown as a table' })
    end },
  { id = 'compare', text = 'Two tables compared',
    run = function()
      local first = {}
      local second = {}
      return first < second
    end },
  { id = 'recursion', text = 'A recursion with no end',
    run = function()
      local function down(n) return down(n + 1) + 1 end
      return down(1)
    end },
  { id = 'memory', text = 'A string that does not fit',
    run = function()
      local big = string.rep('x', 64 * 1024 * 1024)
      return #big
    end },
}

local function build()
  ui.clear(15)
  local top = kMargin
  label('Broken App Test', kTitle, top)
  top = top + ui.line_height(kTitle) + 4

  label('A button, and the page of the error', kBody, top)
  top = top + ui.line_height(kBody) + kGap

  local row = ui.line_height(kBody) + 20
  for _, one in ipairs(crashes) do
    local button = ui.button(width - 2 * kMargin, row)
    if button ~= nil then
      button.title = one.text
      button.background = 15
      button.border = 0
      button.on_click = function()
        console.print('brokenapp: ' .. one.id)
        wanted = one.id
      end
      button:place(kMargin, top)
      top = top + row + kGap
    end
  end
  ui.present('gc16')
end

build()

while true do
  local now = ui.caps()
  if now.width ~= width or now.height ~= height then
    width, height = now.width, now.height
    build()
  end

  -- **I aquí es peta**: the wish of the button is carried out in the app's own turn, which is the only place an error of this app
  -- is an error of the app and not of a hook of the system (docs/30, docs/90).
  if wanted ~= nil then
    local which = wanted
    wanted = nil
    for _, one in ipairs(crashes) do
      if one.id == which then
        one.run()
        console.print('brokenapp: ' .. which .. ' did not break, which is a bug of this app')
      end
    end
  end

  local gesture = touch.gesture()
  while gesture do gesture = touch.gesture() end
  ui.present()
  sys.delay(30):await()
end
