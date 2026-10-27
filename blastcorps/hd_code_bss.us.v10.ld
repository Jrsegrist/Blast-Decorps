/* .bss of libultra objects built from ultralib, at their original
 * addresses (past the end of the ROM image, so NOLOAD). Listed before the
 * splat linker script so it takes precedence over its /DISCARD/. */
SECTIONS
{
    .bss_ul_initialize 0x803FCCD0 (NOLOAD) : { build/us.v10/src.us.v11/hd_code/ul_initialize.c.o(.bss); }
    .bss_ul_pimgr 0x803FCCE0 (NOLOAD) : { build/us.v10/src.us.v11/hd_code/ul_pimgr.c.o(.bss); }
    .bss_ul_rotate 0x803FDEB0 (NOLOAD) : { build/us.v10/src.us.v11/hd_code/ul_rotate.c.o(.bss); }
    .bss_ul_vimgr 0x803FDEC0 (NOLOAD) : { build/us.v10/src.us.v11/hd_code/ul_vimgr.c.o(.bss); }
    .bss_ul_seteventmesg 0x803FF0D0 (NOLOAD) : { build/us.v10/src.us.v11/hd_code/ul_seteventmesg.c.o(.bss); }
    .bss_ul_sptask 0x803FF150 (NOLOAD) : { build/us.v10/src.us.v11/hd_code/ul_sptask.c.o(.bss); }
    .bss_ul_controller 0x803FF190 (NOLOAD) : { build/us.v10/src.us.v11/hd_code/ul_controller.c.o(.bss); }
    .bss_ul_piacs 0x803FF220 (NOLOAD) : { build/us.v10/src.us.v11/hd_code/ul_piacs.c.o(.bss); }
    .bss_ul_timerintr 0x803FF240 (NOLOAD) : { build/us.v10/src.us.v11/hd_code/ul_timerintr.c.o(.bss); }
    .bss_ul_siacs 0x803FF280 (NOLOAD) : { build/us.v10/src.us.v11/hd_code/ul_siacs.c.o(.bss); }
    .bss_ul_kdebugserver 0x803FF2A0 (NOLOAD) : { build/us.v10/src.us.v11/hd_code/ul_kdebugserver.c.o(.bss); }
    .bss_00000 0x80310CD0 (NOLOAD) : { build/us.v10/src.us.v11/hd_code/00000.c.o(.bss); }
    .bss_22EE0 0x80367FA0 (NOLOAD) : { build/us.v10/src.us.v11/hd_code/22EE0.c.o(.bss); }
    .bss_2C560 0x8036BE40 (NOLOAD) : { build/us.v10/src.us.v11/hd_code/2C560.c.o(.bss); }
    .bss_2D810 0x8036BF10 (NOLOAD) : { build/us.v10/src.us.v11/hd_code/2D810.c.o(.bss); }
    .bss_30430 0x8036C6C0 (NOLOAD) : { build/us.v10/src.us.v11/hd_code/30430.c.o(.bss); }
    .bss_54E30 0x803A6A40 (NOLOAD) : { build/us.v10/src.us.v11/hd_code/54E30.c.o(.bss); }
}
