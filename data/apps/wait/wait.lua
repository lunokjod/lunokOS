-- Wait: two lines with a pause in between, and how long the pause really was.
-- While it waits, the system keeps turning: this is what sys.delay() is for.
console.print("waiting")
local t0 = sys.time_ms()
sys.delay(1000):await()
console.print("done after " .. (sys.time_ms() - t0) .. " ms")
