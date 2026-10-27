/* .bss of front-end objects that define their own variables (ultralib objects
 * and game files), at their original
 * addresses (NOLOAD). Listed before the splat linker script so it takes
 * precedence over its /DISCARD/ (see hd_code_bss.us.v10.ld). */
SECTIONS
{
    .bss_ul_conteepread 0x8021AB70 (NOLOAD) : { build/us.v10/src.us.v11/hd_front_end/ul_conteepread.c.o(.bss); }
    .bss_ul_pfsisplug 0x8021ABB0 (NOLOAD) : { build/us.v10/src.us.v11/hd_front_end/ul_pfsisplug.c.o(.bss); }
    .bss_11530 0x80218030 (NOLOAD) : { build/us.v10/src.us.v11/hd_front_end/11530.c.o(.bss); }
}
