-- splash: every picture this system paints on the glass outside a page, one at a time (0.183.0).
--
-- **The list is the system's and not this file's**: `ui.splashes()` answers the words of the moments this build carries
-- —`boot`, `poweroff`, `reboot`, `charging`, `sleep`, `lowbatt`, `oops`— so the page cannot name a moment the system
-- does not have, and a moment added to the kernel arrives here without a line of this app changing (docs/30, docs/50).
--
-- **Choosing one is `ui.splash(word)`**, which is the same call the prompt's `splash` makes: the whole panel is washed
-- with a `gc16`, the drawing sits **in the middle** at its own size and never scaled, and **the page around it is the one
-- the picture's own border asks for** —a drawing made for black paper leaves the glass black and one made for paper
-- leaves it paper— (`splash::show`, `background_of` and `drawing_of`, docs/50). A picture that is not a page is the
-- normal case: the moments of this disk are 460 x 200.
--
-- **And the page comes back with a tap.** The picture is painted outside the page: the app's canvas was not touched, so
-- nothing has changed for the system and its next `present` would paint nothing at all. The way back is therefore the
-- page's own work —every control is told that it owes its pixels again and the glass is asked for a whole clean wash—,
-- which is the same rule the editor follows when it paints its page again (docs/30).

local caps = sys.caps()
if not caps.display then
  console.print('splash: this board has no glass')
  return
end

local view = ui.caps()

-- A step of the face is eleven pixels of line with the bold face of the disk and seven with the table of the flash, so
-- the sizes are asked in pixels and the face answers with the steps it has (0.121.0, docs/30).
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

local function line(scale)
  return scale * ui.line_height(1)
end

local kMargin = 20
local kGap = 12
-- The height of a row is a line of the body and its air, like the rows of every page of this system (docs/30, docs/92).
local kRow = line(kBody) + 20

-- The moments, read from the system: the list this page shows **is** the system's list.
local words = ui.splashes()
-- The moment that is on the glass, or nil when the page is what a person is looking at. It is also the lock that keeps
-- the tap which gives the page back from choosing the row under it.
local showing = nil
-- **The moment a finger has just chosen**, which goes up on the glass in the turn after the choice and not in the call of
-- the row: the press of a control is painted when the finger goes up, and a picture asked for from inside that call would
-- be painted over by the release of the row a moment later (measured: the row came back on top of `lowbatt`).
local pending = nil

local function make_label(width, text, scale, align, valign)
  local label = ui.label(width, line(scale) + 6)
  if label == nil then return nil end

  label.text = text
  label.scale = scale
  label.align = align or 'left'
  label.valign = valign or 'middle'
  label.color = 0
  label.background = 15

  return label
end

-- A sentence on as many labels as it needs. A label of this system does not wrap —a text that does not fit ends in
-- "..."— so the app breaks it with the same measurement the label draws with (docs/30, docs/90).
local function add_lines(parent, width, text, scale)
  local line_text = ''
  local made = 0

  local function emit(words_of_line)
    local label = make_label(width, words_of_line, scale, 'left', 'middle')
    if label ~= nil then
      parent:add(label)
      made = made + 1
    end
  end

  for word in text:gmatch('%S+') do
    local try = (line_text == '') and word or (line_text .. ' ' .. word)

    if line_text ~= '' and ui.text_width(try, scale) > width then
      emit(line_text)
      line_text = word
    else
      line_text = try
    end
  end

  if line_text ~= '' then emit(line_text) end

  return made
end

-- ------------------------------------------------------------------ the two screens

local screen = nil
-- The page is built by a function of its own, declared here and written below, because the way back from a picture calls
-- it and Lua only knows a local that has already been named (docs/30).
local build

local function give_back()
  if screen ~= nil then
    screen:release()
    screen = nil
  end
end

-- **The page again, whole and clean.** It is **built and not patched**: there is no `visible` in this system, and a
-- control that is asked to draw its pixels again is a page that can come back with a piece of it missing —the title of
-- this page was the first to go (measured on the emulator: the list came back and the word `Splash` did not)—. Giving the
-- page back and building it again is what the editor and the about page do with their two screens, and it costs eight
-- controls (docs/30, docs/90).
local function back()
  if showing == nil then return end

  showing = nil
  ui.clear(15)
  build()
  console.print('splash: back to the list')
end

-- **One moment on the glass**: the system paints it and this page only says which one. The word is the moment's name and
-- a word this build does not know is a path, which is the prompt's rule and the same call (docs/30, docs/50).
local function show(index)
  if showing ~= nil or pending ~= nil then return end  -- the tap that gives the page back is not a choice of row

  local word = words[index]
  if word == nil then return end

  pending = word
  console.print(string.format('splash: %s chosen', word))
end

build = function()
  give_back()
  local page = ui.panel(view.width, view.height, 'vertical')
  if page == nil then
    console.print('splash: there is no room for the page')
    return
  end

  page.padding = kMargin
  page.gap = kGap
  page:place(0, 0)

  local title = make_label(view.width - 2 * kMargin, 'Splash', kBig, 'center', 'middle')
  if title ~= nil then page:add(title) end

  add_lines(page, view.width - 2 * kMargin,
            'The pictures of the moments of this system. Choose one and it goes on the glass, a tap gives the page back.',
            kSmall)

  -- **The rows are the moments of the build**: each one is a stop of the dial and a finger presses it, and the list keeps
  -- the page —a page of ink and not a scroll— with its foot saying which page is on the glass (docs/30, docs/90).
  local the_list = ui.list(view.width - 2 * kMargin, kRow)
  if the_list == nil then
    console.print('splash: there is no room for the list')
  else
    the_list.weight = 1
    the_list.row_height = kRow
    page:add(the_list)

    local first = nil
    for i = 1, #words do
      local entry = ui.list_entry(view.width - 2 * kMargin, kRow)
      if entry ~= nil then
        local name = make_label(view.width - 2 * kMargin - 24, words[i], kBody, 'left', 'middle')
        if name ~= nil then
          name:place(12, 2)
          entry:add(name)
        end
        entry.on_click = function() show(i) end
        the_list:add(entry)
        if i == 1 then first = entry end
      end
    end
    -- The ring is on the first row, so the middle key opens something: the list takes the dial when it is built and the
    -- rows are its stops (docs/30).
    if first ~= nil then ui.focus(first) end
    local box = the_list:caps()
    console.print(string.format('splash: %d rows in a list of %d px', #words, box.height))
  end

  screen = page
  ui.present()
end

-- --------------------------------------------------------------------- the app

if #words == 0 then
  console.print('splash: this build carries no picture of a moment')
end

console.print(string.format('splash: %d moments on lunokOS %s', #words, sys.version))

build()

-- **The page is the tree's and the finger is the system's**: what this loop has to do is keep the kernel turning and, while
-- a picture is on the glass, hear the tap that gives the page back. A present with nothing new draws nothing, which is why
-- the way back has to mark the page whole (docs/30, docs/90).
--
-- **The tap that chooses a row is not the tap that gives the page back** (0.183.0): a control reads the gesture and does
-- not take it, and the gesture of a tap is delivered **in the turn after the release**, so the picture goes up and then
-- the tap that opened it arrives. It is taken here, once, before the loop starts listening — otherwise every moment opened
-- and went back in the same breath, and the glass only ever showed the wash of the picture (measured on the emulator: the
-- list came back with the glass still light).
while true do
  if pending ~= nil then
    local word = pending
    pending = nil
    local ok, why = ui.splash(word)
    if ok then
      showing = word
      console.print(string.format('splash: %s on the glass', word))
      -- The gesture of the choosing tap, taken with a ceiling of six hundred milliseconds: it lands in the next turn, and
      -- the ceiling is only there for a system that lost it.
      for _ = 1, 30 do
        if touch.gesture() ~= nil then break end
        sys.delay(20):await()
      end
    else
      console.print(string.format('splash: %s: %s', word, tostring(why)))
    end
  elseif showing == nil then
    ui.present()
  else
    local gesture = touch.gesture()
    if gesture ~= nil and gesture.kind == 'tap' then back() end
  end
  sys.delay(50):await()
end
