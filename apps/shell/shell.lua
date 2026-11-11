--  lunokOS :: apps/shell/shell.lua
--
--  The shell of the system in the glass, with tabs.
--
--  The page is a terminal and not a document: the menu bar on top opens a session
--  ("File" -> "New") and closes the one in front ("File" -> "Close"), the tabs below
--  say which session is in front and each one carries a cross on its right that gives
--  that session back, and the rest of the page is black with white letters.
--
--  **Every tab is a session of the shell and nothing else** (docs/30, "Una sessio del
--  shell"): this app never reads a line of its own, it writes what a person types on
--  the last line of the box to `session.stdin` and prints what the session answers
--  from `session.stdout` and `session.stderr`. The line of the prompt is the only line
--  that is written on: **the box is read-only wherever the caret is not on it**, and
--  that rule lives here, in the app, because the multiline has no caret hook -- a
--  page that reads is a page a person is in, and the caret is what says where.

local caps = sys.caps()
if not caps.display then
  console.print('shell: this board has no glass')
  return
end

local view = ui.caps()

--  The sizes of the paper: the line of a face of `line_px` pixels, as every app of
--  this system does it (`apps/about/about.lua`), because the step of the face is not 1.
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
  return scale * ui.line_height(1)
end

--  **The letters of a terminal are a fixed size and a small one** (0.164.1; the owner:
--  *«posa un tipus de lletra fix i mes petit»*): a terminal does not grow its letters with
--  the screen —a page of sixty columns is a page of sixty columns— and this one is dense on
--  purpose, because a line of shell is read in a glance and not at arm's length. **Three
--  steps of the face**: 33 px of line with the bold face of the disk and 21 with the table
--  of the flash, which is the dense size of the paper project (docs/30, docs/92).
local kTerminal = 3

--  **The prompt is the app's and not the session's** (0.164.1): a session of the shell has no
--  prompt and no echo —a pipe is not a terminal (docs/30)— so the page writes both: the
--  prompt in front of the line a person types, and the line again when it goes, which is
--  what every terminal does and what makes the page readable.
local kPrompt = '> '

local kMargin = 12

--  The tabs of the page: each one keeps the session and the text it has printed.
--  `text` is the truth and `box.text` follows it, because the box is one control and
--  the tabs are several sessions.
local tabs = {}
local current = 0
local opened = 0

local bar, strip, box
--  The hook of the box: the box is made by `build()`, and the hook is written once, below,
--  as a closure over the state of the app (0.164.1).
local on_the_line

local function labels()
  local out = {}
  for i = 1, #tabs do out[i] = tabs[i].title end
  return out
end

--  **The caret is on the line of the prompt when it is not before the text of the tab**:
--  what the tab has printed ends where the prompt begins, so the number that decides is the
--  length of that text and not a search for the last newline (0.164.1). Looking for the
--  newline was a second way of saying the same thing and it was what made the page hard to
--  read: with the prompt in the text, the last line is the prompt's.
local function at_the_prompt()
  local tab = tabs[current]
  if box == nil or tab == nil then return false end
  return box.cursor >= #tab.text
end

local function show(index)
  if #tabs == 0 then
    current = 0
    strip.index = 0
    box.text = kPrompt
    box.cursor = #box.text
    box.readonly = true
    return
  end
  if index < 1 then index = 1 end
  if index > #tabs then index = #tabs end
  current = index
  strip.index = index
  box.text = tabs[index].text .. kPrompt
  box.cursor = #box.text
  box.top = -1
end

local function new_tab()
  local session, why = sys.shell()
  if session == nil then
    --  The page says it where a person is looking instead of printing to a console
    --  that may not be there at all.
    box.text = (box.text or '') .. 'shell: no session: ' .. tostring(why) .. '\n'
    box.cursor = #box.text
    ui.present('du')
    return
  end
  opened = opened + 1
  tabs[#tabs + 1] = { session = session, text = '', title = 'Shell ' .. opened }
  strip.items = labels()
  show(#tabs)
  -- **The quality of the page is the system's** (0.179.5): a tab more is the strip and the box and not a photograph, and the
  -- kernel is the one that knows what the page has drawn since it was clean (docs/30, docs/50).
  ui.present()
end

local function drop(index)
  local tab = tabs[index]
  if tab == nil then return end
  --  **The app frees what the tab held**: the cross is a promise to the session, and
  --  a tab that goes away with the session open leaves it for nobody (docs/30).
  if tab.session ~= nil then tab.session:close() end
  table.remove(tabs, index)
  strip.items = labels()
  show(index)
  ui.present()
end

--  **The page is built and built again** (0.164.1): the keyboard is the system's and it
--  always sits **outside** the view of the app —like the bar— so when it opens the app's
--  view shrinks and the system says so with `on_resize`. A page that kept the box it was
--  given paints over the keyboard, and a page that never washed the glass keeps the picture
--  of the boot in its four corners: `ui.clear(15)` is the wash and building again is what
--  makes the band fit. The controls of the page before are given back, because a
--  control that is replaced without saying so is a control that keeps a slot for ever.
local function give_back()
  if bar ~= nil then bar:release() end
  if strip ~= nil then strip:release() end
  if box ~= nil then box:release() end
  bar, strip, box = nil, nil, nil
end

local function build()
  give_back()
  view = ui.caps()
  --  **And the quality is not this file's** (0.179.5): the clear says what is white —the whole page, because the box of
  --  the terminal is the page— and the present that follows paints it with the pass the inks deserve, which for a page
  --  of letters is the one that cleans without flashing (docs/30, docs/50).
  ui.clear(15)

  local width = view.width - 2 * kMargin
  local bar_h = line(kBody) + 12
  local tabs_h = line(kBody) + 10
  local box_y = kMargin + bar_h + tabs_h + kMargin
  local box_h = view.height - box_y - kMargin
  if box_h < line(kTerminal) * 3 then box_h = line(kTerminal) * 3 end

  --  The menu bar: "File" opens a session and closes the one in front.
  bar = ui.menu_bar(width, bar_h)
  if bar ~= nil then
    bar:place(kMargin, kMargin)
    bar.scale = kBody
    bar.items = { { title = 'File', items = { 'New', 'Close' } } }
    bar.on_pick = function(self, title, entry)
      if entry == 'New' then
        new_tab()
      elseif entry == 'Close' then
        drop(current)
      end
    end
  end

  --  The tabs, with a cross on the right of each one: `closeable` draws it and says which
  --  of the three things of a tab the finger found (`ui.tabs`, docs/30).
  strip = ui.tabs(width, tabs_h)
  if strip ~= nil then
    strip:place(kMargin, kMargin + bar_h)
    strip.scale = kBody
    strip.closeable = true
    strip.items = labels()
    strip.on_change = function(self, index)
      if index ~= nil and index ~= current then show(index) end
    end
    strip.on_close = function(self, index)
      drop(index)
    end
  end

  --  The terminal: black page, white letters and the fixed face of a terminal.
  box = ui.textbox_multiline(width, box_h)
  if box ~= nil then
    box:place(kMargin, box_y)
    box.scale = kTerminal
    box.color = 15
    box.background = 0
    box.wrap = true
    box.readonly = true
    box.text = ''
    box.cursor = 0
    box.on_change = on_the_line
  end

  --  A page without its strip or without its box is a page with nowhere to put a line,
  --  and it says so where a person is looking instead of failing on the first turn.
  if strip == nil or box == nil then
    console.print('shell: the page has no room for a terminal')
    return false
  end

  --  **The page keeps what it had** (0.164.2): the tab that was in front is still the one in front, and an
  --  app with no tab left still shows the prompt of a terminal that waits —the page of a terminal that has
  --  closed its only session is empty and legal, and `File -> New` is the way back—.
  show(current)
  return true
end

--  **A line that ends in a newline is a command** (0.164.1): the prompt says where the line
--  a person wrote begins, the line goes to the session without it, and **the terminal
--  echoes the line** because the session does not —a pipe is not a terminal— and then waits
--  for the answer, which comes back through `stdout` on the same page.
local writing = false
on_the_line = function(self, text)
  if writing then return end
  local tab = tabs[current]
  if tab == nil then return end
  if text:sub(-1) ~= '\n' then return end

  local head = tab.text .. kPrompt
  if text:sub(1, #head) ~= head then return end
  local asked = text:sub(#head + 1, #text - 1)

  writing = true
  tab.text = head .. asked .. '\n'
  local went = tab.session.stdin:write(asked .. '\n')
  box.text = tab.text .. kPrompt
  box.cursor = #box.text
  box.top = -1
  writing = false
  if not went then
    tab.text = tab.text .. 'shell: the line did not go\n'
    box.text = tab.text .. kPrompt
    box.cursor = #box.text
  end
  ui.present('du')
end

--  What a session has printed since the last turn, put on the page of its tab.
local function pump()
  local tab = tabs[current]
  if tab == nil or tab.session == nil then return end

  local said = tab.session.stdout:read()
  local why = tab.session.stderr:read()
  if (said == nil or said == '') and (why == nil or why == '') then return end

  if said ~= nil and said ~= '' then tab.text = tab.text .. said end
  if why ~= nil and why ~= '' then tab.text = tab.text .. why end
  box.text = tab.text .. kPrompt
  box.cursor = #box.text
  box.top = -1
  ui.present('du')
end

--  The page, the first session, and the wash that makes the glass the app's own.
if not build() then return end
new_tab()

--  **The system gives the app another view** —the keyboard takes a band of the glass and
--  gives it back, the bar goes away for a page in full screen— and the page is built again
--  in the box it is given, with the tabs and the text that are already there (0.164.1).
function on_resize(width, height)
  build()
end

while true do
  --  **The caret says where a person is**: on the line of the prompt the box is a
  --  place to write and the keyboard may come up, and anywhere else it is a page being
  --  read, which is the rule the multiline has no hook for.
  local write_here = (tabs[current] ~= nil) and at_the_prompt()
  box.readonly = not write_here
  box.keyboard = write_here

  pump()

  --  The finger: the cross of a tab and the body of a tab, both of them of the strip.
  local fingers = touch.read()
  if fingers ~= nil and #fingers > 0 then
    local f = fingers[1]
    if strip:tap(f.x, f.y) then ui.present('du') end
  end

  --  The gestures of the page are the controls' (the box scrolls itself); this app
  --  takes none of them and empties the queue anyway.
  while touch.gesture() do end

  sys.delay(150):await()
end
