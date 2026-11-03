-- glow: the four effects of the light, one after the other, and a fade on the way out.
-- It is the example of `led.play`, `led.effect` and `led.stop` (docs/30).
--
-- Every wait is a promise, so the light keeps being painted between turns: an effect is
-- not a loop of the app and the app does not stop while it is playing.
local caps = led.caps()
if caps.count == 0 then
  console.print('this board has no light')
  return
end

local list = {
  {kind = 'breathe', color = {0, 0, 200}, period = 3000, cycles = 2, level = 120},
  {kind = 'blink', color = {200, 0, 0}, period = 600, cycles = 4, level = 150},
  {kind = 'sos', color = {200, 0, 0}, period = 200, cycles = 1, level = 200},
  {kind = 'bounce', color = {0, 160, 60}, period = 500, cycles = 2, level = 200},
}

for index = 1, #list do
  local effect = list[index]
  led.play(effect)
  local now = led.effect()
  console.print(now.kind .. ': ' .. now.cycles .. ' cycles of ' .. now.period .. ' ms, ' ..
                  'on up to ' .. now.level)
  -- The effect runs on the kernel's turns: waiting here does not stop it, and the wait is
  -- the length of the effect so the next one starts when this one has said its piece.
  sys.delay(now.period * now.cycles + 100):await()
end

-- On a board with a strip, one light and the rest dark: the light of the panel is a
-- position, counted from zero (docs/30).
if caps.count > 1 then
  led.play{kind = 'breathe', color = {0, 120, 255}, period = 2000, cycles = 2, pixel = 2}
  console.print('one light of the strip, number 2')
  sys.delay(4200):await()
end

-- Out with a fade: a light that goes off in one frame is a slap in the dark.
led.stop(800)
console.print('faded out')
