-- Ping: answers every line with "pong" and how long it was.
-- The board side of tools/serial_ping.py, which measures the round trip of the
-- port (docs/80). It closes with three ESC like any other app.
console.print("ping")
while true do
  local line = console.read_line():await()
  console.print("pong " .. #line)
end
