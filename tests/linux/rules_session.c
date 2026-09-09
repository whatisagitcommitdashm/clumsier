#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../src/backends/linux/rules.h"
#include "../../src/backends/linux/session.h"

typedef struct {
    char calls[64];
    bool fail_open, fail_install, install_owned, fail_remove, fail_drain;
} Fake;
static void called(Fake *fake, char code) {
    size_t length = strlen(fake->calls);
    assert(length + 1 < sizeof(fake->calls));
    fake->calls[length] = code;
    fake->calls[length + 1] = 0;
}
static bool openSession(void *context, char *error) {
    Fake *fake = context; called(fake, 'O');
    if (fake->fail_open) { strcpy(error, "open failed"); return false; }
    return true;
}
static bool install(void *context, bool *owned, char *error) {
    Fake *fake = context; called(fake, 'I');
    *owned = !fake->fail_install || fake->install_owned;
    if (fake->fail_install) { strcpy(error, "install failed"); return false; }
    return true;
}
static bool removeRules(void *context, char *error) {
    Fake *fake = context; called(fake, 'R');
    if (fake->fail_remove) { strcpy(error, "remove failed"); return false; }
    return true;
}
static bool drain(void *context, char *error) {
    Fake *fake = context; called(fake, 'D');
    if (fake->fail_drain) { strcpy(error, "drain failed"); return false; }
    return true;
}
static void closeSession(void *context) { called(context, 'C'); }

static void sessions(void) {
    static const LinuxSessionOps ops = {openSession, install, removeRules, drain, closeSession};
    Fake fake = {0};
    LinuxSession session = {&ops, &fake, false, false};
    char error[NETWORK_ERROR_SIZE] = "";
    unsigned i;
    for (i = 0; i < 20; ++i) {
        fake.calls[0] = 0;
        assert(linuxSessionStart(&session, error));
        assert(!linuxSessionStart(&session, error));
        assert(linuxSessionStop(&session, error));
        assert(linuxSessionStop(&session, error));
        assert(!strcmp(fake.calls, "OIRDC"));
    }
    fake = (Fake){0}; fake.fail_open = true;
    assert(!linuxSessionStart(&session, error));
    assert(!strcmp(fake.calls, "O") && !session.open && !session.owned);
    fake = (Fake){0}; fake.fail_install = true;
    assert(!linuxSessionStart(&session, error));
    assert(!strcmp(fake.calls, "OIDC") && !session.open && !session.owned);
    fake = (Fake){0}; fake.fail_install = fake.install_owned = true;
    assert(!linuxSessionStart(&session, error));
    assert(!strcmp(fake.calls, "OIRDC") && !session.open && !session.owned);
    fake = (Fake){0}; fake.fail_remove = true;
    assert(linuxSessionStart(&session, error));
    assert(!linuxSessionStop(&session, error));
    assert(!strcmp(fake.calls, "OIRDC") && !session.open && session.owned);
    assert(!linuxSessionStart(&session, error));
    fake.fail_remove = false;
    assert(linuxSessionStop(&session, error));
    assert(!strcmp(fake.calls, "OIRDCR") && !session.owned);
    fake = (Fake){0}; fake.fail_drain = true;
    assert(linuxSessionStart(&session, error));
    assert(!linuxSessionStop(&session, error));
    assert(!strcmp(fake.calls, "OIRDC") && !session.open && !session.owned);
    assert(strstr(error, "drain failed"));
}

static void rules(void) {
    CaptureTarget target = {0};
    char text[LINUX_RULES_SIZE], previous[LINUX_RULES_SIZE], error[NETWORK_ERROR_SIZE];
    const char *table = "clumsier_0123456789abcdef";
    assert(linuxBuildRules(&target, table, 65535, text, error));
    assert(strstr(text, "create table inet clumsier_0123456789abcdef"));
    assert(strstr(text, "hook input") && strstr(text, "hook output"));
    assert(strstr(text, "queue num 65535 bypass"));
    assert(!strstr(text, "flush") && !strstr(text, "delete") && !strstr(text, "meta l4proto"));
    target.traffic.protocol = TRAFFIC_TCP;
    target.traffic.remote_port = 443;
    strcpy(target.traffic.remote_address, "192.0.2.2");
    assert(linuxBuildRules(&target, table, 123, text, error));
    assert(strstr(text, "ip saddr 192.0.2.2 meta l4proto tcp tcp sport 443"));
    assert(strstr(text, "ip daddr 192.0.2.2 meta l4proto tcp tcp dport 443"));
    target.traffic.direction = TRAFFIC_INBOUND;
    target.traffic.protocol = TRAFFIC_UDP;
    strcpy(target.traffic.remote_address, "2001:0db8:0:0:0:0:0:2");
    assert(linuxBuildRules(&target, table, 123, text, error));
    assert(strstr(text, "ip6 saddr 2001:db8::2 meta l4proto udp udp sport 443"));
    assert(!strstr(text, "outgoing") && !strstr(text, "nexthdr"));
    target.traffic.direction = TRAFFIC_OUTBOUND;
    assert(linuxBuildRules(&target, table, 123, text, error));
    assert(strstr(text, "ip6 daddr") && !strstr(text, "incoming"));
    strcpy(previous, text);
    strcpy(target.traffic.remote_address, "1.2.3.4; flush ruleset");
    assert(!linuxBuildRules(&target, table, 123, text, error));
    assert(!strcmp(text, previous));
    strcpy(target.traffic.remote_address, "example.com");
    assert(!linuxBuildRules(&target, table, 123, text, error));
    strcpy(target.traffic.remote_address, "fe80::1%eth0");
    assert(!linuxBuildRules(&target, table, 123, text, error));
    target = (CaptureTarget){0};
    strcpy(target.native_backend, "windivert"); strcpy(target.native_filter, "true");
    assert(!linuxBuildRules(&target, table, 123, text, error));
    target = (CaptureTarget){0}; target.traffic.remote_port = 9;
    assert(!linuxBuildRules(&target, table, 123, text, error));
    target = (CaptureTarget){0};
    assert(!linuxBuildRules(&target, "clumsier_x; flush ruleset", 123, text, error));
    assert(!linuxBuildRules(&target, "filter", 123, text, error));
    assert(!linuxBuildRules(&target, "clumsier_", 123, text, error));
}

int main(void) {
    sessions(); rules();
    puts("Linux rules and mocked session lifecycle tests passed (no native networking).");
    return 0;
}
