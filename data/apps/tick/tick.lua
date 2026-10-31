-- Tick: two waits and three lines, with the time each wait really took.
-- If the kernel stopped while waiting, these numbers would not add up.
console.print("tick")

local t0 = sys.time_ms()
sys.delay(500):await()
console.print("tock after " .. (sys.time_ms() - t0) .. " ms")

t0 = sys.time_ms()
sys.delay(1000):await()
console.print("tick after " .. (sys.time_ms() - t0) .. " ms")
