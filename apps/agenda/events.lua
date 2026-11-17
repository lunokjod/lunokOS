-- agenda :: events.lua — ONE EVENT PER LINE, AND THIS FILE IS THE ONLY ONE THAT KNOWS WHAT A LINE IS (0.189.171).
--
-- WHAT IS HERE AND WHAT IS NOT: no screen, no control and no promise. This module parses the file, writes it back,
-- says whether an event falls on a day and says when the next one is. El mes, els botons i les alarmes viuen a
-- `agenda.lua`, que és el fitxer que els té.
--
-- **LA LÍNIA, I EL FORMAT ÉS TEXT A POSTA** (es llegeix amb `cat` i es pot editar a mà):
--
--      2026-10-08|13:30|W|Lunch with Marta|table by the window
--      2026-10-08|13:30-14:30|W|Lunch, the long one|
--      2026-10-09|--:--|-|Camping day|
--      \________/ \_________/ | \______________/ \____________________/
--         the day     the    |    what it is            the notes
--                     hour  the repeat: `-` once, `D` every day,
--                           `W` every week, `M` every month,
--                           `Q` every quarter, `Y` every year
--
-- **I EL CAMP DE L'HORA DIU TRES COSES SENSE QUE LA LÍNIA GUANYI UN CAMP**: `HH:MM` és un event amb hora,
-- `HH:MM-HH:MM` és un interval i `--:--` és un dia SENSE hora. Una línia escrita abans que això existís segueix
-- tenint cinc camps i es llegeix igual, que és el motiu pel qual l'interval viu dins de l'hora i no en un sisè camp.
--
-- **I `|` no és cap caràcter d'un títol**: és la paret entre els camps. La pantalla el refusa i diu per què, i una
-- línia que no es pot llegir ES COMPTA I ES DIU, no desapareix en silenci.
--
-- **Una repetició va cap endavant des del dia de l'event** i mai cap enrere: un event setmanal és aquell dia i cada
-- set dies DESPRÉS. Així el dia de l'event sempre és un dels seus dies.
--
-- **I un event mensual, trimestral o anual en un dia que el mes no té, no passa**: el 31 es repeteix als mesos de 31
-- dies i a cap altre, i el 29 de febrer només en un any de traspàs. L'altra tria —convertir-lo en l'últim dia del mes
-- curt— seria l'app editant una data que ningú li ha demanat.

local M = {}

-- The name of the file inside the app's own ground; WHICH GROUND IT IS IS NOT DECIDED HERE (the app asks `fs.data` for
-- the path and this module is told which one to use, perquè un mòdul de dades no tria la memòria) (docs/30, docs/70).
M.File = 'agenda.txt'

local DayMinutes = 24 * 60

-- **El sostre d'una promesa, en mil·lisegons, el diu l'app** (`alarm.caps().max`) i no aquest fitxer: aquí només
-- serveix per dir «això queda massa lluny per a una promesa» i qui en sap el número és el sistema (docs/30, docs/60).
M.MaxMs = 0

M.Once, M.Daily, M.Weekly, M.Monthly = '-', 'D', 'W', 'M'
M.Quarterly, M.Yearly = 'Q', 'Y'

--- The hour field of a day that has no hour.
M.AllDay = '--:--'

--- The six, in the order a form offers them.
M.Repeats = { M.Once, M.Daily, M.Weekly, M.Monthly, M.Quarterly, M.Yearly }

--- What each one is called on a screen.
M.RepeatName = {
  ['-'] = 'once',
  ['D'] = 'every day',
  ['W'] = 'every week',
  ['M'] = 'every month',
  ['Q'] = 'every quarter',
  ['Y'] = 'every year',
}

-- ======================================================= the calendar's maths
--
-- **ES FA EN MINUTS I NO AMB DATES**, com el `Clock`: el que el xip dona és una hora i un minut, una resta de minuts no
-- té casos rars, i el calendari només hi entra com la suma dels dies dels mesos anteriors. **I els anys de traspàs es
-- compten amb la regla dels segles**, la mateixa que la del nucli, perquè el 2100 no és de traspàs i el 2000 sí.

local MonthDays = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }

--- True if that year is a leap year, with the rule of the centuries.
local function leapYear(y)
  if y % 4 ~= 0 then return false end
  if y % 100 ~= 0 then return true end
  return y % 400 == 0
end

--- How long a month is, which is the one copy of the rule in this app.
function M.daysInMonth(y, mo)
  if mo == 2 and leapYear(y) then return 29 end
  return MonthDays[mo] or 30
end

local function leapsBefore(y)
  local function leaps(n)
    return math.floor(n / 4) - math.floor(n / 100) + math.floor(n / 400)
  end

  return leaps(y - 1) - leaps(1999)
end

--- The days from the 1st of January 2000 to that date, which is the only thing a difference of dates needs.
local function dayOfEpoch(y, mo, d)
  local days = 365 * (y - 2000) + leapsBefore(y)

  for i = 1, mo - 1 do
    days = days + M.daysInMonth(y, i)
  end

  return days + d - 1
end

--- The minutes since the 1st of January 2000, which is where the counting starts.
local function minutesOfEpoch(y, mo, d, h, m)
  return (dayOfEpoch(y, mo, d) * DayMinutes) + (h * 60) + m
end

--- True if that is a date that exists.
function M.validDate(y, mo, d)
  if not y or not mo or not d then return false end
  if mo < 1 or mo > 12 or d < 1 then return false end

  return d <= M.daysInMonth(y, mo)
end

--- The day of the week of a date: 0 = Sunday, the order the system counts in.
function M.weekdayOf(y, mo, d)
  if not M.validDate(y, mo, d) then return nil end

  -- **L'1 de gener del 2000 era dissabte**, que és 6 amb diumenge al zero: és la mateixa expressió que fa servir el
  -- rellotge del nucli, i és aquí perquè una pàgina que dibuixa un calendari des de Lua no l'ha d'escriure dues vegades.
  local days = math.floor(minutesOfEpoch(y, mo, d, 12, 0) / DayMinutes)

  return (days + 6) % 7
end

--- The date `n` days after this one (or before it, if `n` is negative).
function M.addDays(y, mo, d, n)
  local day, mon, yr = d, mo, y

  while n > 0 do
    day = day + 1

    if day > M.daysInMonth(yr, mon) then
      day = 1
      mon = mon + 1

      if mon > 12 then mon = 1; yr = yr + 1 end
    end

    n = n - 1
  end

  while n < 0 do
    day = day - 1

    if day < 1 then
      mon = mon - 1

      if mon < 1 then mon = 12; yr = yr - 1 end

      day = M.daysInMonth(yr, mon)
    end

    n = n + 1
  end

  return yr, mon, day
end

-- ======================================================== what falls on a day

--- True if that event has a day on that date.
function M.occursOn(e, y, mo, d)
  if not e or not M.validDate(y, mo, d) then return false end

  -- **El dia de l'event es compta una vegada i es queda**: es pregunta per cada dia de cada mes que es pinta, i
  -- preguntar-ho una altra vegada i una altra és la crida que va fer que l'escript del paper s'acabés el pressupost.
  if not e.startDays then
    e.startDays = dayOfEpoch(e.y, e.mo, e.d)
  end

  local this = dayOfEpoch(y, mo, d)

  -- Never before the day it was written: a repeat goes forward and not both ways.
  if this < e.startDays then return false end

  if e.rep == M.Daily then return true end
  if e.rep == M.Weekly then return (this - e.startDays) % 7 == 0 end
  if e.rep == M.Monthly then return d == e.d end
  if e.rep == M.Yearly then return d == e.d and mo == e.mo end

  -- **Un trimestre es compta en mesos i no en dies**: noranta dies no és un trimestre, i un event trimestral que queia
  -- a l'octubre ha de caure al gener.
  if e.rep == M.Quarterly then
    local months = (y - e.y) * 12 + (mo - e.mo)

    return d == e.d and months % 3 == 0
  end

  return y == e.y and mo == e.mo and d == e.d
end

--- The events of a day, in the order of the clock: `{ i = <line>, e = <event> }`.
--
-- **El número de línia viatja amb l'event** perquè la pantalla ha de poder canviar o treure EL de la fila que un dit ha
-- marcat, i una taula d'events no té cap altre nom que el seu lloc al fitxer.
--
-- **I un dia sense hora va primer**, com a tot calendari: ocupa el dia sencer, i -1 és la clau d'ordenació que diu
-- «abans de les 00:00» sense inventar-se una hora que no té.
function M.ofDay(list, y, mo, d)
  local out = {}

  for i = 1, #list do
    if M.occursOn(list[i], y, mo, d) then
      out[#out + 1] = { i = i, e = list[i] }
    end
  end

  table.sort(out, function(a, b)
    local ka = a.e.h and (a.e.h * 60 + a.e.m) or -1
    local kb = b.e.h and (b.e.h * 60 + b.e.m) or -1

    return ka < kb
  end)

  return out
end

--- The days of a month that have something, which is what a calendar marks.
function M.daysWithSomething(list, y, mo)
  local days = {}

  for d = 1, M.daysInMonth(y, mo) do
    for i = 1, #list do
      if M.occursOn(list[i], y, mo, d) then
        days[#days + 1] = d
        break
      end
    end
  end

  return days
end

-- ============================================================== the next one

--- The first date on or after `y-mo-d` on which the event happens, or nil.
--
-- **ES COMPTA EN MESOS I NO CAMINANT DIES**, i és un parany que aquest fitxer ja ha pagat una vegada: la primera versió
-- caminava dia a dia buscant una repetició, i seixanta-dos dies es van convertir en milers de crides i l'escript es va
-- quedar sense pressupost amb mitja pantalla pintada. Els mesos candidats surten del mes de l'event, saltant pel pas de
-- la repetició, i els mesos que no tenen aquell dia es salten sense demanar cap data.
local function nextDate(e, y, mo, d)
  if e.rep == M.Once then return e.y, e.mo, e.d end
  if e.rep == M.Daily then return y, mo, d end

  if e.rep == M.Weekly then
    local today = M.weekdayOf(y, mo, d)
    local first = M.weekdayOf(e.y, e.mo, e.d)

    if not today or not first then return nil end

    return M.addDays(y, mo, d, (first - today) % 7)
  end

  -- **Mensual, trimestral i anual: el dia de l'event i els mesos que són a la seva reixa.** Cinc intents cobreixen el
  -- pitjor cas, que és un 29 de febrer que ha d'arribar a un any de traspàs.
  local step = 1

  if e.rep == M.Quarterly then step = 3 elseif e.rep == M.Yearly then step = 12 end

  local gap = (y - e.y) * 12 + (mo - e.mo)

  if gap < 0 then gap = 0 end

  local n = math.floor(gap / step)

  for _ = 1, 5 do
    local total = (e.y * 12 + (e.mo - 1)) + n * step
    local cy = math.floor(total / 12)
    local cm = total % 12 + 1

    if (cy > y) or (cy == y and cm >= mo) then
      if e.d <= M.daysInMonth(cy, cm) then
        if (cy > y) or (cm > mo) or (e.d >= d) then return cy, cm, e.d end
      end
    end

    n = n + 1
  end

  return nil
end

--- How long until the next time that event happens, in ms, or nil.
--
-- **El segon valor diu si alguna cosa era massa lluny** (`true`), i no és un detall: un nil amb «massa lluny» és el que
-- fa que l'app aguanti una promesa per etapes, i un nil sense això —un event sense hora, o un que ja ha passat— no ha
-- d'armar res de res.
--
-- **Un event sense hora no promet res**: no té moment, i un avís a mitjanit seria un despertar a una hora que ningú ha triat.
function M.msToNext(e, now)
  if not e or not now or not now.year or not now.hour then return nil end
  if not e.h or not e.m then return nil end

  local y, mo, d = nextDate(e, now.year, now.month, now.day)

  if not y then return nil end

  local here = minutesOfEpoch(now.year, now.month, now.day, now.hour, now.min)
  local delta = minutesOfEpoch(y, mo, d, e.h, e.m) - here

  -- **Zero no és «ara»**: el que dona el xip és l'hora i el minut i no el segon, o sigui que un delta de zero vol dir
  -- «aquest mateix minut». Una promesa armada a zero tornaria a sonar en el moment d'armar-se, i l'hora d'avui que ja ha
  -- passat s'envia al pròxim dia de la repetició.
  if delta <= 0 then
    if e.rep == M.Once then return nil end

    y, mo, d = M.addDays(now.year, now.month, now.day, 1)
    y, mo, d = nextDate(e, y, mo, d)

    if not y then return nil end

    delta = minutesOfEpoch(y, mo, d, e.h, e.m) - here
  end

  if delta <= 0 then return nil end
  if M.MaxMs > 0 and delta * 60000 > M.MaxMs then return nil, true end

  return delta * 60000
end

--- Which event is the next one, how long is left of it, and whether some event was out of the reach of one promise.
function M.soonest(list, now)
  local best, bestMs, tooFar = nil, nil, false

  for i = 1, #list do
    local ms, far = M.msToNext(list[i], now)

    if far then tooFar = true end
    if ms and (not bestMs or ms < bestMs) then
      best, bestMs = i, ms
    end
  end

  return best, bestMs, tooFar
end

-- ================================================================ the file

--- A row of the list of a day: the hour and what it is. **Curt a posta** —una fila d'aquest sistema es talla i no es
-- parteix— i la repetició no hi és: la línia de sota de la llista la diu quan un dit marca la fila.
--
-- **Tres formes i no una**: `all day` pel dia que no té hora, `08:00-09:00` per a l'interval, i `13:00` per a l'hora
-- que és un moment i no un tros.
function M.rowOf(e)
  if not e.h then return 'all day  ' .. e.title end
  if e.eh then
    return string.format('%02d:%02d-%02d:%02d  %s', e.h, e.m, e.eh, e.em, e.title)
  end

  return string.format('%02d:%02d  %s', e.h, e.m, e.title)
end

--- The hour field of a line: the three shapes in one place, perquè la pantalla l'haja d'escriure i el fitxer de
-- llegir-lo de la mateixa manera.
function M.timeOf(e)
  if not e.h then return M.AllDay end
  if e.eh then
    return string.format('%02d:%02d-%02d:%02d', e.h, e.m, e.eh, e.em)
  end

  return string.format('%02d:%02d', e.h, e.m)
end

--- One line of the file, as text.
function M.lineOf(e)
  return string.format('%04d-%02d-%02d|%s|%s|%s|%s', e.y, e.mo, e.d, M.timeOf(e), e.rep, e.title,
                       e.note or '')
end

--- The whole list, as the file holds it.
function M.textOf(list)
  local out = {}

  for i = 1, #list do
    out[#out + 1] = M.lineOf(list[i])
  end

  return table.concat(out, '\n') .. (#out > 0 and '\n' or '')
end

--- What a line of the file means, or nothing.
--
-- **El camp de l'hora pot dir tres coses**, i una línia que en diu una altra es compta com a illegible i es diu:
-- `HH:MM`, l'interval `HH:MM-HH:MM`, i `--:--` pel dia que no té cap hora.
function M.eventOf(line)
  local y, mo, d, when, rep, title, note =
    string.match(line, '^(%d%d%d%d)%-(%d%d)%-(%d%d)|([%d:%-]+)|([%-DWMTQY])|([^|]*)|(.*)$')

  if not y then return nil end

  local e = {
    y = tonumber(y), mo = tonumber(mo), d = tonumber(d),
    rep = rep,
    title = title,
    note = note or '',
  }

  if not M.validDate(e.y, e.mo, e.d) then return nil end
  if title == '' then return nil end

  if when ~= M.AllDay then
    local h, m = string.match(when, '^(%d%d):(%d%d)$')
    local eh, em

    if not h then
      h, m, eh, em = string.match(when, '^(%d%d):(%d%d)%-(%d%d):(%d%d)$')
    end

    if not h then return nil end

    e.h, e.m = tonumber(h), tonumber(m)

    if e.h > 23 or e.m > 59 then return nil end

    if eh then
      e.eh, e.em = tonumber(eh), tonumber(em)

      -- **Un final que no és després del començament no és cap interval**: la línia no és un event, i dir-ho és millor
      -- que dibuixar un event de zero minuts que la persona no ha escrit.
      if e.eh > 23 or e.em > 59 then return nil end
      if e.eh * 60 + e.em <= e.h * 60 + e.m then return nil end
    end
  end

  return e
end

--- The list and the number of lines that could not be read.
--
-- **Una línia que no és un event es compta i no s'empassa**: el fitxer és text i algú el pot editar a mà, i una llista
-- que queda curta en silenci és un defecte.
function M.parse(text)
  local list, bad = {}, 0

  for line in string.gmatch(text or '', '[^\n]+') do
    local e = M.eventOf(line)

    if e then
      list[#list + 1] = e
    else
      bad = bad + 1
    end
  end

  return list, bad
end

--- The list, read from the file of the app.
--
-- **Aquí el fitxer es llegeix sencer i no a trossos** (docs/30): `fs.read` llegeix qualsevol mida en finestres de mig
-- kilobyte i el que queda com a límit és la memòria de l'intèrpret, o sigui que la caminada de quatre kilobytes a
-- quatre kilobytes del projecte del paper no cal aquí.
function M.load(path)
  local text, why = fs.read(path)

  if text == nil then
    -- **Un fitxer que no hi és no és un error**: la primera vegada que s'obre una agenda no hi ha res, i això és una
    -- agenda buida i no una fallada.
    if why == nil or why.code == 'E_NOENT' then return {}, 0 end

    return nil, why.code or why
  end

  return M.parse(text)
end

--- The list, written back. **El fitxer sencer**, perquè un canvi pot ser a qualsevol lloc.
--
-- I `fs.write` és **atòmica** —escriu un temporal i el reanomena—, o sigui que una escriptura que mor a mitges no deixa
-- mitja agenda sota el nom de la sencera (docs/30, docs/70).
function M.save(path, list)
  local written, why = fs.write(path, M.textOf(list))

  if type(written) ~= 'number' then return false, (why and why.code) or why or 'no room' end

  return true
end

return M
