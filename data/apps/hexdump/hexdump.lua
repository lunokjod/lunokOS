-- Hexdump: the bytes of a memory or a file, as hex and as visible text (docs/30, docs/70).
--   run hexdump                the RTC memory, which is the only one that survives a deep sleep
--   run hexdump /fram          the FRAM, which is the memory that does not need the power
--   run hexdump /sd/photo.bin  any other file, as long as the app may read it (docs/70)
--   open hexdump <path>        the same, through the selector
--
-- The path arrives as `app.args.path` in every case, which is why the command takes one.
--
-- Two columns, like the dump a person already knows: eight digits of offset, sixteen bytes in hex,
-- and the same sixteen between bars. A byte that cannot be shown on a terminal is a dot, because
-- printing a control character as itself is how a dump loses the rest of the line.
--
-- The empty tail is not printed: a memory is mostly zeros and the zeros teach nothing. The first
-- line says how big the memory is and how much of it is in use, which is the number a person wants
-- when the question was "is anything kept there at all?" (docs/50).

local hex_digits = '0123456789abcdef'

local function visible(byte)
  if byte >= 32 and byte <= 126 then
    return string.char(byte)
  end
  return '.'
end

local function hex_row(offset, data)
  local cells = {}
  local shown = {}
  for i = 1, 16 do
    local byte = string.byte(data, i)
    if byte then
      cells[i] = string.sub(hex_digits, math.floor(byte / 16) + 1, math.floor(byte / 16) + 1)
        .. string.sub(hex_digits, byte % 16 + 1, byte % 16 + 1)
      shown[i] = visible(byte)
    else
      cells[i] = '  '  -- a short last row stays aligned with the ones above it
      shown[i] = ' '
    end
  end
  return string.format('%08x', offset) .. '  ' .. table.concat(cells, ' ') .. '  |'
    .. table.concat(shown) .. '|'
end

local path = '/rtc'
if app.args and app.args.path then
  path = app.args.path
end

local f, err = fs.open(path, 'r')
if not f then
  console.print('hexdump: ' .. path .. ' ' .. tostring(err and err.code))
  return
end

local size = f:size()
local chunk = 256
local at = 0
local last = -1  -- where the last byte that is not zero lives
while at < size do
  local data = f:read(chunk)
  if not data or #data == 0 then break end
  for i = 1, #data do
    if string.byte(data, i) ~= 0 then last = at + i - 1 end
  end
  at = at + #data
end

if last < 0 then
  console.print('hexdump: ' .. path .. ' is ' .. size .. ' B and every one of them is zero')
  f:close()
  return
end

console.print('hexdump: ' .. path .. ' is ' .. size .. ' B, ' .. (last + 1) .. ' in use and '
  .. (size - last - 1) .. ' zero after')
if f:seek('set', 0) == nil then
  console.print('hexdump: cannot go back to the start')
  f:close()
  return
end

at = 0
while at <= last do
  local data = f:read(16)
  if not data or #data == 0 then break end
  console.print(hex_row(at, data))
  at = at + #data
end
f:close()
