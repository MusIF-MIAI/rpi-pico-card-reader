/*
 * transcode.c -- card column (13-bit hole mask) to byte decoders.
 *
 * Ported from gemu transcode.c (verbatim semantics). Only the modes needed
 * for program loading are implemented: TC_HEX (loader card) and TC_COLBIN
 * (by-pass program cards), plus TC_BINARY passthrough. TC_NORMAL's 8192-entry
 * Hollerith table is not needed to load decks and is deliberately omitted;
 * it returns the GE blank (0x20) for now.
 */
#include "ge_proto.h"

uint8_t transcode_column(uint16_t column, enum tc_mode mode)
{
    if (mode == TC_BINARY)
        return (uint8_t)(column & 0xFFu);

    if (mode == TC_HEX) {
        /* Loader hex-nibble encoding: the nibble is the SUM of the punched
         * digit-row indices (rows 0..9); zone rows ignored. Two columns are
         * channel-packed into one byte. (gemu transcode.c:574-588, verified
         * against the CPU[1] loader listing.) */
        unsigned n = 0;
        for (int b = 0; b <= 9; b++)
            if (column & (1u << b))
                n += (unsigned)b;
        return (uint8_t)(n & 0x0Fu);
    }

    if (mode == TC_COLBIN) {
        /* By-pass / column-binary: one column -> one byte.
         * byte bit i <- card row B2R[i], B2R = {9,8,7,6,3,2,1,0};
         * rows 4,5,11,12 unused; row 0 is the MSB.
         * (gemu transcode.c:590-607; CPU[1] dwg 4T4714100UA fo.53a.) */
        static const int b2r[8] = {9, 8, 7, 6, 3, 2, 1, 0};
        uint8_t out = 0;
        for (int i = 0; i < 8; i++)
            if (column & (1u << b2r[i]))
                out |= (uint8_t)(1u << i);
        return out;
    }

    /* TC_NORMAL: not needed for loading; table omitted on purpose. */
    return 0x20;
}
