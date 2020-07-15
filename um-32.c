#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <ctype.h>

typedef struct Buffer {
    uint8_t *data;
    size_t len;
} Buffer;

void free_buffer(Buffer b)
{
    free(b.data);
}

typedef struct Mem {
    uint32_t *inst;
    size_t len;
    bool active;
} Mem;

void free_mem(Mem m)
{
    free(m.inst);
}

// Machine state
uint32_t PC;
uint32_t R[8];  // registers
Mem *M;         // memory arrays
uint32_t memarr_count;
bool halted;

typedef enum Op {
    CMOV = 0,
    ARRAY_INDEX = 1,
    ARRAY_AMEND = 2,
    ADD = 3,
    MUL = 4,
    DIV = 5,
    NAND = 6,
    HALT = 7,
    ALLOC = 8,
    ABANDON = 9,
    OUTPUT = 10,
    INPUT = 11,
    LOAD_PROG = 12,
    ORTHOG = 13,
} Op;

void *xmalloc(size_t size)
{
    void *ptr = malloc(size);
    if (!ptr) {
        perror("xmalloc failed");
        exit(1);
    }
    return ptr;
}

void *xcalloc(size_t nmemb, size_t size)
{
    void *ptr = calloc(nmemb, size);
    if (!ptr) {
        perror("xcalloc failed");
        exit(1);
    }
    return ptr;
}

void *xrealloc(void *oldptr, size_t newsize)
{
    void *ptr = realloc(oldptr, newsize);
    if (!ptr) {
        perror("xrealloc failed");
        exit(1);
    }
    return ptr;
}

static void um_32_init(Buffer prog)
{
    PC = 0;
    memset(R, 0, 4 * 8);
    Mem m0 = {0};
    uint32_t ninst = prog.len / 4;
    m0.inst = xmalloc(4 * ninst);
    m0.len = ninst;
    m0.active = true;
    for (int i = 0; i < prog.len; i += 4) {
        uint32_t inst = prog.data[i + 0] << 24 |
                        prog.data[i + 1] << 16 |
                        prog.data[i + 2] <<  8 |
                        prog.data[i + 3] <<  0 ;
        m0.inst[i/4] = inst;
    }
    M = xmalloc(sizeof(Mem));
    memarr_count = 1;
    M[0] = m0;
    halted = false;
}

static void um_32_print_debug_state(void)
{
    printf("PC=%u ", PC);
    for (int i = 0; i < 8; i++) {
        printf("R[%d]=%u ", i, R[i]);
    }
    printf("\n");
}

static const char *um_32_op_name(Op op)
{
    switch (op) {
        case CMOV:
            return "cmov";
        case ARRAY_INDEX:
            return "arrind";
        case ARRAY_AMEND:
            return "arramend";
        case ADD:
            return "add";
        case MUL:
            return "mul";
        case DIV:
            return "div";
        case NAND:
            return "nand";
        case HALT:
            return "halt";
        case ALLOC:
            return "alloc";
        case ABANDON:
            return "abandon";
        case OUTPUT:
            return "output";
        case INPUT:
            return "input";
        case LOAD_PROG:
            return "loadprog";
        case ORTHOG:
            return "orthog";
        default:
            return "UNKNOWNOP";
    }
}

static void um_32_print_debug_inst(uint32_t inst)
{
    uint8_t opnum = (inst >> 28) & 0xf;
    uint8_t reg_a = (inst >>  6) & 0x7;
    uint8_t reg_b = (inst >>  3) & 0x7;
    uint8_t reg_c = (inst >>  0) & 0x7;
    const char *name = um_32_op_name((Op)opnum);
    printf("%s\tA:%d\tB:%d\tC:%d\n", name, reg_a, reg_b, reg_c);
}

#define EXCEPTION(inst) { \
    um_32_print_debug_inst(inst); \
    um_32_print_debug_state(); \
    assert(false); \
}

static void um_32_spin_cycle(void)
{
    while (!halted) {
        if (PC >= M[0].len) {
            EXCEPTION(0);
        }
        // FETCH INSTRUCTION
        uint32_t inst = M[0].inst[PC];
#ifdef DEBUG
        um_32_print_debug_inst(inst);
		um_32_print_debug_state();
#endif
        // ADVANCE PC
        PC += 1;
        // DECODE INSTRUCTION
        uint32_t opnum = (inst >> 28) & 0xf;
        uint32_t reg_a = (inst >>  6) & 0x7;
        uint32_t reg_b = (inst >>  3) & 0x7;
        uint32_t reg_c = (inst >>  0) & 0x7;
        // DISPATCH INSTRUCTION
        switch (opnum) {
            case CMOV:
                if (R[reg_c] != 0) {
                    R[reg_a] = R[reg_b];
                }
                break;
            case ARRAY_INDEX:
                {
                    uint32_t idx = R[reg_b];
                    if (!M[idx].active || idx > (memarr_count - 1)) {
                        EXCEPTION(inst);
                    }
                    uint32_t off = R[reg_c];
                    R[reg_a] = M[idx].inst[off];
                }
                break;
            case ARRAY_AMEND:
                {
                    uint32_t idx = R[reg_a];
                    if (!M[idx].active || idx > (memarr_count - 1)) {
                        EXCEPTION(inst);
                    }
                    uint32_t off = R[reg_b];
                    M[idx].inst[off] = R[reg_c];
                }
                break;
            case ADD:
                R[reg_a] = R[reg_b] + R[reg_c];
                break;
            case MUL:
                R[reg_a] = R[reg_b] * R[reg_c];
                break;
            case DIV:
                R[reg_a] = R[reg_b] / R[reg_c];
                break;
            case NAND:
                R[reg_a] = ~(R[reg_b] & R[reg_c]);
                break;
            case HALT:
                halted = true;
                break;
            case ALLOC:
                {
                    memarr_count += 1;
                    uint32_t idx = memarr_count - 1;
                    M = xrealloc(M, sizeof(Mem) * memarr_count);
                    M[idx].inst = xcalloc(1, R[reg_c] * 4);
                    M[idx].len = R[reg_c];
                    M[idx].active = true;
                    R[reg_b] = idx;
                }
                break;
            case ABANDON:
                {
                    uint32_t idx = R[reg_c];
                    if (idx == 0 || !M[idx].active) {
                        EXCEPTION(inst);
                    }
                    M[idx].active = false;
                }
                break;
            case OUTPUT:
                putchar(R[reg_c]);
                break;
            case INPUT:
                {
                    int c = getchar();
                    if (c != EOF) {
                        R[reg_c] = (uint8_t)c;
                    } else {
                        R[reg_c] = 0xffffffff;
                    }
                }
                break;
            case LOAD_PROG:
                {
                    uint32_t idx = R[reg_b];
                    if (idx != 0) {
                        Mem src = M[idx];
#if 0
                        fprintf(stderr, "** LOADING PROGRAM %d (%ld bytes)\n", idx, src.len * 4);
#endif
                        Mem dest = {0};
                        dest.inst = xmalloc(src.len * 4);
                        memcpy(dest.inst, src.inst, src.len * 4);
                        dest.len = src.len;
                        dest.active = true;
                        Mem old = M[0];
                        M[0] = dest;
                        free_mem(old);
                    }
                    PC = R[reg_c];
                }
                break;
            case ORTHOG:
                {
                    uint8_t reg_a = (inst >> 25) & 0x7;
                    uint32_t val = inst & 0x1ffffff;
                    R[reg_a] = val;
                }
                break;
            default:
                EXCEPTION(inst);
        }
    }
#if 0
    fprintf(stderr, "\n** Program halted.\n");
#endif
}

void usage()
{
    fprintf(stderr, "Usage: %s program\n", program_invocation_name);
    exit(1);
}

Buffer read_entire_file(FILE *f)
{
    fseek(f, 0L, SEEK_END);
    long size = ftell(f);
    rewind(f);
    Buffer buf = {0};
    buf.data = xmalloc(size);
    buf.len = size;
    if (fread(buf.data, size, 1, f) != 1) {
        fprintf(stderr, "error reading entire file\n");
        exit(1);
    }
    return buf;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        usage();
    }

    FILE *f = fopen(argv[1], "r");
    if (!f) {
        perror("opening program file");
        return 1;
    }
    Buffer prog = read_entire_file(f);
    fclose(f);

    um_32_init(prog);
#if 0
    fprintf(stderr, "** UM-32 initialized, program %lu bytes.\n", prog.len);
#endif
    free_buffer(prog);

    um_32_spin_cycle();

    return 0;
}
