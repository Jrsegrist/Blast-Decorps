# Linux test tools for the audio interpreter (port/src/audio/aspmain.c).
#   rsp_tap.so   mupen64plus RSP plugin: LLE RSP (cxd4) + aspmain on every audio task
#   ai_dump.so   mupen64plus audio plugin: the AI stream to a WAV file
#   asp_replay   rerun captured tasks offline
#   wavcmp       compare two WAV files section by section
B ?= build
CFLAGS := -O2 -g -Wall -Wextra -Wno-unused-parameter
ASP := ../../src/audio/aspmain.c ../../src/audio/aspmain.h

all: $(B)/rsp_tap.so $(B)/ai_dump.so $(B)/asp_replay $(B)/asp_lle $(B)/asp_dump $(B)/asp_listdiff $(B)/wavcmp

$(B)/asp_dump: asp_dump.c capture.h capfile.h ranges.h | $(B)
	gcc $(CFLAGS) -o $@ asp_dump.c
$(B)/asp_listdiff: asp_listdiff.c capture.h capfile.h ranges.h | $(B)
	gcc $(CFLAGS) -o $@ asp_listdiff.c
$(B)/asp_lle: asp_lle.c capture.h capfile.h ranges.h m64p_min.h $(ASP) | $(B)
	gcc $(CFLAGS) -rdynamic -o $@ asp_lle.c -ldl

$(B)/rsp_tap.so: rsp_tap.c capture.h ranges.h m64p_min.h $(ASP) | $(B)
	gcc $(CFLAGS) -shared -fPIC -fvisibility=hidden -o $@ rsp_tap.c ../../src/audio/aspmain.c -ldl
$(B)/ai_dump.so: ai_dump.c m64p_min.h | $(B)
	gcc $(CFLAGS) -shared -fPIC -fvisibility=hidden -o $@ ai_dump.c
$(B)/asp_replay: asp_replay.c capture.h capfile.h ranges.h $(ASP) | $(B)
	gcc $(CFLAGS) -o $@ asp_replay.c ../../src/audio/aspmain.c
$(B)/wavcmp: wavcmp.c | $(B)
	gcc $(CFLAGS) -o $@ wavcmp.c -lm
$(B):
	mkdir -p $@
clean:
	rm -rf $(B)
.PHONY: all clean
