-- An app that says goodbye, so the hooks of docs/40 can be seen by hand:
--   run polite
--   then three ESC in a row (or a line that says !!exit)
--
-- The three functions are optional in every app; this one declares all of them so
-- the order in which the system calls them is visible. It waits ten minutes: it
-- never ends on its own during the test.

console.print('polite: here. ESC ESC ESC to close me.')

function on_pause()
  console.print('polite: on_pause')
end

function on_save()
  console.print('polite: on_save')
end

function on_close()
  console.print('polite: on_close')
end

sys.delay(600000):await()
console.print('polite: nobody closed me')
