-- Burst: a hundred waits in a row, one millisecond each.
-- It is the measurement docs/90 asks for: what a hundred promises cost. The heap
-- numbers are the app's own budget, read from inside.
local before = sys.heap()
local started = sys.time_ms()

for i = 1, 100 do
  sys.delay(1):await()
end

local h = sys.heap()
console.print("100 waits in " .. (sys.time_ms() - started) .. " ms")
console.print("heap used " .. h.used .. " B, peak " .. h.peak .. " B, limit " .. h.limit .. " B")
console.print("before: used " .. before.used .. " B, peak " .. before.peak .. " B")
