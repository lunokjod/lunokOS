-- editor: a page you type into, with the menu bar of the system over it.
--   run editor [path]
--
-- **File** opens, saves and closes a document; **View** turns the page read-only and turns the wrapping of long
-- lines on and off. The two entries of View wear a **box**, which is how this system draws a checkbox inside a menu:
-- filled when the setting is on, because the glass has ink and paper and no third tone for "on", and empty when it is
-- off —an entry with a `checked` is a switch and an entry without one is a command— (docs/30).
--
-- **The document is the app's copy**: the box of lines keeps the text it was given and hands the new one over on
-- every key, so opening a file is a field written here and a `present`, and the page is never asked what it holds
-- (docs/30, docs/90).
--
-- **The bar is in front of the page, and it is as tall as its row while nothing hangs from it and as tall as the
-- page while a menu does**: a finger on an entry has to land on the bar and not on the text under it, and how tall
-- a menu comes out is the system's arithmetic —the app does not guess it, it gives the bar the room to cover it—
-- (docs/30, docs/90).
--
-- **Where a file is written is the filesystem's to say**: an app writes its own ground (`/data/editor`) and the
-- path the prompt named (`run editor /sd/notes.txt`). A save the kernel refused says which path it refused and
-- that starting the editor with that path is what grants it (docs/70).

local caps = sys.caps()
if not caps.display then
  console.print('editor: this board has no glass')
  return
end

-- The row of the bar: what the app asks the control for plus the line under it, which is what a finger presses. The
-- system never makes a row smaller than forty-four and this one is a comfortable fifty-two (docs/30, docs/90).
local kBarHeight = 52
local kBarRow = 54
local kStatusH = 46
local kNoteH = 46
local kCardH = 208
-- **A file the person has to name** starts in the app's own ground, which is the one place it may write without the
-- prompt having named a path (docs/70).
local kNewPath = '/data/editor/note.txt'

local box, status, note, bar, kb
local card, card_title, card_field, card_ok, card_cancel
local resume, layout, accept_card, ask_path

local view = ui.caps()
ui.title('Editor')

-- **The document**: the text, where it came from, and the two switches of View. `dirty` is the `*` of the status
-- line, `readonly` is the absence of the keyboard, and `wrap` is the field of the box (docs/30, docs/90).
local doc = {
  path = nil,
  text = '',
  cursor = 0,
  dirty = false,
  pending = false,
  readonly = false,
  wrap = true,
  note = '',
}
local asking = nil   -- 'open' or 'save as' while the card is up
local quits = false  -- Close of the File menu
local bar_tall = false

local function code_of(err)
  if type(err) == 'table' then return tostring(err.code or err.message) end
  return tostring(err)
end

-- **Two lines at the bottom of the page**: the state —the name of the file, its size and the two switches— and, over
-- it, the last thing that happened. One line for both is a line that a path and a sentence do not fit in on this
-- glass, and the note is the one place a person on the board reads what a Save did (docs/30).
local function status_text()
  local parts = {doc.dirty and '*' or ' ', doc.path or '(no file)', #doc.text .. ' B'}
  if doc.readonly then parts[#parts + 1] = 'read only' end
  if doc.wrap then parts[#parts + 1] = 'wrap' end
  return table.concat(parts, '  ')
end

local function say()
  if status ~= nil then status.text = status_text() end
  if note ~= nil then note.text = doc.note end
end

-- **The whole page is painted again and clean**: opening a file, closing a card and unfolding a menu are moments
-- where the page changes all at once, and `gld16` is the wash that takes the old one away without a blink
-- (docs/50, docs/92).
local function redraw()
  -- **The text goes into the box with the page already measured**: a box that is told its text and then resized is a
  -- box that draws its rows at the size of before, and the rows of a long line move when the size changes (docs/30).
  if doc.pending then
    doc.pending = false
    box.text = doc.text
    box.cursor = doc.cursor
  end
  say()
  ui.present('gld16')
end

-- **Every control of the page owes its pixels again**, children included: a control that has already been composed
-- has nothing to compose, so wiping the canvas under it and presenting would leave its place empty (docs/30).
local function invalidate_all(control)
  control:invalidate()
  local kids = control.children
  if kids == nil then return end
  for i = 1, #kids do invalidate_all(kids[i]) end
end

-- **A menu is painted over the page and its card does not go away by itself** (0.86.2): the bar has no buffer of its
-- own —it paints the row and the card straight into the page—, so when the menu folds the pixels of the card are
-- still on the canvas under it. The page is painted again whole: the canvas is wiped, every control is asked for its
-- pixels again, and the bar draws its row (docs/30, docs/90).
local function repaint_page()
  ui.clear(15)
  invalidate_all(ui.root())
  redraw()
end

-- **Where the text goes when the file is not named**: the path of the open file, or the app's own ground. It is the
-- default the Save as card opens with, and the card shows it in full so nobody has to guess (docs/70).
local function path_or_new()
  return doc.path or kNewPath
end

local function load(path)
  local data, err = fs.read(path)
  if data == nil then
    doc.note = 'cannot open ' .. path .. ': ' .. code_of(err)
    return false
  end
  doc.text = data
  doc.cursor = 0
  doc.dirty = false
  doc.path = path
  doc.pending = true
  -- **A new file starts at the top**: the window a person had moved in the file before means nothing in this one,
  -- and the caret goes back to the first row (0.88.0, docs/30).
  box.top = -1
  doc.note = 'opened ' .. path
  console.print('editor: opened ' .. path .. ', ' .. #data .. ' bytes')
  return true
end

local function save(path)
  local written, err = fs.write(path, doc.text)
  if written == nil then
    local why = code_of(err)
    doc.note = 'cannot write ' .. path .. ': ' .. why
    -- **A refusal with a reason is a refusal somebody can act on**: the app's own ground and the path the prompt
    -- named are the two places it may write, and saying so costs one sentence (docs/70).
    if why == 'E_INVAL' then
      doc.note = doc.note .. ' (an app writes its own ground or the path the prompt named)'
    end
    console.print('editor: ' .. doc.note)
    return false
  end
  doc.path = path
  doc.dirty = false
  doc.note = 'saved ' .. path
  console.print('editor: saved ' .. path .. ', ' .. written .. ' bytes')
  return true
end

-- **The menus, built once and handed over**: an entry is a word, `{label=, checked=, enabled=}` or a dash, and the
-- tick of View is written here because the state of the document is the app's (docs/30, docs/90).
local menus = {
  {title = 'File', items = {
    {label = 'Open'},
    '-',
    {label = 'Save'},
    {label = 'Save as'},
    '-',
    {label = 'Close'},
  }},
  {title = 'View', items = {
    {label = 'Read Only', checked = false},
    {label = 'Word wrap', checked = true},
  }},
}

-- ---- the card that asks for a path ----

-- The children of the card are in the card's own pixels, so they are placed in one place and the card only moves
-- (docs/30).
local function place_card()
  if card == nil then return end
  local now = ui.caps()
  local w = now.width - 40
  card:place(20, math.min(kBarRow + 24, now.height - kCardH - 12))
  local half = (w - 48) / 2
  card_title:place(16, 14)
  card_field:place(16, 50)
  card_cancel:place(16, 124)
  card_ok:place(32 + half, 124)
end

-- The page, as the app has it now: the bar on top, the box of lines under it and the status at the bottom. Every
-- control is placed and sized here, which is what an `on_resize` needs (0.84.0, docs/30).
function layout()
  local now = ui.caps()
  local top = kBarRow + 8
  local bottom = now.height - kStatusH - kNoteH - 16
  box:resize(now.width - 24, bottom - top)
  box:place(12, top)
  note:resize(now.width - 24, kNoteH)
  note:place(12, now.height - kStatusH - kNoteH - 12)
  status:resize(now.width - 24, kStatusH)
  status:place(12, now.height - kStatusH - 6)
  bar:resize(now.width, bar_tall and now.height or kBarRow)
  bar:place(0, 0)
  place_card()
end

local function close_card()
  if card == nil then return end
  -- **The children go with it**: `release` marks the control and its fills, and the system frees the slots after the
  -- page has been painted over them (docs/30).
  card:release()
  card = nil
  card_title = nil
  card_field = nil
  card_ok = nil
  card_cancel = nil
  asking = nil
end

local function wrapped_label(parent, w, h, text)
  local label = ui.label(w, h)
  label.text = text
  label.align = 'center'
  label.valign = 'middle'
  parent:add(label)
  return label
end

function ask_path(kind)
  close_card()
  local now = ui.caps()
  local w = now.width - 40
  card = ui.canvas(w, kCardH)
  if card == nil then
    doc.note = 'no room for the card'
    redraw()
    return
  end
  asking = kind
  card:place(20, math.min(kBarRow + 24, now.height - kCardH - 12))
  -- **A card of ink and paper**: two pixels of frame and paper inside, which is the card this system draws and the
  -- only two tones it has. Its size is the one it was made with, and a canvas with a buffer has no other (docs/90).
  card.on_draw = function(self)
    self:rect(0, 0, w, kCardH, 0)
    self:rect(2, 2, w - 4, kCardH - 4, 15)
  end

  card_title = ui.label(w - 32, 46)
  card_title.text = (kind == 'open') and 'Open a file' or 'Save the page as'
  card_title:place(16, 14)
  card:add(card_title)

  -- **The path is typed with the keyboard of the system**, which is the only keyboard there is: the card says where
  -- it will write and the person edits that line (docs/30, docs/70).
  card_field = ui.textbox(w - 32, 56)
  card_field.text = path_or_new()
  card_field.cursor = #card_field.text
  card_field:place(16, 50)
  card_field.on_enter = function(self) accept_card() end
  card:add(card_field)

  local half = (w - 48) / 2
  card_cancel = ui.button(half, 56)
  card_cancel:place(16, 124)
  card_cancel.on_click = function(self)
    close_card()
    resume()
  end
  wrapped_label(card_cancel, half - 24, 44, 'Cancel'):place(12, 13)
  card:add(card_cancel)

  card_ok = ui.button(half, 56)
  card_ok:place(32 + half, 124)
  card_ok.on_click = function(self) accept_card() end
  wrapped_label(card_ok, half - 24, 44, (kind == 'open') and 'Open' or 'Save'):place(12, 13)
  card:add(card_ok)

  -- **The keyboard types into the field and not into the page** while the card is up: one band, one target
  -- (docs/30, docs/90).
  kb.target = card_field
  kb:show('alpha')
  layout()
  say()
  -- **What is left behind the card is paper**: a card that hangs over the text cuts it in the middle of a row, and half
  -- a letter cannot be read. The band is painted on the view before the card is composed over it, and the paper it
  -- covers comes back when the card folds, because then the page is painted whole (docs/30, docs/90).
  local y = math.min(kBarRow + 24, now.height - kCardH - 12)
  ui.rect(0, y - 6, now.width, kCardH + 12, 15)
  ui.present('gld16')
  console.print('editor: ' .. ((kind == 'open') and 'open' or 'save as') .. ' with ' .. card_field.text)
end

-- **Taking the card's answer**: the path is read before the card goes, because the field goes with it (docs/30).
function accept_card()
  if asking == nil or card_field == nil then return end
  local path = card_field.text
  local kind = asking
  close_card()
  if path == '' then
    doc.note = 'a path is needed'
  elseif kind == 'open' then
    load(path)
  else
    save(path)
  end
  resume()
end

-- ---- the page ----

box = ui.textbox_multiline(view.width - 24, view.height - kBarRow - kStatusH - 20)
box.wrap = doc.wrap
box:place(12, kBarRow + 8)
-- **The finger that walks the box moves the window and the bar on the right**: the app registers the gesture with
-- `on_swipe` and the system does the arithmetic —it writes the `top` of the box and presents the page—, and all that is
-- left here is saying where the person is inside the file, which is what the line under the page can do
-- (0.88.0, docs/30, docs/90). Without this function a box has no gesture: the finger puts the caret there and nothing
-- more, and a swipe moves nothing.
box.on_swipe = function(self, rows)
  if rows == nil or rows == 0 then return end
  -- **The row the window is on, with the two numbers the bar shows**: the system has already written `top`, so the
  -- person can read where they are inside the file without counting the pixels of the thumb (docs/90).
  local total = self:lines()
  if total ~= nil and total > 0 and self.top ~= nil and self.top >= 0 then
    doc.note = 'row ' .. (self.top + 1) .. ' of ' .. total
  end
  say()
end
box.on_change = function(self, text)
  doc.text = text
  doc.cursor = self.cursor
  doc.dirty = true
  doc.note = ''
  -- **Writing takes the window back to the caret**: the box follows the caret while nobody has moved it, and whoever has
  -- moved the bar and then writes wants to see what they write (0.88.0, docs/30).
  self.top = -1
  -- **The letters of the page go to the glass with a wash that cleans and does not blink**: a `du` leaves a third of
  -- the ink it finds and the text of the key before stays under the new one (docs/50, docs/92).
  say()
  ui.present('gld16')
end

note = ui.label(view.width - 24, kNoteH)
note.text = ''
note:place(12, view.height - kStatusH - kNoteH - 12)

status = ui.label(view.width - 24, kStatusH)
status.text = status_text()
status:place(12, view.height - kStatusH - 6)

bar = ui.menu_bar(view.width, kBarRow)
bar.bar_height = kBarHeight
bar:place(0, 0)
bar.items = menus

kb = ui.keyboard()

-- **The band comes or goes and the page is another one**: the system says so and the page takes the room it has, which
-- is the whole of what an app has to say about the keyboard (0.84.0, docs/30, docs/90).
function on_resize(width, height)
  if quits then return end
  layout()
  -- **A page that moved is a page painted again**: a control that changes its place leaves the ink of where it was,
  -- and its damage is the box of where it is now, so a present alone keeps the old words on the glass (docs/30, docs/90).
  repaint_page()
end

-- **The page the editor has when no card is up**: the keyboard types into the box of lines, unless the document is
-- read-only —and then there is no keyboard, because a read-only page has nothing to type into— (docs/30, docs/90).
function resume()
  -- **The box says whether it takes keys** (0.91.0): a read-only page keeps its box scrollable and its finger alive,
  -- but a finger on it must not bring a keyboard that has nothing to write.
  box.keyboard = doc.readonly and false or 'alpha'
  if doc.readonly then
    kb:hide()
  else
    kb.target = box
    kb:show('alpha')
  end
  layout()
  repaint_page()
end

-- **A choice of the bar**: the system has already folded the menu, so this reads the action and then runs it
-- (docs/30, docs/90). The numbers are the entries of the menu, dashes included.
local function on_choice(title, entry)
  if title == 1 then
    if entry == 1 then
      ask_path('open')
    elseif entry == 3 then
      if doc.path == nil then
        ask_path('save as')
      else
        save(doc.path)
        redraw()
      end
    elseif entry == 4 then
      ask_path('save as')
    elseif entry == 6 then
      quits = true
    end
  elseif title == 2 then
    if entry == 1 then
      doc.readonly = not doc.readonly
      menus[2].items[1].checked = doc.readonly
      bar.items = menus
      console.print('editor: read only ' .. tostring(doc.readonly))
      resume()
    elseif entry == 2 then
      doc.wrap = not doc.wrap
      menus[2].items[2].checked = doc.wrap
      bar.items = menus
      box.wrap = doc.wrap
      console.print('editor: wrap ' .. tostring(doc.wrap))
      -- **The page is painted again whole**: the rows of the box move when the wrapping changes, and a row that was
      -- there and is not any more leaves its ink if only the box is composed again (docs/30, docs/90).
      repaint_page()
    end
  end
end

bar.on_pick = function(self, title, entry) on_choice(title, entry) end

-- **The page as it starts**: the path the prompt named, or an empty page with the keyboard up. `app.args.path` is
-- the only path the prompt has granted and the one Save writes without asking (docs/30, docs/70).
if app.args ~= nil and app.args.path ~= nil and app.args.path ~= '' then
  load(app.args.path)
end
layout()
resume()
console.print('editor: File opens, saves and closes; View is read only and wrapping')

-- The loop of the page: **a menu that changed is a page painted again**. The bar has no canvas of its own —it paints
-- its card straight into the page—, so the card of the menu before is still on the glass under the one that took its
-- place; a finger that goes from File to View folds one menu and unfolds another, and the page is the only thing that
-- takes the first card away. The room the bar needs is the other thing this loop keeps: as tall as the page while
-- something hangs from it, a row while nothing does (docs/30, docs/90).
local open_was = bar.open or 0
while not quits do
  local open = bar.open or 0
  if open ~= open_was then
    open_was = open
    local tall = open > 0
    if tall ~= bar_tall then
      bar_tall = tall
      layout()
    end
    repaint_page()
  end
  sys.delay(50):await()
end

console.print('editor: closed' .. (doc.dirty and ' with unsaved changes' or ''))
close_card()
kb:hide()
-- **A page that is over leaves paper, and paper with nothing on it**: the glass is the app's while it lives and the
-- next page is not this one's to paint, so the last thing the editor does is clean it. The controls are let go first,
-- because a bar that still owes pixels paints itself again over the wiped page —its damage is its own and the wipe of
-- the canvas does not take it away— and the shell would be looking at the menu of an app that is gone (docs/30, docs/90).
local kids = ui.root().children
for i = #kids, 1, -1 do kids[i]:release() end
ui.clear(15)
ui.present('gld16')
