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

typedef struct Mem {
    uint32_t *inst;
    size_t len;
    bool active;
} Mem;

uint64_t PC;
uint32_t R[8];  // registers
Mem *M;         // memory arrays
uint32_t memarr_count;
bool halted;

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
    assert(prog.len && prog.len % 4 == 0);
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
    printf("PC=%lu ", PC);
    for (int i = 0; i < 8; i++) {
        printf("R[%d]=%u ", i, R[i]);
    }
    printf("\n");
}

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

static const char *um_32_op_name(Op op)
{
    switch (op) {
        case CMOV:
            return "cmov";
            break;
        case ARRAY_INDEX:
            return "arrind";
            break;
        case ARRAY_AMEND:
            return "arramend";
            break;
        case ADD:
            return "add";
            break;
        case MUL:
            return "mul";
            break;
        case DIV:
            return "div";
            break;
        case NAND:
            return "nand";
            break;
        case HALT:
            return "halt";
            break;
        case ALLOC:
            return "alloc";
            break;
        case ABANDON:
            return "abandon";
            break;
        case OUTPUT:
            return "output";
            break;
        case INPUT:
            return "input";
            break;
        case LOAD_PROG:
            return "loadprog";
            break;
        case ORTHOG:
            return "orthog";
            break;
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
    Mem m0 = M[0];
    while (!halted) {
        if (PC >= m0.len) {
            EXCEPTION(0);
        }
        // FETCH INSTRUCTION
        uint32_t inst = m0.inst[PC];
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
        switch (opnum) {
            case CMOV: // conditional move
                /*
                  The register A receives the value in register B,
                  unless the register C contains 0.
                  */
                if (R[reg_c] != 0) {
                    R[reg_a] = R[reg_b];
                }
                break;
            case ARRAY_INDEX: // array index
                /*
                  The register A receives the value stored at offset
                  in register C in the array identified by B.
                  */
                {
                    uint32_t idx = R[reg_b];
                    if (!M[idx].active || idx > (memarr_count - 1)) {
                        EXCEPTION(inst);
                    }
                    uint32_t off = R[reg_c];
                    R[reg_a] = M[idx].inst[off];
                }
                break;
            case ARRAY_AMEND: // array amendment
                /*
                  The array identified by A is amended at the offset
                  in register B to store the value in register C.
                  */
                {
                    uint32_t idx = R[reg_a];
                    if (!M[idx].active || idx > (memarr_count - 1)) {
                        EXCEPTION(inst);
                    }
                    uint32_t off = R[reg_b];
                    M[idx].inst[off] = R[reg_c];
                }
                break;
            case ADD: // addition
                /*
                  The register A receives the value in register B plus 
                  the value in register C, modulo 2^32.
                  */
                R[reg_a] = R[reg_b] + R[reg_c];
                break;
            case MUL: // multiplication
                /*
                  The register A receives the value in register B times
                  the value in register C, modulo 2^32.
                  */
                R[reg_a] = R[reg_b] * R[reg_c];
                break;
            case DIV: // division
                /*
                  The register A receives the value in register B
                  divided by the value in register C, if any, where
                  each quantity is treated treated as an unsigned 32
                  bit number.
                */
                R[reg_a] = R[reg_b] / R[reg_c];
                break;
            case NAND: // not-and
                /*
                  Each bit in the register A receives the 1 bit if
                  either register B or register C has a 0 bit in that
                  position.  Otherwise the bit in register A receives
                  the 0 bit.
                */
                R[reg_a] = ~(R[reg_b] & R[reg_c]);
                break;
            case HALT: // halt
                /*
                  The universal machine stops computation.
                */
                halted = true;
                break;
            case ALLOC: // allocation
                /*
                  A new array is created with a capacity of platters
                  commensurate to the value in the register C. This
                  new array is initialized entirely with platters
                  holding the value 0. A bit pattern not consisting of
                  exclusively the 0 bit, and that identifies no other
                  active allocated array, is placed in the B register.
                */
                {
                    printf("Allocating memory (%d -> %d) %d bytes.\n", memarr_count, memarr_count+1, R[reg_c] * 4);
                    memarr_count += 1;
                    uint32_t idx = memarr_count - 1;
                    M = xrealloc(M, sizeof(Mem) * memarr_count);
                    M[idx].inst = xcalloc(1, R[reg_c] * 4);
                    M[idx].len = R[reg_c];
                    M[idx].active = true;
                    R[reg_b] = idx;
                }
                break;
            case ABANDON: // abandon
                /*
                  The array identified by the register C is abandoned.
                  Future allocations may then reuse that identifier.
                  */
                {
                    uint32_t idx = R[reg_c];
                    if (idx == 0 || !M[idx].active) {
                        EXCEPTION(inst);
                    }
                    M[idx].active = false;
                }
                break;
            case OUTPUT: // output
                /*
                  The value in the register C is displayed on the console
                  immediately. Only values between and including 0 and 255
                  are allowed.
                */
                {
                    int c = (int)R[reg_c];
                    if (isprint(c)) {
                        printf("%c", c);
                    } else {
                        printf("0x%02x", c);
                    }
                }
                break;
            case INPUT: // input
                /*
                  The universal machine waits for input on the console.
                  When input arrives, the register C is loaded with the
                  input, which must be between and including 0 and 255.
                  If the end of input has been signaled, then the 
                  register C is endowed with a uniform value pattern
                  where every place is pregnant with the 1 bit.
                  */
                {
                    int c = getchar();
                    if (c != EOF) {
                        R[reg_c] = c & 0xff;
                    } else {
                        R[reg_c] = 0xffffffff;
                    }
                }
                break;
            case LOAD_PROG: // load program
                /*
                  The array identified by the B register is duplicated
                  and the duplicate shall replace the '0' array,
                  regardless of size. The execution finger is placed
                  to indicate the platter of this array that is
                  described by the offset given in C, where the value
                  0 denotes the first platter, 1 the second, et
                  cetera.

                  The '0' array shall be the most sublime choice for
                  loading, and shall be handled with the utmost
                  velocity.
                */
                {
                    uint32_t idx = R[reg_b];
                    Mem m = M[idx];
                    M[0].inst = m.inst;
                    M[0].len = m.len;
                    PC = R[reg_c];
                }
                break;
            case ORTHOG: // orthography
                /*
				  One special operator does not describe registers in the same way.
				  Instead the three bits immediately less significant than the four
				  instruction indicator bits describe a single register A. The
				  remainder twenty five bits indicate a value, which is loaded
				  forthwith into the register A.

								   A  
								   |  
								   vvv
							  .--------------------------------.
							  |VUTSRQPONMLKJIHGFEDCBA9876543210|
							  `--------------------------------'
							   ^^^^   ^^^^^^^^^^^^^^^^^^^^^^^^^
							   |      |
							   |      value
							   |
							   operator number

						   	   Figure 3. Special Operators

                  The value indicated is loaded into the register A
                  forthwith.
                  */
                {
                    uint8_t reg_a = (inst >> 25) & 0x7;
                    uint32_t val = inst & 0x1ffffff;
                    R[reg_a] = val;
                }
                break;
            default:
                EXCEPTION(inst);
        }
        // DISPATCH INSTRUCTION
    }
    printf("\nProgram halted.\n");
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

void free_buffer(Buffer b)
{
    free(b.data);
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
    fprintf(stderr, "UM-32 initialized, program %lu bytes.\n", prog.len);
    free_buffer(prog);

    um_32_spin_cycle();

    return 0;
}
