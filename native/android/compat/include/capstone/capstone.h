#ifndef DINGOO_PIE_ANDROID_COMPAT_CAPSTONE_H
#define DINGOO_PIE_ANDROID_COMPAT_CAPSTONE_H

#include <stddef.h>
#include <stdint.h>

typedef size_t csh;
typedef int cs_err;
typedef struct cs_insn {
    uint64_t address;
    uint16_t size;
    char mnemonic[32];
    char op_str[160];
} cs_insn;

#define CS_ARCH_MIPS 0
#define CS_MODE_MIPS32 0
#define CS_ERR_OK 0
#define CS_ERR_ARCH 2

#ifdef __cplusplus
extern "C" {
#endif
cs_err cs_open(int arch, int mode, csh* handle);
size_t cs_disasm(csh handle, const uint8_t* code, size_t code_size,
    uint64_t address, size_t count, cs_insn** insn);
void cs_free(cs_insn* insn, size_t count);
cs_err cs_close(csh* handle);
#ifdef __cplusplus
}
#endif

#endif
