/* Pixel-art circles, round caps and symmetric arcs must mirror on the 2px grid. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>

static void symmetry(uint16_t color, int horizontal, int vertical, int cell_pixels)
{
    int left=60,top=60,right=-1,bottom=-1;
    uint16_t pixel=swap16(color);
    for(int y=0;y<60;y++)for(int x=0;x<60;x++)if(cv_px[y*60+x]==pixel) {
        if(x<left)left=x;if(x>right)right=x;if(y<top)top=y;if(y>bottom)bottom=y;
    }
    assert(right>=left && bottom>=top);
    if(cell_pixels==2)for(int y=0;y<60;y+=2)for(int x=0;x<60;x+=2) {
        int a=cv_px[y*60+x]==pixel;
        assert((cv_px[y*60+x+1]==pixel)==a);
        assert((cv_px[(y+1)*60+x]==pixel)==a);
        assert((cv_px[(y+1)*60+x+1]==pixel)==a);
    }
    for(int y=top;y<=bottom;y++)for(int x=left;x<=right;x++) {
        int a=cv_px[y*60+x]==pixel;
        if(horizontal)assert((cv_px[y*60+left+right-x]==pixel)==a);
        if(vertical)assert((cv_px[(top+bottom-y)*60+x]==pixel)==a);
    }
}
int main(void)
{
    for(int cx=27;cx<=30;cx++)for(int cy=27;cy<=30;cy++)for(int r=2;r<=14;r++) {
        cv_begin(60,60,C_BLACK);br_shape_circle(bw_q8(cx),bw_q8(cy),r*256u,0,C_WHITE,1);symmetry(C_WHITE,1,1,2);
        cv_begin(60,60,C_BLACK);br_shape_circle(bw_q8(cx),bw_q8(cy),r*256u,512u,C_WHITE,0);symmetry(C_WHITE,1,1,2);
    }
    for(int cx=27;cx<=30;cx++)for(int cy=27;cy<=30;cy++)for(int r=2;r<=14;r++) {
        cv_begin(60,60,C_BLACK);br_shape_circle_grid(bw_q8(cx),bw_q8(cy),r*256u,0,C_WHITE,1,1);symmetry(C_WHITE,1,1,1);
        cv_begin(60,60,C_BLACK);br_shape_circle_grid(bw_q8(cx),bw_q8(cy),r*256u,256u,C_WHITE,0,1);symmetry(C_WHITE,1,1,1);
        cv_begin(60,60,C_BLACK);br_shape_circle_grid(bw_q8(cx),bw_q8(cy),r*256u,512u,C_WHITE,0,2);symmetry(C_WHITE,1,1,2);
    }
    cv_begin(60,60,C_BLACK);bw_line(15,30,45,30,5120u,C_WHITE);symmetry(C_WHITE,1,1,2);
    for(int q=0;q<=127;q+=127){cv_begin(60,60,C_BLACK);bw_toggle(0,0,60,q);symmetry(C_WHITE,1,1,2);}
    for(int r=5;r<=22;r++)for(int center=27;center<=30;center++) {
        cv_begin(60,60,C_BLACK);bw_arc(bw_q8(center),bw_q8(center),r*256u,512u,40960u,49152u,C_WHITE);symmetry(C_WHITE,1,0,2);
        cv_begin(60,60,C_BLACK);bw_arc(bw_q8(center),bw_q8(center),r*256u,512u,49152u,32768u,C_WHITE);symmetry(C_WHITE,1,0,2);
    }
    for (uint32_t q = 0u; q <= 127u; ++q) for (uint32_t open = 0u; open <= 1u; ++open) {
        cv_begin(60, 60, C_BLACK); bw_gate(0, 0, 60u, q, open);
        for (uint32_t y = 0u; y < 60u; ++y) for (uint32_t x = 0u; x < 60u; ++x)
            assert(cv_px[y * 60u + x] == cv_px[y * 60u + 59u - x]);
    }
    {
        static uint16_t narrow[60u * 60u], medium[60u * 60u];
        cv_begin(60, 60, C_BLACK); bw_gate(0, 0, 60u, 8u, 0u); memcpy(narrow, cv_px, sizeof narrow);
        cv_begin(60, 60, C_BLACK); bw_gate(0, 0, 60u, 64u, 0u); memcpy(medium, cv_px, sizeof medium);
        assert(memcmp(narrow, medium, sizeof narrow));
        cv_begin(60, 60, C_BLACK); bw_gate(0, 0, 60u, 120u, 0u);
        assert(memcmp(medium, cv_px, sizeof medium));
    }
    puts("PASS: 1x1 and 2x2 circles, rounded tracks, toggle pucks, symmetric arcs and gates");
}
