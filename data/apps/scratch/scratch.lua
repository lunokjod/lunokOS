-- Writes, reads back and lists its own ground, so the writing side of docs/70 can
-- be seen by hand:
--   run scratch
-- It ends by trying to write outside its ground, which has to say E_INVAL.

local ground = fs.data()
console.print('my ground is ' .. ground)
fs.mkdir(ground)

local path = fs.data('notes.txt')
local started = sys.time_ms()
local written = fs.write(path, 'hola des de scratch\n')
console.print('wrote ' .. written .. ' bytes in ' .. (sys.time_ms() - started) .. ' ms')

console.print('back: ' .. fs.read(path))

for _, entry in ipairs(fs.list(ground)) do
  console.print('  ' .. entry.type .. ' ' .. entry.name .. ' ' .. entry.size)
end

local ok, err = fs.write('/data/other/x.txt', 'no')
console.print('outside: ' .. tostring(ok) .. ' ' .. tostring(err and err.code))
