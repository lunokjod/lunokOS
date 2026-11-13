-- tz: **the hour of the person, chosen on a map of the world** (0.170.0). The system keeps one number for it —how many
-- minutes the local hour is ahead of UTC— and this is the page where a person finds that number with a finger instead
-- of typing `cfg tz +01:00` (docs/30, docs/60).
--
-- **THE MAP IS NOT HOW THE ZONE IS FOUND, IT IS HOW IT IS READ.** A zone is a band of fifteen degrees of longitude and
-- the finger already says which one: the column under the finger divided by fifteen. The map is the picture that makes
-- the bands mean something —the land under the line, the ocean in the middle of it— and it is a PNG of the disk, so a
-- person who wants another one edits that file and not this code (docs/30, docs/70).
--
-- **THE ZONES ARE DRAWN AS A GRAPH AND NOT AS PAINT**: a band above the map and a band below it with a mark at every
-- hour boundary, and the chosen hour filled in both. Painting the bands over the map would hide the land, and a map of
-- the world with the world hidden is a ruler.
--
-- **THE ZONE IS SAVED WHEN THE APP CLOSES AND NOT WHEN IT IS TOUCHED**, which is the owner's decision (9/11):
-- `time.tz()` writes the setting at once, so the page keeps the choice and calls it from `on_close`. The first day of the
-- week is the other way round —a list is a setting with an immediate effect on every calendar of the system, and a person
-- who turns it is telling the system and not this page— (docs/60).
--
-- **EL QUE COMPTA ÉS L'HORARI D'ESTIU I NO EL DIA DE TRASPÀS** (0.179.50). The owner, with the page on the emulator:
-- *«A l'app Time zone surt "Leap year" i aquesta dada no es important, l'important es el "Daylight time" (horari
-- d'estiu!)»*. The leap day is a calendar of the whole board and it has its own two homes —`cfg leap` and the automatic
-- one of a clock that came from a server—, so the box of this page is the hour a person really has in summer: **checked,
-- the offset of the map goes one hour east** (`kDaylightMinutes`), in the bands, in the label and in what the close
-- saves. It is a shift and not a second setting: the system keeps one number, and a board that reads +02:00 in July and
-- +01:00 in December has the same setting all year (docs/30, docs/60).
--
-- **THE THREE SETTINGS ARE THE SYSTEM'S CONTROLS AND THE MAP IS THE APP'S** (0.174.0): a `ui.button` with its `title`, a
-- `ui.checkbox` with its label and a `ui.selector` whose door opens a `ui.menu`; the ring of the focus walks them and the
-- centre is Enter. The map is a picture and a band of fifteen degrees, so it stays primitives and the finger is its only
-- reader —**and the dial is not the hour's any more**, because a page with focusable controls gives the dial to the
-- system— (docs/30, docs/92).
--
-- **IT IS NOT AN APP OF PROVA**: it carries the icon of a normal app, because it is one a person uses (docs/40).

local caps = sys.caps()
if not caps.display or not caps.touch then
  console.print('this board needs a glass and a touch panel for this')
  return
end

-- The whole page the first time and after a change, and the quality of a page of text when a finger moves the choice:
-- on e-ink that is the difference between a settings page and a page that flashes (docs/50).
local kPageMode = 'gc16'
local kMoveMode = 'du'
-- **Una hora i no una regla**: the hour of summer is what a person asks for, and the system keeps offsets of minutes. A
-- country that shifts by half an hour is a `time.tz` of its own, not a box of this page (docs/30, docs/60).
local kDaylightMinutes = 60

-- **How long a person waits for the time server before the page says it did not answer**: the system's default is twenty
-- seconds, which on this page is twenty seconds of a button that does nothing, and a finger that presses it again
-- deserves an answer and not a queue (0.174.0).
local kSyncTimeoutMs = 8000

-- **The hours the map carries**: -12 to +12, which is the twenty-five bands of fifteen degrees a world map of
-- 360 degrees draws whole. The two the world keeps past the date line (+13 and +14) are not on the map and `cfg tz`
-- is still the way to them (docs/60).
local kFirstHour = -12
local kLastHour = 12
local kHours = kLastHour - kFirstHour + 1
local kMinutesPerHour = 60

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

local kSmall = scale_for(22)
local kBody = scale_for(33)
local kTitle = scale_for(57)

-- The page as the finger sees it: every box is measured once here and both the painter and the hit test read it from
-- here, because a box painted with one number and touched with another only works where nobody is looking.
local box = {}
local page_w = 0
local page_h = 0

local function build_layout()
  local view = ui.caps()
  page_w, page_h = view.width, view.height
  local margin = 20
  local map_w = view.width - 2 * margin
  -- The map is a world of two to one: the band of the page keeps it 500 x 250 on the Paper and whatever fits smaller
  -- elsewhere, and the picture is scaled to it by `ui.image` (docs/30).
  local map_h = math.floor(map_w / 2)

  box.map_x = margin
  box.map_y = 46
  box.map_w = map_w
  box.map_h = map_h
  -- The mark of a zone, above and below the map: eighteen pixels of a band with a line at every boundary.
  box.band_h = 18
  box.top_y = box.map_y - box.band_h - 6
  box.bottom_y = box.map_y + map_h + 6
  box.hours_y = box.bottom_y + box.band_h + 6
  box.label_y = box.hours_y + 30
  box.saved_y = box.label_y + 44

  local y = box.saved_y + 42
  box.sync_x, box.sync_y, box.sync_w, box.sync_h = margin, y, 260, 68
  -- **The row of the checkbox is four pixels taller than its square on every side** (0.174.0): the system gives the
  -- square the height of the row less four pixels above and below —the room its ring needs—, so a square of forty is a
  -- row of forty-eight, and the row is placed four pixels up to keep the square where this page had it (docs/30).
  box.check_box = 40
  box.check_x = margin
  box.check_y = y + 92
  box.check_row_w = map_w
  box.check_row_h = box.check_box + 8
  box.select_x, box.select_y = margin, box.check_y + 76
  box.select_w, box.select_h = map_w, 68
  -- **The list of the two days is a `ui.menu` under the field** (0.174.0): the app says where the card goes and the
  -- system draws the rows and takes the finger and the dial; two rows of a card 160 high are 62 pixels each, so the
  -- card is the height the page has room for and not a row counted by the app (docs/30, docs/90).
  box.list_x, box.list_y = box.select_x, box.select_y + box.select_h
  box.list_w = box.select_w
  -- **Two rows of a menu are never under 104 pixels** —52 is the smallest row the system draws— so a glass with no room
  -- under the field puts the list over its foot instead of asking for a card of nothing (docs/30).
  box.list_h = math.min(160, view.height - box.list_y - 10)
  if box.list_h < 104 then
    box.list_h = 104
    if box.list_y + box.list_h > view.height then box.list_y = view.height - box.list_h end
  end
end

-- **What the page is holding**, which is not yet what the system keeps: the offset of the hour waits for the close, and
-- the first day of the week goes in as soon as it is touched (docs/60). `chosen_hour` is the hour the map says and
-- `chosen` the offset it is worth, and they are both here because the page, the bands and the box of the hour of summer
-- read them from the same place (0.179.50).
local chosen = 0
local chosen_hour = 0
-- **L'horari d'estiu és una hora de més sobre el que diu el mapa** and it is the page's until the close, like the
-- offset itself. `leap` stays because a clock that comes from a server turns the leap day on (the rule of 9/11), and this
-- page is where that clock is asked for (docs/60).
local dst = false
local leap = true
local week = 1
local status = ''
-- **What a click asked for and the loop has to do**: see the `on_click` of the button (0.174.0).
local want_sync = false
local busy = false

-- **El que la pàgina ensenya i desa**: the offset the map says plus the hour of summer, and the hour of the band that is
-- filled. One place each, so a checked box cannot move the label and leave the bands behind (0.179.50, docs/30).
local function shown_offset()
  return chosen + (dst and kDaylightMinutes or 0)
end

local function shown_hour()
  return chosen_hour + (dst and 1 or 0)
end

local function tz_text(minutes)
  local sign = '+'
  if minutes < 0 then
    sign = '-'
    minutes = -minutes
  end

  return string.format('%s%02d:%02d', sign, minutes // 60, minutes % 60)
end

local function hour_at(x)
  local lon = (x - box.map_x) * 360.0 / box.map_w - 180.0
  local hour = math.floor(lon / 15.0 + 0.5)
  if hour < kFirstHour then hour = kFirstHour end
  if hour > kLastHour then hour = kLastHour end

  return hour
end

local function x_of_hour(hour)
  return box.map_x + ((hour * 15.0 + 180.0) / 360.0) * box.map_w
end

-- The two bands of the graph and the marks of the hours, which is where the zones are read: a line at every boundary
-- and the chosen hour filled, above the map and below it (docs/50: a filled band is `du`, and it is a mark and not a
-- page).
local function paint_bands(chosen_hour)
  ui.rect(box.map_x, box.top_y, box.map_w, box.band_h, 15)
  ui.rect(box.map_x, box.bottom_y, box.map_w, box.band_h, 15)
  for i = 0, kHours do
    local hour = kFirstHour + i - 1
    local x = math.floor(x_of_hour(hour) + 0.5)
    ui.line(x, box.top_y, x, box.top_y + box.band_h)
    ui.line(x, box.bottom_y, x, box.bottom_y + box.band_h)
  end

  local left = math.floor(x_of_hour(chosen_hour - 1) + 0.5)
  local right = math.floor(x_of_hour(chosen_hour + 1) + 0.5)
  ui.rect(left + 1, box.top_y, right - left - 1, box.band_h, 0)
  ui.rect(left + 1, box.bottom_y, right - left - 1, box.band_h, 0)
end

-- **The label is measured and not guessed**: a sentence of twenty-six characters at the size of a title does not fit
-- this glass, and a label that ends in "Selected timezone: -" is a label that says nothing (docs/30, docs/92).
local function fit_scale(text, width, biggest)
  local scale = biggest
  while scale > 1 and ui.text_width(text, scale) > width do
    scale = scale - 1
  end
  return scale
end

local function paint_hours()
  ui.rect(box.map_x, box.hours_y, box.map_w, ui.line_height(kSmall), 15)
  -- Every other hour, because twenty-five offsets at this size do not fit and the ones that are read are the two ends
  -- and the zero.
  for hour = kFirstHour, kLastHour, 4 do
    local text = tostring(hour)
    local w = ui.text_width(text, kSmall)
    local x = math.floor(x_of_hour(hour) - w / 2)
    if x < box.map_x then x = box.map_x end
    if x + w > box.map_x + box.map_w then x = box.map_x + box.map_w - w end
    ui.text(x, box.hours_y, text, kSmall, 8)
  end
end

-- **The three settings are the system's controls and not boxes painted by hand** (0.174.0). The owner, with the page on
-- the emulator: *«el desplegable ... no te cap estètica que mostri que es una llista desplegable? per altra banda, el botó
-- de sync NTP tampoc sembla un botó de sistema... son controls UI o uses alguna altra cosa? el focus sembla no funcionar
-- amb ells»*. He was right on all four: they were `ui.frame`, `ui.rect` and `ui.text` with a hit test of this app, so
-- there was nothing focusable and the dial had nowhere to land. A `ui.button` with its `title`, a `ui.checkbox` with a
-- label inside it and a `ui.selector` are the look and the behaviour of every other page of this board, they are drawn
-- by the kernel, and **the ring of the focus arrives at them by itself** (docs/30, docs/92).
local sync = nil
local dstbox = nil
local dstwords = nil
local weekfield = nil
local weekmenu = nil
-- **What the controls ask for is the page's, and it is said here because the controls are made above it**: the two
-- requests to the time server and to the list of the two days, and the place of the controls on the page.
local sync_clock
local open_week_menu
local close_week_menu
local place_settings
-- **I la pàgina sencera**, because the box of the hour of summer paints it from its hook and a hook that runs before the
-- page exists would be a nil (0.179.50, docs/30).
local paint_page

local function refused(what, err)
  console.print('tz: no ' .. what .. ' (' .. tostring(err and err.code or 'the memory') .. ')')
end

-- **A control is born two pixels square and told its box afterwards** (0.174.0): the layout below is what gives the page
-- its boxes, and `resize` is what a page that can be turned needs —the glass says another measure and the same controls
-- move—. A control with a buffer refuses a new size; a button, a checkbox, a selector and a label have none, which is why
-- this page can lay itself out again (docs/30).
place_settings = function()
  if sync == nil then
    local why
    sync, why = ui.button(2, 2)
    if sync == nil then return refused('sync button', why) end
    -- **The word is the button's and not a label inside it**: `title` is painted by the control, so it changes ink with
    -- the state —the focus and the finger invert it— and an app cannot leave the words and the frame disagreeing (0.173.0,
    -- docs/30).
    sync.title = 'Sync NTP'
    sync.primary = true
    -- **The click is a hook and a hook cannot wait** (0.174.0): `net.ntp` is an `await`, so the button leaves the wish here
    -- and the loop of the page does it —which is the rule this project already has for a card's accept: read the action,
    -- close, and then run it— (docs/30, docs/90).
    sync.on_click = function() want_sync = true end

    dstbox, why = ui.checkbox(2, 2)
    if dstbox == nil then return refused('daylight box', why) end
    dstwords, why = ui.label(2, 2)
    if dstwords == nil then return refused('daylight words', why) end
    dstwords.text = 'Daylight time'
    dstwords.valign = 'middle'
    dstbox:add(dstwords)
    -- **La casella és de la pàgina i no del sistema** (0.179.50): the offset of the board is one number, and the hour of
    -- summer is the map's hour plus sixty minutes — a shift of this page, saved with it. What it moves is the band that is
    -- filled and the label, so the page is painted again whole: a box that turned and a band that stayed where it was is
    -- two answers to one finger (docs/30, docs/60).
    dstbox.on_change = function(self, on)
      dst = on
      -- **I es desa en el moment** (0.182.0): the hour of summer is a setting of the system and not a shift of this page, so the
      -- box writes it when it turns and the hour of the board moves with it —`time.date()` reads the offset of the map plus this
      -- hour—. What waits for the close is the offset of the map, which is a number a person walks the map to choose
      -- (docs/30, docs/60).
      time.dst(on)
      console.print('tz: dst ' .. (on and 'on' or 'off') .. ' ' .. tz_text(shown_offset()))
      paint_page()
      ui.present(kPageMode)
    end

    weekfield, why = ui.selector(2, 2)
    if weekfield == nil then return refused('week field', why) end
    weekfield.label = 'Week starts'
    weekfield.items = { 'Monday', 'Sunday' }
    weekfield.on_open = function(self) open_week_menu(self) end
  end

  local line = ui.line_height(kBody)
  sync:resize(box.sync_w, box.sync_h)
  sync:place(box.sync_x, box.sync_y)
  sync.scale = kBody

  dstbox:resize(box.check_row_w, box.check_row_h)
  dstbox:place(box.check_x, box.check_y - (box.check_row_h - box.check_box) // 2)
  dstbox.checked = dst
  dstwords:resize(box.check_row_w - dstbox:box() - 32, line)
  dstwords:place(dstbox:box() + 16, (box.check_row_h - line) // 2)
  dstwords.scale = kBody

  weekfield:resize(box.select_w, box.select_h)
  weekfield:place(box.select_x, box.select_y)
  weekfield.scale = kBody
  weekfield.index = (week == 7) and 2 or 1
end

local function paint_status()
  ui.rect(box.sync_x + box.sync_w + 16, box.sync_y + 20, box.map_w - box.sync_w - 16, ui.line_height(kSmall), 15)
  if status == '' then return end
  ui.text(box.sync_x + box.sync_w + 20, box.sync_y + 20, status, kSmall, 0)
end

paint_page = function()
  ui.clear(15)
  ui.image(box.map_x, box.map_y, '/res/world.png', box.map_w, box.map_h)
  paint_bands(shown_hour())
  paint_hours()

  local sentence = 'Selected timezone: ' .. tz_text(shown_offset())
  ui.text(box.map_x, box.label_y, sentence, fit_scale(sentence, box.map_w, kTitle), 0)
  ui.text(box.map_x, box.saved_y, 'Saved when this app closes', kSmall, 8)

  -- **The three settings are not painted here**: they are children of the page, the system draws them, and this call is
  -- only the text of the page and the answer of the last synchronisation (0.174.0, docs/30).
  paint_status()
  -- **I la pàgina que s'ha esborrat els demana que es tornin a dibuixar** (0.179.50): `ui.clear` paints the paper of the
  -- page over what its children had there, and a control draws only when it is marked —placing it marks it, which is why
  -- the first page is whole—. A page painted whole after a sync or after the box of the hour of summer left the button and
  -- the list white until the next finger (found in the window, 13/11). `invalidate` of a parent asks its children, which is
  -- the rule of 0.168.4 (docs/30, docs/90).
  for _, one in ipairs({ sync, dstbox, weekfield }) do
    if one ~= nil then one:invalidate() end
  end
end

local function paint_choice()
  -- **A finger on the map moves two bands and one line**: the bands and the row of the label paint themselves over
  -- their own paper, and the map under them is not touched, because a page that flashes at every hour is a page nobody
  -- drags across (docs/50).
  paint_bands(shown_hour())
  paint_hours()
  ui.rect(box.map_x, box.label_y, box.map_w, ui.line_height(kTitle), 15)
  local sentence = 'Selected timezone: ' .. tz_text(shown_offset())
  ui.text(box.map_x, box.label_y, sentence, fit_scale(sentence, box.map_w, kTitle), 0)
  ui.present(kMoveMode)
end

-- --------------------------------------------------------------------- the app

build_layout()

-- **The page reads the system first**: the offset it is showing is the one the board has, and the first day of the week
-- is the one every calendar of this board is using (docs/30, docs/60).
chosen = time.tz()
chosen_hour = math.floor(chosen / kMinutesPerHour + (chosen >= 0 and 0.5 or -0.5))
if chosen_hour < kFirstHour then chosen_hour = kFirstHour end
if chosen_hour > kLastHour then chosen_hour = kLastHour end
chosen = chosen_hour * kMinutesPerHour
leap = time.leap()
-- **L'hora d'estiu és la del sistema i no la d'aquesta visita** (0.182.0): the box shows what the board has kept, so a person who
-- comes back finds it ticked and the hour they left (docs/30, docs/60).
dst = time.dst() and true or false
week = time.week()
-- **The controls are placed after the state is read and before the page is painted**: they show the leap day and the
-- first day of the week the board has, and the first `present` composes them over the paper, so this page is made of its
-- primitives and of three controls the system owns (0.174.0).
place_settings()

-- **The places the page can be touched, said out loud when the app is asked to**: `run tz zones` prints the box of the
-- map, the centre of every hour and the boxes of the three controls, so a test presses a zone and a setting without
-- knowing the layout (docs/30, docs/85). **And it says which of them take the ring** (0.174.0): a page whose controls the
-- dial could not reach would be the page the owner complained about, so the app says it and a test can hold it.
if app.args ~= nil and app.args.path == 'zones' then
  console.print(string.format('tz map %d %d %d %d', box.map_x, box.map_y, box.map_w, box.map_h))
  for hour = kFirstHour, kLastHour do
    console.print(string.format('tz hour %d %d %d', hour, math.floor(x_of_hour(hour) + 0.5),
      box.map_y + box.map_h // 2))
  end
  console.print(string.format('tz sync %d %d %d %d', box.sync_x, box.sync_y, box.sync_w, box.sync_h))
  console.print(string.format('tz dst %d %d %d', box.check_x + box.check_box // 2,
    box.check_y + box.check_box // 2, box.check_box))
  console.print(string.format('tz week %d %d %d %d', box.select_x, box.select_y, box.select_w, box.select_h))
  console.print(string.format('tz list %d %d %d %d', box.list_x, box.list_y, box.list_w, box.list_h))
  for _, one in ipairs({ sync, dstbox, weekfield }) do
    ui.focus(one)
    console.print('tz stop ' .. tostring(one.focused))
  end
  ui.focus(sync)
end

paint_page()
ui.present(kPageMode)

-- **Saving is the close and nothing else**: `time.tz()` writes the setting at once, so this page calls it when the
-- system is taking the app away —the pinch, `exit`, or a close of the shell— and a person who leaves without touching
-- anything leaves the board as it was (docs/30, docs/60). **The three taps of the centre are not the way out of this
-- page any more** (0.174.0): a page with focusable controls has the centre for Enter, which is the system's rule, so the
-- pinch of the glass is what closes it (docs/30).
function on_close()
  -- **El que es desa és l'hora del mapa i prou** (0.182.0): the hour of summer has its own setting and it was written when the box
  -- turned, so folding it into the offset here would save summer twice —a board that reads +02:00 in December— and the box would
  -- come up unticked on the next visit (docs/30, docs/60).
  if chosen ~= time.tz() then
    time.tz(chosen)
    console.print('tz: saved ' .. tz_text(chosen))
  end
end

-- **The synchronisation is a promise and the page waits for it**: `net.ntp` is a request of the kernel's queue and this
-- page has nothing else to do while it waits, so the finger that asked sees the answer when it comes (docs/30).
sync_clock = function()
  if busy then return end
  busy = true
  -- **The button says it is working by not being pressable**: `disabled` is the state the system knows, and it takes the
  -- control out of the dial and off the finger both (docs/30).
  sync.disabled = true
  status = 'syncing...'
  paint_status()
  ui.present(kMoveMode)

  -- **A refusal arrives as a raise and not as a `nil`** (0.174.0, docs/30): `await` throws like every promise of this
  -- system, so a board with no network saved —the commonest board there is— took the app down with it when the button
  -- was pressed, which is a bug this page had since it was written and that the case of `test_apps` found. The promise
  -- that is refused is settled before the `await`, so the `pcall` does not cross a wait; the wait of a request that is
  -- really in flight is a yield and Lua lets it through (docs/30).
  local ok = false
  local why = nil
  if caps.net_wifi then
    local asked, answer = pcall(function() return net.ntp(nil, kSyncTimeoutMs):await() end)
    ok = asked and answer == true
    if not asked then
      local code = (type(answer) == 'table') and answer.code or nil
      -- **The sentence for a person and not the code of the kernel**: the three refusals of this request mean three
      -- different things to whoever is holding the board (docs/30).
      if code == 'E_NONET' then
        why = 'no network saved'
      elseif code == 'E_TIMEOUT' then
        why = 'the server did not answer'
      elseif code == 'E_BUSY' then
        why = 'the radio is busy'
      else
        why = 'the request did not go out'
      end
    end
  else
    why = 'this board has no radio'
  end
  busy = false
  sync.disabled = false

  if ok then
    -- **An hour that came from a time server turns the leap day on**: the calendar of the world has the twenty-ninth
    -- of February, and a board that has just been told what day it is does not need to be told that too (the owner's
    -- rule, 9/11, docs/60).
    -- **I el dia de traspàs ja no té casella en aquesta pàgina** (0.179.50): the setting is still turned on by a clock that
    -- came from a server —the rule of 9/11— and it is read and written with `cfg leap` and `time.leap`, but the page of the
    -- hour is the page of the hour (docs/60).
    if not leap then
      leap = time.leap(true)
    end
    local now = time.date()
    status = string.format('synced %04d-%02d-%02d %02d:%02d', now.year, now.month, now.day, now.hour, now.min)
    console.print('tz: ' .. status)
  else
    status = 'no clock: ' .. tostring(why or 'no answer')
    console.print('tz: ' .. status)
  end

  paint_page()
  ui.present(kPageMode)
end

-- **The list of the two days is a `ui.menu` of the system** (0.174.0): the door of the selector is `on_open`, the app puts
-- the card under the field and says the two options, and the system draws the rows, takes the finger and the dial and
-- tells the app which one was chosen. **Closing it is the app's**, which is the rule of the cards of this project: read
-- the action, close, and then do it (docs/30).
open_week_menu = function(field)
  if weekmenu ~= nil then return end
  local menu, why = ui.menu(box.list_w, box.list_h)
  if menu == nil then
    field.choosing = false
    ui.present(kMoveMode)
    return refused('list', why)
  end
  menu.items = { 'Monday', 'Sunday' }
  menu.index = field.index
  menu.scale = kBody
  menu:place(box.list_x, box.list_y)
  menu.on_pick = function(self, index)
    -- **Read, close, then do**: the week is written with the card already gone, so the page under it never moves with a
    -- finger still on the glass (docs/90).
    close_week_menu()
    week = time.week(index == 2 and 7 or 1)
    weekfield.index = index
    console.print('tz: week ' .. ((week == 7) and 'sunday' or 'monday'))
    ui.present(kPageMode)
  end
  weekmenu = menu
  ui.present(kMoveMode)
end

close_week_menu = function()
  if weekmenu == nil then return end
  weekmenu:release()
  weekmenu = nil
  weekfield.choosing = false
  -- **The ring goes back to the door that opened the list**: the card took the dial while it was up and the field is
  -- where the person was, so the next turn of the dial is not a jump across the page (docs/30).
  ui.focus(weekfield)
end

-- **The finger of this page is the map's and nothing else** (0.174.0): the three settings are controls and the system
-- takes their finger —a button that is pressed, a box that turns, a door that opens—, so the only box this app still
-- hit-tests is the map, which is a picture of a world and not a control. **A list that is up is the one thing that takes
-- the finger away from the map**, and a finger that is not on it is its way out, which is what a card over a page means
-- anywhere (docs/30, docs/90).
local function tap(x, y)
  if weekmenu ~= nil then
    if x < box.list_x or x >= box.list_x + box.list_w or y < box.list_y or y >= box.list_y + box.list_h then
      close_week_menu()
      ui.present(kPageMode)
    end
    return
  end

  if x >= box.map_x and x < box.map_x + box.map_w and y >= box.top_y and y < box.map_y + box.map_h + box.band_h + 6 then
    chosen_hour = hour_at(x)
    chosen = chosen_hour * kMinutesPerHour
    console.print('tz: picked ' .. tz_text(chosen))
    paint_choice()
  end
end

-- The loop of the page: a finger on the first frame it is seen, and the system's gestures taken and dropped so that a
-- queue nobody empties does not grow (the same rule the calculator follows, docs/30).
local touched = false

while true do
  local view = ui.caps()
  if view.width ~= page_w or view.height ~= page_h then
    -- **A turned glass is the same page with other boxes**, and the controls move with it (0.174.0).
    build_layout()
    place_settings()
    paint_page()
    ui.present(kPageMode)
  end

  local fingers = touch.read()
  if #fingers > 0 then
    if not touched then
      touched = true
      tap(fingers[1].x, fingers[1].y)
    end
  elseif touched then
    touched = false
  end

  -- **And a button that was pressed asks for the time server now**, here and not in its hook, because the request is an
  -- `await` and a hook runs between two turns (0.174.0, docs/30).
  if want_sync then
    want_sync = false
    sync_clock()
  end

  -- **The keys are not read here any more** (0.174.0): a page with focusable controls has the dial for the system —the
  -- two sides walk the ring and the centre is Enter on the control that has it— and reading `button.down` beside it would
  -- be two pages answering one key. What the dial does to a control, the app hears as `on_click`, `on_open` and
  -- `on_change`, which is the contract of every other app of this tree (docs/30).
  local gesture = touch.gesture()
  while gesture do
    gesture = touch.gesture()
  end

  sys.delay(20):await()
end
