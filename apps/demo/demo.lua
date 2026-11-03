-- Demo: the three things an app can do with the console here: write with
-- colours, wait without stopping the system, and read a line from the keyboard.
-- Colours are asked for; on a console that does not do ANSI they cost nothing.
local caps = sys.caps()
console.print("board " .. sys.board .. (caps.ansi and " with colours" or " without ANSI"))

if caps.ansi then
  term.color(term.BOLD)
  term.color(term.CYAN)
end
console.print("waiting a moment...")
if caps.ansi then
  term.reset()
end

sys.delay(300):await()

if caps.ansi then
  term.color(term.GREEN)
end
console.print("ready")
if caps.ansi then
  term.reset()
end

console.print("write something and press Enter")
local answer = console.read_line():await()
console.print("you said: " .. answer)
