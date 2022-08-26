#include "type.h"

#include "log.h"
#include "arena.h"
#include "portability.h"

#include "dict.h"

#include <string.h>
#include <assert.h>

#define empty() nodes(arena, 0, NULL)
#define singleton(t) singleton_impl(arena, t)
Nodes singleton_impl(IrArena* arena, const Type* type) {
    const Type* arr[] = { type };
    return nodes(arena, 1, arr);
}

bool is_type(const Node* node) {
    switch (node->tag) {
#define NODEDEF(_, _2, _3, name, _4) case name##_TAG:
TYPE_NODES()
#undef NODEDEF
                 return true;
        default: return false;
    }
}

bool are_types_identical(size_t num_types, const Type* types[]) {
    for (size_t i = 0; i < num_types; i++)
        if (types[0] != types[i])
            return false;
    return true;
}

bool is_subtype(const Type* supertype, const Type* type) {
    if (supertype->tag != type->tag)
        return false;
    switch (supertype->tag) {
        case QualifiedType_TAG: {
            // uniform T <: varying T
            if (supertype->payload.qualified_type.is_uniform && !type->payload.qualified_type.is_uniform)
                return false;
            return is_subtype(supertype->payload.qualified_type.type, type->payload.qualified_type.type);
        }
        case RecordType_TAG: {
            const Nodes* supermembers = &supertype->payload.record_type.members;
            const Nodes* members = &type->payload.record_type.members;
            for (size_t i = 0; i < members->count; i++) {
                if (!is_subtype(supermembers->nodes[i], members->nodes[i]))
                    return false;
            }
            return true;
        }
        case FnType_TAG:
            if (supertype->payload.fn_type.is_basic_block != type->payload.fn_type.is_basic_block)
                return false;
            // check returns
            if (supertype->payload.fn_type.return_types.count != type->payload.fn_type.return_types.count)
                return false;
            for (size_t i = 0; i < type->payload.fn_type.return_types.count; i++)
                if (!is_subtype(supertype->payload.fn_type.return_types.nodes[i], type->payload.fn_type.return_types.nodes[i]))
                    return false;
            // check params
            const Nodes* superparams = &supertype->payload.fn_type.param_types;
            const Nodes* params = &type->payload.fn_type.param_types;
            if (params->count != superparams->count) return false;
            for (size_t i = 0; i < params->count; i++) {
                if (!is_subtype(params->nodes[i], superparams->nodes[i]))
                    return false;
            }
            return true;
        case PtrType_TAG: {
            if (supertype->payload.ptr_type.address_space != type->payload.ptr_type.address_space)
                return false;
            return is_subtype(supertype->payload.ptr_type.pointed_type, type->payload.ptr_type.pointed_type);
        }
        case Int_TAG: return supertype->payload.int_type.width == type->payload.int_type.width;
        // simple types without a payload
        default: return true;
    }
    SHADY_UNREACHABLE;
}

void check_subtype(const Type* supertype, const Type* type) {
    if (!is_subtype(supertype, type)) {
        print_node(type);
        printf(" isn't a subtype of ");
        print_node(supertype);
        printf("\n");
        error("failed check_subtype")
    }
}

void deconstruct_operand_type(const Type* type, const Type** type_out, bool* is_uniform_out) {
    if (type->tag == QualifiedType_TAG) {
        *is_uniform_out = type->payload.qualified_type.is_uniform;
        *type_out = type->payload.qualified_type.type;
    } else {
        assert(false && "Expected a value type (annotated with qual_type)");
        *is_uniform_out = Unknown;
        *type_out = type;
    }
}

bool is_operand_uniform(const Type* type) {
    const Type* result_type;
    bool is_uniform;
    deconstruct_operand_type(type, &result_type, &is_uniform);
    return is_uniform;
}

const Type* extract_operand_type(const Type* type) {
    const Type* result_type;
    bool is_uniform;
    deconstruct_operand_type(type, &result_type, &is_uniform);
    return result_type;
}

// TODO: this isn't really accurate to what we want...
// It would be better to have verify_is_value_type, verify_is_operand etc functions.
bool contains_qualified_type(const Type* type) {
    switch (type->tag) {
        case QualifiedType_TAG: return true;
        default: return false;
    }
}

Nodes extract_variable_types(IrArena* arena, const Nodes* variables) {
    LARRAY(const Type*, arr, variables->count);
    for (size_t i = 0; i < variables->count; i++)
        arr[i] = variables->nodes[i]->payload.var.type;
    return nodes(arena, variables->count, arr);
}

Nodes extract_types(IrArena* arena, Nodes values) {
    LARRAY(const Type*, arr, values.count);
    for (size_t i = 0; i < values.count; i++)
        arr[i] = values.nodes[i]->type;
    return nodes(arena, values.count, arr);
}

const Type* derive_fn_type(IrArena* arena, const Function* fn) {
    return fn_type(arena, (FnType) { .is_basic_block = fn->is_basic_block, .param_types = extract_variable_types(arena, &fn->params), .return_types = fn->return_types });
}

const Type* check_type_fn(IrArena* arena, Function fn) {
    assert(!fn.is_basic_block || fn.return_types.count == 0);
    return qualified_type(arena, (QualifiedType) {
        .is_uniform = true,
        .type = derive_fn_type(arena, &fn)
    });
}

const Type* check_type_global_variable(IrArena* arena, GlobalVariable global_variable) {
    return qualified_type(arena, (QualifiedType) {
        .type = ptr_type(arena, (PtrType) {
            .pointed_type = global_variable.type,
            .address_space = global_variable.address_space
        }),
        .is_uniform = true
    });
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
const Type* check_type_var(IrArena* arena, Variable variable) {
    assert(variable.type);
    return variable.type;
}

const Type* check_type_qualified_type(IrArena* arena, QualifiedType qualified_type) {
    assert(!contains_qualified_type(qualified_type.type));
    return NULL;
}

const Type* check_type_pack_type(IrArena* arena, PackType pack_type) {
    assert(!contains_qualified_type(pack_type.element_type));
    return NULL;
}

const Type* check_type_untyped_number(IrArena* arena, UntypedNumber untyped) {
    error("should never happen");
}

const Type* check_type_int_literal(IrArena* arena, IntLiteral lit) {
    return qualified_type(arena, (QualifiedType) {
        .is_uniform = true,
        .type = int_type(arena, (Int) { .width = lit.width })
    });
}

const Type* check_type_true_lit(IrArena* arena) { return qualified_type(arena, (QualifiedType) { .type = bool_type(arena), .is_uniform = true }); }
const Type* check_type_false_lit(IrArena* arena) { return qualified_type(arena, (QualifiedType) { .type = bool_type(arena), .is_uniform = true }); }

const Type* check_type_tuple(IrArena* arena, Tuple tuple) {
    return record_type(arena, (RecordType) {
        .members = extract_types(arena, tuple.contents),
        .special = NotSpecial,
        .names = strings(arena, 0, NULL)
    });
}

const Type* check_type_fn_ret(IrArena* arena, Return ret) {
    // TODO check it then !
    return NULL;
}

const Type* wrap_multiple_yield_types(IrArena* arena, Nodes types) {
    switch (types.count) {
        case 0: return unit_type(arena);
        case 1: return types.nodes[0];
        default: return record_type(arena, (RecordType) {
            .members = types,
            .names = strings(arena, 0, NULL),
            .special = MultipleReturn,
        });
    }
    SHADY_UNREACHABLE;
}

Nodes unwrap_multiple_yield_types(IrArena* arena, const Type* type) {
    switch (type->tag) {
        case Unit_TAG: return nodes(arena, 0, NULL);
        case RecordType_TAG:
            if (type->payload.record_type.special == MultipleReturn)
                return type->payload.record_type.members;
            // fallthrough
        default: return nodes(arena, 1, (const Node* []) { type });
    }
}

const Type* check_type_if_instr(IrArena* arena, If if_instr) {
    if (extract_operand_type(if_instr.condition->type) != bool_type(arena))
        error("condition of a selection should be bool");
    // TODO check the contained Merge instrs
    if (if_instr.yield_types.count > 0)
        assert(if_instr.if_false);

    return wrap_multiple_yield_types(arena, if_instr.yield_types);
}

const Type* check_type_loop_instr(IrArena* arena, Loop loop_instr) {
    // TODO check param against initial_args
    // TODO check the contained Merge instrs
    return wrap_multiple_yield_types(arena, loop_instr.yield_types);
}

const Type* check_type_match_instr(IrArena* arena, Match match_instr) {
    // TODO check param against initial_args
    // TODO check the contained Merge instrs
    return wrap_multiple_yield_types(arena, match_instr.yield_types);
}

/// Oracle of what casts are legal
static bool is_reinterpret_cast_legal(const Type* src_type, const Type* dst_type) {
    // TODO implement rules
    assert(is_type(src_type) && is_type(dst_type));
    return true;
}

static const Type* get_actual_mask_type(IrArena* arena) {
    switch (arena->config.subgroup_mask_representation) {
        case SubgroupMaskAbstract: return mask_type(arena);
        case SubgroupMaskSpvKHRBallot: return pack_type(arena, (PackType) { .element_type = int32_type(arena), .width = 4 });
        default: error("unimplemented");
    }
}

/// Checks the operands to a Primop and returns the produced types
const Type* check_type_prim_op(IrArena* arena, PrimOp prim_op) {
    for (size_t i = 0; i < prim_op.operands.count; i++) {
        const Node* operand = prim_op.operands.nodes[i];
        assert(!operand || is_type(operand) || is_value(operand));
    }

    switch (prim_op.op) {
        case neg_op: {
            assert(prim_op.operands.count == 1);
            return prim_op.operands.nodes[0]->type;
            // return qualified_type(arena, (QualifiedType) { .is_uniform = , .type = bool_type(arena) });
        }
        case rshift_arithm_op:
        case rshift_logical_op:
        case lshift_op:

        case add_op:
        case sub_op:
        case mul_op:
        case div_op:
        case mod_op: {
            assert(prim_op.operands.count == 2);
            const Type* first_operand_type = extract_operand_type(prim_op.operands.nodes[0]->type);
            bool is_result_uniform = true;

            for (size_t i = 0; i < prim_op.operands.count; i++) {
                const Node* arg = prim_op.operands.nodes[i];

                bool arg_uniform;
                const Type* arg_actual_type;
                deconstruct_operand_type(arg->type, &arg_actual_type, &arg_uniform);

                is_result_uniform &= arg_uniform;
                // we work with numerical operands
                assert(arg_actual_type->tag == Int_TAG && "todo improve this check");
                assert(first_operand_type->tag == Int_TAG && "todo improve this check");
                assert(arg_actual_type->payload.int_type.width == first_operand_type->payload.int_type.width && "Arithmetic operations expect all operands to have the same widths");
            }

            IntSizes width = first_operand_type->payload.int_type.width;
            return qualified_type(arena, (QualifiedType) {
                .is_uniform = is_result_uniform,
                .type = int_type(arena, (Int) {
                    .width = width
                })
            });
        }

        case or_op:
        case xor_op:
        case and_op: {
            bool is_uniform = true;
            const Type* first_arg_type = extract_operand_type(prim_op.operands.nodes[0]->type);
            for (size_t i = 0; i < prim_op.operands.count; i++) {
                const Node* arg = prim_op.operands.nodes[i];
                is_uniform &= is_operand_uniform(arg->type);
                const Type* arg_type = extract_operand_type(arg->type);
                assert(arg_type == first_arg_type && "Operands must have the same type");
                switch (arg_type->tag) {
                    case Int_TAG:
                    case Bool_TAG: break;
                    default: error("Logical operations can only be applied on booleans and on integers");
                }
            }
            return qualified_type(arena, (QualifiedType) { .is_uniform = is_uniform, .type = first_arg_type });
        }

        case lt_op:
        case lte_op:
        case gt_op:
        case gte_op:
        case eq_op:
        case neq_op: {
            assert(prim_op.operands.count == 2);
            bool is_result_uniform = true;
            const Type* first_op_type = extract_operand_type(prim_op.operands.nodes[0]->type);

            for (size_t i = 0; i < prim_op.operands.count; i++) {
                const Node* arg = prim_op.operands.nodes[i];

                bool op_uniform;
                const Type* op_type;
                deconstruct_operand_type(arg->type, &op_type, &op_uniform);

                is_result_uniform &= op_uniform;
                assert(op_type == first_op_type && "Comparison operators need to be applied to the same types");
            }
            return qualified_type(arena, (QualifiedType) { .is_uniform = is_result_uniform, .type = bool_type(arena) });
        }
        case get_stack_pointer_op:
        case get_stack_pointer_uniform_op: {
            return qualified_type(arena, (QualifiedType) { .is_uniform = prim_op.op == get_stack_pointer_uniform_op, .type = int32_type(arena) });
        }
        case set_stack_pointer_op:
        case set_stack_pointer_uniform_op: {
            assert(prim_op.operands.count == 1);
            bool is_uniform = prim_op.op == set_stack_pointer_uniform_op;
            if (is_uniform)
                assert(is_operand_uniform(prim_op.operands.nodes[0]->type));
            assert(extract_operand_type(prim_op.operands.nodes[0]->type) == int32_type(arena));
            return unit_type(arena);
        }
        case push_stack_uniform_op:
        case push_stack_op: {
            assert(prim_op.operands.count == 2);
            const Type* element_type = prim_op.operands.nodes[0];
            assert(!contains_qualified_type(element_type) && "annotations do not go here");
            const Type* qual_element_type = qualified_type(arena, (QualifiedType) {
                .is_uniform = prim_op.op == push_stack_uniform_op,
                .type = element_type
            });
            // the operand has to be a subtype of the annotated type
            assert(is_subtype(qual_element_type, prim_op.operands.nodes[1]->type));
            return unit_type(arena);
        }
        case pop_stack_op:
        case pop_stack_uniform_op: {
            assert(prim_op.operands.count == 1);
            const Type* element_type = prim_op.operands.nodes[0];
            assert(!contains_qualified_type(element_type) && "annotations do not go here");
            return qualified_type(arena, (QualifiedType) { .is_uniform = prim_op.op == pop_stack_uniform_op, .type = element_type});
        }
        case load_op: {
            assert(prim_op.operands.count == 1);

            const Node* ptr = prim_op.operands.nodes[0];
            bool ptr_uniform;
            const Node* ptr_type;
            deconstruct_operand_type(ptr->type, &ptr_type, &ptr_uniform);
            assert(ptr_type->tag == PtrType_TAG);
            const PtrType* node_ptr_type_ = &ptr_type->payload.ptr_type;
            const Type* elem_type = node_ptr_type_->pointed_type;
            return qualified_type(arena, (QualifiedType) {
                .type = elem_type,
                .is_uniform = ptr_uniform
            });
        }
        case store_op: {
            assert(prim_op.operands.count == 2);

            const Node* ptr = prim_op.operands.nodes[0];
            bool ptr_uniform;
            const Node* ptr_type;
            deconstruct_operand_type(ptr->type, &ptr_type, &ptr_uniform);
            assert(ptr_type->tag == PtrType_TAG);

            const PtrType* ptr_type_payload = &ptr_type->payload.ptr_type;
            const Type* elem_type = ptr_type_payload->pointed_type;
            // we don't enforce uniform stores - but we care about storing the right thing :)
            const Type* val_expected_type = qualified_type(arena, (QualifiedType) {
                .is_uniform = false,
                .type = elem_type
            });

            const Node* val = prim_op.operands.nodes[1];
            assert(is_subtype(val_expected_type, val->type));
            return unit_type(arena);
        }
        case alloca_logical_op:
        case alloca_slot_op:
        case alloca_op: {
            bool is_slot = prim_op.op == alloca_slot_op;
            bool is_logical = prim_op.op == alloca_logical_op;

            assert(prim_op.operands.count == (is_slot ? 2 : 1));
            const Type* elem_type = prim_op.operands.nodes[0];
            assert(is_type(elem_type));
            return qualified_type(arena, (QualifiedType) {
                .is_uniform = true,
                .type = ptr_type(arena, (PtrType) {
                    .pointed_type = elem_type,
                    .address_space = is_logical ? AsFunctionLogical : AsPrivatePhysical
                })
            });
        }
        case lea_op: {
            assert(prim_op.operands.count >= 2);

            const Node* base = prim_op.operands.nodes[0];
            bool uniform = is_operand_uniform(base->type);

            const Type* curr_ptr_type = extract_operand_type(base->type);
            assert(curr_ptr_type->tag == PtrType_TAG && "lea expects a pointer as a base");

            const Node* offset = prim_op.operands.nodes[1];
            if (offset) {
                const Type* offset_type;
                bool offset_uniform;
                deconstruct_operand_type(offset->type, &offset_type, &offset_uniform);
                assert(offset_type->tag == Int_TAG && "lea expects an integer offset or NULL");
                const Type* pointee_type = curr_ptr_type->payload.ptr_type.pointed_type;
                assert(pointee_type->tag == ArrType_TAG && "if an offset is used, the base pointer must point to an array");
                uniform &= offset_uniform;
            }

            // enter N levels of pointers
            size_t i = 2;
            while (true) {
                assert(curr_ptr_type->tag == PtrType_TAG && "lea is supposed to work on, and yield pointers");
                if (i >= prim_op.operands.count) break;

                const Node* selector = prim_op.operands.nodes[i];
                const Type* selector_type;
                bool selector_uniform;
                deconstruct_operand_type(selector->type, &selector_type, &selector_uniform);

                assert(selector_type->tag == Int_TAG && "selectors must be integers");
                uniform &= selector_uniform;
                const Type* pointee_type = curr_ptr_type->payload.ptr_type.pointed_type;
                switch (pointee_type->tag) {
                    case ArrType_TAG: {
                        curr_ptr_type = ptr_type(arena, (PtrType) {
                            .pointed_type = pointee_type->payload.arr_type.element_type,
                            .address_space = curr_ptr_type->payload.ptr_type.address_space
                        });
                        break;
                    }
                    case RecordType_TAG: {
                        assert(selector->tag == IntLiteral_TAG && "selectors when indexing into a record need to be constant");
                        size_t index = extract_int_literal_value(selector, false);
                        assert(index < pointee_type->payload.record_type.members.count);
                        curr_ptr_type = ptr_type(arena, (PtrType) {
                            .pointed_type = pointee_type->payload.record_type.members.nodes[index],
                            .address_space = curr_ptr_type->payload.ptr_type.address_space
                        });
                        break;
                    }
                    // also remember to assert literals for the selectors !
                    default: error("lea selectors can only work on pointers to arrays or records")
                }
                i++;
            }

            return qualified_type(arena, (QualifiedType) {
                .is_uniform = uniform,
                .type = curr_ptr_type
            });
        }
        case reinterpret_op: {
            assert(prim_op.operands.count == 2);
            const Node* source = prim_op.operands.nodes[1];
            const Type* src_type;
            bool src_uniform;
            deconstruct_operand_type(source->type, &src_type, &src_uniform);

            const Type* target_type = prim_op.operands.nodes[0];
            assert(!contains_qualified_type(target_type));
            assert(is_reinterpret_cast_legal(src_type, target_type));

            return qualified_type(arena, (QualifiedType) {
                .is_uniform = src_uniform,
                .type = target_type
            });
        }
        case select_op: {
            assert(prim_op.operands.count == 3);
            const Type* condition_type;
            bool condition_uniform;
            deconstruct_operand_type(prim_op.operands.nodes[0]->type, &condition_type, &condition_uniform);

            const Type* alternatives_types[2];
            bool alternatives_uniform[2];
            bool alternatives_all_uniform = true;
            for (size_t i = 0; i < 2; i++) {
                deconstruct_operand_type(prim_op.operands.nodes[1 + i]->type, &alternatives_types[i], &alternatives_uniform[i]);
                alternatives_all_uniform &= alternatives_uniform[i];
            }

            assert(is_subtype(bool_type(arena), condition_type));
            // todo find true supertype
            assert(are_types_identical(2, alternatives_types));

            return qualified_type(arena, (QualifiedType) {
                .is_uniform = alternatives_all_uniform && condition_uniform,
                .type = alternatives_types[0]
            });
        }
        case extract_dynamic_op:
        case extract_op: {
            assert(prim_op.operands.count >= 2);
            const Type* source = prim_op.operands.nodes[0];

            const Type* current_type;
            bool is_uniform;
            deconstruct_operand_type(source->type, &current_type, &is_uniform);

            for (size_t i = 1; i < prim_op.operands.count; i++) {
                assert(!contains_qualified_type(current_type));

                // Check index is valid !
                const Node* ith_index = prim_op.operands.nodes[i];
                bool dynamic_index = prim_op.op == extract_dynamic_op;
                if (dynamic_index) {
                    const Type* index_type;
                    bool index_uniform;
                    deconstruct_operand_type(ith_index->type, &index_type, &index_uniform);
                    is_uniform &= index_uniform;
                    assert(index_type->tag == Int_TAG && "extract_dynamic requires integers for the indices");
                } else {
                    assert(ith_index->tag == IntLiteral_TAG && "extract takes integer literals");
                }

                // Go down one level...
                switch(current_type->tag) {
                    case RecordType_TAG: {
                        assert(!dynamic_index);
                        size_t index_value = ith_index->payload.int_literal.value_i32;
                        assert(index_value < current_type->payload.record_type.members.count);
                        current_type = current_type->payload.record_type.members.nodes[index_value];
                        continue;
                    }
                    case ArrType_TAG: {
                        assert(!dynamic_index);
                        current_type = current_type->payload.arr_type.element_type;
                        continue;
                    }
                    case PackType_TAG: {
                        current_type = current_type->payload.pack_type.element_type;
                        continue;
                    }
                    default: error("Not a valid type to extract from")
                }
            }
            return qualified_type(arena, (QualifiedType) {
                .is_uniform = is_uniform,
                .type = current_type
            });
        }
        case convert_op: {
            assert(prim_op.operands.count == 2);
            const Type* dst_type = prim_op.operands.nodes[0];
            assert(!contains_qualified_type(dst_type));

            const Type* src_type;
            bool is_uniform;
            deconstruct_operand_type(prim_op.operands.nodes[1], &src_type, &is_uniform);
            return qualified_type(arena, (QualifiedType) {
                .is_uniform = is_uniform,
                .type = dst_type
            });
        }
        case empty_mask_op:
        case subgroup_active_mask_op: {
            assert(prim_op.operands.count == 0);
            return qualified_type(arena, (QualifiedType) {
                .is_uniform = true,
                .type = get_actual_mask_type(arena)
            });
        }
        case subgroup_ballot_op: {
            assert(prim_op.operands.count == 1);
            return qualified_type(arena, (QualifiedType) {
                .is_uniform = true,
                .type = get_actual_mask_type(arena)
            });
        }
        case subgroup_elect_first_op: {
            assert(prim_op.operands.count == 0);
            return qualified_type(arena, (QualifiedType) {
                .is_uniform = false,
                .type = bool_type(arena)
            });
        }
        case subgroup_local_id_op: {
            assert(prim_op.operands.count == 0);
            return qualified_type(arena, (QualifiedType) {
                .is_uniform = false,
                .type = int32_type(arena)
            });
        }
        case subgroup_broadcast_first_op: {
            assert(prim_op.operands.count == 1);
            const Type* operand_type = extract_operand_type(prim_op.operands.nodes[0]->type);
            //assert(operand_type->tag == Int_TAG || operand_type->tag == Bool_TAG);
            return qualified_type(arena, (QualifiedType) {
                .is_uniform = true,
                .type = operand_type
            });
        }
        case mask_is_thread_active_op: {
            // TODO assert input is uniform
            assert(prim_op.operands.count == 2);
            return qualified_type(arena, (QualifiedType) {
                .is_uniform = true,
                .type = bool_type(arena)
            });
        }
        case debug_printf_op: {
            return unit_type(arena);
        }
        default: error("unhandled primop %s", primop_names[prim_op.op]);
    }
}

static void check_arguments_types_against_parameters_helper(Nodes param_types, Nodes arg_types) {
    if (param_types.count != arg_types.count)
        error("Mismatched number of arguments/parameters");
    for (size_t i = 0; i < param_types.count; i++)
        check_subtype(param_types.nodes[i], arg_types.nodes[i]);
}

static Nodes check_callsite_helper(const Type* callee_type, Nodes argument_types) {
    assert(!contains_qualified_type(callee_type) && callee_type->tag == FnType_TAG);
    const FnType* fn_type = &callee_type->payload.fn_type;
    check_arguments_types_against_parameters_helper(fn_type->param_types, argument_types);

    return fn_type->return_types;
}

const Type* check_type_call_instr(IrArena* arena, Call call) {
    for (size_t i = 0; i < call.args.count; i++) {
        const Node* argument = call.args.nodes[i];
        assert(is_value(argument));
    }

    const Type* callee_type;
    bool callee_uniform;
    deconstruct_operand_type(call.callee->type, &callee_type, &callee_uniform);
    assert(callee_uniform);
    return wrap_multiple_yield_types(arena, check_callsite_helper(callee_type, extract_types(arena, call.args)));
}

const Type* check_type_let(IrArena* arena, Let let) {
    //Nodes output_types = typecheck_instruction(arena, let.instruction);
    const Type* result_type = let.instruction->type;

    // check outputs
    Nodes var_tys = extract_variable_types(arena, &let.variables);
    switch (result_type->tag) {
        case Unit_TAG: error("You can only let-bind non-unit nodes");
        case RecordType_TAG: {
            if (result_type->payload.record_type.members.count != var_tys.count)
                error("let variables count != yield count from operation")
            for (size_t i = 0; i < var_tys.count; i++)
                check_subtype(var_tys.nodes[i], result_type->payload.record_type.members.nodes[i]);
            break;
        }
        default: {
            assert(var_tys.count == 1);
            check_subtype(var_tys.nodes[0], result_type);
            break;
        }
    }

    return unit_type(arena);
}

const Type* check_type_branch(IrArena* arena, Branch branch) {
    for (size_t i = 0; i < branch.args.count; i++) {
        const Node* argument = branch.args.nodes[i];
        assert(is_value(argument));
    }

    switch (branch.branch_mode) {
        case BrTailcall: {
            const Type* callee_type;
            bool callee_uniform;
            deconstruct_operand_type(branch.target->type, &callee_type, &callee_uniform);
            assert(callee_type->tag == PtrType_TAG && "Tail calls are indirect calls, they consume pointers to functions.");

            assert(callee_type->payload.ptr_type.address_space == AsProgramCode);
            callee_type = callee_type->payload.ptr_type.pointed_type;

            // TODO say something about uniformity of the target ?
            check_callsite_helper(callee_type, extract_types(arena, branch.args));
            return NULL;
        }
        case BrJump: {
            const Type* target_type;
            bool target_uniform;
            deconstruct_operand_type(branch.target->type, &target_type, &target_uniform);
            assert(target_uniform && "Non-uniform jump targets are not allowed");
            check_callsite_helper(target_type, extract_types(arena, branch.args));
            return NULL;
        }
        case BrIfElse: {
            const Type* condition_type;
            bool uniform;
            deconstruct_operand_type(branch.branch_condition->type, &condition_type, &uniform);
            assert(bool_type(arena) == condition_type);

            const Node* branches[2] = { branch.true_target, branch.false_target };
            for (size_t i = 0; i < 2; i++) {
                const Type* target_type;
                bool target_uniform;
                deconstruct_operand_type(branches[i]->type, &target_type, &target_uniform);
                assert(target_uniform && "Non-uniform branch targets are not allowed");
                check_callsite_helper(target_type, extract_types(arena, branch.args));
            }

            return NULL;
        }
        case BrSwitch: error("TODO")
    }

    // TODO check arguments and that both branches match
    return NULL;
}

const Type* check_type_join(IrArena* arena, Join join) {
    for (size_t i = 0; i < join.args.count; i++) {
        const Node* argument = join.args.nodes[i];
        assert(is_value(argument));
    }

    const Type* join_target_type;
    bool join_target_uniform;
    deconstruct_operand_type(join.join_at->type, &join_target_type, &join_target_uniform);
    assert(join_target_uniform);

    if (join.is_indirect) {
        assert(join_target_type->tag == PtrType_TAG);
        join_target_type = join_target_type->payload.ptr_type.pointed_type;
    }

    check_callsite_helper(join_target_type, extract_types(arena, join.args));

    return NULL;
}

const Type* check_type_callc(IrArena* arena, Callc callc) {
    for (size_t i = 0; i < callc.args.count; i++) {
        const Node* argument = callc.args.nodes[i];
        assert(is_value(argument));
    }

    const Type* callee_type;
    bool callee_uniform;
    deconstruct_operand_type(callc.callee->type, &callee_type, &callee_uniform);
    assert(callee_type->tag == PtrType_TAG);
    callee_type = callee_type->payload.ptr_type.pointed_type;

    const Nodes returned_types = check_callsite_helper(callee_type, extract_types(arena, callc.args));

    const Type* ret_cont_type;
    bool ret_cont_uniform;
    deconstruct_operand_type(callc.ret_cont->type, &ret_cont_type, &ret_cont_uniform);
    if (callc.is_return_indirect) {
        assert(ret_cont_type->tag == PtrType_TAG);
        ret_cont_type = ret_cont_type->payload.ptr_type.pointed_type;
    }

    check_callsite_helper(ret_cont_type, returned_types);

    return NULL;
}

const Type* check_type_fn_addr(IrArena* arena, FnAddr fn_addr) {
    assert(!contains_qualified_type(fn_addr.fn->type));
    assert(fn_addr.fn->tag == Function_TAG);
    return qualified_type(arena, (QualifiedType) {
        .is_uniform = true,
        .type = ptr_type(arena, (PtrType) {
            .pointed_type = fn_addr.fn->type,
            .address_space = AsProgramCode,
        })
    });
}

#pragma GCC diagnostic pop