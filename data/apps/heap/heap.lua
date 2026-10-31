-- Heap: asks for a megabyte and gives it back, so the memory of a board can be watched working
-- (docs/75, docs/95).
--   run heap
--
-- The number lives here and not in an argument, because what this app is for is one question: does
-- the board hold a big allocation and give it back? Change the number to ask for more.
--
-- On the Paper the block goes to the **PSRAM** and on an ATOM the same script dies at its ceiling,
-- which is the whole point of the extra memory and the reason this is a demo and not a test: the
-- interpreter decides where a block lives and the app only asks (docs/75).

local entries = 60000  -- about a megabyte: sixteen bytes a slot on this interpreter

local function show(label)
  local h = sys.heap()
  console.print(label .. ': used ' .. h.used .. ' B, peak ' .. h.peak .. ' B, limit ' .. h.limit
    .. ' B, extra free ' .. h.extra_free .. ' B')
end

show('before')
console.print('asking for ' .. entries .. ' entries')
local t = {}
for i = 1, entries do t[i] = i end
show('holding ' .. #t)

-- Let it go and ask the collector for the memory: what comes back is the proof that the block was
-- the app's and not the board's (docs/75).
t = nil
sys.gc('full')
show('after dropping it')
