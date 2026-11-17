-- brokenapp: una app que peta de set maneres diferents, per mirar la pàgina d'error del sistema (0.189.162).
--
-- L'amo: *«Si peta una app, que menys que saber el perqué amb la pantalla i els dits. Un modal amb el missatge d'error? vull
-- detalls del perqué una app peta. Fes una app d'exemple que peti de diferents maneres al polsar uns botons. Diga-li "Broken App
-- Test"»*.
--
-- **Cada botó és una manera de trencar-se i el perquè és diferent**: a nil that is indexed, a global that is not a function, a
-- message of the app's own, an error thrown as a table, a loop that never ends and is cut by the budget of the turn, a recursion
-- with no end and a string that does not fit in the memory. The system paints the page of the error with what it said and the
-- traceback of where, and it waits for a finger —the page is `kernel::app_error` and this app is only the way to call it— (docs/30,
-- docs/50).
--
-- **El botó no peta, i això és la lliçó**: an error thrown inside `on_click` is a hook of the system and it is called in a protected
-- call of its own, so the app would go on as if nothing had happened (the same road that made `app.exit()` inside a hook not close
-- the app, 0.189.101). The button writes a wish and **the page's own turn** is what breaks, which is the road a control of this
-- system takes for anything that ends the app (docs/30, docs/90).

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
-- que una pàgina d'error es pugui reproduir.
local crashes = {
  { id = 'nil-index', text = 'Index a nil',
    run = function()
      local nothing = nil
      return nothing.field
    end },
  { id = 'nil-call', text = 'Cridar el que no hi és',
    run = function()
      no_such_function_here()
    end },
  { id = 'message', text = 'Error amb missatge',
    run = function()
      error('the message this app wrote on purpose')
    end },
  { id = 'table', text = 'Error amb taula',
    run = function()
      error({ code = 'E_BROKEN', message = 'an error thrown as a table' })
    end },
  { id = 'budget', text = 'Bucle sense fi',
    run = function()
      local n = 0
      while true do n = n + 1 end
    end },
  { id = 'recursion', text = 'Recursió sense fi',
    run = function()
      local function down(n) return down(n + 1) + 1 end
      return down(1)
    end },
  { id = 'memory', text = 'No hi cap a la memòria',
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
