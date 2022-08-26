#include "shady/runtime.h"

#include "log.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

static const char* shader =
"// trivial function that returns its argument\n"
"@EntryPoint(\"compute\") @WorkgroupSize(64, 1, 1) fn main() {\n"
"    return;\n"
"}";

int main(int argc, const char* argv[]) {
    set_log_level(DEBUG);
    info_print("Shady runtime test starting...\n");

    RuntimeConfig config = {
        .use_validation = true,
        .dump_spv = true,
    };
    Runtime* runtime = initialize_runtime(config);
    Device* device = initialize_device(runtime);
    assert(device);
    Program* program = load_program(runtime, shader);
    wait_completion(launch_kernel(program, device, 1, 1, 1, 0, NULL));
    shutdown_runtime(runtime);
}