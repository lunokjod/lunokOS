-- edit: **a modal text editor, like vim**, for the serial port of the board and for the native console.
--
--   run edit <path>        opens the file (and makes a new one if it is not there)
--   open txt <path>        the same, when nobody else opens .txt files
--
-- The keys are those of the vim people use every day, and `Ctrl+L` paints the page again (the log of the system goes
-- through the same console and may have broken it). **Normal**: `h j k l` (or the arrows), `0 $`, `w b`, `gg G`,
-- `x X`, `dd yy p P`, `D J`, `r<c>`, `u` and `Ctrl+R`, `/` to search with `n`/`N`, `:` for the orders. **Insert**
-- (`i a I o O`): it types, `Enter` splits the line, `Backspace` joins it again, `Tab` puts two spaces and `ESC` goes
-- back to normal mode.
--
-- **The file is never loaded whole.** The editor reads **the index of lines** once —where every line starts, with
-- `fs.read_at` in pieces of 64 KB— and after that it reads **only the lines that are seen**: opening the 1.3 MB file
-- `http` leaves behind is reading twenty-two lines and a table of positions. What limits it is not the size of the
-- file but the memory of the interpreter, and what goes in is **one number per line** (docs/30, docs/70, docs/75).
--
-- **A line of more than 8 KB is read by window and is not written**: it is seen and navigated (the window moves with
-- the caret) but editing it would mean holding it whole in memory. Normal lines are edited, and `:w` writes the new
-- file **copying from the original the pieces that have not changed**, 64 KB at a time: the whole file is never
-- anywhere.
--
-- The file the prompt names is the one the app can save: the prompt has given it that path and only that one
-- (docs/30, docs/70).

local caps = sys.caps()
if not caps.ansi then
  console.print('edit: this console has no ANSI: use a terminal, not a plain pipe')
  return
end

-- ---------------------------------------------------------------------------------------------------
-- The file: an index of lines and the lines that are seen
-- ---------------------------------------------------------------------------------------------------

-- **`starts[i]` is where line i starts, in bytes and 1-based**, and `0` means the line has been made by the editor and
-- its text is in `edits[i]`. The end of the file acts as the end of the last line (docs/70).
local file = {path = nil, size = 0, eol = false}
local starts = {}
-- The text of the lines that are not what is in the file. They are few entries: only what has changed is there, not
-- the file (docs/75).
local edits = {}
-- What has been read to paint: `{text, from, long}` per line.
local cache = {}
-- Are there changes that are not saved?
local changed = false

-- A line longer than this is read by window and is not written (docs/30).
local kLineCap = 8192
-- The window of a long line: what is held in memory around it.
local kWindow = 2048
-- The piece the file is read with to make the index and to copy it when saving.
local kChunk = 65536

local row, col = 1, 1
local top = 1
local left = 0

local mode = 'normal'
local prompt = nil
local prompt_text = ''
local message = ''

local yank = nil
local yank_linewise = false
local search = nil
local pending = nil

-- Desfer i refer: a stack of operations, not of pages.
local undo, redo = {}, {}
local kUndoDepth = 200

-- ---------------------------------------------------------------------------------------------------
-- El terminal
-- ---------------------------------------------------------------------------------------------------

local function out(text) console.write(text) end

local function size()
  local w = term.width()
  local h = term.height()
  if w == nil or w < 20 then w = 80 end
  if h == nil or h < 6 then h = 24 end
  return w, h
end

local function expand(text)
  if not text:find('\t') then return text end
  local parts, width = {}, 0
  for i = 1, #text do
    local c = text:sub(i, i)
    if c == '\t' then
      local n = 8 - (width % 8)
      parts[#parts + 1] = string.rep(' ', n)
      width = width + n
    else
      parts[#parts + 1] = c
      width = width + 1
    end
  end
  return table.concat(parts)
end

-- ---------------------------------------------------------------------------------------------------
-- The lines, in pieces
-- ---------------------------------------------------------------------------------------------------

local function line_count() return #starts end

-- What line i makes, without counting the newline.
local function line_len(i)
  if starts[i] == nil then return 0 end
  if starts[i] == 0 then return #(edits[i] or '') end
  local next_start = starts[i + 1]
  if next_start ~= nil then return next_start - 1 - starts[i] end
  return file.size + 1 - starts[i] - (file.eol and 1 or 0)
end

local function read_range(i, from, want)
  if want <= 0 then return '' end
  local got = fs.read_at(file.path, starts[i] - 1 + from - 1, want)
  if got == nil then return '' end
  return got
end

-- The whole text of a line, or what the editor has put in it.
local function text_of(i)
  local put = edits[i]
  if put ~= nil then return put end
  if starts[i] == nil or starts[i] == 0 then return '' end
  local text = read_range(i, 1, line_len(i))
  -- The CR of a Windows file is not part of the line (docs/70).
  if #text > 0 and text:sub(-1) == '\r' then text = text:sub(1, -2) end
  return text
end

-- **The text of a line to paint it**: whole if it fits, and a window around the caret if not. The window is read again
-- when the caret leaves it, so moving through a line of a hundred thousand bytes is reading two thousand bytes every
-- time (docs/70).
local function line_cache(i, want_col)
  local cached = cache[i]
  if cached ~= nil then
    if not cached.long then return cached end
    if want_col >= cached.from and want_col <= cached.from + #cached.text then return cached end
  end
  local len = line_len(i)
  local entry
  if starts[i] == 0 or len <= kLineCap then
    entry = {text = text_of(i), from = 1, long = false}
  else
    local from = want_col - math.floor(kWindow / 2)
    if from < 1 then from = 1 end
    if from > len - kWindow + 1 then from = len - kWindow + 1 end
    if from < 1 then from = 1 end
    local text = read_range(i, from, math.min(kWindow, len - from + 1))
    if #text > 0 and text:sub(-1) == '\r' and from + #text - 1 == len then text = text:sub(1, -2) end
    entry = {text = text, from = from, long = true}
  end
  cache[i] = entry
  return entry
end

-- The text of a line that can be edited, or `nil` if it is too long (and it says so).
local function editable(i)
  if starts[i] ~= 0 and line_len(i) > kLineCap then
    message = string.format('the line is %d B: over the %d B of a line that can be edited (it is shown in a window)',
      line_len(i), kLineCap)
    return nil
  end
  return text_of(i)
end

local function clamp_col()
  local len = line_len(row)
  if col < 1 then col = 1 end
  if col > len + 1 then col = len + 1 end
end

local function clamp_row()
  if row < 1 then row = 1 end
  if row > line_count() then row = line_count() end
end

local function cursor_screen_col()
  local entry = line_cache(row, col)
  return #expand(entry.text:sub(1, math.max(0, col - entry.from))) + 1
end

local function keep_cursor_visible(w, text_rows)
  if row < top then top = row end
  if row > top + text_rows - 1 then top = row - text_rows + 1 end
  if top < 1 then top = 1 end
  local screen = cursor_screen_col()
  if screen <= left then left = screen - 1 end
  if screen - left > w - 1 then left = screen - w end
  if left < 0 then left = 0 end
end

-- ---------------------------------------------------------------------------------------------------
-- Dibuixar
-- ---------------------------------------------------------------------------------------------------

local painted = {}
local painted_status, painted_prompt = nil, nil

local function paint(screen_row, text, w)
  if painted[screen_row] == text then return end
  painted[screen_row] = text
  term.move(screen_row, 1)
  local padded = text
  if #padded < w then padded = padded .. string.rep(' ', w - #padded) end
  out(padded:sub(1, w))
end

local function status_line(w)
  local name = file.path or '[no name]'
  if #name > 26 then name = '...' .. name:sub(-23) end
  local head = ' ' .. (changed and (name .. ' [+]') or name)
  local pct = math.floor((row - 1) * 100 / math.max(1, line_count() - 1))
  local tail = string.format('%d,%d  %d%% ', row, col, pct)
  local gap = w - #head - #tail
  if gap < 0 then
    head = head:sub(1, math.max(0, w - #tail - 1))
    gap = math.max(0, w - #head - #tail)
  end
  return head .. string.rep(' ', gap) .. tail
end

local function message_line(w)
  if prompt ~= nil then
    local text = prompt .. prompt_text
    if #text > w then text = text:sub(#text - w + 1) end
    return text
  end
  local text = message
  if line_len(row) > kLineCap and text == '' then
    text = string.format('[line of %d B: window, read only]', line_len(row))
  end
  if #text > w then text = text:sub(1, w) end
  return text
end

local function render()
  local w, h = size()
  local text_rows = h - 2
  clamp_row()
  clamp_col()
  keep_cursor_visible(w, text_rows)

  for i = 0, text_rows - 1 do
    local r = top + i
    local text
    if r > line_count() then
      text = '~'
    else
      local entry = line_cache(r, (r == row) and col or 1)
      text = expand(entry.text):sub(left + 1, left + w)
    end
    paint(i + 1, text, w)
  end
  local status = status_line(w)
  if painted_status ~= status then
    painted_status = status
    term.move(h - 1, 1)
    out(status:sub(1, w))
  end
  local line = message_line(w)
  if painted_prompt ~= line then
    painted_prompt = line
    term.move(h, 1)
    out(line .. string.rep(' ', math.max(0, w - #line)))
  end
  term.move(1 + (row - top), math.min(w, cursor_screen_col() - left))
end

local function repaint_all()
  painted = {}
  painted_status, painted_prompt = nil, nil
  term.clear()
  render()
end

-- ---------------------------------------------------------------------------------------------------
-- Desfer
-- ---------------------------------------------------------------------------------------------------

local function push_undo(op)
  undo[#undo + 1] = op
  if #undo > kUndoDepth then table.remove(undo, 1) end
  redo = {}
  changed = true
end

local function set_line(r, text, grouping)
  local before = edits[r] or text_of(r)
  if before == text then return end
  edits[r] = text
  cache[r] = nil
  local last = undo[#undo]
  if grouping and last ~= nil and last.kind == 'line' and last.r == r then
    last.after = text
  else
    push_undo({kind = 'line', r = r, before = before, after = text})
  end
end

-- Moves the keys of `edits` when the lines move place, and throws away what has been painted.
local function shift_edits(from, delta)
  local moved = {}
  for k, v in pairs(edits) do
    if k >= from then moved[k + delta] = v else moved[k] = v end
  end
  edits = moved
  cache = {}
end

local function splice_lines(r, remove_count, new_lines)
  local removed = {}
  for i = 0, remove_count - 1 do removed[i + 1] = edits[r + i] or text_of(r + i) end
  for _ = 1, remove_count do
    if starts[r] ~= nil then table.remove(starts, r) end
  end
  shift_edits(r, -remove_count)
  for i = #new_lines, 1, -1 do
    table.insert(starts, r, 0)
    -- Every new line goes into `r` and pushes the ones after it.
    local moved = {}
    for k, v in pairs(edits) do
      if k >= r then moved[k + 1] = v else moved[k] = v end
    end
    edits = moved
    edits[r] = new_lines[i]
  end
  if #starts == 0 then starts = {0} end
  push_undo({kind = 'lines', r = r, removed = removed, added = new_lines})
  cache = {}
end

local function undo_once()
  local op = table.remove(undo)
  if op == nil then
    message = 'already at the oldest change'
    return
  end
  if op.kind == 'line' then
    local now = edits[op.r] or text_of(op.r)
    edits[op.r] = op.before
    redo[#redo + 1] = {kind = 'line', r = op.r, before = op.after, after = now}
    row, col = op.r, math.min(col, line_len(op.r) + 1)
  else
    for _ = 1, #op.added do
      if starts[op.r] ~= nil then table.remove(starts, op.r) end
    end
    shift_edits(op.r, -#op.added)
    for i = #op.removed, 1, -1 do
      table.insert(starts, op.r, 0)
      local moved = {}
      for k, v in pairs(edits) do
        if k >= op.r then moved[k + 1] = v else moved[k] = v end
      end
      edits = moved
      edits[op.r] = op.removed[i]
    end
    redo[#redo + 1] = {kind = 'lines', r = op.r, removed = op.added, added = op.removed}
    row = math.min(op.r, line_count())
    col = 1
  end
  cache = {}
  changed = true
  message = 'undo'
end

local function redo_once()
  local op = table.remove(redo)
  if op == nil then
    message = 'already at the newest change'
    return
  end
  if op.kind == 'line' then
    edits[op.r] = op.after
    undo[#undo + 1] = {kind = 'line', r = op.r, before = op.before, after = op.after}
    row, col = op.r, math.min(col, line_len(op.r) + 1)
  else
    for _ = 1, #op.removed do
      if starts[op.r] ~= nil then table.remove(starts, op.r) end
    end
    shift_edits(op.r, -#op.removed)
    for i = #op.added, 1, -1 do
      table.insert(starts, op.r, 0)
      local moved = {}
      for k, v in pairs(edits) do
        if k >= op.r then moved[k + 1] = v else moved[k] = v end
      end
      edits = moved
      edits[op.r] = op.added[i]
    end
    undo[#undo + 1] = op
    row = math.min(op.r, line_count())
    col = 1
  end
  cache = {}
  changed = true
  message = 'redo'
end

-- ---------------------------------------------------------------------------------------------------
-- The index and the save
-- ---------------------------------------------------------------------------------------------------

-- **The index of lines, in one walk and in pieces**: the file is never held whole, 64 KB after 64 KB are asked for and
-- the positions where every line starts are kept (docs/70, docs/75).
local function build_index(path, total)
  starts = {1}
  local at = 0
  while at < total do
    local want = math.min(kChunk, total - at)
    local got = fs.read_at(path, at, want)
    if got == nil or #got == 0 then break end
    local from = 1
    while true do
      local nl = got:find('\n', from, true)
      if nl == nil then break end
      starts[#starts + 1] = at + nl + 1
      from = nl + 1
    end
    at = at + #got
  end
  -- A newline at the end is not one empty line too many: vim does not count it either.
  if #starts > 1 and starts[#starts] == total + 1 then table.remove(starts) end
end

local function load(path)
  local stat = fs.stat(path)
  if stat == nil then
    file = {path = path, size = 0, eol = true}
    starts, edits = {0}, {[1] = ''}
    changed = false
    row, col, top, left = 1, 1, 1, 0
    undo, redo, cache = {}, {}, {}
    return true, 'new file'
  end
  if stat.type ~= 'file' then return false, 'not a file' end
  file = {path = path, size = stat.size or 0, eol = false}
  if file.size > 0 then
    local last = fs.read_at(path, file.size - 1, 1)
    file.eol = (last == '\n')
  end
  build_index(path, file.size)
  edits = {}
  -- An empty file is one empty line, and vim does not make one more either.
  if file.size == 0 then starts, edits = {0}, {[1] = ''} end
  changed = false
  row, col, top, left = 1, 1, 1, 0
  undo, redo, cache = {}, {}, {}
  return true
end

-- Copies a run of lines untouched from the original to the new file, 64 KB at a time.
local function copy_run(handle, from_line, to_line)
  local at = starts[from_line]
  local stop = starts[to_line + 1] or (file.size + 1)
  while at < stop do
    local want = math.min(kChunk, stop - at)
    local got = fs.read_at(file.path, at - 1, want)
    if got == nil or #got == 0 then break end
    handle:write(got)
    at = at + #got
  end
end

-- **Saving is writing the new file to a temporary one and renaming it**: what has not changed is copied from the
-- original in pieces and what has changed is written from memory (docs/70).
local function save(path)
  if path == nil or path == '' then path = file.path end
  if path == nil or path == '' then return false, 'no file name: :w <path>' end
  local temp = path .. '.tmp'
  local handle, err = fs.open(temp, 'w')
  if handle == nil then return false, tostring(err and err.message or 'cannot write') end
  local total = line_count()
  local ok = true
  local i = 1
  while i <= total and ok do
    if edits[i] == nil and starts[i] ~= 0 then
      local last = i
      while last + 1 <= total and edits[last + 1] == nil and starts[last + 1] ~= 0 do
        last = last + 1
      end
      copy_run(handle, i, last)
      i = last + 1
    else
      ok = handle:write(edits[i] or '') ~= nil
      if i < total or file.eol then ok = (handle:write('\n') ~= nil) and ok end
      i = i + 1
    end
  end
  handle:close()
  if not ok then
    fs.remove(temp)
    return false, 'cannot write'
  end
  fs.remove(path)
  local renamed, rename_err = fs.rename(temp, path)
  if renamed == nil then
    fs.remove(temp)
    return false, tostring(rename_err and rename_err.message or 'cannot rename')
  end
  file.path = path
  local stat = fs.stat(path)
  file.size = (stat ~= nil) and (stat.size or 0) or 0
  if file.size > 0 then
    local last = fs.read_at(path, file.size - 1, 1)
    file.eol = (last == '\n')
  end
  -- What has been written is the file: the index is made again and what had been edited now belongs to the file.
  edits, cache, changed = {}, {}, false
  build_index(path, file.size)
  if #starts == 0 then starts = {0} end
  return true
end

-- ---------------------------------------------------------------------------------------------------
-- Les tecles
-- ---------------------------------------------------------------------------------------------------

local pushed = nil
local function next_byte()
  if pushed ~= nil then
    local b = pushed
    pushed = nil
    return b
  end
  return console.read_byte():await()
end
local function put_back(b) pushed = b end

local function read_key()
  local b = next_byte()
  if b == 27 then
    local b2 = next_byte()
    if b2 == 91 or b2 == 79 then
      local b3 = next_byte()
      if b3 == 51 then
        local b4 = next_byte()
        if b4 == 126 then return 'delete' end
        put_back(b4)
        return 'unknown'
      end
      if b3 == 53 then next_byte(); return 'pgup' end
      if b3 == 54 then next_byte(); return 'pgdn' end
      if b3 == 65 then return 'up' end
      if b3 == 66 then return 'down' end
      if b3 == 67 then return 'right' end
      if b3 == 68 then return 'left' end
      if b3 == 72 then return 'home' end
      if b3 == 70 then return 'end' end
      return 'unknown'
    end
    put_back(b2)
    return 'esc'
  end
  if b == 13 or b == 10 then return 'enter' end
  if b == 127 or b == 8 then return 'backspace' end
  if b == 9 then return 'tab' end
  if b == 3 then return 'ctrl-c' end
  if b == 18 then return 'ctrl-r' end
  if b == 12 then return 'ctrl-l' end
  if b == 6 then return 'ctrl-f' end
  if b == 2 then return 'ctrl-b' end
  if b < 32 then return 'unknown' end
  return string.char(b)
end

-- ---------------------------------------------------------------------------------------------------
-- Searching without the whole file
-- ---------------------------------------------------------------------------------------------------

-- The first piece where the pattern appears from an offset, with the margin of the pattern so that a match that falls
-- in the middle of two pieces is found too (docs/70).
local function first_hit(from)
  local at = from
  while at < file.size do
    local want = math.min(kChunk, file.size - at) + #search - 1
    local got = fs.read_at(file.path, at, want)
    if got == nil or #got == 0 then break end
    local hit = got:find(search, 1, true)
    if hit ~= nil then return at + hit end
    at = at + math.min(kChunk, file.size - at)
  end
  return nil
end

-- The last appearance before an offset, and the one of the whole file if there is none before it.
local function last_hit(before)
  local found = nil
  local at = 0
  while at < file.size do
    local want = math.min(kChunk, file.size - at) + #search - 1
    local got = fs.read_at(file.path, at, want)
    if got == nil or #got == 0 then break end
    local from = 1
    while true do
      local hit = got:find(search, from, true)
      if hit == nil then break end
      if at + hit < before then found = at + hit end
      from = hit + 1
    end
    at = at + math.min(kChunk, file.size - at)
  end
  return found
end

local function line_of_offset(offset)
  local lo, hi = 1, #starts
  while lo < hi do
    local mid = math.floor((lo + hi + 1) / 2)
    if starts[mid] <= offset then lo = mid else hi = mid - 1 end
  end
  return lo
end

local function do_search(backwards)
  if search == nil or search == '' then
    message = 'no search pattern yet'
    return
  end
  local at
  if backwards then
    at = last_hit(starts[row] - 1 + col - 1)
    if at == nil then at = last_hit(file.size + 1) end
  else
    at = first_hit(starts[row] - 1 + col)
    if at == nil then at = first_hit(0) end
  end
  if at == nil then
    message = 'pattern not found: ' .. search
    return
  end
  row = line_of_offset(at)
  col = at - starts[row] + 1
  cache = {}
  left = 0
end

-- ---------------------------------------------------------------------------------------------------
-- Les ordres
-- ---------------------------------------------------------------------------------------------------

local quitting = false

local function command(text)
  if text == '' then return end
  local number = text:match('^(%d+)$')
  if number ~= nil then
    row = math.max(1, math.min(tonumber(number), line_count()))
    col = 1
    return
  end
  if text == 'q' then
    if changed then
      message = 'no write since last change: :q! throws it away'
    else
      quitting = true
    end
    return
  end
  if text == 'q!' then
    quitting = true
    return
  end
  if text == 'w' then
    local ok, err = save(nil)
    message = ok and string.format('"%s" %d lines written', file.path, line_count())
                  or ('write failed: ' .. tostring(err))
    return
  end
  local path = text:match('^w (.-)$')
  if path ~= nil then
    local ok, err = save(path)
    message = ok and (string.format('"%s" written', path)) or ('write failed: ' .. tostring(err))
    return
  end
  if text == 'wq' or text == 'x' then
    local ok, err = save(nil)
    if not ok then
      message = 'write failed: ' .. tostring(err)
      return
    end
    quitting = true
    return
  end
  if text == 'e!' then
    local ok, err = load(file.path)
    message = ok and 'reloaded' or tostring(err)
    repaint_all()
    return
  end
  path = text:match('^e (.-)$')
  if path ~= nil then
    if changed then
      message = 'no write since last change: :w first'
      return
    end
    local ok, err = load(path)
    if not ok then
      message = 'cannot open: ' .. tostring(err)
      return
    end
    message = '"' .. path .. '"'
    repaint_all()
    return
  end
  message = 'not an editor command: ' .. text
end

-- ---------------------------------------------------------------------------------------------------
-- Moviments i edicions
-- ---------------------------------------------------------------------------------------------------

local function word_forward()
  local text = text_of(row)
  local i = col
  while i <= #text and text:sub(i, i):match('%w') do i = i + 1 end
  while i <= #text and not text:sub(i, i):match('%w') do i = i + 1 end
  if i > #text and row < line_count() then
    row = row + 1
    text = text_of(row)
    i = 1
    while i <= #text and not text:sub(i, i):match('%w') do i = i + 1 end
  end
  col = math.min(i, line_len(row) + 1)
end

local function word_back()
  local text = text_of(row)
  local i = col - 1
  while i >= 1 and not text:sub(i, i):match('%w') do i = i - 1 end
  while i >= 2 and text:sub(i - 1, i - 1):match('%w') do i = i - 1 end
  if i < 1 and row > 1 then
    row = row - 1
    col = line_len(row) + 1
    return
  end
  col = math.max(1, i)
end

local function delete_char()
  local text = editable(row)
  if text == nil or col > #text then return end
  set_line(row, text:sub(1, col - 1) .. text:sub(col + 1))
end

local function insert_text(piece, grouping)
  local text = editable(row)
  if text == nil then return end
  set_line(row, text:sub(1, col - 1) .. piece .. text:sub(col), grouping)
  col = col + #piece
end

local function split_line()
  local text = editable(row)
  if text == nil then return end
  local head, tail = text:sub(1, col - 1), text:sub(col)
  splice_lines(row, 1, {head, tail})
  row, col = row + 1, 1
end

local function join_lines()
  if row >= line_count() then return end
  local here, below = editable(row), editable(row + 1)
  if here == nil or below == nil then return end
  local at = #here + 1
  splice_lines(row, 2, {here .. ' ' .. below})
  row, col = row, at
end

local function backspace()
  if col > 1 then
    local text = editable(row)
    if text == nil then return end
    set_line(row, text:sub(1, col - 2) .. text:sub(col))
    col = col - 1
  elseif row > 1 then
    local here, above = editable(row), editable(row - 1)
    if here == nil or above == nil then return end
    local at = #above + 1
    splice_lines(row - 1, 2, {above .. here})
    row, col = row - 1, at
  end
end

-- ---------------------------------------------------------------------------------------------------
-- El mode normal
-- ---------------------------------------------------------------------------------------------------

local function normal_key(k)
  local w, h = size()
  local page = h - 2
  local len = line_len(row)

  if pending == 'd' then
    pending = nil
    if k == 'd' then
      splice_lines(row, 1, {})
      if row > line_count() then row = line_count() end
      col = 1
      return
    end
    if k == 'w' then
      -- **How far the word reaches**: if the movement stays on the line the piece is erased, and if it goes to the one
      -- below it is erased up to the end of that one, which is what vim does with `dw` (docs/30).
      local text = editable(row)
      if text == nil then return end
      local start_row, start_col = row, col
      word_forward()
      local to = (row == start_row) and col or (#text + 1)
      set_line(start_row, text:sub(1, start_col - 1) .. text:sub(to))
      row = start_row
      col = math.min(start_col, line_len(row) + 1)
      return
    end
    return
  end
  if pending == 'y' then
    pending = nil
    if k == 'y' then
      yank, yank_linewise = {text_of(row)}, true
      message = '1 line yanked'
    end
    return
  end
  if pending == 'g' then
    pending = nil
    if k == 'g' then row, col, top = 1, 1, 1 end
    return
  end
  if pending == 'r' then
    pending = nil
    if #k == 1 then
      local text = editable(row)
      if text ~= nil and col <= #text then
        set_line(row, text:sub(1, col - 1) .. k .. text:sub(col + 1))
      end
    end
    return
  end

  if k == 'h' or k == 'left' then
    if col > 1 then col = col - 1 end
  elseif k == 'l' or k == 'right' then
    if col < len + 1 then col = col + 1 end
  elseif k == 'j' or k == 'down' then
    if row < line_count() then row = row + 1 end
    clamp_col()
  elseif k == 'k' or k == 'up' then
    if row > 1 then row = row - 1 end
    clamp_col()
  elseif k == '0' or k == 'home' then
    col = 1
    left = 0
  elseif k == '$' or k == 'end' then
    col = len + 1
  elseif k == 'w' then
    word_forward()
  elseif k == 'b' then
    word_back()
  elseif k == 'g' then
    pending = 'g'
  elseif k == 'G' then
    row, col = line_count(), 1
  elseif k == 'ctrl-f' or k == 'pgdn' then
    row = math.min(line_count(), row + page)
    col = 1
  elseif k == 'ctrl-b' or k == 'pgup' then
    row = math.max(1, row - page)
    col = 1
  elseif k == 'x' or k == 'delete' then
    delete_char()
  elseif k == 'X' then
    if col > 1 then
      col = col - 1
      delete_char()
    end
  elseif k == 'D' then
    local text = editable(row)
    if text ~= nil and col <= #text then set_line(row, text:sub(1, col - 1)) end
  elseif k == 'J' then
    join_lines()
  elseif k == 'd' then
    pending = 'd'
  elseif k == 'y' then
    pending = 'y'
  elseif k == 'r' then
    pending = 'r'
  elseif k == 'p' or k == 'P' then
    if yank == nil then
      message = 'nothing to put'
    elseif yank_linewise then
      local at = (k == 'p') and row + 1 or row
      splice_lines(at, 0, yank)
      row, col = at, 1
    else
      insert_text(table.concat(yank))
    end
  elseif k == 'i' then
    mode = 'insert'
  elseif k == 'a' then
    if col <= len then col = col + 1 end
    mode = 'insert'
  elseif k == 'I' then
    col = 1
    mode = 'insert'
  elseif k == 'A' then
    col = len + 1
    mode = 'insert'
  elseif k == 'o' then
    splice_lines(row + 1, 0, {''})
    row, col = row + 1, 1
    mode = 'insert'
  elseif k == 'O' then
    splice_lines(row, 0, {''})
    col = 1
    mode = 'insert'
  elseif k == 'u' then
    undo_once()
  elseif k == 'ctrl-r' then
    redo_once()
  elseif k == ':' then
    prompt, prompt_text = ':', ''
  elseif k == '/' then
    prompt, prompt_text = '/', ''
  elseif k == 'n' then
    do_search(false)
  elseif k == 'N' then
    do_search(true)
  elseif k == 'ctrl-l' then
    repaint_all()
  elseif k == 'ctrl-c' then
    message = ''
  end
end

-- ---------------------------------------------------------------------------------------------------
-- The insert mode
-- ---------------------------------------------------------------------------------------------------

local function insert_key(k)
  if k == 'esc' then
    mode = 'normal'
    if col > 1 then col = col - 1 end
    clamp_col()
  elseif k == 'enter' then
    split_line()
  elseif k == 'backspace' then
    backspace()
  elseif k == 'tab' then
    insert_text('  ', true)
  elseif k == 'left' then
    if col > 1 then col = col - 1 end
  elseif k == 'right' then
    if col <= line_len(row) then col = col + 1 end
  elseif k == 'up' then
    if row > 1 then row = row - 1 end
    clamp_col()
  elseif k == 'down' then
    if row < line_count() then row = row + 1 end
    clamp_col()
  elseif k == 'home' then
    col = 1
  elseif k == 'end' then
    col = line_len(row) + 1
  elseif k == 'delete' then
    delete_char()
  elseif #k == 1 then
    insert_text(k, true)
  end
end

local function prompt_key(k)
  if k == 'esc' then
    prompt, prompt_text = nil, ''
  elseif k == 'enter' then
    local text = prompt_text
    local what = prompt
    prompt, prompt_text = nil, ''
    if what == '/' then
      if text ~= '' then
        search = text
        do_search(false)
      end
    else
      command(text)
    end
  elseif k == 'backspace' then
    prompt_text = prompt_text:sub(1, -2)
  elseif #k == 1 then
    prompt_text = prompt_text .. k
  end
end

-- ---------------------------------------------------------------------------------------------------
-- El programa
-- ---------------------------------------------------------------------------------------------------

local opened = nil
if app ~= nil and app.args ~= nil then opened = app.args.path end

console.raw(true)
term.clear()
term.hide_cursor()

if opened ~= nil then
  local ok, why = load(opened)
  if not ok then
    message = tostring(why)
  elseif why ~= nil then
    message = why
  end
else
  message = 'no file: :e <path> to open one, :w <path> to write one'
  file = {path = nil, size = 0, eol = true}
  starts, edits = {0}, {[1] = ''}
end

while not quitting do
  render()
  local k = read_key()
  if prompt ~= nil then
    prompt_key(k)
  elseif mode == 'insert' then
    insert_key(k)
  else
    normal_key(k)
  end
end

term.clear()
term.show_cursor()
term.move(1, 1)
console.raw(false)
