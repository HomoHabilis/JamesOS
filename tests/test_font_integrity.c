/* Check bitmap bounds and nonblank glyphs without firmware or a device. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../firmware/src/james_font.h"

int main(void) {
    for (unsigned c=32; c<=126; ++c) {
        uint32_t w=james_font_width((uint8_t)c), ink=0;
        assert(w>0 && w<=8);
        for (unsigned row=0; row<8; ++row) {
            uint32_t bits=james_font_row((uint8_t)c,row);
            assert(bits < (1u<<w)); ink|=bits;
        }
        if(c==' ') assert(ink==0); else if(!ink) {
            fprintf(stderr,"blank glyph U+%04X\n",c); return 1;
        }
        assert(james_font_row((uint8_t)c,8)==0);
    }
    /* Numeric columns and values use the same cell width as the capitals. */
    for (unsigned c = '0'; c <= '9'; ++c) {
        assert(james_font_width((uint8_t)c) == james_font_width('A'));
        for (unsigned other = c + 1; other <= '9'; ++other) {
            unsigned differs = 0;
            for (unsigned row = 0; row < 8; ++row)
                differs |= james_font_row((uint8_t)c, row) != james_font_row((uint8_t)other, row);
            assert(differs); /* Every value must remain distinguishable. */
        }
    }
    /* The plus sign is a centered 5x5 cross, with one-pixel arms. */
    {
        static const uint8_t plus[8] = {0,0,4,4,31,4,4,0};
        assert(james_font_width('+') == 5u);
        for (unsigned row=0; row<8; ++row) assert(james_font_row('+',row) == plus[row]);
    }
    /* M's left serifs extend one pixel beyond its upright. */
    assert(james_font_width('M') == 6u);
    assert(james_font_row('M', 0) == 0x31u);
    assert(james_font_row('M', 5) == 0x11u);
    assert(james_font_row('M', 6) == 0x31u);
    puts("PASS: all 95 printable ASCII glyphs have bounded bitmap rows; only space is blank");
    return 0;
}
