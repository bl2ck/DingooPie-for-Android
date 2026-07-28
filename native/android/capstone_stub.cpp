#include <capstone/capstone.h>

cs_err cs_open(int, int, csh* handle)
{
    if (handle) *handle = 0;
    return CS_ERR_ARCH;
}

size_t cs_disasm(csh, const uint8_t*, size_t, uint64_t, size_t, cs_insn** insn)
{
    if (insn) *insn = nullptr;
    return 0;
}

void cs_free(cs_insn*, size_t)
{
}

cs_err cs_close(csh* handle)
{
    if (handle) *handle = 0;
    return CS_ERR_OK;
}
