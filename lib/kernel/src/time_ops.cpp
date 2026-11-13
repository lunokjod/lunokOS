// **The hour of the person, handed to the interpreter** (0.125.0).
//
// `hal/civil.h` turns an epoch into a date and `kernel/calendar.h` says which weekday that date is, and both live once
// in this system. What the kernel adds here is the one number that is a setting —how many minutes the local hour is
// ahead of UTC— so that `time.local()` says what a person's watch says and not what the quartz counts, and so that
// `time.tz()` from Lua and `cfg tz +01:00` from the prompt write the same value (docs/30, docs/60).
#include "kernel/cfg.h"
#include "kernel/calendar.h"
#include "kernel/fs_ops.h"

namespace kernel {
namespace {

int32_t tz_minutes() { return cfg::tz_minutes(); }

bool set_tz_minutes(int32_t minutes) { return cfg::set_tz_minutes(minutes); }

bool parse_tz(const char* text, int32_t& minutes) { return cfg::parse_tz(text, minutes); }

uint8_t weekday_of(uint16_t year, uint8_t month, uint8_t day) {
  return calendar::weekday_of(year, month, day);
}

bool leap_years() { return cfg::leap_years(); }

bool dst() { return cfg::dst(); }
bool set_dst(bool on) { return cfg::set_dst(on); }
int32_t offset_minutes() { return cfg::offset_minutes(); }

bool set_leap_years(bool on) { return cfg::set_leap_years(on); }

uint8_t week_start() { return cfg::week_start(); }

bool set_week_start(uint8_t first) { return cfg::set_week_start(first); }

}  // namespace

void install_time_ops() {
  luabind::TimeOps ops;
  ops.tz_minutes = &tz_minutes;
  ops.set_tz_minutes = &set_tz_minutes;
  ops.parse_tz = &parse_tz;
  ops.weekday_of = &weekday_of;
  // **L'hora d'estiu** (0.182.0): the box of the Time zone page is a setting of the system and the hour of a person is the
  // offset of the map plus sixty minutes when it is on (docs/30, docs/60).
  ops.dst = &dst;
  ops.set_dst = &set_dst;
  ops.offset_minutes = &offset_minutes;
  ops.leap_years = &leap_years;
  ops.set_leap_years = &set_leap_years;
  ops.week_start = &week_start;
  ops.set_week_start = &set_week_start;
  luabind::set_time_ops(ops);
}

}  // namespace kernel
