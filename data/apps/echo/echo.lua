-- Echo: asks for a line and says it back.
-- While this app runs the keyboard is its own: the shell sees none of what is
-- typed. Enter ends the line. Three ESC in a row, or !!exit when the app is not
-- reading, close it (docs/30).
console.print("write something and press Enter")
local line = console.read_line():await()
console.print("you said: " .. (line or "(nothing)"))
