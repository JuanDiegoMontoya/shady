#include "compile.h"

#include "parser/parser.h"
#include "builtin_code.h"
#include "transform/internal_constants.h"

#define KiB * 1024
#define MiB * 1024 KiB

CompilerConfig default_compiler_config() {
    return (CompilerConfig) {
        .allow_frontend_syntax = false,
        .per_thread_stack_size = 1 KiB,
        .per_subgroup_stack_size = 1 KiB,

        .subgroup_size = 32,

        .target_spirv_version = {
            .major = 1,
            .minor = 4
        }
    };
}

#define mod (*pmod)

CompilationResult run_compiler_passes(CompilerConfig* config, Module** pmod) {
    ArenaConfig aconfig = {
        .name_bound = true,
        .allow_fold = false,
        .check_types = false
    };
    Module* old_mod;

    IrArena* old_arena = NULL;
    IrArena* tmp_arena = NULL;

    generate_dummy_constants(config, mod);

    RUN_PASS(bind_program)
    RUN_PASS(normalize)

    // TODO: do this late
    patch_constants(config, mod);

    aconfig.check_types = true;
    RUN_PASS(infer_program)

    aconfig.allow_fold = true;

    RUN_PASS(setup_stack_frames)
    RUN_PASS(mark_leaf_functions)

    RUN_PASS(lower_cf_instrs)
    RUN_PASS(opt_restructurize)

    RUN_PASS(lower_callf)
    RUN_PASS(opt_simplify_cf)

    RUN_PASS(lower_continuations)

    RUN_PASS(opt_simplify_cf)
    RUN_PASS(opt_stack)

    RUN_PASS(lower_tailcalls)

    RUN_PASS(eliminate_constants)

    RUN_PASS(lower_subgroup_ops)

    aconfig.subgroup_mask_representation = SubgroupMaskSpvKHRBallot;
    RUN_PASS(lower_mask)
    RUN_PASS(lower_stack)
    RUN_PASS(lower_physical_ptrs)
    RUN_PASS(lower_subgroup_vars)

    return CompilationNoError;
}

#undef mod

static size_t num_builtin_sources_files = 1;
static const char* builtin_source_files[] = { builtin_scheduler_txt };

CompilationResult parse_files(CompilerConfig* config, size_t num_files, const char** files_contents, Module* mod) {
    size_t num_source_files = num_builtin_sources_files + num_files;

    for (size_t i = 0; i < num_source_files; i++) {
        const char* input_file_contents = NULL;

        if (i < num_builtin_sources_files) {
            input_file_contents = builtin_source_files[i];
        } else {
            input_file_contents = files_contents[i - num_builtin_sources_files];
        }

        debugv_print("Parsing: \n%s\n", input_file_contents);
        ParserConfig pconfig = {
            .front_end = config->allow_frontend_syntax
        };
        parse(pconfig, input_file_contents, mod);
    }

    return CompilationNoError;
}
