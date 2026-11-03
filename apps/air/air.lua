-- air: how warm and how wet the room is, once every two seconds.
-- It is the example of `env.read()` (docs/30).
local caps = sys.caps()
if not caps.env then
  console.print('this board has no ambient sensor')
  return
end

while true do
  local air, err = env.read()
  if air then
    console.print(string.format('air %.1f C %.1f %%', air.celsius, air.humidity))
  else
    -- A sensor that is there and does not answer is not the same as no sensor at all, and the
    -- code says which one it is (docs/30).
    console.print('the sensor did not answer: ' .. tostring(err and err.code))
  end
  sys.delay(2000):await()
end
