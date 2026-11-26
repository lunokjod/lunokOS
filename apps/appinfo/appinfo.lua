-- appinfo: what every app of `/apps` is, how much of the disk it takes, and the two things a person may do to it.
--
-- **The list is read and not typed** (0.189.157): the ids come from `apps.list()`, the fields of the manifest from the
-- `manifest.json` of each folder —title, version, author, entry, boards, description— and the rest from what the door
-- already knows about the app (`apps.info`: whether this board can run it, whether it is a launcher of its own and whether
-- it sits on the card of the panel). An app dropped into `/apps` appears here the next time the page is built and one that
-- is removed cannot be listed: the owner asked for a page that «parseja el manifest de les apps de /apps inclòs l'espai en
-- disc», and a list written into this file would start lying the day an app is added (docs/40, docs/45).
--
-- **The folder of the app is walked and the rest is asked**: `fs.list` gives every entry of `/apps/<id>` with its size and
-- `walk()` adds them up, while its cache, its data and its settings —`/session/<id>`, `/data/<id>` and `/cfg/<id>`— are of that
-- app and the file ground refuses them to everybody else, so they are counted by the system with `admin.space_of(id, ground)`
-- (0.189.160, the owner: *«A l'app Aplication Info no surt mai el tamany de caché, sempre diu 0B 0 files»*). The volume itself
-- is `fs.space('/apps')`, which is the same answer the `fs` command of the prompt gives (docs/30, docs/70).
--
-- **The two buttons are doors of the system** (0.189.157): `admin.forget_cache(id)` and `admin.uninstall(id)`, because an
-- app may not write outside its own ground (docs/70 §5) — a page of apps that erases another app has to ask the kernel, and
-- the kernel is the one that knows what an uninstall takes with it. **Uninstalling takes the folder, the alarms the app left
-- in the queue and its cache, and keeps `/cfg/<id>` and `/data/<id>`**: a settings file is the person's and not the app's,
-- and a page that deleted them in silence would be a page that loses work. The two answers are the system's numbers —files,
-- bytes, alarms, blocks— and the page shows those and not the ones it hoped for (docs/30).
--
-- **A copy of the manifest is not kept**: the detail reads the file every time it is built, so a version that changed under
-- the page is the version that is shown (docs/30, docs/45).
--
-- **`-d <id>` obre la fitxa d'una app, i la pàgina no llegeix res més** (0.189.161, i el 17/11 l'amo ho va precisar: *«s'ha de
-- mostrar la vista detalls directament (sense escanejar res mes que no sigui aquella app) i el botó "Back" ha de tancar
-- l'aplicació»*): the launcher's long press on a cell and the prompt's `run appinfo -d battery` both leave that switch in
-- `app.args.flags`, and the page then reads **that one manifest with its folder** and no other app —there is no list of the disk
-- behind it—. **Aquell botó no hi és des de la 0.192.28** (l'amo: *«Treu el botó UI "Back" de la vista detalls»*): la fitxa es
-- tanca amb el que tanca qualsevol pàgina d'aquesta casa —la creu de la targeta de la barra i el pessic de gran a petit— i, quan
-- té la llista a sota, la fletxa de la targeta i el pessic hi tornen (docs/30, docs/40).

local caps = sys.caps()
if not caps.display then
  console.print('appinfo: this board has no glass')
  return
end

local view = { width = 0, height = 0 }
do
  local c = ui.caps()
  view.width, view.height = c.width, c.height
end

-- The three sizes of the paper project in pixels of line, asked of the face the system is drawing with (docs/30, docs/90).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end

  local scale = math.floor(line_px / step + 0.5)
  if scale < 1 then scale = 1 end
  if scale > 16 then scale = 16 end

  return scale
end

local kSmall = scale_for(33)
local kBody = scale_for(43)
local kBig = scale_for(82)
-- **I la de la llista, un pas menys que el cos** (0.189.178, i la 0.189.177 se'n va passar: l'amo, *«T'has passat, ara la
-- lletra de la llista es massa petita!»*): the 33 px of the paper project are three steps of this face and the body is four, so
-- the list is the 33 of the paper's own listing —the step its cards used for a name— and not the two that made a page of
-- twenty-three names a page nobody could read (docs/30, docs/45, docs/90).
local kList = scale_for(33)
-- **I la línia del compte, una passa més avall** (0.189.178): the count of the apps and the room left is a note over the list
-- and not a row of it —at the size of the rows it took two lines of the glass and pushed two apps to the page after— (docs/30).
local kNote = scale_for(22)

local function line(scale)
  return scale * ui.line_height(1)
end

local kMargin = 20
local kListGap = 8
local kDetailGap = 10
-- **La icona de la fitxa és la de l'app** (0.192.28): the launcher draws a cell's picture at 72, i la fitxa la posa al costat del
-- nom a la mateixa mida —una icona de 94 escalada a 72 és el que fa el llançador amb el mateix dibuix— (docs/30, docs/45).
local kIconSide = 72
local kIconGap = 12
-- **El pas de la fila és el de la lletra que hi ha a dins** (0.189.177): the row of a list is the finger's box, and this one
-- keeps the air of a row that a finger hits without looking (docs/30, docs/90).
local kRow = line(kList) + 14
-- **I els botons tenen el seu propi**: a button's label is the body of the page, and a button the height of a list's row —twelve
-- pixels of air over a line of the list— leaves the label of the one below painted over it, que és el que l'amo va veure el
-- 18/11 amb els tres botons de la fitxa («queden solapades»). El número és el que tenia la fila abans de la 0.189.177, quan la
-- llista i el botó compartien el mateix i el botó encara anava bé (docs/30, docs/90).
local kButton = line(kBody) + 20

-- ------------------------------------------------------------------ the state of the page (docs/30 § state)

local kLayout = 'v1'
local kStateBytes = 1024
local state_block = nil

local function the_state()
  if state_block == nil then
    local block = sys.session('state', kStateBytes)
    if block == nil then return nil end
    state_block = block
  end
  return state_block
end

local function save_state(now)
  local block = the_state()
  if block == nil then return false end
  local lines = { kLayout }
  for _, key in ipairs({ 'page', 'focus', 'marked' }) do
    if now[key] ~= nil then lines[#lines + 1] = key .. '=' .. tostring(now[key]) end
  end
  block:clear()
  block:write(0, table.concat(lines, '\n'))
  return true
end

local function load_state()
  local block = the_state()
  if block == nil then return nil end
  local body = block:read(0, kStateBytes) or ''
  -- The block is a kilobyte and what is written is shorter: behind it is the zero the kernel filled it with, and a line
  -- followed by zeros is not a value (docs/30).
  local zero = body:find('\0', 1, true)
  if zero ~= nil then body = body:sub(1, zero - 1) end
  local now = {}
  local first = true
  for one in body:gmatch('[^\n]+') do
    if first then
      first = false
      if one ~= kLayout then return nil end
    else
      local key, value = one:match('^([%w_]+)=(.*)$')
      if key ~= nil then now[key] = value end
    end
  end
  if first then return nil end
  return now
end

-- **La forma de les dues pantalles**: the page of the list, the row with the ring and the app whose detail is open —both
-- **by id** and not by number, because an app that is added or removed moves every row under it (docs/30, docs/45 §5 bis).
local shape = { page = 1, focus = nil, marked = nil }

-- What the last door answered, said on the page: a person who pressed "Uninstall" has to read what went.
local notice = nil

-- ------------------------------------------------------------------ reading the disk

-- **`fs.list` says the size of every entry it hands over**, so one walk is one listing per folder and not a `stat` per file
-- (docs/30, docs/70).
local function walk(path, depth)
  local files, folders, bytes = 0, 0, 0
  if depth > 6 then return files, folders, bytes end

  local list = fs.list(path)
  if list == nil then return files, folders, bytes end

  for _, one in ipairs(list) do
    if one.type == 'dir' then
      folders = folders + 1
      local f, d, b = walk(path .. '/' .. one.name, depth + 1)
      files = files + f
      folders = folders + d
      bytes = bytes + b
    else
      files = files + 1
      bytes = bytes + (one.size or 0)
    end
  end

  return files, folders, bytes
end

-- **A size a person reads**: bytes under a thousand, then kB and MB, with the decimal only when it says something.
local function human(bytes)
  if bytes == nil then return '?' end
  if bytes < 1024 then return string.format('%d B', bytes) end
  if bytes < 1024 * 1024 then
    local kb = bytes / 1024
    if kb < 10 then return string.format('%.1f kB', kb) end
    return string.format('%d kB', math.floor(kb + 0.5))
  end
  return string.format('%.1f MB', bytes / (1024 * 1024))
end

-- **Els números d'una fitxa en una línia** (0.189.168, l'amo: *«Application info no diu res al log quan carrega... m'agradaria
-- més informació»*): the card is made of these numbers —its folder, its cache, its data, its settings and its alarms—, i una
-- línia que els digui tots és la que fa que una xifra estranya al vidre es pugui comprovar al registre sense llegir la pàgina
-- (docs/30, docs/85).
local function numbers_of(one)
  return string.format('%d file(s) %s, cache %d block(s) %s, data %d file(s) %s, cfg %d file(s) %s, %d alarm(s)',
                       one.app_files, human(one.app_bytes), one.cache_blocks, human(one.cache_bytes), one.data_files,
                       human(one.data_bytes), one.cfg_files, human(one.cfg_bytes), one.alarms)
end

-- **One app, read from its folder and from the door**: the fields of the manifest that the door does not carry —the author,
-- the entry, the boards and the sentence— are read here with `json.get_string`, which is the whole parser this system gives
-- an app (docs/30).
local function read_app(id)
  local one = { id = id }

  local what = apps.info(id)
  if what ~= nil then
    one.title = what.title
    one.version = what.version
    one.icon = what.icon
    one.launcher = what.launcher
    one.card = what.card
    one.works_here = what.works_here
  end

  local text = fs.read('/apps/' .. id .. '/manifest.json')
  if text ~= nil then
    one.author = json.get_string(text, 'author')
    one.entry = json.get_string(text, 'entry')
    one.description = json.get_string(text, 'description')
    local boards = text:match('"boards"%s*:%s*%[(.-)%]')
    if boards ~= nil then
      one.boards = boards:gsub('"', ''):gsub('%s+', ' '):gsub('^%s*(.-)%s*$', '%1')
    end
  else
    one.reading = 'the manifest cannot be read'
  end

  if one.title == nil or one.title == '' then one.title = id end
  if one.version == nil or one.version == '' then one.version = '?' end

  -- **La carpeta de l'app la llegeix la pàgina; els altres tres terrenys els compta el sistema** (0.189.160, l'amo: *«A l'app
  -- Aplication Info no surt mai el tamany de caché, sempre diu 0B 0 files»*): `/data/<id>`, `/cfg/<id>` i `/session/<id>` són de
  -- l'app i el terreny dels fitxers no deixa que cap altra app els recorri, o sigui que `walk` hi tornava sempre zero. La porta
  -- `admin.space_of` dona els mateixos números que l'esborrada, que és el que fa que la fitxa i el botó no es contradiguin
  -- (docs/30, docs/70).
  one.app_files, one.app_folders, one.app_bytes = walk('/apps/' .. id, 0)

  local cache = admin.space_of(id, 'cache')
  one.cache_files = (cache ~= nil) and cache.files or 0
  one.cache_blocks = (cache ~= nil) and cache.blocks or 0
  one.cache_bytes = (cache ~= nil) and cache.bytes or 0

  local data = admin.space_of(id, 'data')
  one.data_files = (data ~= nil) and data.files or 0
  one.data_bytes = (data ~= nil) and data.bytes or 0

  local cfg = admin.space_of(id, 'cfg')
  one.cfg_files = (cfg ~= nil) and cfg.files or 0
  one.cfg_bytes = (cfg ~= nil) and cfg.bytes or 0

  -- **The alarms the app left in the queue of the system**, counted by name: they are what an uninstall takes with it, and a
  -- person about to press the button has to know how many there are (docs/30, docs/60).
  one.alarms = 0
  local queue = alarm.list()
  if queue ~= nil then
    for _, entry in ipairs(queue) do
      if entry.app == id then one.alarms = one.alarms + 1 end
    end
  end

  return one
end

-- **El que la llista necessita i no tot el que una fitxa sap** (0.189.179, l'amo: *«va molt lent carregant»*): a row of the list
-- is a name and a version, i això és el que la porta de les apps ja té. `read_app` llegeix el manifest **una segona vegada**, recorre
-- la carpeta de l'app, pregunta al sistema pels seus tres terrenys i recorre la cua d'alarmes —per a cada app—, o sigui unes cent
-- cinquanta operacions de flaix per pintar vint-i-tres noms; la carpeta i els terrenys es demanen **en obrir la fitxa**, que és
-- qui els ensenya (docs/30, docs/70, docs/75).
local function read_entry(id)
  local one = { id = id }
  local what = apps.info(id)
  if what ~= nil then
    one.title = what.title
    one.version = what.version
  end
  if one.title == nil or one.title == '' then one.title = id end
  if one.version == nil or one.version == '' then one.version = '?' end
  return one
end

-- Every app of the folder, in the order of the door, which is the order the launcher draws them in.
local apps_here = {}
local reading = nil

local function read_all()
  apps_here = {}
  reading = nil

  local ids = apps.list()
  if ids == nil then
    reading = 'the door of the apps did not answer'
    return
  end

  for _, id in ipairs(ids) do
    apps_here[#apps_here + 1] = read_entry(id)
  end

  if #apps_here == 0 then reading = 'there is no app in /apps' end
end

local function index_of(id)
  for i = 1, #apps_here do
    if apps_here[i].id == id then return i end
  end
  return 0
end

-- ------------------------------------------------------------------ the pieces of a page

local function make_label(width, text, scale, align, valign)
  local label = ui.label(width, line(scale) + 6)
  if label == nil then return nil end

  label.text = text
  label.scale = scale
  label.align = align or 'left'
  label.valign = valign or 'top'
  label.color = 0
  label.background = 15

  return label
end

-- A label of this system does not wrap —a text that does not fit ends in "..."— so the page breaks its sentences, with the
-- same call the label draws with (docs/30, docs/90).
local function broken(text, width, scale)
  local lines = {}
  local one = ''

  for word in tostring(text):gmatch('%S+') do
    local try = (one == '') and word or (one .. ' ' .. word)

    if one ~= '' and ui.text_width(try, scale) > width then
      lines[#lines + 1] = one
      one = word
    else
      one = try
    end
  end

  if one ~= '' then lines[#lines + 1] = one end

  return lines
end

local function add_lines(parent, width, text, scale, align)
  local lines = broken(text, width, scale)
  if #lines == 0 then lines = { '' } end

  for i = 1, #lines do
    local label = make_label(width, lines[i], scale, align)
    if label ~= nil then parent:add(label) end
  end
end

-- **`yes`/`no` and not `true`/`false`**: the page is read by a person and the switches are the manifest's.
local function yn(value)
  if value then return 'yes' end
  return 'no'
end

local screen = nil

-- **La fitxa demanada amb `-d` i res més** (0.189.166): with the switch the page reads **that one app** and no other, so the
-- list is not there and the way out of the detail is closing the app —la creu de la targeta i el pessic, que és el que fa el
-- sistema amb una pàgina que no té res a sota (0.192.28)—. It is a local up here and not a value read at the end of the file
-- porque `show` el llegeix i una local declarada més avall no el veuria (docs/30, docs/40).
local details_only = false

local function give_back()
  if screen ~= nil then
    screen:release()
    screen = nil
  end
end

-- ------------------------------------------------------------------ the two screens

local show
local build_cover

-- **A row of the list**: the name on the left and the version on the right, side by side and not one over the other,
-- because a label paints its own paper and a second one at the bottom would cut the first (docs/30, docs/45).
local function add_entry(the_list, one)
  local entry = ui.list_entry(view.width - 2 * kMargin, kRow)
  if entry == nil then return nil end

  local version_w = 150
  local name = make_label(view.width - 2 * kMargin - version_w - 36, one.title, kList, 'left', 'middle')
  if name ~= nil then
    name:place(12, 2)
    entry:add(name)
  end

  local version = make_label(version_w, one.version, kList, 'right', 'middle')
  if version ~= nil then
    version:place(view.width - 2 * kMargin - version_w - 12, 2)
    entry:add(version)
  end

  entry.on_click = function() show(one.id) end
  entry.on_focus = function(self, on)
    if not on then return end
    shape.focus = one.id
    save_state(shape)
  end

  the_list:add(entry)
  return entry
end

build_cover = function()
  ui.clear(15)

  local page_control = ui.panel(view.width, view.height, 'vertical')
  page_control.padding = kMargin
  page_control.gap = kListGap
  page_control:place(0, 0)

  -- **La pàgina no porta el nom de si mateixa** (0.189.177): the bar of the system already says *Application Info*, and a
  -- heading of eighty pixels over a list of twenty-three names is a fifth of the glass saying what the glass is already
  -- saying. The first line is the count and the room left, which is what a person came for (docs/30, docs/40).
  local space = fs.space('/apps')
  local summary
  if space ~= nil then
    summary = string.format('%d apps in /apps, %s free of %s', #apps_here, human(space.free), human(space.total))
  else
    summary = string.format('%d apps in /apps', #apps_here)
  end
  add_lines(page_control, view.width - 2 * kMargin, summary, kNote)

  -- **El que la darrera porta ha contestat** (docs/30): a person who pressed Uninstall reads here what went, and the line
  -- stays until another door answers or the app is closed — it is the app's line and not the system's.
  if notice ~= nil then
    add_lines(page_control, view.width - 2 * kMargin, notice, kSmall)
  end
  if reading ~= nil then
    add_lines(page_control, view.width - 2 * kMargin, reading, kSmall)
  end

  local the_list = ui.list(view.width - 2 * kMargin, kRow)
  if the_list == nil then
    console.print('appinfo: there is no room for the list of apps')
  else
    the_list.weight = 1
    the_list.row_height = kRow
    page_control:add(the_list)

    local first = nil
    local rows = {}
    for i = 1, #apps_here do
      rows[i] = add_entry(the_list, apps_here[i])
      if i == 1 then first = rows[i] end
    end

    if shape.page > 1 then the_list.page = shape.page end
    local wanted = nil
    for i = 1, #apps_here do
      if shape.focus ~= nil and apps_here[i].id == shape.focus then wanted = rows[i] end
    end
    if wanted ~= nil then ui.focus(wanted) elseif first ~= nil then ui.focus(first) end

    shape.page = the_list.page
    save_state(shape)

    the_list.on_page = function(self, page)
      shape.page = page
      save_state(shape)
    end

    local box = the_list:caps()
    console.print(string.format('appinfo: %d apps in a list of %d px', #apps_here, box.height))
  end

  screen = page_control
  ui.present()
end

-- **The detail is a list and not a column of labels** (0.189.157): an app whose sentence is long has more to say than a
-- glass holds, and the list of this system is the control that pages itself —a row that does not fit a page is placed out
-- of the frame, the dial and the slide turn the page and the foot says which one is on the glass—, so nothing of an app is
-- behind a sentence that ends in "..." (docs/30, docs/90). A row of one fact is a name on the left and what it is worth on
-- the right; a row of the sentence is one line of it, broken by this file with the same call the label draws with.
local kFact = line(kSmall) + 14

local function add_fact(the_list, name, value)
  local entry = ui.list_entry(view.width - 2 * kMargin, kFact)
  if entry == nil then return nil end

  local width = view.width - 2 * kMargin
  local value_w = 300
  local name_label = make_label(width - value_w - 12, name, kSmall, 'left', 'middle')
  if name_label ~= nil then
    name_label:place(12, 4)
    entry:add(name_label)
  end

  local value_label = make_label(value_w, value, kSmall, 'right', 'middle')
  if value_label ~= nil then
    value_label:place(width - value_w - 12, 4)
    entry:add(value_label)
  end

  the_list:add(entry)
  return entry
end

local function add_plain(the_list, text)
  local entry = ui.list_entry(view.width - 2 * kMargin, kFact)
  if entry == nil then return nil end

  local label = make_label(view.width - 2 * kMargin - 24, text, kSmall, 'left', 'middle')
  if label ~= nil then
    label:place(12, 4)
    entry:add(label)
  end

  the_list:add(entry)
  return entry
end

local function add_button(parent, text, on_click)
  local button = ui.button(view.width - 2 * kMargin, kButton)
  if button == nil then return nil end

  button.background = 15
  button.border = 0
  button.on_click = on_click

  local words = make_label(view.width - 2 * kMargin - 24, text, kBody, 'center', 'middle')
  if words ~= nil then
    words:place(12, 4)
    button:add(words)
  end

  parent:add(button)
  return button
end

-- **What is done is done after the page has been let go** (docs/30, docs/90): erasing a folder of `/apps` while the finger
-- is still inside an event of the page is a present over a box that is being taken away, so a button leaves the work in
-- `wanted` and the loop at the bottom of the file runs it on the next turn — the road the settings page takes.
local wanted = nil

local function forget_cache(id)
  local answer, why = admin.forget_cache(id)
  if answer == nil then
    notice = string.format('%s: the cache was not cleared (%s)', id, tostring(why and why.message or 'no answer'))
    console.print('appinfo: forget_cache ' .. id .. ' failed: ' .. tostring(why and why.code or 'the memory'))
    return
  end

  notice = string.format('%s: %d block(s), %d file(s), %s of cache gone', id, answer.blocks, answer.files,
                         human(answer.bytes))
  console.print(string.format('appinfo: forget_cache %s: %d block(s), %d file(s), %d B', id, answer.blocks,
                              answer.files, answer.bytes))
end

local function uninstall(id)
  local answer, why = admin.uninstall(id)
  if answer == nil then
    notice = string.format('%s: it was not uninstalled (%s)', id, tostring(why and why.message or 'no answer'))
    console.print('appinfo: uninstall ' .. id .. ' failed: ' .. tostring(why and why.code or 'the memory'))
    return false
  end

  -- **La cau amb les seves dues meitats** (0.189.164): a block of the memory and its file are one session in two places, o
  -- sigui que el número que aquesta pàgina deia abans era el doble del que hi havia (docs/30, docs/75).
  notice = string.format('%s uninstalled: %d file(s) and %s of folder, %d alarm(s), %d cache block(s) and %d file(s)',
                         id, answer.files, human(answer.bytes), answer.alarms, answer.cache_blocks, answer.cache_files)
  console.print(string.format('appinfo: uninstall %s: %d file(s), %d B, %d alarm(s), %d cache block(s), %d file(s)', id,
                              answer.files, answer.bytes, answer.alarms, answer.cache_blocks, answer.cache_files))
  return true
end

-- **La pregunta d'una esborrada és una targeta del sistema** (docs/30): the app says what it wants to ask and what to do
-- with a yes, and the card owns the fingers that fall on it.
local card = nil

local function close_card()
  if card ~= nil then
    card:release()
    card = nil
  end
end

local function ask(title, text, accept, on_yes)
  close_card()

  local dialog, why = ui.dialog(view.width - 2 * kMargin, math.min(view.height - 2 * kMargin, 4 * kRow))
  if dialog == nil then
    console.print('appinfo: no card for the question (' .. tostring(why and why.code or 'the memory') .. ')')
    return
  end

  dialog.title = title
  dialog.text = text
  dialog.accept = accept
  dialog.cancel = 'Keep'
  dialog.on_accept = function()
    wanted = function()
      close_card()
      on_yes()
    end
  end
  dialog.on_cancel = function()
    wanted = function()
      close_card()
      if screen ~= nil then ui.present() end
    end
  end

  dialog:place(kMargin, kMargin)
  card = dialog
  ui.focus(dialog)
  ui.present()
end

show = function(id)
  local index = index_of(id)
  if index == 0 then
    console.print('appinfo: ' .. tostring(id) .. ' is not in /apps any more')
    build_cover()
    return
  end

  -- **I la fitxa sí que llegeix l'app sencera** (0.189.179): the numbers of its folder, its three grounds and its alarms are
  -- what the page is for, and they are one app's work and not twenty-three (docs/30, docs/70).
  local one = read_app(id)
  apps_here[index] = one

  give_back()
  ui.clear(15)

  local page_control = ui.panel(view.width, view.height, 'vertical')
  page_control.padding = kMargin
  page_control.gap = kDetailGap
  page_control:place(0, 0)

  local width = view.width - 2 * kMargin

  -- **La icona de l'app al costat del seu nom** (0.192.28, l'amo: *«L'app info ha de mostrar l'icona de l'app (si en té) al títol,
  -- al costat del nom de l'app»*): the manifest names the picture and the door gives its path (`apps.info`, which is the same
  -- path the launcher draws a cell with), i una app que no en té —`Paint` no en porta— es queda amb el nom i prou. El dibuix va
  -- en un canvas propi i no a la pàgina perquè la pàgina és un panell i el que hi ha a dins és el que el panell col·loca.
  local credits = string.format('%s %s, by %s', one.id, one.version, one.author or '?')

  -- **El dibuix es fa abans de mesurar el títol** (0.192.28): an app whose manifest names a picture that is not there —or a
  -- board with no memory for one more canvas— is an app without an icon, and the page says its name in the whole width instead
  -- of leaving a hole where the drawing would be (docs/30, docs/45).
  local art = nil
  if one.icon ~= nil and one.icon ~= '' then
    local canvas = ui.canvas(kIconSide, kIconSide)
    if canvas ~= nil then
      local drawn, why = canvas:image(0, 0, one.icon, kIconSide, kIconSide)
      if drawn then
        art = canvas
      else
        console.print('appinfo: no picture for ' .. one.id .. ': ' .. tostring(why))
        canvas:release()
      end
    end
  end

  local words_w = (art ~= nil) and (width - kIconSide - kIconGap) or width
  local head_h = #broken(one.title, words_w, kBig) * (line(kBig) + 6)
  if art ~= nil and head_h < kIconSide then head_h = kIconSide end

  local head = ui.panel(width, head_h, 'horizontal')
  if head == nil then
    add_lines(page_control, width, one.title, kBig, 'center')
  else
    head.gap = kIconGap
    page_control:add(head)

    if art ~= nil then
      -- **Centrada en l'alçada del títol**: a title of two lines makes the head taller than the icon, i el dibuix va al mig
      -- del que el nom ocupa (docs/30, docs/90).
      art:place(0, math.max(0, (head_h - kIconSide) // 2))
      head:add(art)
    end

    local words = ui.panel(words_w, head_h, 'vertical')
    if words ~= nil then
      head:add(words)
      add_lines(words, words_w, one.title, kBig, 'left')
    else
      add_lines(page_control, width, one.title, kBig, 'center')
    end
  end
  -- **I la línia del nom i la versió torna a tenir tota l'amplada**: beside the icon it broke in two lines over a title that
  -- already says which app this is (docs/30, docs/90).
  add_lines(page_control, width, credits, kSmall)

  if notice ~= nil then add_lines(page_control, width, notice, kSmall) end
  if one.reading ~= nil then add_lines(page_control, width, one.reading, kSmall) end

  local the_list = ui.list(width, kFact)
  if the_list == nil then
    console.print('appinfo: there is no list for the detail of ' .. one.id)
  else
    the_list.weight = 1
    the_list.row_height = kFact
    page_control:add(the_list)

    -- **The facts first and the prose after**: a person who opens an app wants its version and what it takes of the disk
    -- before its sentence, and the sentence —broken here, one line per row— goes on the pages that follow, so a paragraph
    -- of any length is read on the pages of the list instead of pushing the facts out of the glass (docs/30, docs/90).
    add_fact(the_list, 'Version', one.version)
    add_fact(the_list, 'Author', one.author or '?')
    add_fact(the_list, 'Entry', one.entry or '?')
    add_fact(the_list, 'Boards', one.boards or '?')
    add_fact(the_list, 'Works here', yn(one.works_here))
    add_fact(the_list, 'Launcher', yn(one.launcher))
    add_fact(the_list, 'On the card', yn(one.card))
    add_fact(the_list, 'Folder', string.format('%s, %d file(s)', human(one.app_bytes), one.app_files))
    add_fact(the_list, 'Cache', string.format('%s, %d file(s)', human(one.cache_bytes), one.cache_files))
    -- **I quants blocs en té la memòria** (0.189.160): una sessió que el sistema té a la memòria i el seu fitxer al disc són la
    -- mateixa cosa, i la fila de dalt ja compta el fitxer: el bloc es diu a part perquè és el que la cau té de viu i el que
    -- `Clear the cache` deixa anar primer (docs/30, docs/75).
    add_fact(the_list, 'In the memory', string.format('%d block(s)', one.cache_blocks))
    add_fact(the_list, 'Data', string.format('%s, %d file(s)', human(one.data_bytes), one.data_files))
    add_fact(the_list, 'Settings', string.format('%s, %d file(s)', human(one.cfg_bytes), one.cfg_files))
    add_fact(the_list, 'Alarms', string.format('%d', one.alarms))

    if one.description ~= nil and one.description ~= '' then
      local plain = one.description:gsub('`', '')
      for _, text in ipairs(broken(plain, width - 24, kSmall)) do
        add_plain(the_list, text)
      end
    end

    console.print('appinfo: the detail of ' .. one.id)
  end

  -- **Les dues portes**: an app is not offered the uninstall of itself — the kernel would refuse it, and a button
  -- that cannot work is a button that lies (docs/30, docs/45).
  if one.id ~= app.id then
    add_button(page_control, 'Clear the cache', function()
      ask('Clear the cache', string.format('Throw away the %s of sessions of %s? Its data and its settings stay.',
                                           human(one.cache_bytes), one.title), 'Clear',
          function()
            forget_cache(one.id)
            if index_of(one.id) == 0 then
              if details_only then app.exit() end
              build_cover()
            elseif details_only then
              -- **Es torna a llegir aquesta i prou**: in the requested card there is no list to refresh, and a `read_all` here
              -- seria l'escaneig del disc que el commutador estalvia (0.189.166, docs/30). The detail reads the app whole by
              -- itself (0.189.179).
              show(one.id)
            else
              read_all()
              show(one.id)
            end
          end)
    end)

    add_button(page_control, 'Uninstall', function()
      ask('Uninstall', string.format('Delete %s, with its %d file(s), %d alarm(s) and its cache? Its data and its settings '
                                       .. 'stay.', one.title, one.app_files, one.alarms), 'Delete',
          function()
            if uninstall(one.id) then
              shape.marked = nil
              -- **I si la fitxa era l'única cosa que hi havia, l'app es tanca** (0.189.166): the app it was about is gone and
              -- there is no list under the page, o sigui que el que queda és el mateix Back del commutador (docs/30).
              if details_only then
                save_state(shape)
                app.exit()
              end
              read_all()
              build_cover()
            else
              show(one.id)
            end
          end)
    end)
  end

  -- **I el camí de tornada és el del sistema, i no un botó** (0.192.28, l'amo: *«Treu el botó UI "Back" de la vista detalls»*):
  -- the page that the list opened is a view on top of it, o sigui que la fletxa de la targeta de la barra i el pessic de gran a
  -- petit donen la llista, i una fitxa demanada amb `-d` —que no té cap vista empesa— la tanquen la creu i el pessic, que és el
  -- que fa el sistema amb qualsevol pàgina que no té res a sota (docs/30, docs/40).

  screen = page_control
  ui.present()

  -- The detail is a view on top of the cover: a pinch that comes from big to small gives it back and the app hears
  -- `on_back`, so the cover is what a finger finds under the detail (docs/30). **Amb `-d` no hi ha cap coberta a sota**
  -- (0.189.166): sense la vista empesa, el pessic de gran a petit fa el que fa amb qualsevol pàgina d'una sola vista —tanca
  -- l'app— i és el mateix final que la creu de la targeta (docs/30, docs/40).
  if not details_only then app.view_push() end

  shape.marked = one.id
  save_state(shape)
end

-- **A pinch took the detail away** (docs/30): the system has already given the view back, so this only puts the cover on the
-- glass again.
function on_back(depth)
  give_back()
  close_card()
  console.print(string.format('appinfo: back by the gesture, %d views left', depth))
  shape.marked = nil
  save_state(shape)
  build_cover()
end

-- ------------------------------------------------------------------ the app

-- **`-d <id>`: la fitxa d'una app, i res més** (0.189.161, i el que l'amo va precisar el 17/11: *«si l'app "Application info"
-- s'obre amb el paràmetre -d s'ha de mostrar la vista detalls directament (sense escanejar res mes que no sigui aquella app) i
-- el botó "Back" ha de tancar l'aplicació»*). És el commutador que el llançador deixa a `app.args.flags` i el que el prompt
-- escriu amb `run appinfo -d battery`, i **es mira abans de llegir res**: el sentit del commutador és no pagar l'escaneig de
-- tot el disc —una carpeta recorreguda, tres terrenys comptats i la cua d'alarmes per app— per ensenyar una fitxa. Guanya a la
-- forma que la persona va deixar al bloc `state`, perquè el que demana una ordre és el que ha de passar (docs/30, docs/40).
local asked = nil
local flags = app.args and app.args.flags or nil
if flags ~= nil then
  asked = flags:match('%-d%s+(%S+)')
end

-- **El rellotge de la càrrega, que és el que el registre ha de dir** (0.189.168): it is taken before the first read and it is
-- the monotonic one —`sys.time_ms()`—, que és la durada d'aquesta tirada i no salta quan arriba una hora del món (docs/30).
local began = sys.time_ms()

-- **Una app que aquesta placa té, i prou**: `apps.info` is the door that answers whether the name is an app that runs here,
-- o sigui que un nom que no hi és no fa que la pàgina ensenyi una fitxa buida —cau a la llista i ho diu (docs/30, docs/40).
if asked ~= nil then
  -- **`works_here` i no només el manifest** (0.189.166): the same rule `app.run` follows —una app d'una altra placa és un nom
  -- que la porta llista i el nucli refusa—, o sigui que una fitxa d'una app que aquesta placa no pot córrer no s'obre (docs/30,
  -- docs/40).
  local what = apps.info(asked)
  if what ~= nil and what.works_here then
    details_only = true
    -- **Quant ha costat llegir-la i què s'hi ha trobat** (0.189.168).
    apps_here = { read_app(asked) }
    console.print(string.format('appinfo: the details of %s were asked for (loaded in %d ms)', asked,
                                sys.time_ms() - began))
    console.print('appinfo: ' .. asked .. ': ' .. numbers_of(apps_here[1]))
  else
    console.print('appinfo: there is no app ' .. asked .. ' on this board')
  end
end

-- **La llista de tot el disc només quan no hi ha cap fitxa demanada** (0.189.166).
if not details_only then read_all() end

-- The shape the person left, read before anything is placed: the page of the list, the row with the ring and the app whose
-- detail was open. A page that is not a number and an id that is not in the folder are dropped here (docs/30, docs/45).
local was = load_state()
if was ~= nil then
  local page = tonumber(was.page)
  if page ~= nil and page >= 1 then shape.page = math.floor(page) end
  if was.focus ~= nil and was.focus ~= '' then shape.focus = was.focus end
  if was.marked ~= nil and was.marked ~= '' then shape.marked = was.marked end
  -- **I quina forma és la que ha tornat** (0.189.168): the numbers of the block are what says whether the page came back where
  -- it was, i una línia amb ells és el que fa que una pàgina que torna malament es pugui veure al registre (docs/30, docs/45).
  console.print(string.format('appinfo: the shape of the page is %s (page %s, ring %s, detail %s)', kLayout,
                              tostring(shape.page), tostring(shape.focus or 'none'), tostring(shape.marked or 'none')))
else
  -- **I el cas normal d'una arrencada** (0.189.167 esborra el `state` de totes les apps): la pàgina comença pel principi i ho diu
  -- (docs/30, docs/45).
  console.print('appinfo: no shape of a page, it starts from the beginning')
end

if details_only then
  -- **Cap llista que comptar**: «1 apps» seria la fitxa comptada com si fos un disc, i la línia del detall ja s'ha dit
  -- (0.189.166, docs/30).
  show(asked)
else
  if reading ~= nil then
    console.print('appinfo: ' .. reading)
  else
    -- **Quantes, quant han costat i quant hi cap** (0.189.168): the scan is the whole point of this page —a folder walked and
    -- three grounds counted for every app—, o sigui que el registre en diu el preu i l'espai que hi ha, que és el que una
    -- persona mira quan el disc va just (docs/30, docs/70).
    local space = fs.space('/apps')
    local left = ''
    if space ~= nil then
      left = string.format(', %s free of %s', human(space.free), human(space.total))
    end
    console.print(string.format('appinfo: %d apps read in %d ms%s (lunokOS %s on %s)', #apps_here,
                                sys.time_ms() - began, left, sys.version, sys.board))
    -- **I les que no s'han pogut llegir, una per línia**: a manifest that this system does not read is a card that would show
    -- nothing, i el registre és l'únic lloc on es pot veure quina app és (docs/30, docs/40).
    for i = 1, #apps_here do
      local one = apps_here[i]
      if one.reading ~= nil then
        console.print(string.format('appinfo: %s: %s', one.id, one.reading))
      end
    end
  end

  build_cover()

  -- **I si la persona tenia un detall obert, és el que torna**: the same road a finger takes, and an id that is not in the
  -- folder opens nothing at all (docs/30, docs/45 §5 bis).
  if shape.marked ~= nil and asked == nil and index_of(shape.marked) > 0 then
    console.print('appinfo: came back to ' .. shape.marked)
    show(shape.marked)
  end
end

function on_pause() save_state(shape) end
function on_save() save_state(shape) end
function on_close() save_state(shape) end

-- The page is a tree of controls and the finger, the dial and the keys are the system's: what this loop does is keep the
-- kernel turning, run what a button left to be done and offer the page again. A present with nothing new draws nothing
-- (docs/30, docs/90).
while true do
  if wanted ~= nil then
    local doing = wanted
    wanted = nil
    sys.delay(300):await()
    doing()
  end

  ui.present()
  sys.delay(50):await()
end
