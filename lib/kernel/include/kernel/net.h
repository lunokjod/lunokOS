// The network, as the kernel runs it: one attempt at a time, never a wait.
//
// The radio belongs to the board (hal::INet) and the deadline belongs here: the
// kernel asks how the association is going in every turn and gives the app its
// answer as a promise (docs/30, docs/50).
#ifndef LUNOK_KERNEL_NET_H
#define LUNOK_KERNEL_NET_H

#include <stdint.h>

#include "hal/hal.h"
#include "luabind/luabind.h"

namespace kernel {
namespace net {

// Remembers the board's network (nullptr when it has none) and installs the
// operations the interpreter calls.
void boot(hal::IPlatform& platform);

// True when this board has a radio at all.
bool available();

// Hands the network to the board from outside: the access point goes up and the
// person gives it the name and the password with the Espressif app. When the
// board has them, the association is started with what it stored, so the answer
// means what a person expects: **on the network**. False when nothing started,
// and then nothing will be answered (docs/50).
bool provision(const char* pop, const char* service_name, hal::ProvTransport transport,
                uint32_t timeout_ms);

// True when the board already knows a network.
bool provisioned();

// Forgets the stored network.
bool forget();

// Drops whatever is going on: the app that asked is gone and nobody wants the
// answer any more (docs/30).
void cancel_public();

// Called once per turn by the kernel: watches the state, enforces the deadline
// and hands the answer to whoever asked (docs/30).
void poll();

// A command of the shell is not an app and has no promise: it starts a job and
// hears the answer here, in its own turn. An app and a command never run
// together, so there is one answer and one waiter (docs/30).
typedef void (*Done)(bool ok, const char* code, uint32_t count);
void set_done(Done done);

// True while a job is running, so the shell knows a prompt is not due yet.
bool busy();

// What the radio is doing, in one word ("scanning", "connecting", "provisioning"
// or "idle"): a command that finds it busy can say what it is waiting for instead
// of a "busy" with no subject (docs/30).
const char* busy_with();

// The scan fills a table the shell reads entry by entry: the count answers in
// the callback above and the entries are read afterwards, so nothing is copied
// twice (docs/30, docs/50).
bool scan_start(uint32_t timeout_ms);
bool scan_record(uint32_t index, char* ssid, size_t cap, int32_t& rssi);

// Stores a network and starts the association with it, or connects with the one
// already stored. Both answer in the callback above (docs/50).
bool add_network(const char* ssid, const char* pass, uint32_t timeout_ms);
bool connect_stored(uint32_t timeout_ms);

// The network the board has stored, for the shell to show it (docs/50).
bool stored_ssid(char* out, size_t cap);

// Sets the clock from a time server. It goes through the same queue, so it waits
// its turn like everything else, and it does **not** keep the radio on (docs/60).
bool ntp(const char* server, uint32_t timeout_ms);

// Downloads a page through the queue. It brings the network up by itself (with
// what the board has stored), lets go of it when it is done, and the answer is
// the status and the body of the page (docs/30). The refusal says which way it
// did not start, because a busy radio and an address that does not fit are not
// the same mistake.
luabind::NetRefuse http_get(const char* url, uint32_t max_bytes, uint32_t timeout_ms);

// The answer of the page that has just arrived. Valid while the answer is being
// given and no longer: the shell prints it there, the interpreter copies it into a
// string, and the buffer goes back to the board right after (docs/75).
int http_status();
const char* http_body(uint32_t& len);

// The longest address the queue can carry, in bytes. A longer one is refused, and
// never cut: half an address is a page nobody asked for (docs/30).
uint32_t http_url_cap();

// True when the station is associated **and** addressed. A time server needs a
// network, and so does a page: asking one without this is asking the radio to
// shout into nothing (docs/60).
bool is_up();

// Drops whatever is running and takes the radio off the network. It is the only
// way back out of a connection that nobody asked to keep (docs/30, docs/75).
void stop();

// Stands for hal::NetState as a number, so the interpreter keeps knowing nothing
// about the HAL.
uint8_t state();

// The same state as a word ("off", "connecting", "up", "failed"), for a person
// reading the console: `state: 2` said nothing (docs/30).
const char* state_name();

}  // namespace net
}  // namespace kernel

#endif  // LUNOK_KERNEL_NET_H
