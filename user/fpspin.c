#include "libc.h"

/* FPU/SSE kontextus teszt - "zavaró" task.
 * Folyamatosan FP munkát végez saját mintázattal, sok yield-del. Ha a
 * scheduler nem őrizné meg az XMM regisztereket, vagy ennek a tasknak az
 * állapota szivárogna át a fptest-be (vagy fordítva), a válasz elromlik. */

void _start(void) {
    double acc = 0.0;
    for (int round = 0; round < 200; round++) {
        acc = 0.0;
        for (int i = 1; i <= 1000; i++) {
            acc += (double)i * 0.5;
        }
        /* 1+2+...+1000 = 500500, fele 250250 - pontosan ábrázolható */
        if (acc != 250250.0) {
            print("[fpspin] FAIL: FP eredmeny elromlott!\n");
            exit(1);
        }
        yield();
    }
    print("[fpspin] PASS: 200 kor FP szamitas konzisztens\n");
    exit(0);
}
