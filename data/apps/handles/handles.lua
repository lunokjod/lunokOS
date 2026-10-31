-- Shows the open-file side of docs/30 and docs/70 by hand:
--   run handles
-- It writes a file through a handle, reads it back three ways, seeks, and then
-- holds eight handles to show that the ninth says E_NO_HANDLES.

local newline = string.char(10)
local ground = fs.data()
fs.mkdir(ground)
local path = fs.data('handles.txt')

local out = fs.open(path, 'w')
console.print('opened for writing: ' .. tostring(out) .. ' size ' .. out:size())
console.print('wrote ' .. out:write('one' .. newline) .. ' bytes')
out:write('two' .. newline)
out:close()
console.print('closed: ' .. tostring(out))

local f = fs.open(path, 'r')
console.print('size ' .. f:size())
console.print('line ' .. f:read('*l'))
console.print('seek back to ' .. f:seek('set', 0))
console.print('rest ' .. #f:read('*a') .. ' bytes, at ' .. f:seek('cur', 0))
f:close()

local held = {}
for i = 1, 8 do
  held[i] = fs.open(path, 'r')
end
console.print('holding ' .. #held .. ' handles')
local ninth, err = fs.open(path, 'r')
console.print('ninth ' .. tostring(ninth) .. ' ' .. tostring(err and err.code))
for i = 1, 8 do held[i]:close() end

local again = fs.open(path, 'r')
console.print('after closing all: ' .. tostring(again))
again:close()

-- A file that is not there is E_NOENT, and it is said before the backend is
-- asked to open it: the ESP32 core logs an error for that on the board.
local missing, err_gone = fs.open(fs.data('no-hi-es.txt'), 'r')
console.print('missing ' .. tostring(missing) .. ' ' .. tostring(err_gone and err_gone.code))
