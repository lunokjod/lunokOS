-- timeclock: **a time clock —three keys, the day under them, and the week behind an eye** (the port of the paper
-- project's Timeclock, 0.125.0).
--
--   THE THREE KEYS  play opens the day, pause closes a stretch of work and opens one of rest (and while the day rests
--                   the same key is a play and takes the work back), and stop closes whatever is open.
--   THE LIST        the marks of the day, oldest first; a finger marks a row and **the box under the list is the note of
--                   that row**, written to the file at once.
--   THE TWO NUMBERS work and rest, from the marks and the chip's clock, so a stretch that spans a switch-off or a flat
--                   battery is still the length it really was.
--   THE WEEK        five bars from Monday to Friday, the work in ink and the rests in grey, the hours of each day under
--                   its bar and the average in the line of the top bar.
--
-- **One file and not three.** The original was `pos_launch.lua` + `day.lua` + `week.lua`, run with `files.load()`.
-- `dofile`, `loadfile` and `load` are removed from an app's state here on purpose (docs/20), so an app cannot execute a
-- file: the data, the drawing of the chart and the page are below in the order they are needed, and each part says
-- which of the original's files it is (docs/45).
--
-- **Where the marks live is the system's ground and not the app's choice**: `fs.data()` is `/data/timeclock/` on the
-- unit the system picked (docs/70). The format is the original's and the file only ever grows —a mark is one append of
-- twenty-four bytes— so the day is read as the **tail** of the file and the app never loads a year to show an
-- afternoon:
--
--      2026-11-02|09:00:00|S                    S start, P pause, R resume, E end, one mark a line
--      2026-11-02|09:00:00|what it was           the notes, a second file, because a note is edited and an edit
--                                                is a rewrite
--
-- **The three keys are marks and not words**, which is what the owner asked for in the original (27/10, *«USA ICONES
-- PER ESTALVIAR ESPAI»*): a play, two bars, a square, the eye that opens the week and the arrow that comes back are
-- five glyphs of `res/keys.ttf`, a face of the app's own folder that `tools/make_keys.py` takes out of the same
-- `FontAwesome.otf` the original's `keys.pof` was rasterised from.
--
-- **And they are the codes 1 to 5 and not the private use area of `U+F04B`**, because a text here is **one byte a
-- character** and a code point of that area is three bytes of UTF-8: the room no text of a page means is the codes
-- below space in this system, and the face is the one that says what a code draws (docs/30). **When the face is not
-- there the keys say the words** —`Start`, `Pause`/`Resume`, `End`, `Week`/`Day`—, the fallback the original itself
-- had, because half an app is worse than one that needs reading (docs/45).
--
-- **The mark is the `title` of the button and not a label laid on it**, which is the original's `set_text()`: a button
-- paints its own title with the ink of the state it is in, so a key that the focus or a finger turns black says its
-- figure in paper white instead of showing a hole (docs/30).
--
-- **The hours are the chip's and the day is the person's**: `time.now()` is the epoch the coin cell keeps, so a stretch
-- that crosses a reboot still measures what it measured, and `time.date()` says what that epoch is in the hour of the
-- person, which is the offset `cfg tz +01:00` keeps (docs/30, docs/60). If the clock was never set the three keys are
-- **dead** and the line above them says why: a mark at 00:00 would be a truth with the face of a lie.
--
-- **The work day goes from 07:00 to 19:00** and every interval is cut to that window before it is added: a day opened
-- at 06:00 and closed at 21:00 with an hour of rest is eleven hours of work and one of rest. **The marks are not
-- touched** —what the clock said is what happened—, only the sum is cut.
--
-- **Nothing is cleaned at midnight: it is cleaned on the next play.** The day on the glass is the **last day that has
-- marks** in the file, so the night passes and the day stays where it was; what changes is which key can be pressed. The
-- play key always opens today: it writes today's first mark and the list is left with one row.

local caps = sys.caps()
if not caps.display then
  console.print('timeclock: this board has no glass')
  return
end

local view = ui.caps()

-- A step of the face is eleven pixels of line with the bold face of the disk and seven with the table of the flash, so
-- the sizes of the original are asked in pixels and the face answers with the steps it has (docs/45).
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
local kTitle = scale_for(57)

local function line(scale)
  return scale * ui.line_height(1)
end

-- **The face of the marks**, read before any control exists because a control keeps the face it was given when it was
-- built: `res/keys.ttf` is five shapes —a play, a pause, a stop, an eye and an arrow back— and no letter at all, so it
-- is the mark of a key and not the text of the page (docs/30).
local keys_face, keys_why = ui.load_font('/res/keys.ttf')
local icons = (keys_face ~= nil)

if not icons then
  console.print('timeclock: /res/keys.ttf: ' .. tostring(keys_why) .. ': the keys say words')
end

-- **The five marks, one byte each**, and the byte is the code the face answers to and not a letter of any alphabet:
-- the file says which of them is a picture, so a key cannot show the wrong figure (docs/30).
local kPlay, kPauseMark, kStopMark = '\1', '\2', '\3'
local kEyeMark, kBackMark = '\4', '\5'

-- **The size of a mark**: the figure of the original is 41 px in a key of 84, and five steps of a body of nine are
-- 45 px of em, which is the 39 px of the play triangle and the 29 of the eye, measured on the file (docs/45).
local kMarkScale = 5

-- The padding and the gaps of the original, and the height of a key: the line of the body plus twice the vertical
-- padding of a button of the paper project (`lineHeight() + 2 * padY`), which is 63 px there and 64 here (docs/45).
local kMargin = 20
local kGap = 10
local kKeyGap = 16
local kKeyH = 84
local kRow = line(kBody) + 20
local kNoteH = 63
local kHintH = line(kSmall) + 6
local kKeyW = math.floor((view.width - 2 * kMargin - 2 * kKeyGap) / 3)
-- **The eye is a key of the top row**: 78 px wide with the face —the mark is forty-five and a button keeps sixteen of
-- padding on each side of its title—, and as wide as the word `Week` when the face is not there and the key has to say
-- it (docs/45).
local kEyeH = 63
local kEyeW = icons and 78 or 96

-- ============================================================================ the data (the original's `day.lua`)

local kFile = 'marks.txt'
local kNotes = 'notes.txt'
-- How much of the marks file is read to show one day: a shift is six marks of twenty-four bytes, and a day with more
-- marks than eight kilobytes is a day nobody worked.
local kTailBytes = 8192
local kNoteMax = 60

-- The two tables the data functions work on, declared before them for the reason a name is visible where it is written.
local marks, notes = {}, {}

local kStart, kPause, kResume, kEnd = 'S', 'P', 'R', 'E'
local kKindName = {S = 'Start', P = 'Pause', R = 'Resume', E = 'End'}

-- **The work day, in seconds past midnight**: in at 07:00 at the earliest and out at 19:00 at the latest, and these two
-- numbers are the only place it lives.
local kWorkFrom = 7 * 3600
local kWorkUntil = 19 * 3600

local function clip(text, max)
  text = tostring(text or '')
  if max < 4 or #text <= max then return text end
  return text:sub(1, max - 3) .. '...'
end

-- A text cut to what a label of this width holds, measured with the same call the label draws with: a text cut in
-- silence is a text that lies about what is in the file (docs/30).
local function clip_to(text, width, scale)
  text = tostring(text or '')
  if ui.text_width(text, scale) <= width then return text end

  local cut = text
  while #cut > 1 and ui.text_width(cut .. '...', scale) > width do
    cut = cut:sub(1, #cut - 1)
  end

  return cut .. '...'
end

-- A note as the file can hold it: one line, with no wall inside. A `|` is the wall between the fields and a note with
-- one inside cannot be told from a note that ends there, so it becomes a slash instead of being refused.
local function clean_note(text)
  text = tostring(text or '')
  text = text:gsub('|', '/'):gsub('[\r\n\t]', ' ')
  if #text > kNoteMax then text = text:sub(1, kNoteMax) end
  return text
end

local function sec_of(m)
  return m.h * 3600 + m.mi * 60 + m.s
end

local function stamp_of(m)
  return string.format('%04d-%02d-%02d|%02d:%02d:%02d', m.y, m.mo, m.d, m.h, m.mi, m.s)
end

local function key_of(t)
  return string.format('%04d-%02d-%02d', t.year, t.month, t.day)
end

local function parts_of(key)
  local y, mo, d = tostring(key or ''):match('^(%d%d%d%d)%-(%d%d)%-(%d%d)$')
  if not y then return nil end
  return tonumber(y), tonumber(mo), tonumber(d)
end

local function parse_line(text)
  local y, mo, d, h, mi, s, k =
    text:match('^(%d%d%d%d)%-(%d%d)%-(%d%d)|(%d%d):(%d%d):(%d%d)|([SPRE])%s*$')
  if not y then return nil end

  y, mo, d = tonumber(y), tonumber(mo), tonumber(d)
  return {key = string.format('%04d-%02d-%02d', y, mo, d), y = y, mo = mo, d = d,
          h = tonumber(h), mi = tonumber(mi), s = tonumber(s), kind = k}
end

-- **A length as a person reads it**: `5h 42m`, and never `5.7h`.
local function duration(sec)
  if not sec or sec < 0 then sec = 0 end
  local mins = math.floor(sec / 60)
  return string.format('%dh %02dm', math.floor(mins / 60), mins % 60)
end

local function work_text(sec) return 'Work ' .. duration(sec) end
local function rest_text(sec) return 'Rest ' .. duration(sec) end

-- The time since the last mark, and **an empty line when there is nothing to count from**: a number counted from a mark
-- of three days ago is not "the time since", it is the age of the file.
local function since_text(now_sec, m)
  if not m or not now_sec then return '' end
  local d = now_sec - sec_of(m)
  if d < 0 then d = 0 end
  return string.format('%s since %02d:%02d', duration(d), m.h, m.mi)
end

-- The tail of the marks file, which is all of it this app ever reads, **and its first line is thrown away** when the
-- read did not start at byte zero: half a line parses as a line that is not there, and that is one mark more or less.
local function tail()
  local path = fs.data(kFile)
  if path == nil then return nil, 'this app has no ground on this board' end

  local info = fs.stat(path)
  if info == nil then return '' end             -- no file yet is a day that has not started
  if info.type ~= 'file' then return nil, kFile .. ' is not a file' end

  local size = info.size or 0
  if size <= 0 then return '' end

  local from = 0
  if size > kTailBytes then from = size - kTailBytes end

  local all = fs.read_at(path, from, size - from)
  if all == nil then return nil, 'could not read ' .. kFile end

  if from > 0 then
    local cut = all:find('\n', 1, true)
    all = cut and all:sub(cut + 1) or ''
  end

  return all
end

-- The marks of one day, oldest first, and how many lines could not be read.
local function load_day(key)
  local all, why = tail()
  if not all then return nil, why, 0 end

  local marks, bad = {}, 0
  for text in (all .. '\n'):gmatch('([^\n]*)') do
    if text ~= '' then
      local m = parse_line(text)
      if not m then
        bad = bad + 1
      elseif m.key == key then
        marks[#marks + 1] = m
      end
    end
  end

  return marks, nil, bad
end

-- **The last day that has something in the file**: the date of the last line and not "the last day before today",
-- because a clock put back can leave a line from the future and that line is a mark somebody wrote.
local function last_day()
  local all, why = tail()
  if not all then return nil, why end

  local last = nil
  for text in (all .. '\n'):gmatch('([^\n]*)') do
    if text ~= '' then
      local m = parse_line(text)
      if m then last = m.key end
    end
  end

  return last
end

-- A mark added to the file: one append and the file is never rewritten.
local function append_mark(m)
  local path = fs.data(kFile)
  if path == nil then return false, 'this app has no ground on this board' end

  local ground = fs.data()
  if ground ~= nil then pcall(fs.mkdir, ground) end

  local file, why = fs.open(path, 'a')
  if file == nil then return false, why or 'the file did not open' end

  local wrote = file:write(stamp_of(m) .. '|' .. m.kind .. '\n')
  file:close()

  if wrote == nil then return false, 'the mark did not reach the disk' end
  return true
end

-- The notes of the file, a list of `{stamp, note}` and not a table keyed by the stamp: the whole file is written back
-- on an edit and a table walked with `pairs` has no order to write.
local function load_notes()
  local path = fs.data(kNotes)
  if path == nil then return {}, 'this app has no ground on this board' end

  local info = fs.stat(path)
  if info == nil or info.type ~= 'file' then return {} end

  local all = fs.read(path)
  if all == nil then return {}, 'could not read ' .. kNotes end

  local notes = {}
  for text in (all .. '\n'):gmatch('([^\n]*)') do
    if text ~= '' then
      local stamp, note = text:match('^(%d%d%d%d%-%d%d%-%d%d|%d%d:%d%d:%d%d)|(.*)$')
      if stamp then notes[#notes + 1] = {stamp = stamp, note = note} end
    end
  end

  return notes
end

local function save_notes()
  local path = fs.data(kNotes)
  if path == nil then return false, 'this app has no ground on this board' end

  local ground = fs.data()
  if ground ~= nil then pcall(fs.mkdir, ground) end

  local parts = {}
  for i = 1, #notes do parts[i] = notes[i].stamp .. '|' .. notes[i].note end

  local text = table.concat(parts, '\n')
  if text ~= '' then text = text .. '\n' end

  local wrote = fs.write(path, text)
  if wrote == nil then return false, 'the note did not reach the disk' end
  return true
end

local function index_of_note(stamp)
  for i = 1, #notes do
    if notes[i].stamp == stamp then return i end
  end
  return nil
end

local function note_of(m)
  local i = index_of_note(stamp_of(m))
  return i and notes[i].note or ''
end

-- Writes a note into the list: an empty note takes the line away, because a line with nothing on it is a line nobody
-- wrote.
local function set_note(m, text)
  local stamp = stamp_of(m)
  local note = clean_note(text)
  local i = index_of_note(stamp)

  if note == '' then
    if i then table.remove(notes, i) end
    return
  end

  if i then
    notes[i].note = note
  else
    notes[#notes + 1] = {stamp = stamp, note = note}
  end
end

-- The state of the day at its last mark: none, working, paused or closed. **It is read from the marks and not kept as a
-- switch**, because a switch is a second truth and an app killed with the battery flat has to come back saying what its
-- marks say.
local function state_of(list)
  local state = 'none'
  for i = 1, #list do
    local k = list[i].kind
    if k == kStart or k == kResume then
      state = 'working'
    elseif k == kPause then
      state = 'paused'
    elseif k == kEnd then
      state = 'closed'
    end
  end
  return state
end

local function can_start(state) return state == 'none' or state == 'closed' end
local function can_pause(state) return state == 'working' or state == 'paused' end
local function can_end(state) return state == 'working' or state == 'paused' end

-- How much of an interval falls inside the work day, and nothing of it outside.
local function inside_work_day(a, b)
  if b <= a then return 0 end
  if a < kWorkFrom then a = kWorkFrom end
  if b > kWorkUntil then b = kWorkUntil end
  if b <= a then return 0 end
  return b - a
end

-- The worked and the rested seconds of a day, up to `now_sec`. An interval is closed by the mark after it, and what is
-- still open is counted up to now —which is why the screen adds a minute by itself while somebody is working—. A length
-- that comes out negative is zero: the clock can be put back by hand.
local function totals(list, now_sec)
  local work, rest = 0, 0
  local from, what = nil, nil

  local function close_at(at)
    if not from then return end
    local d = inside_work_day(from, at)
    if what == 'work' then work = work + d else rest = rest + d end
    from = nil
  end

  for i = 1, #list do
    local m = list[i]
    local at = sec_of(m)
    if m.kind == kStart or m.kind == kResume then
      close_at(at)
      from, what = at, 'work'
    elseif m.kind == kPause then
      close_at(at)
      from, what = at, 'rest'
    else
      close_at(at)
    end
  end

  if from and now_sec and now_sec > from then close_at(now_sec) end

  return work, rest
end

local function row_of(m, note)
  local text = string.format('%02d:%02d %s', m.h, m.mi, kKindName[m.kind] or '?')
  if note and note ~= '' then text = text .. '  ' .. note end
  return text
end

-- =========================================================================== the week (the original's `day.lua` tail)

local kDayNames = {'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'}

-- **The five working days of the week of a date**, Monday to Friday and nothing else. **The average is of the days
-- that have marks** and not of five: a Monday with no mark is a day this app knows nothing about, and counting it as a
-- zero would make the average of somebody who started on Wednesday look like half of what it is.
local function week_of(key, today_key, now_sec)
  -- **The day of a key is found by walking from today and not by a formula of the calendar**: a second copy of the
  -- leap rule is a second answer, and this system keeps it once. A file with no mark in the last year is not a week
  -- anybody is looking at, and the walk stops there.
  local now = time.now()
  if now == nil or now <= 0 then return nil, 'the clock does not know what day it is' end

  local found = nil
  for step = 0, 366 do
    local here = time.date(now - step * 86400)
    if here ~= nil and key_of(here) == key then
      found = -step
      break
    end
    if step > 0 then
      local ahead = time.date(now + step * 86400)
      if ahead ~= nil and key_of(ahead) == key then
        found = step
        break
      end
    end
  end

  if found == nil then return nil, 'that week is not in the last year of this clock' end

  local day = time.date(now + found * 86400)
  if day == nil or day.wday == nil or day.wday < 1 then return nil, 'the clock does not know what day it is' end

  local monday = found - (day.wday - 1)

  local by_day = {}
  local all = tail()
  if all == nil then return nil, 'could not read ' .. kFile end

  for text in (all .. '\n'):gmatch('([^\n]*)') do
    if text ~= '' then
      local m = parse_line(text)
      if m then
        local list = by_day[m.key]
        if list == nil then
          list = {}
          by_day[m.key] = list
        end
        list[#list + 1] = m
      end
    end
  end

  local rows, sum, count = {}, 0, 0

  for i = 0, 4 do
    local at = time.date(now + (monday + i) * 86400)
    if at == nil then return nil, 'that week is not a date this clock can have' end

    local day_key = key_of(at)
    local list = by_day[day_key] or {}

    -- **Today's open stretch counts and a past day's does not**: a day that was never closed says "still open" on its
    -- own screen, and counting it up to now would invent the hours of every night since.
    local up_to = (day_key == today_key) and now_sec or nil
    local work, rest = totals(list, up_to)

    rows[#rows + 1] = {key = day_key, name = kDayNames[at.wday] or '?', work = work, rest = rest,
                       total = work + rest, marks = #list}

    if #list > 0 then
      sum = sum + work
      count = count + 1
    end
  end

  local function short(day_key)
    local _, mo, d = parts_of(day_key)
    return string.format('%02d/%02d', mo or 0, d or 0)
  end

  return {rows = rows, count = count, average = (count > 0) and math.floor(sum / count) or 0,
          label = short(rows[1].key) .. '-' .. short(rows[5].key)}
end

-- ================================================================================ the week on one sheet (`week.lua`)

-- **A drawing and not five controls**: a chart does not take the focus, does not answer a key and is not touched. The
-- work is ink (0), the rests grey (8), the grid 11 and the paper 15 (docs/45).
local kWorkInk, kRestInk, kGridInk = 0, 8, 11
local kGutter, kUnder, kLegend, kPad = 56, 66, 34, 6

local function paint_week(cv, w, h, week)
  local rows = (week and week.rows) or {}
  if #rows == 0 then return end

  local px = kGutter
  local py = kPad
  local pw = w - kGutter - kPad
  local ph = h - kUnder - kLegend - kPad

  -- **A page with no room for a picture is a page with no picture**, and not a chart squeezed into a line.
  if pw < 80 or ph < 60 then return end

  -- The top of the scale is the tallest day rounded up to half an hour, and never less than four hours: a bar that
  -- fills the whole page for a shift of one hour says "a long day" to anybody who looks at it.
  local top = 0
  for i = 1, #rows do
    if rows[i].total > top then top = rows[i].total end
  end
  top = math.max(4 * 3600, math.ceil(top / 1800) * 1800)

  -- The grid: zero, half and the top, with their hours on the left.
  for k = 0, 2 do
    local at = py + ph - math.floor(ph * k / 2)
    local text = string.format('%dh', math.floor(top * k / 2 / 3600))
    cv:line(px, at, px + pw, at, kGridInk)
    -- **The hour sits over its line and the one of the top line sits under it**: a label above the first grid line
    -- would be painted outside the canvas, where nothing is drawn.
    local ty = at - line(kSmall)
    if ty < 0 then ty = at end
    cv:text(px - 8 - ui.text_width(text, kSmall), ty, text, kSmall)
  end

  -- The bars, one slot each, the work at the bottom and the rest on top of it: a day is read as one column and where
  -- the ink ends inside it is where the work ended.
  local slot = pw / #rows
  local bw = math.floor(slot * 0.56)
  if bw < 10 then bw = 10 end

  for i = 1, #rows do
    local row = rows[i]
    local cx = px + slot * (i - 1) + slot / 2
    local left = math.floor(cx - bw / 2)
    local total_h = math.floor(row.total / top * ph)
    local work_h = math.floor(row.work / top * ph)

    if work_h > 0 then cv:rect(left, py + ph - work_h, bw, work_h, kWorkInk) end
    if total_h > work_h then cv:rect(left, py + ph - total_h, bw, total_h - work_h, kRestInk) end

    cv:text(math.floor(cx - ui.text_width(row.name, kSmall) / 2), py + ph + 4, row.name, kSmall)

    local text = string.format('%dh%02d', math.floor(row.total / 3600), math.floor((row.total % 3600) / 60))
    if ui.text_width(duration(row.total), kSmall) <= slot - 6 then text = duration(row.total) end
    cv:text(math.floor(cx - ui.text_width(text, kSmall) / 2), py + ph + 4 + line(kSmall) + 4, text, kSmall)
  end

  -- A week with no marks says so above the axis: five empty columns read as "there is nothing here", which is true, but
  -- the line says it in words.
  if week.count == 0 then
    local text = 'No marks this week'
    cv:text(math.floor(px + (pw - ui.text_width(text, kSmall)) / 2), py + math.floor(ph / 2), text, kSmall)
  end

  -- The legend: the two inks have to be named, or the grey is a mystery.
  local ly = h - kLegend + 2
  cv:rect(2, ly + 8, 16, 16, kWorkInk)
  cv:text(24, ly, 'Work', kSmall)
  cv:rect(92, ly + 8, 16, 16, kRestInk)
  cv:text(114, ly, 'Rest', kSmall)
end

-- ============================================================================== what survives and what is on the glass

local page = 1              -- 1 the day, 2 the week
local marked = 0            -- the mark the note box is writing, counted from one (0 = none)
local shown_day = ''        -- the day on the glass, the last one that has marks
local day_of_marks = ''     -- the day the marks in memory were read for
local today_shown = ''      -- the day of the clock at the last refresh: the tick compares this and not the clock
local spoiled = ''
local totals_shown = ''
local edge_shown = ''
local week_data = nil
local rows_built = {}       -- the texts the list is showing, so a repaint costs a comparison

local status_label, eye_btn, since_label, work_label, rest_label
local hint_label, note_box, start_btn, pause_btn, end_btn
local list, week_canvas, back_btn
local screen = {}           -- every control of the page that is on the glass, given back on a turn
local loading = false

-- **The page's own numbers and the two functions that build it**: they are named here because the closures below —the
-- row of a list, a key— call them, and a name that is a local of the page would be invisible to a function written
-- before it.
local refresh, turn, build_day, build_week
-- **Where the controls are**, filled when the page is built: `run timeclock keys` prints it so that a test puts a
-- finger on a key without knowing the layout, and a page that moves the keys cannot move the test's finger onto a
-- different one without the numbers saying so (docs/30, docs/85).
local spots = {}
local list_y, list_height, max_rows = 0, 0, 0

local function give_back()
  for i = 1, #screen do
    if screen[i] ~= nil then pcall(function() screen[i]:release() end) end
    screen[i] = nil
  end
  screen = {}
  list, week_canvas, back_btn = nil, nil, nil
  status_label, eye_btn, since_label, work_label, rest_label = nil, nil, nil, nil, nil
  hint_label, note_box, start_btn, pause_btn, end_btn = nil, nil, nil, nil, nil
end

local function make_label(width, height, text, scale, align, valign)
  local label = ui.label(width, height)
  if label == nil then return nil end
  label.text = text
  label.scale = scale
  label.align = align or 'left'
  label.valign = valign or 'middle'
  label.color = 0
  label.background = 15
  return label
end

-- **What one key says**, when the same key says two things: the middle one is a pause while the day works and a play
-- while it rests, and the eye is an eye on the day and an arrow back on the week. The mark is the shape and the word is
-- what is left when the face is not on the disk, and the two need different sizes (docs/45).
local function key_cell(button, mark, word, word_scale)
  if button == nil then return end

  if icons then
    button.title = mark
    button.scale = kMarkScale
  else
    button.title = word
    button.scale = word_scale or kBody
  end
end

-- A key of the row, and **its mark is the title of the button and not a label laid on it**, which is what the
-- original did (`set_text()` of a button of the paper project): a button paints its own title with the ink of the
-- state it is in —black on paper, and the paper's white when the focus or a finger turns the face black— so a key
-- that is walked to and pressed says the same figure at rest and while it is held. A label of its own would be
-- another control, with the colours of the app, that the button cannot turn over (docs/30, docs/45).
local function make_key(width, height, mark, word)
  local button = ui.button(width, height)
  if button == nil then return nil end

  button.background = 15
  button.border = 0
  button.color = 0
  if icons then button:font(keys_face) end
  key_cell(button, mark, word)

  return button
end

-- =================================================================================== the clock and the day, read again

-- The chip's clock, in its parts, or nil and the reason.
local function now_parts()
  local now = time.now()
  if now == nil or now <= 0 then return nil, 'the clock is not set' end

  local t = time.date(now)
  if t == nil then return nil, 'the clock does not know what day it is' end
  return t
end

local function day_text(t)
  return string.format('%04d-%02d-%02d', t.year, t.month, t.day)
end

-- The two names a day has on the screen. **A day that is not today cannot be "working since 09:12"**: a stretch that was
-- never closed is a day somebody forgot to end, and the line says exactly that.
local function status_text(state, last, is_today)
  if state == 'none' then return 'Not started today' end

  local at = string.format('%02d:%02d', last.h, last.mi)

  if state == 'working' then
    return (is_today and 'Working since ' or 'Left open at ') .. at
  end

  if state == 'paused' then
    if is_today then return 'Paused since ' .. at end
    return 'Left paused at ' .. at
  end

  return 'Day closed at ' .. at
end

-- **The day's marks and the day, read again from the file.** The day that is read is the last one that has marks and not
-- today, and that is what keeps the screen on the day that was being worked on until the play key opens a new one.
local function reload()
  local t, why = now_parts()
  if not t then
    marks, notes = {}, {}
    spoiled = tostring(why)
    shown_day, day_of_marks = '', ''
    return nil
  end

  local last, last_why = last_day()
  if last == nil and type(last_why) == 'string' then spoiled = last_why end

  shown_day = last or day_text(t)

  local read, read_why, bad = load_day(shown_day)
  if read == nil then
    marks, notes = {}, {}
    day_of_marks = ''
    spoiled = 'Could not read ' .. kFile .. ': ' .. tostring(read_why)
    return nil
  end

  marks = read
  if bad and bad > 0 then
    spoiled = string.format('%d line(s) of %s could not be read', bad, kFile)
  else
    spoiled = ''
  end

  local said, notes_why = load_notes()
  notes = said or {}
  if notes_why then spoiled = 'Could not read the notes: ' .. tostring(notes_why) end

  day_of_marks = shown_day
  return t
end

-- The edge of the work day, in words, and the empty text when there is no edge to name: the hours only grow between
-- 07:00 and 19:00, and a number that has stopped without a word about why is a screen that looks broken.
local function outside_work_day(state, is_today, now_sec)
  if not is_today or not now_sec then return '' end
  if state ~= 'working' and state ~= 'paused' then return '' end

  local edge, verb
  if now_sec < kWorkFrom then
    edge, verb = kWorkFrom, 'starts'
  elseif now_sec >= kWorkUntil then
    edge, verb = kWorkUntil, 'ends'
  else
    return ''
  end

  return string.format('The work day %s at %02d:%02d', verb, math.floor(edge / 3600),
                       math.floor(edge % 3600 / 60))
end

local function is_today()
  local t = now_parts()
  return t ~= nil and day_text(t) == shown_day
end

local function load_week()
  local y, mo, d = parts_of(shown_day)
  week_data = nil
  if not y then return end

  local t = now_parts()
  local now_sec = t and (t.hour * 3600 + t.min * 60 + t.sec) or nil
  local today_key = t and day_text(t) or nil

  local data, why = week_of(shown_day, today_key, now_sec)
  if not data then
    spoiled = 'Could not read the week: ' .. tostring(why)
    return
  end

  week_data = data
end

-- ============================================================================== writing everything the screen says

local function rows_of_day()
  local rows = {}
  for i = 1, #marks do rows[i] = row_of(marks[i], note_of(marks[i])) end
  return rows
end

-- The list of marks, built again when its rows are not the ones it is showing. A row is a control of this app and not a
-- line of data, because a finger marks the row it touched and a list of data does not say which row that was (docs/30).
local function rebuild_list(width, height, first_row, last_row)
  local built = {}
  for i = first_row, last_row do built[#built + 1] = row_of(marks[i], note_of(marks[i])) end

  local same = (#built == #rows_built)
  if same then
    for i = 1, #built do
      if built[i] ~= rows_built[i] then same = false break end
    end
  end
  if same then return end

  local old = list
  local fresh = ui.list(width, height)
  if fresh == nil then return end

  fresh.row_height = kRow
  fresh.separator = true
  fresh.scale = kBody
  fresh.background = 15
  fresh.text = 0
  fresh.dim = 8

  for i = first_row, last_row do
    local entry = ui.list_entry(width, kRow)
    if entry ~= nil then
      entry.background = 15
      entry.border = 0
      entry.separator = true
      entry.marked = (i == marked)

      local words = make_label(width - 24, kRow - 8, clip_to(row_of(marks[i], note_of(marks[i])), width - 24, kBody),
                               kBody, 'left', 'middle')
      if words ~= nil then
        words:place(12, 4)
        entry:add(words)
      end

      local at = i
      entry.on_click = function()
        marked = at
        refresh()
        ui.present('gld16')
      end

      fresh:add(entry)
    end
  end

  fresh:place(kMargin, list_y)
  list = fresh
  rows_built = built

  if old ~= nil then pcall(function() old:release() end) end
end

-- **Everything the screen says, written again, in one place**: the state, the two numbers, the rows, the box and the
-- three keys are views of the same fact, and an app that writes them from five places contradicts itself after the
-- second mark.
refresh = function(t)
  local why
  if not t then t, why = now_parts() end

  -- The day on the glass is always the day of the marks in memory: every path that paints comes through here, so a day
  -- that has been read cannot be painted with another day's marks.
  if t and day_of_marks ~= shown_day then t = reload() or t end

  -- A file with nothing in it has no last day, so the sheet it shows is today.
  if t and #marks == 0 and shown_day ~= day_text(t) then
    shown_day, day_of_marks = day_text(t), day_text(t)
  end

  if page == 2 then
    if status_label ~= nil then
      local line_text = 'no marks this week'
      if week_data == nil then
        line_text = 'no week to show'
      elseif week_data.count > 0 then
        line_text = string.format('avg %s in %d day(s)', duration(week_data.average), week_data.count)
      end
      status_label.text = line_text
      key_cell(eye_btn, kBackMark, 'Day', kSmall)
      ui.title(week_data and ('Week ' .. week_data.label) or 'Week')
      if week_canvas ~= nil then week_canvas:invalidate() end
    end
    return
  end

  if status_label == nil then return end

  if not t then
    -- **No clock, no time clock**: the three keys are dead and the screen says why, because an hour recorded from a
    -- clock that does not know the date is a lie that looks exactly like a truth.
    ui.title('Timeclock')
    status_label.text = clip(tostring(why), 36)
    since_label.text = ''
    work_label.text = 'Work --'
    rest_label.text = 'Rest --'
    hint_label.text = 'Set the clock first: cfg tz and a time source'

    start_btn.enabled = false
    pause_btn.enabled = false
    end_btn.enabled = false
    key_cell(pause_btn, kPauseMark, 'Pause')

    loading = true
    note_box.text = ''
    loading = false
    note_box.enabled = false

    rebuild_list(view.width - 2 * kMargin, list_height, 1, 0)
    totals_shown = 'Work --|Rest --'
    return
  end

  local today = day_text(t)
  local today_is = (shown_day == today)
  today_shown = today

  local state = state_of(marks)
  local last = marks[#marks]

  local now_sec = today_is and (t.hour * 3600 + t.min * 60 + t.sec) or nil
  local work, rest = totals(marks, now_sec)

  local wtext, rtext = work_text(work), rest_text(rest)
  local since = since_text(now_sec, last)
  totals_shown = wtext .. '|' .. rtext .. '|' .. since

  work_label.text = wtext
  rest_label.text = rtext
  since_label.text = since

  status_label.text = clip(status_text(state, last or {h = 0, mi = 0}, today_is), 36)
  ui.title(shown_day ~= '' and shown_day or 'Timeclock')

  -- The rows: the last ones that fit, and the ones that did not are counted and said.
  local n = #marks
  local first_row = 1
  if n > max_rows then first_row = n - max_rows + 1 end

  if marked < first_row or marked > n then marked = 0 end

  rebuild_list(view.width - 2 * kMargin, list_height, first_row, n)

  -- The box is the note of the marked row, and with no row marked it is dead: a box that writes into nothing is a note
  -- somebody typed and lost.
  local chosen = marked > 0 and marks[marked] or nil

  loading = true
  note_box.text = chosen and note_of(chosen) or ''
  note_box.cursor = 0
  loading = false
  note_box.enabled = chosen ~= nil
  -- The block of the caret is the app's to blink here, so a box with no row to write keeps it off (docs/30).
  if chosen == nil then note_box.caret = false end

  -- The edge of the work day is said before the note, because it is about the two numbers at the top and not about the
  -- row under the finger.
  local edge = outside_work_day(state, today_is, now_sec)
  edge_shown = edge

  if spoiled ~= '' then
    hint_label.text = clip(spoiled, 36)
  elseif edge ~= '' then
    hint_label.text = edge
  elseif chosen then
    hint_label.text = string.format('Note for %02d:%02d %s', chosen.h, chosen.mi, kKindName[chosen.kind] or '?')
  elseif not today_is then
    hint_label.text = 'This day is over: press play'
  elseif n == 0 then
    hint_label.text = 'A new day: the play key opens it'
  elseif first_row > 1 then
    hint_label.text = string.format('%d older mark(s) are above', first_row - 1)
  else
    hint_label.text = 'Mark a row to write its note'
  end

  -- The play key always opens today, so it is alive on a day of the past even when that day was left open.
  start_btn.enabled = (not today_is) or can_start(state)

  -- One key for two things, and the mark on it is the truth: while the day rests the middle key is a play —go back to
  -- work—, and a pause figure on a paused day would be a key that does nothing.
  if state == 'paused' then
    key_cell(pause_btn, kPlay, 'Resume')
  else
    key_cell(pause_btn, kPauseMark, 'Pause')
  end

  pause_btn.enabled = today_is and can_pause(state)
  end_btn.enabled = today_is and can_end(state)
end

-- ===================================================================================== writing a mark and its note

local function mark(kind)
  local t, why = now_parts()
  if not t then
    spoiled = tostring(why)
    refresh()
    return
  end

  local m = {key = day_text(t), y = t.year, mo = t.month, d = t.day, h = t.hour, mi = t.min, s = t.sec, kind = kind}
  local ok, wrote = append_mark(m)

  if not ok then
    -- **A mark that did not reach the disk is said and not invented**: the screen is read again from the file —which
    -- does not have it— and the line says why.
    spoiled = 'Not saved: ' .. tostring(wrote)
  else
    spoiled = ''
  end

  if ok then
    console.print(string.format('timeclock: mark %s at %02d:%02d', kKindName[kind] or kind, m.h, m.mi))
  else
    console.print('timeclock: not saved: ' .. tostring(wrote))
  end

  local again = reload()
  if ok then marked = #marks end

  refresh(again or t)
  ui.present('gld16')
end

local function do_start() mark(kStart) end

-- True when the day on the glass is today, asked again here and not only in `refresh()`: the clock turns at midnight
-- while the screen stays where it was.
local function on_today()
  local t = now_parts()
  return t ~= nil and day_text(t) == shown_day
end

local function do_pause()
  if not on_today() then
    spoiled = 'This day is over: press play'
    refresh()
    ui.present('gld16')
    return
  end

  if state_of(marks) == 'paused' then mark(kResume) else mark(kPause) end
end

local function do_end()
  if not on_today() then
    spoiled = 'This day is over: press play'
    refresh()
    ui.present('gld16')
    return
  end

  mark(kEnd)
end

-- **The page says what it is showing**: the day on the glass with the hours it has, or the week with its average.
-- It is one line per turn of the sheet and per start, and it is what an emulator reads instead of looking at the glass.
local function say_day()
  local work, rest = totals(marks, nil)
  console.print(string.format('timeclock: day %s, %d marks, work %s, rest %s, %s', shown_day, #marks,
                              duration(work), duration(rest), status_text(state_of(marks), marks[#marks] or
                              {h = 0, mi = 0}, shown_day == today_shown)))
end

local function say_week()
  if week_data == nil then
    console.print('timeclock: week: no week to show')
    return
  end

  console.print(string.format('timeclock: week %s, avg %s in %d day(s)', week_data.label,
                              duration(week_data.average), week_data.count))
end

-- What is typed goes to the file at once: a note is a few hundred bytes, and holding it in memory until the app leaves
-- would lose it on a flat battery, the one moment a time clock is asked about.
local function on_note(text)
  if loading then return end

  local m = marked > 0 and marks[marked] or nil
  if not m then return end

  local clean = clean_note(text)
  if clean == note_of(m) then return end

  set_note(m, clean)

  local ok, why = save_notes()
  if ok then
    spoiled = ''
  else
    spoiled = 'Note not saved: ' .. tostring(why)
  end

  if ok then
    console.print(string.format('timeclock: note for %02d:%02d is "%s"', m.h, m.mi, clean))
  else
    console.print('timeclock: the note was not saved: ' .. tostring(why))
  end

  -- The row carries the note too, so the list is written again.
  rows_built = {}
  refresh()

  if spoiled ~= '' then
    hint_label.text = clip(spoiled, 36)
  else
    hint_label.text = string.format('Note for %02d:%02d %s', m.h, m.mi, kKindName[m.kind] or '?')
  end

  ui.present('gld16')
end

console.print('timeclock: three keys, the day under them and the week behind the eye')

-- ============================================================================================= the page, built again

-- The page of the day: the two numbers at the top, the rows of the day, the line that says what to do with them, the
-- note of the marked row and the three keys.
build_day = function()
  local top = kMargin

  status_label = make_label(view.width - 2 * kMargin - kEyeW - 12, kEyeH, '', kSmall, 'left', 'middle')
  status_label:place(kMargin, top)
  screen[#screen + 1] = status_label

  eye_btn = ui.button(kEyeW, kEyeH)
  eye_btn.background = 15
  eye_btn.border = 0
  eye_btn.color = 0
  if icons then eye_btn:font(keys_face) end
  -- **The eye is a mark and the top row is the word**: the figure is `U+F06E` of the original, which is the eye that
  -- opens the week, and the same key carries the arrow back when the week is the sheet on the glass (docs/45).
  key_cell(eye_btn, kEyeMark, 'Week', kSmall)
  eye_btn:place(view.width - kMargin - kEyeW, top)
  eye_btn.on_click = function() turn(true) end
  screen[#screen + 1] = eye_btn
  spots.eye = {x = view.width - kMargin - kEyeW, y = top, w = kEyeW, h = kEyeH}

  local y = top + kEyeH + kGap

  since_label = make_label(view.width - 2 * kMargin, kHintH, '', kSmall, 'left', 'middle')
  since_label:place(kMargin, y)
  screen[#screen + 1] = since_label
  y = y + kHintH + kGap

  work_label = make_label(view.width - 2 * kMargin, line(kTitle) + 6, '', kTitle, 'left', 'middle')
  work_label:place(kMargin, y)
  screen[#screen + 1] = work_label
  y = y + line(kTitle) + 6

  rest_label = make_label(view.width - 2 * kMargin, line(kTitle) + 6, '', kTitle, 'left', 'middle')
  rest_label:place(kMargin, y)
  screen[#screen + 1] = rest_label
  y = y + line(kTitle) + 6 + kGap

  -- The keys, the note and the line are measured from the bottom so that the row of keys sits where a thumb looks for
  -- it, whatever the line height of the face turns out to be.
  local keys_y = view.height - kMargin - kKeyH
  local box_y = keys_y - kGap - kNoteH
  local hint_y = box_y - kGap - kHintH

  list_y = y
  list_height = hint_y - kGap - y
  max_rows = math.max(1, math.floor(list_height / kRow))
  rows_built = {}
  list = nil

  hint_label = make_label(view.width - 2 * kMargin, kHintH, '', kSmall, 'left', 'middle')
  hint_label:place(kMargin, hint_y)
  screen[#screen + 1] = hint_label

  note_box = ui.textbox(view.width - 2 * kMargin, kNoteH)
  note_box.caret = false
  note_box.scale = kBody
  note_box.background = 15
  note_box.on_change = function(self, text) on_note(text or self:text()) end
  note_box:place(kMargin, box_y)
  screen[#screen + 1] = note_box
  spots.note = {x = kMargin, y = box_y, w = view.width - 2 * kMargin, h = kNoteH}
  spots.list = {x = kMargin, y = list_y, w = view.width - 2 * kMargin, h = list_height}

  -- **The three keys of the original, mark and word**: a play opens the day, two bars rest it and a square closes it,
  -- and each of them says its word when the face is not on the disk (docs/45).
  start_btn = make_key(kKeyW, kKeyH, kPlay, 'Start')
  pause_btn = make_key(kKeyW, kKeyH, kPauseMark, 'Pause')
  end_btn = make_key(kKeyW, kKeyH, kStopMark, 'End')

  start_btn:place(kMargin, keys_y)
  pause_btn:place(kMargin + kKeyW + kKeyGap, keys_y)
  end_btn:place(kMargin + 2 * (kKeyW + kKeyGap), keys_y)
  screen[#screen + 1] = start_btn
  screen[#screen + 1] = pause_btn
  screen[#screen + 1] = end_btn
  spots.start = {x = kMargin, y = keys_y, w = kKeyW, h = kKeyH}
  spots.pause = {x = kMargin + kKeyW + kKeyGap, y = keys_y, w = kKeyW, h = kKeyH}
  spots.stop = {x = kMargin + 2 * (kKeyW + kKeyGap), y = keys_y, w = kKeyW, h = kKeyH}

  start_btn.on_click = function() do_start() end
  pause_btn.on_click = function() do_pause() end
  end_btn.on_click = function() do_end() end
end

-- The page of the week: a canvas that the chart is painted on and the same eye in the bar, which says `Day` here.
build_week = function()
  local top = kMargin

  status_label = make_label(view.width - 2 * kMargin - kEyeW - 12, kEyeH, '', kSmall, 'left', 'middle')
  status_label:place(kMargin, top)
  screen[#screen + 1] = status_label

  eye_btn = ui.button(kEyeW, kEyeH)
  eye_btn.background = 15
  eye_btn.border = 0
  eye_btn.color = 0
  if icons then eye_btn:font(keys_face) end
  -- **The same key, going back**: on the week the figure is `U+F060` of the original —the arrow that comes back to the
  -- day— and it is the one the top row keeps, because a key that changes its place is a key to look for (docs/45).
  key_cell(eye_btn, kBackMark, 'Day', kSmall)
  eye_btn:place(view.width - kMargin - kEyeW, top)
  eye_btn.on_click = function() turn(true) end
  screen[#screen + 1] = eye_btn

  local y = top + kEyeH + kGap
  local height = view.height - kMargin - y

  week_canvas = ui.canvas(view.width - 2 * kMargin, height)
  spots.chart = {x = kMargin, y = y, w = view.width - 2 * kMargin, h = height}
  if week_canvas ~= nil then
    week_canvas:place(kMargin, y)
    week_canvas.on_draw = function(self)
      self:clear(15)
      paint_week(self, view.width - 2 * kMargin, height, week_data)
    end
    screen[#screen + 1] = week_canvas
  end
end

-- Turning the sheet is another picture and not a repaint: the page that was there is given back —fills and all— and the
-- other one is built, and the whole page is washed so that nothing of the chart stays under the day (docs/30, docs/50).
turn = function(_)
  give_back()
  page = (page == 1) and 2 or 1
  if page == 2 then load_week() end

  ui.clear(15, 'gld16')

  if page == 2 then build_week() else build_day() end
  refresh()

  if page == 2 then say_week() else say_day() end

  -- **The page is new and the quality is the system's** (0.179.5): the clear above is already a whole-page wash of sixteen
  -- levels, and the pass that flashes a second time over the same paper is what this file was asking for —a day is words
  -- and a week is a chart of a few grays, not a photograph— (docs/30, docs/50).
  ui.present()
end

-- The keyboard takes a band of the view, and a page that moved is a page painted again (docs/30).
function on_resize(width, height)
  view = ui.caps()
  give_back()
  ui.clear(15, 'gld16')
  if page == 2 then build_week() else build_day() end
  refresh()
  ui.present('gld16')
end

-- ====================================================================================================== the clock

-- **Once a second and not fifty times**: reading the clock is two I2C transactions, and the loop turns fifty times a
-- second. What moves is a minute, and only the text of the numbers is written again —a control that rewrites its text
-- marks its own rectangle, so a minute costs one band of text and not a pass of the whole screen—.
local function on_tick(now_sec, t)
  if page ~= 1 or t == nil then return false end

  -- Midnight does not clean the screen: what changes is which key can be pressed, and `today_shown` is the latch so it
  -- happens once a day.
  if day_text(t) ~= today_shown then
    refresh(t)
    return true
  end

  -- And only today's day moves: a day of the past is a closed account.
  if shown_day ~= today_shown then return false end

  local state = state_of(marks)
  if not can_pause(state) then return false end

  -- The edge of the work day is a change of the screen too: one refresh per crossing and not one per second.
  if outside_work_day(state, true, now_sec) ~= edge_shown then
    refresh(t)
    return true
  end

  local work, rest = totals(marks, now_sec)
  local since = since_text(now_sec, marks[#marks])
  local text = work_text(work) .. '|' .. rest_text(rest) .. '|' .. since

  if text == totals_shown then return false end

  totals_shown = text
  work_label.text = work_text(work)
  rest_label.text = rest_text(rest)
  since_label.text = since
  return true
end

local t = reload()

ui.clear(15, 'gld16')
if page == 2 then load_week() end
if page == 2 then build_week() else build_day() end
refresh(t)
if page == 2 then say_week() else say_day() end

-- **Where the controls are, when the app is asked to say it** (`run timeclock keys`), the same idea the calculator
-- has: the eye, the box of the note, the row of the list and the three keys, with the middle of each.
if app.args ~= nil and app.args.path == 'keys' then
  for _, name in ipairs({'eye', 'note', 'list', 'start', 'pause', 'stop', 'chart'}) do
    local spot = spots[name]
    if spot ~= nil then
      console.print(string.format('Timeclock %s %d %d %d %d', name, spot.x + math.floor(spot.w / 2),
                                  spot.y + math.floor(spot.h / 2), spot.w, spot.h))
    end
  end
end

ui.present()

-- **The memory it costs, said once**: the ceiling of an app is the board's and this page is the biggest of the tree,
-- so the number is worth having in the log of an emulator (docs/75).
if sys.heap ~= nil then
  local heap = sys.heap()
  if heap ~= nil and heap.used ~= nil then
    console.print(string.format('timeclock: heap %d of %d B, peak %d', heap.used, heap.limit, heap.peak or 0))
  end
end

local tick_at = 0
while true do
  local ms = sys.time_ms()
  if ms < tick_at then tick_at = 0 end

  if ms - tick_at >= 1000 then
    tick_at = ms
    local now, now_why = now_parts()
    if now ~= nil then
      local now_sec = now.hour * 3600 + now.min * 60 + now.sec
      if on_tick(now_sec, now) then ui.present('du') end
    end
  end

  ui.present()
  sys.delay(50):await()
end
