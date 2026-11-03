-- images: **una imatge de cada família que el sistema llegeix**, i el que cada família sap fer.
--   run images
--
-- Les imatges són a `/res` —el que l'app porta—: una fotografia en JPEG, un cartell en BMP de 24 bits, vuit
-- tintes en un BMP de 8, un dibuix amb transparència en GIF, una icona amb alfa en ICO, línies primes en PNG
-- —que no les sua—, dos degradats en PGM i PPM i un quadre d'un bit en PBM. **No són el mateix dibuix
-- nou vegades**: cada format s'hi veu pel que hi sap fer, i la línia de sota ho diu.
--
-- `ui.image_info` llegeix la capçalera i diu la família i la mida; `ui.image` descodifica el fitxer dins del
-- canvas fila a fila, i la pàgina només decideix on cau i com de gran. El requadre gris de darrere és perquè
-- **es vegi la transparència** del GIF i de l'ICO: el que hi ha darrere és el paper, i sobre gris es nota.
local caps = sys.caps()
if not caps.display then
  console.print('images: this board has no glass')
  return
end

local view = ui.caps()
ui.title('Images')

-- **Cada fitxer amb el que hi fa**: el nom, i la frase que explica per què aquest format i no un altre.
local pictures = {
  { path = '/res/photo.jpg', why = 'JPEG: a photograph, and the gradients hold' },
  { path = '/res/poster.bmp', why = 'BMP 24 bits: flat ink, not a byte wasted' },
  { path = '/res/logo8.bmp', why = 'BMP 8 bits: eight inks and a table' },
  { path = '/res/cartoon.gif', why = 'GIF: a palette and a transparent corner' },
  { path = '/res/icon.ico', why = 'ICO: an icon, alpha and all' },
  { path = '/res/lines.png', why = 'PNG: thin lines, exactly as drawn' },
  { path = '/res/shades.pgm', why = 'PGM: grey, eight bits, one ink' },
  { path = '/res/colours.ppm', why = 'PPM: colour, eight bits a channel' },
  { path = '/res/checks.pbm', why = 'PBM: one bit, ink or paper' },
}

local index = 1
local shown = 0
local failures = 0
local leaving = false
local controls = {}
-- **Un canvi de focus és un canvi d'estat de la pàgina i s'ha de rentar**: l'anell és negre sobre blanc i
-- un `du` hi deixa el de abans a un quart de la tinta, o sigui un anell gris que ningú no ha demanat. El
-- torn següent al moviment el pinta amb `gc16`, i com que els botons són l'únic que ha canviat, la regió
-- que parpalleja és la fila dels botons i no la pàgina (docs/30, docs/50).
local clean = false

local top = 64
local box_w = view.width - 80
local box_h = view.height - 340

local function info_of(path)
  local info, err = ui.image_info(path)
  if info == nil then
    console.print(string.format('images: %s: %s', path, tostring(err and err.message)))
  end
  return info
end

local function bytes_of(path)
  local stat = fs.stat(path)
  return (stat ~= nil and stat.size) or 0
end

local function draw(i)
  local picture = pictures[i]
  local info = info_of(picture.path)
  ui.clear(15)
  -- **Els botons tornen a la pàgina**: `ui.clear` esborra la vista i cada botó és el seu propi canvas, o
  -- sigui que el que no ha canviat no es tornaria a compondre i la pàgina el perdria (docs/30, docs/90).
  for _, control in ipairs(controls) do control:invalidate() end
  ui.text(40, 12, string.format('%d / %d', i, #pictures), 2, 0)
  if info == nil then
    failures = failures + 1
    ui.text(40, top, 'no picture', 2, 0)
    ui.text(40, top + 60, picture.path, 1, 8)
    ui.present('gc16')
    return
  end
  -- **La imatge cap al requadre amb la seva proporció**: l'escala és la més petita de les dues i la mida a
  -- què es dibuixa és la del fitxer per aquesta escala (docs/30).
  local scale = math.min(box_w / info.width, box_h / info.height)
  local w = math.max(1, math.floor(info.width * scale))
  local h = math.max(1, math.floor(info.height * scale))
  local x = math.floor((view.width - w) / 2)
  local y = top + math.floor((box_h - h) / 2)
  -- **Un fons gris de la mida del dibuix**, perquè la transparència es vegi: el que no és del GIF és el paper
  -- i sobre gris es nota què hi és i què no (docs/30).
  ui.rect(x, y, w, h, 11)

  local ok, why = ui.image(x, y, picture.path, w, h)
  if ok == nil then
    failures = failures + 1
    console.print(string.format('images: %s: %s', picture.path, tostring(why and why.message)))
    ui.text(40, top, 'not drawn', 2, 0)
    ui.text(40, top + 60, tostring(why and why.message), 1, 8)
  else
    shown = shown + 1
    console.print(string.format('images: %s %dx%d (%d bytes) at %dx%d, %d of %d', info.format, info.width,
      info.height, bytes_of(picture.path), w, h, i, #pictures))
  end
  ui.text(40, view.height - 236, string.format('%s  %d x %d  %d B', info.format, info.width, info.height,
    bytes_of(picture.path)), 2, 0)
  ui.text(40, view.height - 196, picture.path, 1, 8)
  ui.text(40, view.height - 168, picture.why, 1, 0)
  ui.present('gc16')
  -- La pàgina sencera acaba de sortir d'un `gc16`: no hi ha cap anell pendent de rentar.
  clean = false
end

local function step(delta)
  index = index + delta
  if index < 1 then index = #pictures end
  if index > #pictures then index = 1 end
  draw(index)
end

-- **Tres botons al peu de la pàgina**: enrere, sortir i endavant, que amb el dial de la placa és el mateix
-- que amb un dit (docs/30, docs/90).
local function make_button(x, width, text, action)
  local button, err = ui.button(width, 72)
  if button == nil then
    console.print(string.format('images: no room for "%s": %s', text, tostring(err and err.message)))
    return nil
  end
  button.background = 15
  button.border = 0
  button:place(x, view.height - 84)
  local label, label_err = ui.label(width - 20, 52)
  if label == nil then
    console.print(string.format('images: no room for the label: %s',
      tostring(label_err and label_err.message)))
    return nil
  end
  label.text = text
  label.align = 'center'
  label.valign = 'middle'
  label.scale = 2
  label:place(10, 10)
  button:add(label)
  button.on_click = action
  button.on_activate = action
  button.on_focus = function(self, focused)
    if focused then clean = true end
  end
  controls[#controls + 1] = button
  return button
end

make_button(30, 150, 'back', function() step(-1) end)
make_button(205, 140, 'quit', function()
  leaving = true
  console.print('images: quit pressed, leaving the page')
end)
make_button(view.width - 170, 140, 'next', function() step(1) end)

draw(index)
console.print(string.format('images: %d pictures, %d drawn, %d failed', #pictures, shown, failures))

-- **L'app es queda i escolta**: una app que plega després de pintar és una app que ningú no pot prémer. La
-- imatge de cada pas es presenta amb el seu rentat dins de `draw`; els torns del mig presenten només el que
-- els controls han canviat —no res, quasi sempre— i no costen tinta (docs/30, docs/90).
while not leaving do
  -- **El torn després d'un moviment de focus és un rentat**: `gc16` és el que deixa l'anell que arriba
  -- nítid i el que se'n va esborrat, i només els botons són a la llista de danys, o sigui que el que
  -- parpalleja és la fila dels botons (docs/30, docs/90).
  ui.present(clean and 'gc16' or nil)
  clean = false
  sys.delay(200):await()
end
console.print('images: out')
