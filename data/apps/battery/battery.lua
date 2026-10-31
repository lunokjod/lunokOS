-- The battery, from the console (docs/30, docs/50):
--   run battery
--
-- Two questions that look like one and are not: whether the board can say anything
-- (`power.caps().battery`) and what it says (`power.battery()`). A board with no fuel
-- gauge answers `nil`, and `nil` is **not** an empty battery — a board that cannot say is
-- a different thing from a board that says zero, and this app prints the difference.
--
-- The millivolts are a measurement and the percentage a curve: 3,7 V is nearer 40 % than
-- the 45 % a straight line would say, because a cell keeps most of its charge in its last
-- volt. The bar is only for the eye (docs/50).

local caps = power.caps()
if not caps.battery then
  console.print('this board does not know its battery (not the same as 0 V)')
  return
end

local b = power.battery()
if not b then
  -- The capability says yes and the reading says no: that is a divided pin that is not
  -- answering, and saying so is the whole point of asking twice.
  console.print('the board has a battery and the reading failed')
  return
end

local filled = math.floor(b.pct * 20 / 100 + 0.5)
console.print(string.format('%d mV, %d %%, [%s%s]',
                            b.mv, b.pct,
                            string.rep('#', filled),
                            string.rep('.', 20 - filled)))

-- The charging state is only true when the board has a pin to ask, and neither the Paper
-- nor the bench has one: this says what it can say and not a guess (docs/30).
if b.charging then
  console.print('on the cable')
else
  console.print('charging: the board cannot tell')
end
