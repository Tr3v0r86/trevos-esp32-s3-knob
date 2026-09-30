#include <assert.h>
#include <stdio.h>
#include "wheel_decode.h"
static int run(const int *raw, int n) { int prev = 0, s = 0; for (int i = 0; i < n; i++) s += wheel_decode(&prev, raw[i]); return s; }
int main(void) {
    int cw[87], ccw[87];                      /* June fixture: 29 detents of 0 -> +/-1 -> 0 */
    for (int i = 0; i < 29; i++) { cw[3*i]=0; cw[3*i+1]=1; cw[3*i+2]=0; ccw[3*i]=0; ccw[3*i+1]=-1; ccw[3*i+2]=0; }
    assert(run(cw, 87) == 29 && run(ccw, 87) == -29);
    int rev[] = {0, 1, 0, -1, 0, 1, 0};       /* reversal burst at 1 ms sampling */
    assert(run(rev, 7) == 1);
    int slow[] = {0, 0};                      /* 8 ms polling missed a 3 ms excursion (C2) */
    assert(run(slow, 2) == 0);
    puts("PASS wheel_decode"); return 0;
}
