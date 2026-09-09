#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "prototype/console.h"

typedef struct {
    bool running;
    unsigned starts, stops, attempts, fail_apply;
    LagSettings started[8], applied[32];
    CaptureTarget target;
} Fake;

static bool start(void *context, const CaptureTarget *target, const LagSettings *lag, char *error) {
    Fake *fake = context;
    (void)error;
    assert(!fake->running && fake->starts < 8);
    fake->started[fake->starts++] = *lag;
    fake->target = *target;
    fake->running = true;
    return true;
}
static bool apply(void *context, const LagSettings *lag, char *error) {
    Fake *fake = context;
    assert(fake->running && fake->attempts < 32);
    fake->applied[fake->attempts++] = *lag;
    if (fake->attempts == fake->fail_apply) {
        strcpy(error, "Injected apply failure."); return false;
    }
    return true;
}
static void stop(void *context) {
    Fake *fake = context;
    if (fake->running) ++fake->stops;
    fake->running = false;
}
static bool running(void *context) { return ((Fake *)context)->running; }

static int run(Fake *fake, int argc, char **argv, const char *commands, size_t length,
               char *output_text, char *error_text) {
    const NetworkBackendOps operations = {start, apply, stop, running};
    NetworkBackend backend = {&operations, fake};
    FILE *input = tmpfile(), *output = tmpfile(), *errors = tmpfile();
    int result;
    size_t count;
    assert(input && output && errors);
    assert(fwrite(commands, 1, length, input) == length);
    rewind(input);
    result = prototypeRunStreams(backend, argc, argv, input, output, errors);
    rewind(output);
    count = fread(output_text, 1, 16383, output); output_text[count] = 0;
    rewind(errors);
    count = fread(error_text, 1, 16383, errors); error_text[count] = 0;
    fclose(input); fclose(output); fclose(errors);
    assert(!fake->running); /* EOF, quit and argument errors all clean up. */
    return result;
}

int main(void) {
    char output[16384], errors[16384];
    char *preset_args[] = {"clumsier", "--preset", "examples/four-leaps.json", "--baseline", "50"};
    char *plain_args[] = {"clumsier"};
    char *bad_args[] = {"clumsier", "--baseline", "99999999999999999999999999999"};
    Fake fake = {0};
    const char *commands = "start\nstart\nnext\nnext\nprevious\nreset\nstop\nstop\nnext\nstart\n";
    assert(run(&fake, 5, preset_args, commands, strlen(commands), output, errors) == 0);
    assert(!errors[0] && fake.starts == 2 && fake.stops == 2 && fake.attempts == 4);
    assert(fake.target.traffic.direction == TRAFFIC_INBOUND);
    assert(fake.started[0].inbound_ms == 150 && fake.started[1].inbound_ms == 0);
    assert(fake.applied[0].inbound_ms == 0 && fake.applied[1].inbound_ms == 100);
    assert(fake.applied[2].inbound_ms == 0 && fake.applied[3].inbound_ms == 150);
    assert(strstr(output, "Expected: ~200 ms") && strstr(output, "Lowest available"));

    memset(&fake, 0, sizeof(fake));
    fake.fail_apply = 2;
    commands = "start\nnext\nnext\nstatus\nreset\nbaseline 45\nload missing-preset-file.json\n"
               "stop\nbaseline 100\nstart\nnext\nquit\n";
    assert(run(&fake, 5, preset_args, commands, strlen(commands), output, errors) == 0);
    assert(strstr(errors, "Injected apply failure") && strstr(errors, "Stop capture") && strstr(errors, "Cannot open"));
    assert(fake.starts == 2 && fake.stops == 2 && fake.attempts == 4);
    assert(fake.started[1].inbound_ms == 100 && fake.applied[2].inbound_ms == 150);
    assert(strstr(output, "Step 2/4: Leap 2") && !strstr(output, "Step 3/4: Leap 3"));

    memset(&fake, 0, sizeof(fake));
    commands = "baseline 50\nload examples/four-leaps.json\nnext\nstatus\nquit\n";
    assert(run(&fake, 1, plain_args, commands, strlen(commands), output, errors) == 0);
    assert(!fake.starts && !fake.attempts && !errors[0]);
    assert(strstr(output, "Stopped | Four leaps"));

    memset(&fake, 0, sizeof(fake));
    commands = "delay 70 30\nstart\ndelay -1 20\ndelay 999999999999999999999 0\n"
               "delay 4x 5\ndelay 1 2 extra\nstart extra\ndelay 0 0\nquit\n";
    assert(run(&fake, 1, plain_args, commands, strlen(commands), output, errors) == 0);
    assert(fake.starts == 1 && fake.started[0].inbound_ms == 70 && fake.started[0].outbound_ms == 30);
    assert(fake.attempts == 1 && !fake.applied[0].enabled && fake.stops == 1);
    assert(strstr(errors, "Use: delay") && strstr(errors, "takes no arguments"));

    memset(&fake, 0, sizeof(fake));
    assert(run(&fake, 3, bad_args, "", 0, output, errors) == 1);
    assert(!fake.starts && strstr(errors, "Invalid --baseline"));
    {
        char oversized[6000];
        memset(oversized, 'x', sizeof(oversized));
        memcpy(oversized, "start ", 6);
        oversized[sizeof(oversized) - 1] = '\n';
        assert(run(&fake, 1, plain_args, oversized, sizeof(oversized), output, errors) == 0);
        assert(!fake.starts && strstr(errors, "too long"));
    }
    {
        const char binary[] = "start\0\nquit\n";
        assert(run(&fake, 1, plain_args, binary, sizeof(binary) - 1, output, errors) == 0);
        assert(!fake.starts && strstr(errors, "NUL"));
    }
    commands = "delay 1 2\nstart"; /* Final command also works without a newline. */
    assert(run(&fake, 1, plain_args, commands, strlen(commands), output, errors) == 0);
    assert(fake.starts == 1 && fake.stops == 1 && !errors[0]);
    puts("PASS prototype console: real preset loading, sequence controls, failed changes, input validation and EOF cleanup");
    return 0;
}
