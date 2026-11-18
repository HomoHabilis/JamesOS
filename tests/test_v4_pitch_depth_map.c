#include "../tools/legacy_pitch_depth_map.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    for (int old=-127; old<=127; ++old) {
        int mapped=jamesos_v16_pitch_depth(old);
        unsigned target=(unsigned)(old<0?-old:old)*256u;
        unsigned mag=(unsigned)(mapped<0?-mapped:mapped);
        unsigned actual=mag*mag*256u/127u;
        unsigned err=actual>target?actual-target:target-actual;
        assert((old<0 && mapped<=0) || (old>=0 && mapped>=0));
        for (unsigned i=0; i<128; ++i) {
            unsigned candidate=i*i*256u/127u;
            unsigned e=candidate>target?candidate-target:target-candidate;
            assert(err<=e);
        }
    }
    puts("all signed legacy pitch depths map to the closest current peak");
    return 0;
}
