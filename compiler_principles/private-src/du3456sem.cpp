#include "du3456sem.hpp"
#include "duerr.hpp"

namespace mlc
{
bool checkNumber(std::string input, int &result)
{
    std::vector<int> bignum;
    bool overflow = false;
    result = 0;

    for (auto &&c : input)
    {
        bignum.push_back(c - '0');
    }

    for (size_t i = 0; i < 31 && !overflow; ++i)
    {
        int digit = divideBignum(bignum, overflow);
        result |= digit << i;
    }

    return overflow;
}

int divideBignum(std::vector<int> &bignum, bool &overflow)
{
    int remainder = 0;
    for (size_t i = 0; i < bignum.size(); ++i)
    {
        int digit = remainder * 10 + bignum[i];
        remainder = digit % 2;
        bignum[i] = digit / 2;

        overflow = bignum[i] == 0 && (overflow || i == 0);
    }

    return remainder;
}
int totalArrayDimension(mlc::type_pointer arrType)
{
    int dim = 1;
    while (arrType->cat() == TCAT_ARRAY)
    {
        auto rangeType = arrType->access_array()->index_type()->access_range();
        auto lowerBound = rangeType->lowerBound();
        auto upperBound = rangeType->upperBound();

        dim *= (*upperBound - *lowerBound + 1);

        arrType = arrType->access_array()->element_type();
    }

    return dim;
}

static void store_temp_variable(mlc::icblock_pointer block, mlc::type_category type, mlc::stack_address addr)
{
    switch (type)
    {
    case TCAT_BOOL:
        block->append<ai::LSTB>(addr);
        break;
    case TCAT_INT:
        block->append<ai::LSTI>(addr);
        break;
    case TCAT_REAL:
        block->append<ai::LSTR>(addr);
        break;
    case TCAT_STR:
        block->append<ai::LSTS>(addr);
        break;
    }
}

static void store_glob_variable(mlc::icblock_pointer block, mlc::type_category type, mlc::stack_address addr)
{
    switch (type)
    {
    case TCAT_BOOL:
        block->append<ai::GSTB>(addr);
        break;
    case TCAT_INT:
        block->append<ai::GSTI>(addr);
        break;
    case TCAT_REAL:
        block->append<ai::GSTR>(addr);
        break;
    case TCAT_STR:
        block->append<ai::GSTS>(addr);
        break;
    }
}

void store_variable(mlc::icblock_pointer block, mlc::typed_symbol_pointer tsp)
{
    switch (tsp->kind())
    {
    case SKIND_LOCAL_VARIABLE:
        store_temp_variable(block, tsp->type()->cat(), tsp->access_local_variable()->address());
        break;
    case SKIND_PARAMETER_BY_REFERENCE:
        store_temp_variable(block, tsp->type()->cat(), tsp->access_parameter_by_reference()->address());
        break;
    case SKIND_GLOBAL_VARIABLE:
    {
        store_glob_variable(block, tsp->type()->cat(), tsp->access_global_variable()->address());
        break;
    }
    default:
        break;
    }
}

void load_variable(mlc::icblock_pointer block, mlc::typed_symbol_pointer tsp)
{
    auto type = tsp->type()->cat();
    switch (tsp->kind())
    {
    case SKIND_LOCAL_VARIABLE:
    {
        auto addr = tsp->access_local_variable()->address();
        switch (type)
        {
        case TCAT_BOOL:
            block->append<ai::LLDB>(addr);
            break;
        case TCAT_INT:
            block->append<ai::LLDI>(addr);
            break;
        case TCAT_REAL:
            block->append<ai::LLDR>(addr);
            break;
        case TCAT_STR:
            block->append<ai::LLDS>(addr);
            break;
        case TCAT_ARRAY:
            block->append<ai::LREF>(addr);
            break;
        }
        break;
    }
    case SKIND_GLOBAL_VARIABLE:
    {
        auto addr = tsp->access_global_variable()->address();
        switch (type)
        {
        case TCAT_BOOL:
            block->append<ai::GLDB>(addr);
            break;
        case TCAT_INT:
            block->append<ai::GLDI>(addr);
            break;
        case TCAT_REAL:
            block->append<ai::GLDR>(addr);
            break;
        case TCAT_STR:
            block->append<ai::GLDS>(addr);
            break;
        case TCAT_ARRAY:
            block->append<ai::GREF>(addr);
            break;
        }
        break;
    }
    }
}

void ref_variable(mlc::icblock_pointer block, mlc::typed_symbol_pointer tsp)
{
    switch (tsp->kind())
    {
    case SKIND_LOCAL_VARIABLE:
        block->append<ai::LREF>(tsp->access_local_variable()->address());
        break;
    case SKIND_PARAMETER_BY_REFERENCE:
        block->append<ai::LLDP>(tsp->access_parameter_by_reference()->address());
        break;
    case SKIND_GLOBAL_VARIABLE:
    {
        block->append<ai::GREF>(tsp->access_global_variable()->address());
        break;
    }
    default:
        break;
    }
}

static void append_many(mlc::MlaskalCtx *ctx, mlc::icblock_pointer out, mlc::icblock_pointer body, mlc::symbol_pointer var, int count)
{
    auto varTyped = var->access_typed();
    auto label_loop = ctx->tab->new_label();
    auto label_cond = ctx->tab->new_label();

    out->append<ai::LDLITI>(ctx->tab->ls_int().add(0));
    store_variable(out, varTyped);

    out->append_with_target<ai::JMP>(label_cond);

    out->add_label(label_loop);
    out = mlc::icblock_merge_and_kill(out, body);

    load_variable(out, varTyped);
    out->append<ai::LDLITI>(ctx->tab->one());
    out->append<ai::ADDI>();
    store_variable(out, varTyped);

    out->add_label(label_cond);

    load_variable(out, varTyped);
    out->append<ai::LDLITI>(ctx->tab->ls_int().add(count));
    out->append<ai::EQI>();
    out->append_with_target<ai::JF>(label_loop);
}

void load_array(mlc::MlaskalCtx *ctx, mlc::icblock_pointer block, mlc::icblock_pointer src, mlc::type_pointer arrType)
{
    auto elemType = arrType;
    while (elemType->cat() == TCAT_ARRAY)
    {
        elemType = elemType->access_array()->element_type();
    }

    int elemCount = totalArrayDimension(arrType);
    for (int i = 0; i < elemCount; ++i)
    {
        auto loop_block = mlc::icblock_create();
        loop_block = mlc::icblock_merge_and_kill(loop_block, src);
        auto type = elemType->cat();
        switch (type)
        {
        case TCAT_BOOL:
            block->append<ai::XLDB>();
            break;
        case TCAT_INT:
            block->append<ai::XLDI>();
            break;
        case TCAT_REAL:
            block->append<ai::XLDR>();
            break;
        case TCAT_STR:
            block->append<ai::XLDS>();
            break;
        }
        block = mlc::icblock_merge_and_kill(loop_block, block);
    }
}

void store_array(mlc::MlaskalCtx *ctx, mlc::icblock_pointer block, int arrLine, mlc::type_pointer arrType, mlc::exprConst &arr, mlc::exprConst &expr)
{

    auto elemType = arrType;
    while (elemType->cat() == TCAT_ARRAY)
    {
        elemType = elemType->access_array()->element_type();
    }

    int elemCount = totalArrayDimension(arrType);
    auto loop_block = mlc::icblock_create();
    loop_block = mlc::icblock_merge_and_kill(loop_block, expr.code);
    for (int i = 0; i < elemCount; ++i)
    {
        auto type = elemType->cat();
        switch (type)
        {
        case TCAT_BOOL:
            block->append<ai::XLDB>();
            break;
        case TCAT_INT:
            block->append<ai::XLDI>();
            break;
        case TCAT_REAL:
            block->append<ai::XLDR>();
            break;
        case TCAT_STR:
            block->append<ai::XLDS>();
            break;
        }

        switch (type)
        {
        case TCAT_BOOL:
            block->append<ai::XSTB>();
            break;
        case TCAT_INT:
            block->append<ai::XSTI>();
            break;
        case TCAT_REAL:
            block->append<ai::XSTR>();
            break;
        case TCAT_STR:
            block->append<ai::XSTS>();
            break;
        }
        block = mlc::icblock_merge_and_kill(loop_block, block);
    }
}

};