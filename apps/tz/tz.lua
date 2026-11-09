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
-- `time.tz()` writes the setting at once, so the page keeps the choice and calls it from `on_close`. The leap day and
-- the first day of the week are the other way round —a checkbox and a list are settings with an immediate effect on
-- every calendar of the system, and a person who turns one of them is telling the system and not this page— (docs/60).
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
  box.check_x = margin
  box.check_y = y + 92
  box.check_box = 40
  box.select_x, box.select_y = margin, box.check_y + 76
  box.select_w, box.select_h = map_w, 68
  -- The list of the two days is under the field and over the page: two rows of the height of the field.
  box.list_row = 56
  box.list_y = box.select_y + box.select_h
  box.list_w = map_w
end

-- **What the page is holding**, which is not yet what the system keeps: the offset of the hour waits for the close, and
-- the two calendar settings go in as soon as they are touched (docs/60).
local chosen = 0
local leap = true
local week = 1
local list_open = false
local status = ''
local busy = false

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

local function paint_button(x, y, w, h, text, filled)
  if filled then ui.rect(x, y, w, h, 0) end
  ui.frame(x, y, w, h)
  local ink = filled and 15 or 0
  local tw = ui.text_width(text, kBody)
  local th = ui.line_height(kBody)
  ui.text(math.floor(x + (w - tw) / 2), math.floor(y + (h - th) / 2), text, kBody, ink)
end

local function paint_checkbox()
  local size = box.check_box
  ui.frame(box.check_x, box.check_y, size, size)
  if leap then
    -- The cross of the system's checkbox, drawn with two lines: a box with a mark is what a person reads as "on".
    ui.line(box.check_x + 6, box.check_y + 6, box.check_x + size - 6, box.check_y + size - 6)
    ui.line(box.check_x + size - 6, box.check_y + 6, box.check_x + 6, box.check_y + size - 6)
  end
  local th = ui.line_height(kBody)
  ui.text(box.check_x + size + 16, box.check_y + math.floor((size - th) / 2), 'Leap year', kBody, 0)
end

local function paint_select()
  ui.frame(box.select_x, box.select_y, box.select_w, box.select_h)
  local th = ui.line_height(kBody)
  local y = box.select_y + math.floor((box.select_h - th) / 2)
  ui.text(box.select_x + 14, y, 'Week starts', kBody, 8)
  local value = (week == 7) and 'Sunday' or 'Monday'
  local vw = ui.text_width(value, kBody)
  ui.text(box.select_x + box.select_w - vw - 44, y, value, kBody, 0)
  -- The chevron of a field that opens: two lines and a point, which is the only mark a page has for "there is more
  -- under this" (docs/90).
  local cx = box.select_x + box.select_w - 26
  local cy = box.select_y + box.select_h // 2
  ui.line(cx - 8, cy - 3, cx, cy + 5)
  ui.line(cx, cy + 5, cx + 8, cy - 3)

  if list_open then
    for i = 1, 2 do
      local y0 = box.list_y + (i - 1) * box.list_row
      local picked = (i == 1 and week == 1) or (i == 2 and week == 7)
      if picked then ui.rect(box.select_x, y0, box.list_w, box.list_row, 0) end
      ui.frame(box.select_x, y0, box.list_w, box.list_row)
      local name = (i == 1) and 'Monday' or 'Sunday'
      local lh = ui.line_height(kBody)
      ui.text(box.select_x + 14, y0 + math.floor((box.list_row - lh) / 2), name, kBody, picked and 15 or 0)
    end
  end
end

local function paint_status()
  ui.rect(box.sync_x + box.sync_w + 16, box.sync_y + 20, box.map_w - box.sync_w - 16, ui.line_height(kSmall), 15)
  if status == '' then return end
  ui.text(box.sync_x + box.sync_w + 20, box.sync_y + 20, status, kSmall, 0)
end

local function paint_page(chosen_hour)
  ui.clear(15)
  ui.image(box.map_x, box.map_y, '/res/world.png', box.map_w, box.map_h)
  paint_bands(chosen_hour)
  paint_hours()

  local sentence = 'Selected timezone: ' .. tz_text(chosen)
  ui.text(box.map_x, box.label_y, sentence, fit_scale(sentence, box.map_w, kTitle), 0)
  ui.text(box.map_x, box.saved_y, 'Saved when this app closes', kSmall, 8)

  paint_button(box.sync_x, box.sync_y, box.sync_w, box.sync_h, 'Sync NTP', false)
  paint_checkbox()
  paint_select()
  paint_status()
end

local function paint_choice()
  -- **A finger on the map moves two bands and one line**: the bands and the row of the label paint themselves over
  -- their own paper, and the map under them is not touched, because a page that flashes at every hour is a page nobody
  -- drags across (docs/50).
  paint_bands(chosen // kMinutesPerHour)
  paint_hours()
  ui.rect(box.map_x, box.label_y, box.map_w, ui.line_height(kTitle), 15)
  local sentence = 'Selected timezone: ' .. tz_text(chosen)
  ui.text(box.map_x, box.label_y, sentence, fit_scale(sentence, box.map_w, kTitle), 0)
  ui.present(kMoveMode)
end

-- --------------------------------------------------------------------- the app

ui.title('Time zone')

build_layout()

local chosen_hour = 0
chosen = 0
-- **The page reads the system first**: the offset it is showing is the one the board has, and the two calendar
-- settings are the ones every calendar of this board is using (docs/30, docs/60).
chosen = time.tz()
chosen_hour = math.floor(chosen / kMinutesPerHour + (chosen >= 0 and 0.5 or -0.5))
if chosen_hour < kFirstHour then chosen_hour = kFirstHour end
if chosen_hour > kLastHour then chosen_hour = kLastHour end
chosen = chosen_hour * kMinutesPerHour
leap = time.leap()
week = time.week()

-- **The places the page can be touched, said out loud when the app is asked to**: `run tz zones` prints the box of the
-- map and the centre of every hour, so a test presses a zone without knowing the layout (docs/30, docs/85).
if app.args ~= nil and app.args.path == 'zones' then
  console.print(string.format('tz map %d %d %d %d', box.map_x, box.map_y, box.map_w, box.map_h))
  for hour = kFirstHour, kLastHour do
    console.print(string.format('tz hour %d %d %d', hour, math.floor(x_of_hour(hour) + 0.5),
      box.map_y + box.map_h // 2))
  end
  console.print(string.format('tz sync %d %d %d %d', box.sync_x, box.sync_y, box.sync_w, box.sync_h))
  console.print(string.format('tz leap %d %d %d', box.check_x + box.check_box // 2,
    box.check_y + box.check_box // 2, box.check_box))
  console.print(string.format('tz week %d %d %d %d', box.select_x, box.select_y, box.select_w, box.select_h))
  console.print(string.format('tz list %d %d %d %d', box.select_x, box.list_y, box.list_w, box.list_row))
end

paint_page(chosen_hour)
ui.present(kPageMode)

-- **Saving is the close and nothing else**: `time.tz()` writes the setting at once, so this page calls it when the
-- system is taking the app away —three taps of the centre, the pinch, `exit`— and a person who leaves without touching
-- anything leaves the board as it was (docs/30, docs/60).
function on_close()
  if chosen ~= time.tz() then
    time.tz(chosen)
    console.print('tz: saved ' .. tz_text(chosen))
  end
end

-- **The synchronisation is a promise and the page waits for it**: `net.ntp` is a request of the kernel's queue and this
-- page has nothing else to do while it waits, so the finger that asked sees the answer when it comes (docs/30).
local function sync_clock()
  if busy then return end
  busy = true
  status = 'syncing...'
  paint_status()
  ui.present(kMoveMode)

  local ok = false
  if caps.net_wifi then
    ok = net.ntp():await()
  end
  busy = false

  if ok then
    -- **An hour that came from a time server turns the leap day on**: the calendar of the world has the twenty-ninth
    -- of February, and a board that has just been told what day it is does not need to be told that too (the owner's
    -- rule, 9/11, docs/60).
    if not leap then
      leap = time.leap(true)
    end
    local now = time.date()
    status = string.format('synced %04d-%02d-%02d %02d:%02d', now.year, now.month, now.day, now.hour, now.min)
    console.print('tz: ' .. status)
  else
    status = 'no clock: the radio is off'
    console.print('tz: ' .. status)
  end

  paint_page(chosen_hour)
  ui.present(kPageMode)
end

-- The keys are the dial of the board: the two sides walk the hours and the centre syncs, which is the same three
-- things a finger does with more precision (docs/30).
local spots = {}
for _, key in ipairs(button.list()) do
  spots[key.key] = key.spot
end
local down = {}

local function press(key)
  local spot = spots[key]
  if spot == nil then return end

  if spot == 'left' or spot == 'up' then
    if chosen_hour > kFirstHour then chosen_hour = chosen_hour - 1 end
  elseif spot == 'right' or spot == 'down' then
    if chosen_hour < kLastHour then chosen_hour = chosen_hour + 1 end
  elseif spot ~= 'centre' then
    return
  else
    return sync_clock()
  end

  chosen = chosen_hour * kMinutesPerHour
  paint_choice()
end

local function tap(x, y)
  -- An open list takes the page: a finger that is not on a row closes it and does nothing else, which is what a list
  -- that opens over a page means anywhere (docs/30).
  if list_open then
    list_open = false
    if x >= box.select_x and x < box.select_x + box.list_w then
      if y >= box.list_y and y < box.list_y + box.list_row then
        week = time.week(1)
        console.print('tz: week monday')
      elseif y >= box.list_y + box.list_row and y < box.list_y + 2 * box.list_row then
        week = time.week(7)
        console.print('tz: week sunday')
      end
    end
    paint_page(chosen_hour)
    ui.present(kMoveMode)
    return
  end

  if x >= box.map_x and x < box.map_x + box.map_w and y >= box.top_y and y < box.map_y + box.map_h + box.band_h + 6 then
    chosen_hour = hour_at(x)
    chosen = chosen_hour * kMinutesPerHour
    console.print('tz: picked ' .. tz_text(chosen))
    paint_choice()
    return
  end

  if x >= box.sync_x and x < box.sync_x + box.sync_w and y >= box.sync_y and y < box.sync_y + box.sync_h then
    sync_clock()
    return
  end

  if x >= box.check_x and x < box.check_x + box.select_w and y >= box.check_y and
      y < box.check_y + box.check_box then
    leap = time.leap(not leap)
    console.print('tz: leap ' .. (leap and 'on' or 'off'))
    paint_page(chosen_hour)
    ui.present(kMoveMode)
    return
  end

  if x >= box.select_x and x < box.select_x + box.select_w and y >= box.select_y and y < box.select_y + box.select_h then
    list_open = true
    paint_page(chosen_hour)
    ui.present(kMoveMode)
  end
end

-- The loop of the page: a finger on the first frame it is seen, and the system's gestures taken and dropped so that a
-- queue nobody empties does not grow (the same rule the calculator follows, docs/30).
local touched = false

while true do
  local view = ui.caps()
  if view.width ~= page_w or view.height ~= page_h then
    build_layout()
    paint_page(chosen_hour)
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

  for key in pairs(spots) do
    local is_down = button.down(key)
    if is_down and not down[key] then press(key) end
    down[key] = is_down
  end

  local gesture = touch.gesture()
  while gesture do
    gesture = touch.gesture()
  end

  sys.delay(20):await()
end
