--  lunokOS :: apps/shell/shell.lua
--
--  The shell of the system in the glass, with tabs.
--
--  **The page is a terminal and its menus are the system's** (0.189.29, the owner: *«L'app lua Shell no te menús
--  contextuals, i ha de ser amb menús contextuals com l'editor. Has fet una guarrada amb botons i vistes extra. Usa sempre
--  que puguis UIControls»*): a `ui.menu_bar` on top with **File** and **Commands**, a `ui.tabs` under it, the terminal in
--  the rest of the page and a card of the system when a name is asked for. **There is no page of the app behind a button
--  any more**: what a menu of the system can do, a menu of the system does (docs/30, docs/90).
--
--  **I la llista de comandes va per temes** (the owner: *«El menú contextual: File i Commands, si la llista de comandes no
--  hi cap, organitza-ho per temes en submenús»*): `sys.commands()` answers **fifty** words and a menu of this system holds
--  twelve, so `Commands` offers the themes —Files, Disk, Apps, System, Power, Time, Network, Hardware— and a finger on one
--  of them opens a `ui.menu` with the words of that theme. A word is **put on the line of the prompt and not sent**: the
--  person still presses Enter, which is what makes the list a help and not a launcher (docs/30).
--
--  **Every tab is a session of the shell and nothing else** (docs/30, "Una sessió del shell"): this app never reads a line
--  of its own, it writes what a person types on the last line of the box to `session.stdin` and prints what the session
--  answers from `session.stdout` and `session.stderr`.
--
--  **I el quadre és una pàgina on s'escriu, no una pàgina que es llegeix** (0.189.29, the owner: *«Tampoc li funciona el
--  teclat, polso sobre el cursor i no es desplega»*): the box used to be `readonly` while the caret was not on the line of
--  the prompt, and the system asks that field **at the moment of the tap** —so the tap that moved the caret into the line
--  was a tap on a box that took no keys, and the band never came up—. The box takes keys always and a key that lands
--  before the prompt is put back by `on_the_line`, which is the same rule said where it can be obeyed (docs/30, docs/90).
--
--  **I la tinta és la justa** (the owner: *«L'app ha de refrescar el mínim possible. Només les zones afectades i amb
--  qualitat de tinta adient (la justa i necessària per tapar el que hi ha)»*): the controls are made **once** and moved
--  when the view changes, so nothing is released and painted again on a turn; a line that arrives is a `du` of the box —
--  the pass that writes over what is there without flashing— and a page that has not changed pays no ink at all
--  (docs/30, docs/50).
--
--  **I les sessions es poden batejar i no es pot tancar l'última** (the owner: *«L'app Shell pot obrir sessions noves i
--  posalsi nom (tap llarg a la pestanya) a la sessió. Pot tancar-les totes menys l'última»*): a long press on a tab asks
--  the name with a card, `File -> Close session` and `Close others` are there, and with one tab the strip carries **no
--  cross at all** (`closeable` is false) because a page with no session is a page with nowhere to write (docs/30).

local caps = sys.caps()
if not caps.display then
  console.print('shell: this board has no glass')
  return
end

local view = ui.caps()

--  The sizes of the paper: the line of a face of `line_px` pixels, as every app of this system does it, because the step of
--  the face is not 1.
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step < 1 then step = 1 end
  local scale = math.floor(line_px / step + 0.5)
  if scale < 1 then scale = 1 end
  if scale > 16 then scale = 16 end
  return scale
end

local kBody = scale_for(43)

local function line(scale)
  local step = ui.line_height(1)
  --  **Un pas de zero no és una pàgina de zero píxels** (0.189.9): Eleven is the step of the bold face of the disk, and it
  --  is only the floor for an answer that has not arrived (docs/30, docs/92).
  if step < 1 then step = 11 end
  return scale * step
end

--  **The letters of a terminal are a fixed size and a small one** (0.164.1, the owner: *«posa un tipus de lletra fix i
--  mes petit»*): a page of sixty columns is a page of sixty columns, and this one is dense on purpose (docs/30, docs/92).
local kTerminal = 3

--  **El prompt és de l'app i no de la sessió** (0.164.1): a session of the shell has no prompt and no echo —a pipe is not a
--  terminal— so the page writes both (docs/30).
local kPrompt = '> '

local kMargin = 12
--  **Vuit pestanyes és el que el control sap dibuixar** (`ui.tabs`, docs/30): a ninth one would be a tab nobody can read.
local kTabCap = 8
--  **Un toc és llarg a partir de mig segon**: the press that a finger makes to type is not a press to name a tab.
local kLongPressMs = 600

--  The tabs of the page: each one keeps the session and the text it has printed.
local tabs = {}
local current = 0
local opened = 0

--  **El bloc de `state`** (0.189.12, and this app is the second one that has it): what is kept is the shape of the view —
--  the titles, the tab in front and **the last lines of each terminal**— because the owner asked for exactly that: *«ha
--  d'usar la sessió "state" per guardar els logs de les terminals oberts (últimes línies), els títols, quina pestanya
--  estava activa… per recuperar l'estat al arrencar l'app de nou»*. **The session itself does not come back** —a session
--  of the shell is a pipe and a pipe of a dead app is a pipe of nobody— so the page comes back with new sessions and the
--  lines of before as their history, which is what a person reads (docs/30, docs/45).
local kStateBytes = 4096   -- this page's view **is** text, so it is the one app whose block is bigger than a kilobyte
local kLayout = 'v1'       -- v1: the tab in front, how many there are, and a title and a tail per tab
local kTailBytes = 900     -- the last lines of a terminal that come back: fourteen lines of a shell of this face

local state = nil
local function the_state()
  if state == nil then
    local block = sys.session('state', kStateBytes)
    if block == nil then return nil end
    state = block
  end
  return state
end

local function escape(text)
  return (tostring(text or ''):gsub('\\', '\\\\'):gsub('\n', '\\n'))
end

local function unescape(text)
  --  **A Lua pattern does not escape with a backslash** —`%` is its escape— so one `\\` in the literal is one backslash in
  --  the text, and `\\(.)` would be asking for two of them (docs/30).
  return (text:gsub('\\(.)', function(c) return (c == 'n') and '\n' or c end))
end

local function save_state(now)
  local block = the_state()
  if block == nil then return false end
  local lines = { kLayout }
  for _, key in ipairs({ 'focus', 'page', 'tab', 'scroll', 'marked', 'tabs' }) do
    if now[key] ~= nil then lines[#lines + 1] = key .. '=' .. tostring(now[key]) end
  end
  for name, value in pairs(now.text or {}) do
    lines[#lines + 1] = 'text:' .. name .. '=' .. escape(value)
  end
  local body = table.concat(lines, '\n')
  if #body > kStateBytes then return false end
  block:clear()
  block:write(0, body)
  return true
end

local function load_state()
  local block = the_state()
  if block == nil then return nil end
  local body = block:read(0, kStateBytes) or ''
  -- **El bloc és de mil vint-i-quatre bytes i el que s'hi escriu és més curt** (0.189.52): el que hi ha darrere és
  -- el zero amb què el nucli va fer el bloc, i una línia amb zeros al darrere no és cap valor —`tonumber` en treu
  -- `nil` i un nom deixa de ser el nom—, o sigui que el text s'acaba a la primera marca de zero (docs/30).
  local zero = body:find('\0', 1, true)
  if zero ~= nil then body = body:sub(1, zero - 1) end
  local now = { text = {} }
  local first = true
  for one in body:gmatch('[^\n]+') do
    if first then
      first = false
      if one ~= kLayout then return nil end
    elseif one:sub(1, 5) == 'text:' then
      local name, value = one:match('^text:([^=]+)=(.*)$')
      if name ~= nil then now.text[name] = unescape(value) end
    else
      local key, value = one:match('^([%w_]+)=(.*)$')
      if key ~= nil then now[key] = value end
    end
  end
  if first then return nil end
  return now
end

--  **The last lines of a terminal**, which is what comes back: the tail is counted in bytes from the end and cut at the
--  first newline, so a history is whole lines and never half a word (docs/30).
local function tail_of(text, budget)
  if text == nil or text == '' or budget == nil or budget <= 0 then return '' end
  if #text <= budget then return text end
  local cut = #text - budget
  local start = text:find('\n', cut, true)
  if start == nil then return text:sub(cut + 1) end
  return text:sub(start + 1)
end

--  **El que la pàgina és ara**, dit al bloc: la pestanya de davant, quantes n'hi ha, i el títol i la cua de cada terminal.
--  **La cua es reparteix entre les pestanyes**: the block is four kilobytes and a page of eight sessions is eight tails, so
--  a tail is shorter when there are more of them and no tab is left out of the state (docs/30, docs/75).
local function keep_the_shape()
  local text = {}
  local budget = (kStateBytes - 200) // math.max(1, #tabs)
  if budget > kTailBytes then budget = kTailBytes end
  for i = 1, #tabs do
    text['t' .. i] = tabs[i].title
    text['l' .. i] = tail_of(tabs[i].text, budget)
  end
  save_state({ tab = current, tabs = #tabs, text = text })
end

local function labels()
  local out = {}
  for i = 1, #tabs do out[i] = tabs[i].title end
  return out
end

--  The controls of the page, made once (see the header): the bar, the strip, the terminal and the card of a name.
local bar, strip, box
local bar_tall = false       -- the bar takes the page's height while a menu hangs from it (docs/30)
local theme, theme_at          -- the menu of a theme, while it is open and where it is
local card, card_title, card_field, card_ok, card_cancel
local asking = 0               -- the tab a name is being asked for, or 0
local kb = nil                 -- the band of the system, asked for when the card needs it
--  **Un toc llarg** is a press a finger holds: it is decided where the finger lifts and read by the strip's own hooks, so
--  it is one of the page's own values and not the control's (docs/30, docs/90).
local long_press = false
--  The hook of the line: written below, and called by the box that `create()` makes.
local on_the_line
--  The value the line writes while the box's text is being put back, so a change the page made is not read as a change of
--  the person (docs/30).
local writing = false
--  **Els menús de la barra, fets quan la pàgina els demana**: la barra en sap quants tancaments es poden oferir, i el que
--  depèn de la pàgina es torna a dir quan la pàgina canvia (docs/30).
local the_menus
--  **La carta d'un tema** es tanca des de dins del seu propi `on_pick`, o sigui que es diu abans d'obrir-la (docs/30).
local close_the_theme

--  What the box says for a tab: its text, the prompt and the line a person is writing.
local function the_page_of(tab)
  return (tab.text or '') .. kPrompt .. (tab.typed or '')
end

local function show(index)
  if #tabs == 0 then
    current = 0
    if strip ~= nil then strip.index = 0 end
    if box ~= nil then
      box.text = kPrompt
      box.cursor = #box.text
    end
    return
  end
  if index < 1 then index = 1 end
  if index > #tabs then index = #tabs end
  current = index
  if strip ~= nil then strip.index = index end
  --  **El que s'està escrivint és de la pestanya i no del quadre** (0.189.9): the box is one control and the tabs are
  --  several sessions, so the line a person is writing lives in the tab and the box is composed from it (docs/30).
  if box ~= nil then
    box.text = the_page_of(tabs[index])
    box.cursor = #box.text
    box.top = -1
  end
  keep_the_shape()
end

--  **No es pot tancar l'última** (the owner: *«Pot tancar-les totes menys l'última»*): a page with no session has nowhere
--  to write, and the strip says so before the finger: `closeable` is false while there is one tab and the cross is not
--  drawn at all (docs/30).
local function keep_the_strip()
  if strip == nil then return end
  strip.closeable = (#tabs > 1)
  strip.items = labels()
  --  **I la barra sap si les portes de tancar es poden fer**: a `File` with `Close session` on a page of one session is a
  --  menu that offers what the page refuses (docs/30).
  if bar ~= nil then bar.items = the_menus() end
end

--  **A session more, on the page and in the strip** (0.189.29): a tab carries a name from the moment it is born —`Shell 3`
--  and not a number nobody reads— and the name is the person's to change with a long press (docs/30).
local function new_tab(title, history)
  if #tabs >= kTabCap then
    console.print('shell: the strip holds ' .. kTabCap .. ' sessions')
    return
  end
  local session, why = sys.shell()
  if session == nil then
    console.print('shell: no session: ' .. tostring(why))
    return
  end
  opened = opened + 1
  tabs[#tabs + 1] = {
    session = session,
    text = history or '',
    typed = '',
    title = (title ~= nil and title ~= '') and title or ('Shell ' .. opened),
  }
  keep_the_strip()
  show(#tabs)
  ui.present()
end

local function drop(index)
  local tab = tabs[index]
  if tab == nil then return end
  if #tabs <= 1 then
    console.print('shell: the last session stays')
    return
  end
  --  **The app frees what the tab held**: the cross is a promise to the session, and a tab that goes away with the session
  --  open leaves it for nobody (docs/30).
  if tab.session ~= nil then tab.session:close() end
  table.remove(tabs, index)
  keep_the_strip()
  show(math.min(index, #tabs))
  ui.present()
end

local function drop_others()
  if #tabs <= 1 then return end
  local kept = tabs[current]
  for i = #tabs, 1, -1 do
    if tabs[i] ~= kept then
      if tabs[i].session ~= nil then tabs[i].session:close() end
      table.remove(tabs, i)
    end
  end
  keep_the_strip()
  show(1)
  ui.present()
end

-- ------------------------------------------------------------------ los menús

--  **Les comandes per temes** (the owner: *«si la llista de comandes no hi cap, organitza-ho per temes en submenús»*): the
--  bar holds five titles and sixteen entries and a `ui.menu` holds twelve, so the list of the prompt is offered as
--  **themes in the bar** and the words of a theme in a card under it. A word the table does not know is not lost: it goes
--  to `Other`, which is the theme of what this page has not been told about yet (docs/30).
local themes = {
  { title = 'Files', words = { 'cat', 'ls', 'cd', 'pwd', 'mkdir', 'rm', 'mv', 'cp', 'hexdump', 'fs' } },
  { title = 'Disk', words = { 'mounts', 'free', 'stats', 'mem', 'sessions', 'clear' } },
  { title = 'Apps', words = { 'apps', 'run', 'stop', 'home', 'repo', 'lua', 'fonts', 'version' } },
  { title = 'System', words = { 'caps', 'panel', 'splash', 'cfg', 'keys', 'log', 'panic', 'help', 'echo', 'history' } },
  { title = 'Power', words = { 'poweroff', 'deepsleep', 'reboot', 'battery', 'led', 'sleep' } },
  { title = 'Time', words = { 'tz', 'date', 'alarm', 'ntp' } },
  { title = 'Network', words = { 'wifi', 'provision' } },
  { title = 'Hardware', words = { 'imu', 'touch', 'buttons', 'serial' } },
}

--  **The theme of a word**, and the words no theme names: a command the kernel adds tomorrow is in the list of the door
--  and not in this table, and hiding it would be the one way a person cannot find it (docs/30).
local function theme_of(word)
  for i = 1, #themes do
    for j = 1, #themes[i].words do
      if themes[i].words[j] == word then return i end
    end
  end
  return 0
end

local function the_orphans()
  local names = sys.commands()
  local out = {}
  if names == nil then return out end
  for i = 1, #names do
    if theme_of(names[i]) == 0 then out[#out + 1] = names[i] end
  end
  return out
end

--  **La barra**: `File` are the doors of a session and `Commands` the themes of the prompt (docs/30, docs/90).
the_menus = function()
  local words = {}
  for i = 1, #themes do words[#words + 1] = { label = themes[i].title } end
  local orphans = the_orphans()
  if #orphans > 0 then words[#words + 1] = { label = 'Other' } end
  return {
    { title = 'File', items = {
      { label = 'New session' },
      { label = 'Rename session' },
      '-',
      { label = 'Close session', enabled = (#tabs > 1) },
      { label = 'Close others', enabled = (#tabs > 1) },
    } },
    { title = 'Commands', items = words },
  }
end

--  **El que tria la barra**: the system has already folded the menu, so this reads the numbers and does the thing in the
--  same turn —a menu of the system is not a page of the app and there is nothing to release (docs/30, docs/90).
local function open_theme(index)
  local words
  if index <= #themes then
    words = themes[index].words
  else
    words = the_orphans()
  end
  if #words == 0 then return end
  local row = line(kBody) + 10
  local w = math.min(view.width - 2 * kMargin, 360)
  local h = (#words + 1) * row + 8
  if h > view.height - 2 * kMargin then h = view.height - 2 * kMargin end
  theme = ui.menu(w, h)
  if theme == nil then return end
  local items = {}
  for i = 1, #words do items[#items + 1] = words[i] end
  items[#items + 1] = 'Cancel'
  theme.items = items
  theme.title = (index <= #themes) and themes[index].title or 'Other'
  theme.scale = kBody
  theme_at = { x = (view.width - w) // 2, y = kMargin + line(kBody) + 12 }
  theme:place(theme_at.x, theme_at.y)
  theme.on_pick = function(self, picked)
    local word = words[picked]
    close_the_theme()
    if word ~= nil then insert_command(word) end
  end
  -- **El menú de la barra el plega el sistema quan el dit cau fora** (0.192.39), i aquí l'app oblida el que tenia a la mà (docs/90).
  theme.on_cancel = function() close_the_theme() end
  ui.present()
end

close_the_theme = function()
  if theme == nil then return end
  theme:release()
  theme, theme_at = nil, nil
  ui.present()
end

--  **La paraula va a la línia del prompt** i el cursor hi queda darrere: exactly what a person would have typed, with the
--  Enter still to be pressed (docs/30).
function insert_command(word)
  local tab = tabs[current]
  if tab == nil or word == nil then return end
  tab.typed = (tab.typed or '') .. word
  show(current)
  if box ~= nil then
    box.text = the_page_of(tab)
    box.cursor = #box.text
    box.top = -1
  end
  ui.present('du')
end

--  **The card of a name** (0.189.29): a panel of the system with the name of the tab in a box of one line and the two
--  doors —`Cancel` and `Name it`—, which is the same card the editor asks a path with (docs/30, docs/90).
local function card_size()
  local w = math.min(view.width - 4 * kMargin, 420)
  local h = ui.line_height(kBody) * 2 + line(kBody) + 40
  return w, h
end

local function place_the_card()
  if card == nil then return end
  local w, h = card_size()
  card:place((view.width - w) // 2, (view.height - h) // 2)
  local half = (w - 40) // 2
  if card_title ~= nil then card_title:place(16, 10) end
  if card_field ~= nil then card_field:place(16, ui.line_height(kBody) + 18) end
  local at = h - line(kBody) - 26
  if card_cancel ~= nil then card_cancel:place(16, at) end
  if card_ok ~= nil then card_ok:place(24 + half, at) end
end

local function close_the_card()
  if card == nil then return end
  if kb ~= nil then kb:hide() end
  card:release()
  card, card_title, card_field, card_ok, card_cancel = nil, nil, nil, nil, nil
  --  **El cercle torna a la caixa amb la targeta encara comptada com a oberta** (0.189.34): the ring going back is a focus
  --  change, and the box asks for the band when it takes the focus —which is right for a finger and wrong here, where a
  --  person has just finished naming a session and the page is not to lose a third of the glass for it (docs/30).
  if box ~= nil then ui.focus(box) end
  asking = 0
  ui.present()
end

local function ask_the_name(index)
  local tab = tabs[index]
  if tab == nil then return end
  asking = index
  local w, h = card_size()
  --  **Un panell sense ordre** (0.189.29): the children of this card are placed by `place_the_card`, so the panel must not
  --  arrange them as well —two ways of deciding a place is a card with its words where nobody put them— (docs/30).
  card = ui.panel(w, h)
  if card == nil then
    asking = 0
    return
  end
  card_title = ui.label(w - 32, ui.line_height(kBody))
  if card_title ~= nil then
    card_title.text = 'Name of the session'
    card_title.scale = kBody
    card_title.align = 'left'
    card:add(card_title)
  end
  card_field = ui.textbox(w - 32, ui.line_height(kBody) + 8)
  if card_field ~= nil then
    card_field.text = tab.title
    card_field.cursor = #card_field.text
    card_field.scale = kBody
    card_field.keyboard = 'alpha'
    card:add(card_field)
  end
  local half = (w - 40) // 2
  card_cancel = ui.button(half, line(kBody) + 12)
  if card_cancel ~= nil then
    card_cancel.title = 'Cancel'
    card_cancel.scale = kBody
    card_cancel.on_click = function() close_the_card() end
    card:add(card_cancel)
  end
  card_ok = ui.button(half, line(kBody) + 12)
  if card_ok ~= nil then
    card_ok.title = 'Name it'
    card_ok.scale = kBody
    card_ok.on_click = function()
      local tab_now = tabs[asking]
      local name = (card_field ~= nil) and card_field.text or ''
      if tab_now ~= nil and name ~= '' then
        tab_now.title = name
        keep_the_strip()
        keep_the_shape()
      end
      close_the_card()
    end
    card:add(card_ok)
  end
  place_the_card()
  if kb ~= nil and card_field ~= nil then
    kb.target = card_field
    kb:show('alpha')
  end
  ui.focus(card_field)
  ui.present()
end

-- ------------------------------------------------------------------ la pàgina

--  **Els controls es fan una vegada i es mouen** (the owner: *«L'app ha de refrescar el mínim possible»*): a control that
--  is released and made again on every change is a slot of the glass that goes and comes, and the ink of the page before
--  is what a person sees in the corners. The bar, the strip and the box live as long as the app; `layout()` is what
--  follows a view that changed (docs/30, docs/50).
local function create()
  view = ui.caps()
  local width = view.width - 2 * kMargin
  local bar_h = line(kBody) + 12
  local tabs_h = line(kBody) + 10

  --  The paper, once: a page of letters over the white the system left, and the pass is the system's (docs/30, docs/50).
  ui.clear(15)

  --  The tabs: `closeable` is the system's cross on the right of each one and it is false while there is one session, so
  --  the last one cannot be given back by a finger either (docs/30).
  strip = ui.tabs(width, tabs_h)
  if strip ~= nil then
    strip.scale = kBody
    strip.items = labels()
    strip.on_change = function(self, index)
      if index == nil then return end
      local held = long_press
      long_press = false
      if held then
        --  **Un toc llarg batia la pestanya** (the owner: *«posalsi nom (tap llarg a la pestanya)»*): the system has just
        --  said which tab the finger was on, so the card is asked for that one and not for the one that was in front
        --  (docs/30, docs/90).
        ask_the_name(index)
      elseif index ~= current then
        show(index)
        ui.present()
      end
    end
    strip.on_close = function(self, index)
      long_press = false
      drop(index)
    end
  end

  --  The terminal: black page, white letters and the fixed face of a terminal. **The box takes keys always**: a tap on it
  --  is a tap on a page a person writes on, and a key that lands before the prompt is put back by `on_the_line` (0.189.29).
  local box_y = kMargin + bar_h + tabs_h + kMargin
  local box_h = view.height - box_y - kMargin
  if box_h < line(kTerminal) * 3 then box_h = line(kTerminal) * 3 end
  box = ui.textbox_multiline(width, box_h)
  if box ~= nil then
    box:place(kMargin, box_y)
    box.scale = kTerminal
    box.color = 15
    box.background = 0
    box.wrap = true
    box.readonly = false
    box.keyboard = 'alpha'
    --  **El cursor d'un terminal hi és sense teclat** (0.189.9): a box that says `blink` keeps its block with the band
    --  away: one cell of ink every `cfg cursor delay` milliseconds, which is what a terminal does (docs/30, docs/90).
    box.blink = true
    box.text = ''
    box.cursor = 0
    box.on_change = on_the_line
    --  **I LA BANDA ARRIBA QUAN LA CAIXA PREN EL FOCUS** (0.189.34, l'amo: *«continua sense sortir el teclat»*): a tap on
    --  the terminal is a tap to type, and the road the system takes —the ring lands, the control is activated, the band
    --  comes— left this page with a cursor and no keyboard. Asking for the band from the focus is the same call the card of
    --  a name makes, it is answered whether the finger or the dial brought the ring, and it does not depend on that road
    --  (docs/30, docs/90). **`kb` is nil while this page is being made** —the band is asked for at the end of the start—
    --  so a focus given during the build brings nothing, which is what a page that opens should do.
    box.on_focus = function(self, on)
      --  **Mentre la targeta d'un nom és oberta, el teclat és el seu** (docs/30): the card has a field of its own and its own
      --  band, and the ring going back to the terminal when the card closes must not raise one over the page.
      if kb == nil or asking ~= 0 then return end
      if on then
        kb.target = box
        kb:show('alpha')
      else
        --  **I la banda marxa amb el cercle** (docs/30): a band over a page nobody is writing into is a third of the glass
        --  given away.
        kb:hide()
      end
    end
  end

  --  **LA BARRA, I DESPRÉS DE TOT** (0.189.34, l'amo: *«Els menús contextuals no obren»*): the bar is the **last** control of
  --  this page and not the first. The unfolded menu is a card the bar paints **into the page** under its own row, and the
  --  controls of a page are composed in the order they were made —the last one in front—, so a bar made first painted its
  --  card and the terminal painted its black paper over it: the menu was there and nobody could see a pixel of it. It is the
  --  same order the editor has, and its `File` has always opened (docs/30, docs/90).
  --  **I mentre un menú penja la barra és tan alta com la pàgina**: the card is drawn inside the control's own frame and a bar
  --  given the height of one row has nowhere to put it; the frame is the page's while nothing is open (`bar_tall`), and the
  --  space the bar does not paint is the page's (docs/30, docs/90).
  bar = ui.menu_bar(width, bar_h)
  if bar ~= nil then
    bar.bar_height = bar_h
    bar.scale = kBody
    bar.items = the_menus()
    bar.on_pick = function(self, title, entry)
      --  **File** is the first title and **Commands** the second, in the order `the_menus` writes them (docs/30).
      if title == 1 then
        if entry == 1 then
          new_tab(nil, nil)
        elseif entry == 2 then
          ask_the_name(current)
        elseif entry == 4 then
          drop(current)
        elseif entry == 5 then
          drop_others()
        end
      elseif title == 2 then
        open_theme(entry)
      end
    end
  end

  if bar == nil or strip == nil or box == nil then
    console.print('shell: the page has no room for a terminal')
    return false
  end
  return true
end

--  **Where the controls are**, which is the whole of what a view that changed needs (the keyboard takes a band, the bar
--  goes away for a full screen page): nothing is made and nothing is released, and the present that follows paints the
--  boxes that moved and no more (docs/30, docs/90).
local function layout()
  view = ui.caps()
  local width = view.width - 2 * kMargin
  local bar_h = line(kBody) + 12
  local tabs_h = line(kBody) + 10
  if bar ~= nil then
    --  **La barra és tan alta com la pàgina mentre un menú és obert** (0.189.34): `bar_tall` is the flag the editor learned
    --  the same way, and the frame the bar does not paint is the page's (docs/30, docs/90).
    bar:resize(width, bar_tall and (view.height - kMargin) or bar_h)
    bar.bar_height = bar_h
    bar:place(kMargin, kMargin)
  end
  if strip ~= nil then
    strip:resize(width, tabs_h)
    strip:place(kMargin, kMargin + bar_h)
  end
  local box_y = kMargin + bar_h + tabs_h + kMargin
  local box_h = view.height - box_y - kMargin
  if box_h < line(kTerminal) * 3 then box_h = line(kTerminal) * 3 end
  if box ~= nil then
    box:resize(width, box_h)
    box:place(kMargin, box_y)
  end
  if card ~= nil then place_the_card() end
end

-- ------------------------------------------------------------------ la línia

--  **A line that ends in a newline is a command** (0.164.1): the prompt says where the line a person wrote begins, the
--  line goes to the session without it, and **the terminal echoes the line** because the session does not, and then waits
--  for the answer, which comes back through `stdout` on the same page.
on_the_line = function(self, text)
  if writing then return end
  local tab = tabs[current]
  if tab == nil then return end

  local head = (tab.text or '') .. kPrompt
  if text:sub(1, #head) ~= head then
    --  **Una tecla que cau abans del prompt es torna a posar** (0.189.29): the box takes keys everywhere so the band comes
    --  up on a tap, and a page of a terminal is written on its last line and everywhere else is read —so what the key did
    --  to the text of before is undone here, which is the rule the `readonly` field could not keep and still open the band
    --  (the owner: *«polso sobre el cursor i no es desplega»*)— (docs/30, docs/90).
    writing = true
    box.text = the_page_of(tab)
    box.cursor = #box.text
    box.top = -1
    writing = false
    ui.present('du')
    return
  end
  if text:sub(-1) ~= '\n' then
    tab.typed = text:sub(#head + 1)
    return
  end
  local asked = text:sub(#head + 1, #text - 1)

  writing = true
  tab.typed = ''
  tab.text = head .. asked .. '\n'
  local went = (tab.session ~= nil) and tab.session.stdin:write(asked .. '\n')
  box.text = tab.text .. kPrompt
  box.cursor = #box.text
  box.top = -1
  writing = false
  if not went then
    tab.text = tab.text .. 'shell: the line did not go\n'
    box.text = tab.text .. kPrompt
    box.cursor = #box.text
  end
  keep_the_shape()
  --  **La tinta de la línia és la del text i no una rentada** (0.189.29): a `du` writes over what is there, which is what a
  --  terminal needs —a line of letters over a black page does not ghost— and a `gc16` of the whole page for every Enter
  --  would be four flashes for one line (docs/30, docs/50).
  ui.present('du')
end

--  What a session has printed since the last turn, put on the page of its tab. **A tab that is not in front keeps its
--  lines and paints nothing**: the text of a session that is behind is not on the glass (docs/30).
local function pump()
  local tab = tabs[current]
  if tab == nil or tab.session == nil then return end

  local said = tab.session.stdout:read()
  local why = tab.session.stderr:read()
  if (said == nil or said == '') and (why == nil or why == '') then return end

  if said ~= nil and said ~= '' then tab.text = tab.text .. said end
  if why ~= nil and why ~= '' then tab.text = tab.text .. why end
  writing = true
  box.text = the_page_of(tab)
  box.cursor = #box.text
  box.top = -1
  writing = false
  keep_the_shape()
  ui.present('du')
end

-- ------------------------------------------------------------------ la volta

--  The page, then the sessions: the ones the block remembers —with their names and the last lines of each— or one new.
if not create() then return end
local was = load_state()
if was ~= nil then
  local count = tonumber(was.tabs) or 1
  if count < 1 then count = 1 end
  if count > kTabCap then count = kTabCap end
  for i = 1, count do
    new_tab(was.text['t' .. i], was.text['l' .. i])
  end
  local at = tonumber(was.tab) or 1
  show(at)
  console.print(string.format('shell: %d session(s) from the state, on %d', #tabs, current))
else
  new_tab(nil, nil)
end
layout()
--  **El focus va a la caixa quan la pàgina ja és col·locada** (0.189.34): a box that is placed after it took the dial was a
--  dial the walk had to find again; the call is made here, with the page in its final size, which is what the editor does
--  by giving the box the focus from the moment it appears (docs/30, docs/90).
if box ~= nil then ui.focus(box) end
ui.present()

--  **El sistema dóna una altra vista a l'app** —the keyboard takes a band of the glass and gives it back, the bar goes away
--  for a page in full screen— and the page only puts its controls where they fit (0.164.1, 0.189.29).
function on_resize(width, height)
  layout()
  ui.present()
end

--  The three doors of the farewell: the shape is kept, because a page that is only written when it changes is a page that
--  loses the last line of a person who left in a hurry (docs/30).
kb = ui.keyboard()
function on_pause() keep_the_shape() end
function on_save() keep_the_shape() end
function on_close() keep_the_shape() end

--  **El dit**: a tap is the strip's, a long press is a name asked for, and the rest of the gestures are the controls' (the
--  box scrolls itself). The press is remembered where it began and the tap is delivered when the finger lifts, which is
--  what a tap is —the page before this delivered one on every frame the finger was down— (docs/30, docs/90).
local press_at, press_x, press_y = nil, 0, 0
local last_x, last_y = 0, 0
--  **El menú que era obert al torn d'abans** (0.189.34): the bar paints its card into the page, so a menu that folds leaves
--  the card of before on the glass and the page is the only thing that takes it away. One number and one compare per turn,
--  the same loop the editor has (docs/30, docs/90).
local open_was = 0

while true do
  --  **Un menú que s'obre o es plega canvia l'alçada de la barra i demana la pàgina**: without it the card of a menu has
  --  nowhere to be drawn and the one of the menu before stays under the new one (docs/30, docs/90).
  local open = (bar ~= nil and bar.open) or 0
  if open ~= open_was then
    open_was = open
    local tall = open > 0
    if tall ~= bar_tall then
      bar_tall = tall
      layout()
    end
    ui.present()
  end

  local fingers = touch.read()
  if fingers ~= nil and #fingers > 0 then
    local f = fingers[1]
    last_x, last_y = f.x, f.y
    if press_at == nil then
      press_at = sys.time_ms()
      press_x, press_y = f.x, f.y
    end
  elseif press_at ~= nil then
    local held = sys.time_ms() - press_at
    local moved = math.abs(last_x - press_x) + math.abs(last_y - press_y)
    if held >= kLongPressMs and moved < 24 then long_press = true end
    if strip ~= nil then
      --  **The strip is the only control this page delivers a tap to by hand**: the others are the system's own and they
      --  hear the finger where they are. The numbers are the view's, which is what `tabs_tap` takes —it takes its own place
      --  off them (docs/30, docs/90).
      if strip:tap(last_x, last_y) then ui.present('du') end
    end
    press_at = nil
  end

  if box ~= nil and tabs[current] ~= nil then
    --  **El quadre no és mai de només llegir** (0.189.29, the owner: *«polso sobre el cursor i no es desplega»*): the system
    --  asks `readonly` and `keyboard` **at the moment of the tap**, so a box whose answer depends on where the caret is was
    --  a box that refused the tap that was going to move the caret. It takes keys everywhere and `on_the_line` puts back
    --  what landed before the prompt (docs/30, docs/90).
    pump()
  end

  --  The gestures of the page are the controls' (the box scrolls itself, the menu is the system's); this app takes none of
  --  them and empties the queue anyway.
  while touch.gesture() do end

  --  **I el present només quan hi ha alguna cosa** (the owner: *«L'app ha de refrescar el mínim possible»*): a present with
  --  nothing damaged draws nothing, and this line is the one that lets a control of the system that changed by itself —the
  --  bar that unfolds— reach the glass in the turn it changed (docs/30, docs/50).
  ui.present()

  sys.delay(150):await()
end
