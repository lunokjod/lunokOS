-- wipe: **the page that empties the store of keys, the card and the flash** (0.171.0, and the flash from the 0.171.1). The
-- three places the system keeps and no page ever shows, and the door of `fs` that no app had before: `fs.format()` empties
-- a volume —`/nvs` erases every key of the store, `/sd` takes every file and folder of the card— and `fs.format('/littlefs')`
-- does the same with the disk the system itself lives on, which is the one that can leave the board with nothing but the
-- prompt. All three are one-way, and that is the whole of this page (docs/30, docs/70).
--
-- **THE WARNING IS THE SYSTEM'S CARD AND NOT A DRAWING OF THIS APP**: `ui.dialog` brings the frame, the wrapping, the two
-- doors, the ring and the finger, and the old rule of this project is the shape of the page —read the action, close the
-- card, and then run it— (docs/30). The one thing this page adds is which door is lit: **the safe one** (`emphasis = 1`
-- and `choice = 1`), because a finger that lands on a card it did not read must not be the finger that emptied the
-- board.
--
-- **THE BAR DOES NOT LIE**: `fs.format` is one call and it is the whole of the erasing, so the page cannot say how much
-- is done —it shows `-1`, the «don't know yet» of `ui.progress`, and puts it at 100 when the answer comes back— (docs/30,
-- docs/50). The turn that paints the bar goes before the call that holds the kernel, so the person sees what is
-- happening and not a page that froze.
--
-- **THE CARD THAT ENDS SUGGESTS A RESTART AND DOES NOT TAKE IT BY ITSELF**: the store is read at boot, so a board that
-- boots with an empty store is the one that has forgotten it, and the door `Restart` is the person's to press. The card
-- is closed before the board goes, which is the same rule as above: `sys.reboot` never runs under a card (docs/30).
--
-- **AND A DOOR THAT CANNOT BE OPENED IS SAID SO**: a board without a store has no NVS door and a board with an empty slot
-- has no card door —the button is `disabled`, so it draws in the dim tone, the dial walks past it and a finger on it
-- does nothing— (docs/30).

local caps = sys.caps()
if not caps.display then
  console.print('wipe: this board has no glass, and this page is one')
  return
end

-- **What the page can empty is what is mounted and not what the board has** (docs/70): `caps.nvs` says the chip keeps a
-- store and `caps.sd` that the board has a slot, and neither of them says that a card is in it —that is a row of
-- `fs.mounts`, the same truth `mounts` prints at the prompt.
local mounted = {}
for _, mount in ipairs(fs.mounts()) do mounted[mount.path] = mount end

local nvs_ready = mounted['/nvs'] ~= nil
local sd_ready = mounted['/sd'] ~= nil and mounted['/sd'].writable == true
local lfs_ready = mounted['/littlefs'] ~= nil and mounted['/littlefs'].writable == true

-- The words of the three doors, in one place: the page, the warning and the card that ends read from here and not from
-- nine copies that can disagree.
local kDoors = {
  nvs = {
    path = '/nvs',
    word = 'Erase the NVS',
    note = 'The settings of the board and of every app.',
    ask_title = 'Erase the NVS?',
    ask_text = 'Every key of the store goes: the settings of the board, the network it remembers and the data every app '
      .. 'put there. There is no way back.',
    ask_note = 'This cannot be undone.',
    accept = 'Erase',
    things = 'keys',
    what = 'store',
    reason = 'the NVS was erased',
    absent = 'this board has no store',
  },
  sd = {
    path = '/sd',
    word = 'Format the SD card',
    note = 'Every file of the card, the apps included.',
    ask_title = 'Format the SD card?',
    ask_text = 'Every file and every folder of the card goes, and the apps installed on it go with them. The card is '
      .. 'left with no file in it at all.',
    ask_note = 'This cannot be undone.',
    accept = 'Format',
    things = 'files and folders',
    what = 'card',
    reason = 'the card was formatted',
    absent = 'there is no card in the slot',
  },
  -- **The one that can leave the board with nothing but the prompt** (0.171.1): the flash carries the apps that came with
  -- the system, their pictures, the fonts and this very page, so the warning says that and not a list of counts.
  lfs = {
    path = '/littlefs',
    word = 'Format the flash',
    note = 'The apps and the pictures of the system go.',
    ask_title = 'Format the flash?',
    ask_text = 'Every file of the flash goes: the apps that came with the system, their pictures, the fonts and the data. '
      .. 'The board comes back with the prompt and nothing else, and this page is read from here.',
    ask_note = 'This cannot be undone.',
    accept = 'Format',
    things = 'files and folders',
    what = 'flash',
    reason = 'the flash was formatted',
    absent = 'this board has no flash disk',
  },
}

-- The sizes of the page, in pixels of line, and the face turns them into steps: a step is eleven pixels with the bold
-- face of the disk and seven with the five by seven of the flash (docs/30, docs/92).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end

  local scale = math.floor(line_px / step + 0.5)
  if scale < 1 then scale = 1 end
  if scale > 16 then scale = 16 end

  return scale
end

local view = ui.caps()

local function code_of(err)
  if type(err) == 'table' and err.code ~= nil then return err.code end
  return 'E_UNKNOWN'
end

-- **A page that cannot be made says so and stops**, because a page half built is a page with a button nobody can reach
-- (docs/30). The system's words are the ones the console gets.
local function refused(what, err)
  console.print('wipe: no ' .. what .. ': ' .. code_of(err))
  return nil
end

-- **The scale a line wants, kept beside it**: a label cannot hold a field of the app's, and the page asks for a size in
-- pixels of line and not in steps, so the wanted step is remembered here and the width of the page picks the one that
-- fits (docs/30).
local want = setmetatable({}, { __mode = 'k' })

-- **A word that does not fit goes down a step and not to three dots**: the glass of this project is two and a half tones
-- and a title cut at the edge says nothing to nobody (docs/30, docs/92).
local function fit(text, width, scale)
  local step = scale
  while step > 1 and ui.text_width(text, step) > width do
    step = step - 1
  end
  return step
end

local function line(text, scale, color)
  -- **A control is born with a size and not without one**: the system refuses a frame of nothing, and it takes a canvas
  -- as a whole number of bytes wide, so a control of one pixel is a control of none (docs/50). The layout below is what
  -- gives the page its boxes; until then these are two pixels square.
  local one, err = ui.label(2, 2)
  if one == nil then return refused('line of text', err) end
  one.text = text
  one.scale = scale
  one.color = color
  one.valign = 'middle'
  want[one] = scale
  return one
end

local head = line('Wipe', scale_for(46), 0)
if head == nil then return end
local sub = line('Three places this board keeps and never shows.', scale_for(22), 8)

-- **The warning comes later and the doors are made first**: a door says what it does and `ask` is the page below it, so
-- the name travels and the body is written once.
local ask

-- **The words of a door are a label inside the button and not a button that knows how to write**: the fill is the
-- system's, the frame is the system's, and turning the button off turns the words off with it (docs/30).
local function make_door(kind)
  local words = kDoors[kind]
  local door = {}
  door.kind = kind
  local failed = nil
  door.button, failed = ui.button(2, 2)
  if door.button == nil then return refused('door', failed) end
  door.label = line(words.word, scale_for(34), 0)
  if door.label == nil then return nil end
  door.label.align = 'center'
  door.button:add(door.label)
  door.button.on_click = function() ask(kind) end
  door.note = line(words.note, scale_for(21), 8)
  return door
end

local door = { nvs = make_door('nvs'), sd = make_door('sd'), lfs = make_door('lfs') }
if door.nvs == nil or door.sd == nil or door.lfs == nil then return end

local foot = line('A restart is what makes it true.', scale_for(20), 8)
local bar = nil
local status = line('', scale_for(24), 0)
local status_w = view.width
-- **Where the bar goes, said once**: the message of a door is the same place whether or not the bar has been made yet,
-- so the page does not move under a finger (docs/30).
local bar_y = 0

-- ------------------------------------------------------------------ the page

-- **The page of this board is up and down and every measure comes from it** (540 x 960 on the Paper): a column of two
-- doors with the words under each and the bar that says what happened at the bottom, and no number written for one glass
-- that another board would not have (docs/30).
local function layout()
  view = ui.caps()
  local w, h = view.width, view.height
  local m = math.max(10, w // 24)
  local inner = w - 2 * m
  local pad = math.max(8, h // 40)

  local head_h = math.max(22, math.floor(h * 0.055))
  local sub_h = math.max(16, math.floor(h * 0.035))
  local door_h = math.max(48, math.floor(h * 0.12))
  local note_h = math.max(16, math.floor(h * 0.03))
  local bar_h = math.max(12, math.floor(h * 0.025))
  local status_h = math.max(18, math.floor(h * 0.032))

  -- **A cursor and not eight coordinates**: the page is one column that goes down, so every block says its height and
  -- takes the next place, and a page of another size is the same page with other numbers (docs/30).
  local y = pad
  head.scale = fit(head.text, inner, want[head] or 2)
  head:resize(inner, head_h)
  head:place(m, y)
  y = y + head_h + math.floor(pad * 0.6)

  sub.scale = fit(sub.text, inner, want[sub] or 2)
  sub:resize(inner, sub_h)
  sub:place(m, y)
  y = y + sub_h + pad

  for _, kind in ipairs({ 'nvs', 'sd', 'lfs' }) do
    local one = door[kind]
    one.box = { x = m, y = y, w = inner, h = door_h }
    one.button:resize(inner, door_h)
    one.button:place(m, y)
    one.label.scale = fit(one.label.text, inner - 2 * m, want[one.label] or 2)
    one.label:resize(inner - 2 * m, door_h)
    one.label:place(m, 0)
    y = y + door_h + math.floor(pad * 0.5)
    one.note_w = inner
    one.note.scale = fit(one.note.text, inner, want[one.note] or 2)
    one.note:resize(inner, note_h)
    one.note:place(m, y)
    y = y + note_h + pad
  end

  -- The bar keeps its place once it exists: it is where the answer appears, and a bar that moves the page under itself
  -- is a page that jumps when a door is pressed (docs/30).
  bar_y = y
  if bar ~= nil then
    bar:resize(inner, bar_h)
    bar:place(m, bar_y)
  end
  y = y + bar_h + math.floor(pad * 0.6)

  status.scale = fit(status.text, inner, want[status] or 2)
  status:resize(inner, status_h)
  status:place(m, y)
  status_w = inner
  foot.scale = fit(foot.text, inner, want[foot] or 2)
  foot:resize(inner, status_h)
  foot:place(m, math.max(y + status_h, h - status_h - pad))
end

-- The status line is the one thing that changes while the page stays: the answer of a door, in the ink and not in the
-- dim tone of a note, because it is what happened.
local function say(text, color)
  status.text = text
  status.scale = fit(text, status_w, want[status] or 2)
  status.color = color or 0
end

-- **The door that cannot be used is not in the way of the dial**: `disabled` is the state the system knows, and it takes
-- the control out of the walk and the finger both (docs/30).
local function dress_doors()
  for _, kind in ipairs({ 'nvs', 'sd', 'lfs' }) do
    local one = door[kind]
    local ready = (kind == 'nvs' and nvs_ready) or (kind == 'sd' and sd_ready) or (kind == 'lfs' and lfs_ready)
    one.button.disabled = not ready
    -- **The words of a door that is off go to the tone of a note**: the frame of a button knows it is off and a label
    -- inside it is the app's, so the two tones are said here too (docs/30, docs/92).
    one.label.color = ready and 0 or 8
    one.note.text = ready and kDoors[kind].note or kDoors[kind].absent
    one.note.scale = fit(one.note.text, one.note_w or view.width, want[one.note] or 2)
  end
end

local function show_bar(value)
  if bar == nil then
    local m = math.max(10, view.width // 24)
    local made, err = ui.progress(view.width - 2 * m, math.max(12, math.floor(view.height * 0.025)))
    if made == nil then
      console.print('wipe: no bar: ' .. code_of(err))
      return
    end
    bar = made
    layout()
  end
  bar.value = value
end

-- ------------------------------------------------------------------ the cards

local card = nil
local busy = false
-- **What a door asked for and what the turn has not done yet**: `nil` when the page owes nothing (docs/30).
local wanted = nil

local function close_card()
  if card == nil then return end
  card:release()
  card = nil
end

-- **The fingers and the dial go back to the page after a card is gone**: the card had the focus and a released control
-- takes it with it, so the ring is put back where a person can find it (docs/30).
local function after_card()
  for _, kind in ipairs({ 'nvs', 'sd', 'lfs' }) do
    if not door[kind].button.disabled then
      ui.focus(door[kind].button)
      break
    end
  end
  ui.present('gc16')
end

local function work(kind)
  local words = kDoors[kind]
  busy = true
  for _, one in pairs(door) do one.button.disabled = true end
  say('Emptying the ' .. kDoors[kind].what .. '...', 8)
  show_bar(-1)
  ui.present('gc16')
  -- **The turn that paints the bar goes first**: `fs.format` holds the kernel while it walks what it empties, so the
  -- page says what it is doing and then does it (docs/30).
  sys.delay(30):await()

  local answer, err = fs.format(words.path)
  busy = false

  -- What is mounted may have changed under the call —a card is formatted and the card is still there— and the doors are
  -- dressed from the mounts again so that the page never says a door is open when it is not.
  mounted = {}
  for _, mount in ipairs(fs.mounts()) do mounted[mount.path] = mount end
  nvs_ready = mounted['/nvs'] ~= nil
  sd_ready = mounted['/sd'] ~= nil and mounted['/sd'].writable == true
  lfs_ready = mounted['/littlefs'] ~= nil and mounted['/littlefs'].writable == true
  dress_doors()

  if answer == nil then
    say('Nothing was done: ' .. code_of(err), 0)
    console.print('wipe: ' .. kind .. ' refused ' .. code_of(err))
    after_card()
    return
  end

  if bar ~= nil then bar.value = 100 end
  say(string.format('%d %s gone from the %s.', answer.erased, words.things, words.what), 0)
  console.print('wipe: ' .. kind .. ' erased ' .. answer.erased)

  -- **The card that ends**: it says what happened, it offers a restart and it lets the person say no. An empty store is
  -- a fact of the next boot, which is why the door is here and not a restart in the middle of the page.
  local w = math.min(view.width - 40, 560)
  local h = math.min(420, view.height - 80)
  close_card()
  local made, card_err = ui.dialog(w, h)
  if made == nil then
    say('No room for the card: restart the board when you want it back.', 0)
    console.print('wipe: no card: ' .. code_of(card_err))
    after_card()
    return
  end
  card = made
  card.scale = 3
  card.title = 'Done'
  card.text = string.format('%d %s went. What the board had in memory is still there until it restarts, so a board '
    .. 'that boots now is the one that has forgotten it.', answer.erased, words.things)
  card.note = 'Restart now?'
  card.accept = 'Restart'
  card.cancel = 'Not now'
  card.emphasis = 2
  card.choice = 2
  card:place((view.width - w) // 2, math.max(0, (view.height - h) // 2))
  -- **The card takes the dial**: a card over the page whose focus is still on the door behind it is a card that the
  -- centre of the dial does not answer, and the page would erase with a question on it (docs/30, docs/90).
  ui.focus(card)
  card.on_accept = function(self)
    -- Read the action, close the card, and then run it: the board going down under a card leaves the card on the glass
    -- of a board that is not there any more (docs/30).
    close_card()
    ui.present('gc16')
    sys.reboot(words.reason)
  end
  card.on_cancel = function(self)
    close_card()
    after_card()
  end
  ui.present('gc16')
end

-- **The warning**: the card asks, the accept door is the one the person has to walk to, and the two doors answer with a
-- callback that closes the card first (docs/30).
ask = function(kind)
  if busy then return end
  local words = kDoors[kind]
  close_card()
  local w = math.min(view.width - 40, 560)
  local h = math.min(420, view.height - 80)
  local made, err = ui.dialog(w, h)
  if made == nil then
    say('No room for the card of the warning: nothing was done.', 0)
    console.print('wipe: no card: ' .. code_of(err))
    return
  end
  card = made
  -- **Three steps and not four**: the message of a warning is six lines of a card and not three, and a card of the
  -- system's body in a card of this size leaves the words cut in the middle (docs/30, docs/92).
  card.scale = 3
  card.title = words.ask_title
  card.text = words.ask_text
  card.note = words.ask_note
  card.accept = words.accept
  card.cancel = 'Keep'
  -- The safe door is the lit one and the one the dial is on: a card about to destroy something never starts on the door
  -- that destroys it.
  card.emphasis = 1
  card.choice = 1
  card:place((view.width - w) // 2, math.max(0, (view.height - h) // 2))
  ui.focus(card)
  -- **The door says what was asked and the turn of the app does it**: a hook of a control runs inside the system's call
  -- and cannot wait for anything, and the erasing waits for the ink of the bar before it starts. So the accept closes the
  -- card and leaves the wish here, which is the old rule of the project taken to the letter —read the action, close the
  -- card, and then run it— (docs/30).
  card.on_accept = function(self)
    close_card()
    wanted = kind
    ui.present('gc16')
  end
  card.on_cancel = function(self)
    close_card()
    say('Nothing was done.', 8)
    after_card()
  end
  say('')
  ui.present('gc16')
end

-- ------------------------------------------------------------------ the app

layout()
dress_doors()
say('Nothing has been touched yet.', 8)
ui.present('gc16')

-- **The places the page can be pressed, said out loud when the app is asked to**: `run wipe boxes` prints the box of each
-- door and which of them can be pressed on this board, so a test presses a door without knowing the layout (docs/30,
-- docs/85).
if app.args ~= nil and app.args.path == 'boxes' then
  for _, kind in ipairs({ 'nvs', 'sd', 'lfs' }) do
    local one = door[kind]
    console.print(string.format('wipe %s %d %d %d %d', kind, one.box.x, one.box.y, one.box.w, one.box.h))
  end
  console.print('wipe nvs ' .. (nvs_ready and 'open' or 'closed'))
  console.print('wipe sd ' .. (sd_ready and 'open' or 'closed'))
  console.print('wipe lfs ' .. (lfs_ready and 'open' or 'closed'))
end

console.print('wipe: the store of keys, the card and the flash, emptied on request')

function on_resize(width, height)
  if width == nil then return end
  layout()
  dress_doors()
  ui.present('gc16')
end

-- **Nothing of this page is saved**: the two doors write the system and not a setting of the app, so there is no
-- `on_close` here — the store keeps what the boot reads and the card keeps what the person put in it (docs/30).
while true do
  -- **The work of a door happens here and not inside its hook** (0.171.0): the erasing waits for the turn that painted
  -- the bar, and a hook of a control is called from inside the system and cannot wait (docs/30).
  if wanted ~= nil then
    local kind = wanted
    wanted = nil
    work(kind)
  end
  sys.delay(50):await()
end
