-- glass: what the touch panel has under the finger, in a loop, and the shapes the system makes out
-- of it. It is the example of `touch.read()` and `touch.gesture()` (docs/30).
--
-- The points are the frame of the kernel's last turn (twenty milliseconds at most) and not a fresh
-- read of the chip: the glass gives a frame once and whoever takes it clears it, so a second reader
-- would find nobody down while a finger is still there (docs/30, docs/50).
local caps = sys.caps()
if not caps.touch then
  console.print('this board has no touch panel')
  return
end

local glass = touch.caps()
console.print(string.format('glass %d x %d, up to %d fingers', glass.width, glass.height,
  glass.points))
console.print('touch it: a tap, a double tap, a triple, a long press, a drag with one finger or two,')
console.print('a pinch. Ctrl+C or three taps of the power key to leave.')

-- What was down in the previous turn, so the loop says a finger once and not forty times a second.
local was_down = false

while true do
  local fingers = touch.read()
  if #fingers > 0 then
    -- A line per frame while somebody is touching, with every finger and its area. The area is not a
    -- pressure: it is how much of the glass the chip measures under the finger, and a thumb and a
    -- fingernail give two numbers (docs/50).
    local parts = {}
    for i, finger in ipairs(fingers) do
      parts[i] = string.format('#%d %d,%d s%d', finger.id, finger.x, finger.y, finger.size)
    end
    console.print(table.concat(parts, '  '))
    was_down = true
  elseif was_down then
    console.print('nobody')
    was_down = false
  end

  -- The gestures are taken once each: asking again does not repeat them, which is what lets the loop
  -- print them here without the app having to remember what it already said (docs/30).
  local gesture = touch.gesture()
  while gesture do
    if gesture.kind == 'pinch' then
      -- How much the two fingers came apart (positive) or together (negative), and where the middle
      -- of the pair was and went. The chip does not say this: it is arithmetic over two points.
      console.print(string.format('pinch around %d,%d: %+d px, middle went %+d,%+d in %d ms',
        gesture.x, gesture.y, gesture.spread, gesture.dx, gesture.dy, gesture.held_ms))
    elseif gesture.kind == 'drag' then
      -- A drag is one finger or two, and `fingers` says which: two of them moving together is a pan
      -- and it comes with how much they spread, which for a pan is close to zero.
      console.print(string.format('%s with %d finger(s) from %d,%d went %d,%d in %d ms (spread %+d)',
        gesture.kind, gesture.fingers, gesture.x, gesture.y, gesture.dx, gesture.dy,
        gesture.held_ms, gesture.spread))
    elseif gesture.kind == 'two' then
      console.print(string.format('%s fingers at %d,%d', gesture.fingers, gesture.x, gesture.y))
    else
      console.print(string.format('%s at %d,%d after %d ms', gesture.kind, gesture.x, gesture.y,
        gesture.held_ms))
    end
    gesture = touch.gesture()
  end

  -- Fifty milliseconds: the kernel reads the glass every turn, so a loop any faster than this only
  -- prints the same frame twice (docs/30).
  sys.delay(50):await()
end
