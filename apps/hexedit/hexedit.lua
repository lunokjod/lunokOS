-- hexedit: **un editor hexadecimal**, per al port sèrie de la placa i per a la consola nadiua.
--
--   run hexedit <path>     obre el fitxer (binari o no) i el mostra en una graella
--   open bin <path>        el mateix, si ningú altre obre els .bin
--
-- La graella és la de sempre: el desplaçament a l'esquerra, setze bytes en hexadecimal al mig i el que es pot
-- llegir a la dreta. **El cursor té dos nibli**: `0`–`9` i `a`–`f` escriuen el de dalt i passen al de baix, i el
-- segon escriu el byte i passa al següent. Les fletxes mouen, `PgUp`/`PgDn` salten una pàgina, `u` desfà i
-- `:w`, `:q`, `:wq`, `:e` i `:<desplaçament>` (en hexadecimal si comença per `0x`) són les ordres del `edit`.
--
-- **El fitxer no es carrega sencer si no hi cap.** Fins a 256 KB es té a la memòria i s'hi pot esborrar i
-- inserir un byte (`x` i `i`), que mouen el que hi ha darrere; més gran, l'editor llegeix **la finestra que es
-- veu** amb `fs.read_at` i els bytes que s'escriuen es guarden amb un mànec al seu desplaçament (`:w`), o
-- sigui que un binari de qualsevol mida es pot mirar i pedaçar sense tenir-lo sencer enlloc. En un fitxer
-- d'aquesta mida `x` i `i` no hi són: esborrar un byte voldria dir tornar a escriure el fitxer sencer (docs/30,
-- docs/70, docs/75).
--
-- El fitxer que la persona ha nomenat al prompt és el que es pot desar (docs/30, docs/70).

local caps = sys.caps()
if not caps.ansi then
  console.print('hexedit: this console has no ANSI: use a terminal, not a plain pipe')
  return
end

-- Fins a això el fitxer cap a la memòria de l'intèrpret i l'editor el té sencer.
local kWhole = 256 * 1024

local path, size = nil, 0
local whole = false     -- el fitxer sencer a `data`, o una finestra a `win`
local data = ''
local win = {base = 1, text = ''}
-- Els bytes que s'han escrit i encara no són al fitxer (a la finestra; al sencer ja són a `data`).
local dirty = {}
-- Desfer: el que hi havia abans de cada byte, amb el seu desplaçament.
local undo = {}
local kUndoDepth = 64

local pos, nibble = 1, 0
local top = 0
local message = ''

local function out(text) console.write(text) end

local function size_of()
  local w = term.width()
  local h = term.height()
  if w == nil or w < 20 then w = 80 end
  if h == nil or h < 6 then h = 24 end
  return w, h
end

local function bytes_per_row(w)
  local per = math.floor((w - 8 - 2 - 1) / 4)
  if per > 16 then per = 16 end
  if per < 4 then per = 4 end
  return per
end

local function printable(b)
  if b >= 32 and b < 127 then return string.char(b) end
  return '.'
end

-- ---------------------------------------------------------------------------------------------------
-- Els bytes: del fitxer sencer o de la finestra
-- ---------------------------------------------------------------------------------------------------

-- **La finestra del fitxer**: el que cal per pintar la pàgina i dues files de marge a cada costat, perquè
-- moure's una fila amunt o avall no torni a llegir res (docs/70).
local function load_window(per, text_rows)
  local span = per * (text_rows + 4)
  local row_of_pos = math.floor((pos - 1) / per)
  local first = math.max(1, (row_of_pos - 2) * per + 1)
  if first > size then first = math.max(1, size) end
  local want = math.min(span, size - first + 1)
  win.base = first
  win.text = ''
  if want > 0 then win.text = fs.read_at(path, first - 1, want) or '' end
end

local function byte_at(i)
  if i < 1 or i > size then return nil end
  if whole then return data:byte(i) end
  local put = dirty[i]
  if put ~= nil then return put end
  local at = i - win.base + 1
  if at < 1 or at > #win.text then
    -- Una fila que la finestra no cobreix (no ha de passar si es carrega bé): es demana el byte sol.
    local one = fs.read_at(path, i - 1, 1)
    if one == nil or #one == 0 then return nil end
    return one:byte(1)
  end
  return win.text:byte(at)
end

local function set_byte(i, value)
  local before = byte_at(i)
  if whole then
    data = data:sub(1, i - 1) .. string.char(value) .. data:sub(i + 1)
  else
    dirty[i] = value
  end
  undo[#undo + 1] = {offset = i, before = before}
  if #undo > kUndoDepth then table.remove(undo, 1) end
  return true
end

-- ---------------------------------------------------------------------------------------------------
-- Dibuixar
-- ---------------------------------------------------------------------------------------------------

local painted = {}
local painted_status, painted_bottom = nil, nil

local function paint(row, text, w)
  if painted[row] == text then return end
  painted[row] = text
  term.move(row, 1)
  local padded = text
  if #padded < w then padded = padded .. string.rep(' ', w - #padded) end
  out(padded:sub(1, w))
end

local function row_text(offset, per)
  local parts = {string.format('%08x  ', offset)}
  for i = 0, per - 1 do
    local b = byte_at(offset + i + 1)
    parts[#parts + 1] = (b == nil) and '   ' or string.format('%02x ', b)
  end
  parts[#parts + 1] = '|'
  for i = 0, per - 1 do
    local b = byte_at(offset + i + 1)
    parts[#parts + 1] = (b == nil) and ' ' or printable(b)
  end
  parts[#parts + 1] = '|'
  return table.concat(parts)
end

local function cursor_column(per)
  return 11 + ((pos - 1) % per) * 3 + nibble + 1
end

local function status_line(w)
  local name = path or '[no name]'
  if #name > 24 then name = '...' .. name:sub(-21) end
  local head = string.format(' %s%s', name, next(dirty) ~= nil and ' [+]' or '')
  local at = (size == 0) and 0 or (pos - 1)
  local tail = string.format('0x%08x/%d B  %d%% ', at, size,
    size == 0 and 0 or math.floor(at * 100 / size))
  local gap = w - #head - #tail
  if gap < 0 then
    head = head:sub(1, math.max(0, w - #tail - 1))
    gap = math.max(0, w - #head - #tail)
  end
  return head .. string.rep(' ', gap) .. tail
end

local function render()
  local w, h = size_of()
  local per = bytes_per_row(w)
  local text_rows = h - 2
  if not whole then load_window(per, text_rows) end
  local rows = math.ceil(size / per)
  local row_of_cursor = math.floor((pos - 1) / per)
  if row_of_cursor < top then top = row_of_cursor end
  if row_of_cursor > top + text_rows - 1 then top = row_of_cursor - text_rows + 1 end
  if top < 0 then top = 0 end
  if top > math.max(0, rows - 1) then top = math.max(0, rows - 1) end

  for i = 0, text_rows - 1 do
    local r = top + i
    local text
    if r >= rows then
      text = (r == 0) and '00000000  (empty)' or ''
    else
      text = row_text(r * per, per)
    end
    paint(i + 1, text, w)
  end
  local status = status_line(w)
  if painted_status ~= status then
    painted_status = status
    term.move(h - 1, 1)
    out(status:sub(1, w))
  end
  local bottom = message
  if #bottom > w then bottom = bottom:sub(1, w) end
  if painted_bottom ~= bottom then
    painted_bottom = bottom
    term.move(h, 1)
    out(bottom .. string.rep(' ', math.max(0, w - #bottom)))
  end
  term.move(1 + (row_of_cursor - top), cursor_column(per))
end

local function repaint_all()
  painted = {}
  painted_status, painted_bottom = nil, nil
  term.clear()
  render()
end

-- ---------------------------------------------------------------------------------------------------
-- El fitxer
-- ---------------------------------------------------------------------------------------------------

local function load(name)
  local stat = fs.stat(name)
  if stat == nil then
    path, size, whole, data, dirty, undo = name, 0, true, '', {}, {}
    pos, nibble, top = 1, 0, 0
    return true, 'new file'
  end
  if stat.type ~= 'file' then return false, 'not a file' end
  path, size = name, stat.size or 0
  dirty, undo = {}, {}
  pos, nibble, top = 1, 0, 0
  whole = size <= kWhole
  if whole then
    data = fs.read(name) or ''
    size = #data
  else
    data = ''
  end
  return true
end

-- **Desar**: el fitxer sencer d'un cop si el té a la memòria, i si no els bytes que s'han escrit, cadascun al
-- seu desplaçament amb un mànec (docs/70).
local function save()
  if path == nil then return false, 'no file name' end
  if whole then
    local wrote, err = fs.write(path, data)
    if wrote == nil then
      local why = tostring(err and err.message or 'cannot write')
      if path == '/rtc' or path == '/fram' then
        why = string.format('%s keeps its size (%d bytes here)', path, #data)
      end
      return false, why
    end
    return true
  end
  if next(dirty) == nil then return true end
  local handle, err = fs.open(path, 'r+')
  if handle == nil then return false, tostring(err and err.message or 'cannot write') end
  local ok = true
  for offset, value in pairs(dirty) do
    if handle:seek('set', offset - 1) == nil then
      ok = false
      break
    end
    if handle:write(string.char(value)) == nil then
      ok = false
      break
    end
  end
  handle:close()
  if not ok then return false, 'cannot write' end
  dirty = {}
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
  if b == 12 then return 'ctrl-l' end
  if b < 32 then return 'unknown' end
  return string.char(b)
end

local function hex_value(k)
  if #k ~= 1 or not k:match('^[0-9a-fA-F]$') then return nil end
  return tonumber(k, 16)
end

-- Un nibli escrit: el de dalt o el de baix del byte, i el cursor passa al següent.
local function write_nibble(value)
  local current = byte_at(pos)
  if current == nil then
    message = 'past the end of the file: there is no byte to write'
    return
  end
  local next_value
  if nibble == 0 then
    next_value = value * 16 + (current % 16)
  else
    next_value = (current - current % 16) + value
  end
  set_byte(pos, next_value)
  if nibble == 0 then
    nibble = 1
  else
    nibble = 0
    if pos < size then pos = pos + 1 end
  end
end

-- Esborrar i inserir mouen el que hi ha darrere: només es fa quan el fitxer és a la memòria.
local function structural_ok()
  if whole then return true end
  message = string.format('%d B: too big to move the bytes (the ceiling is %d); x and i need it whole',
    size, kWhole)
  return false
end

local function delete_byte()
  if not structural_ok() or pos > size then return end
  local before = data
  data = data:sub(1, pos - 1) .. data:sub(pos + 1)
  size = #data
  undo[#undo + 1] = {whole = before}
  if #undo > kUndoDepth then table.remove(undo, 1) end
  if pos > size and size > 0 then pos = size end
  nibble = 0
  message = string.format('byte deleted, %d B left', size)
end

local function insert_byte()
  if not structural_ok() then return end
  local before = data
  if pos > size then
    data = data .. '\0'
  else
    data = data:sub(1, pos - 1) .. '\0' .. data:sub(pos)
  end
  size = #data
  undo[#undo + 1] = {whole = before}
  if #undo > kUndoDepth then table.remove(undo, 1) end
  nibble = 0
  message = string.format('byte inserted, %d B', size)
end

local function undo_once()
  local entry = table.remove(undo)
  if entry == nil then
    message = 'already at the oldest change'
    return
  end
  if entry.whole ~= nil then
    data = entry.whole
    size = #data
    if pos > size then pos = math.max(1, size) end
  else
    local now = byte_at(entry.offset)
    set_byte(entry.offset, entry.before)
    table.remove(undo)  -- `set_byte` hi afegeix la desfeta, que no és una acció nova
    if pos ~= entry.offset then pos = entry.offset end
  end
  message = 'undo'
end

-- ---------------------------------------------------------------------------------------------------
-- Les ordres
-- ---------------------------------------------------------------------------------------------------

local quitting = false

local function goto_offset(text)
  local value
  if text:sub(1, 2) == '0x' then
    value = tonumber(text:sub(3), 16)
  else
    value = tonumber(text)
  end
  if value == nil then
    message = 'not an offset: ' .. text
    return
  end
  if size == 0 then
    pos = 1
  else
    pos = math.max(1, math.min(value + 1, size))
  end
  nibble = 0
end

local function command(text)
  if text == '' then return end
  if text:match('^%d+$') or text:match('^0x%x+$') then
    goto_offset(text)
    return
  end
  if text == 'q' then
    if next(dirty) ~= nil then
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
    local ok, err = save()
    message = ok and string.format('"%s" %d bytes', path, size) or ('write failed: ' .. tostring(err))
    return
  end
  if text == 'wq' or text == 'x' then
    local ok, err = save()
    if not ok then
      message = 'write failed: ' .. tostring(err)
      return
    end
    quitting = true
    return
  end
  if text == 'e!' then
    local ok, err = load(path)
    message = ok and 'reloaded' or tostring(err)
    repaint_all()
    return
  end
  local named = text:match('^e (.-)$')
  if named ~= nil then
    if next(dirty) ~= nil then
      message = 'no write since last change: :w first'
      return
    end
    local ok, err = load(named)
    if not ok then
      message = 'cannot open: ' .. tostring(err)
      return
    end
    message = '"' .. named .. '"'
    repaint_all()
    return
  end
  message = 'not an editor command: ' .. text
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
  if not ok then message = tostring(why) end
  if ok and why ~= nil then message = why end
else
  message = 'no file: :e <path> to open one'
end

local prompt = nil
local prompt_text = ''

while not quitting do
  render()
  local k = read_key()
  local w, h = size_of()
  local per = bytes_per_row(w)
  local page = per * (h - 2)

  if prompt ~= nil then
    if k == 'esc' then
      prompt, prompt_text = nil, ''
    elseif k == 'enter' then
      local text = prompt_text
      prompt, prompt_text = nil, ''
      command(text)
    elseif k == 'backspace' then
      prompt_text = prompt_text:sub(1, -2)
    elseif #k == 1 then
      prompt_text = prompt_text .. k
    end
  elseif k == ':' then
    prompt, prompt_text = ':', ''
  elseif hex_value(k) ~= nil then
    write_nibble(hex_value(k))
  elseif k == 'left' or k == 'h' then
    if nibble == 1 then
      nibble = 0
    elseif pos > 1 then
      pos = pos - 1
      nibble = 1
    end
  elseif k == 'right' or k == 'l' then
    if nibble == 0 then
      nibble = 1
    elseif pos < size then
      pos = pos + 1
      nibble = 0
    end
  elseif k == 'up' or k == 'k' then
    if pos > per then pos = pos - per end
  elseif k == 'down' or k == 'j' then
    if pos + per <= size then pos = pos + per end
  elseif k == 'home' or k == '0' then
    pos = pos - ((pos - 1) % per)
    nibble = 0
  elseif k == 'end' or k == '$' then
    pos = math.min(math.max(1, size), pos - ((pos - 1) % per) + per - 1)
    nibble = 0
  elseif k == 'pgup' then
    pos = math.max(1, pos - page)
  elseif k == 'pgdn' then
    pos = math.min(math.max(1, size), pos + page)
  elseif k == 'G' then
    pos = math.max(1, size)
    nibble = 0
  elseif k == 'x' or k == 'delete' then
    delete_byte()
  elseif k == 'i' then
    insert_byte()
  elseif k == 'u' then
    undo_once()
  elseif k == 'ctrl-l' then
    repaint_all()
  elseif k == 'ctrl-c' then
    message = ''
  end
end

term.clear()
term.show_cursor()
term.move(1, 1)
console.raw(false)
