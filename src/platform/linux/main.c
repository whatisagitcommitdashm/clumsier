#include <stdio.h>
#include "../../backends/linux/backend.h"
#include "../../prototype/console.h"

int main(int argc, char **argv) {
    char error[NETWORK_ERROR_SIZE] = "";
    LinuxBackend *backend = linuxBackendCreate(error);
    int result;
    if (!backend) { fprintf(stderr, "%s\n", error); return 1; }
    result = prototypeRun(linuxBackendInterface(backend), argc, argv);
    linuxBackendLastError(backend, error);
    if (error[0]) result = 1;
    linuxBackendDestroy(backend);
    return result;
}
