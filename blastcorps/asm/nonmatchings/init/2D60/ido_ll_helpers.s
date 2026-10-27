glabel __ull_rshift
/* 2D60 80221A60 AFA40000 */  sw         $a0, ($sp)
/* 2D64 80221A64 AFA50004 */  sw         $a1, 4($sp)
/* 2D68 80221A68 AFA60008 */  sw         $a2, 8($sp)
/* 2D6C 80221A6C AFA7000C */  sw         $a3, 0xc($sp)
/* 2D70 80221A70 DFAF0008 */  ld         $t7, 8($sp)
/* 2D74 80221A74 DFAE0000 */  ld         $t6, ($sp)
/* 2D78 80221A78 01EE1016 */  dsrlv      $v0, $t6, $t7
/* 2D7C 80221A7C 0002183C */  dsll32     $v1, $v0, 0
/* 2D80 80221A80 0003183F */  dsra32     $v1, $v1, 0
/* 2D84 80221A84 03E00008 */  jr         $ra
/* 2D88 80221A88 0002103F */   dsra32    $v0, $v0, 0

glabel __ull_rem
/* 2D8C 80221A8C AFA40000 */  sw         $a0, ($sp)
/* 2D90 80221A90 AFA50004 */  sw         $a1, 4($sp)
/* 2D94 80221A94 AFA60008 */  sw         $a2, 8($sp)
/* 2D98 80221A98 AFA7000C */  sw         $a3, 0xc($sp)
/* 2D9C 80221A9C DFAF0008 */  ld         $t7, 8($sp)
/* 2DA0 80221AA0 DFAE0000 */  ld         $t6, ($sp)
/* 2DA4 80221AA4 01CF001F */  ddivu      $zero, $t6, $t7
/* 2DA8 80221AA8 15E00002 */  bnez       $t7, .L80221AB4
/* 2DAC 80221AAC 00000000 */   nop
/* 2DB0 80221AB0 0007000D */  break      7
.L80221AB4:
/* 2DB4 80221AB4 00001010 */   mfhi      $v0
/* 2DB8 80221AB8 0002183C */  dsll32     $v1, $v0, 0
/* 2DBC 80221ABC 0003183F */  dsra32     $v1, $v1, 0
/* 2DC0 80221AC0 03E00008 */  jr         $ra
/* 2DC4 80221AC4 0002103F */   dsra32    $v0, $v0, 0

glabel __ull_div
/* 2DC8 80221AC8 AFA40000 */  sw         $a0, ($sp)
/* 2DCC 80221ACC AFA50004 */  sw         $a1, 4($sp)
/* 2DD0 80221AD0 AFA60008 */  sw         $a2, 8($sp)
/* 2DD4 80221AD4 AFA7000C */  sw         $a3, 0xc($sp)
/* 2DD8 80221AD8 DFAF0008 */  ld         $t7, 8($sp)
/* 2DDC 80221ADC DFAE0000 */  ld         $t6, ($sp)
/* 2DE0 80221AE0 01CF001F */  ddivu      $zero, $t6, $t7
/* 2DE4 80221AE4 15E00002 */  bnez       $t7, .L80221AF0
/* 2DE8 80221AE8 00000000 */   nop
/* 2DEC 80221AEC 0007000D */  break      7
.L80221AF0:
/* 2DF0 80221AF0 00001012 */   mflo      $v0
/* 2DF4 80221AF4 0002183C */  dsll32     $v1, $v0, 0
/* 2DF8 80221AF8 0003183F */  dsra32     $v1, $v1, 0
/* 2DFC 80221AFC 03E00008 */  jr         $ra
/* 2E00 80221B00 0002103F */   dsra32    $v0, $v0, 0

glabel __ll_lshift
/* 2E04 80221B04 AFA40000 */  sw         $a0, ($sp)
/* 2E08 80221B08 AFA50004 */  sw         $a1, 4($sp)
/* 2E0C 80221B0C AFA60008 */  sw         $a2, 8($sp)
/* 2E10 80221B10 AFA7000C */  sw         $a3, 0xc($sp)
/* 2E14 80221B14 DFAF0008 */  ld         $t7, 8($sp)
/* 2E18 80221B18 DFAE0000 */  ld         $t6, ($sp)
/* 2E1C 80221B1C 01EE1014 */  dsllv      $v0, $t6, $t7
/* 2E20 80221B20 0002183C */  dsll32     $v1, $v0, 0
/* 2E24 80221B24 0003183F */  dsra32     $v1, $v1, 0
/* 2E28 80221B28 03E00008 */  jr         $ra
/* 2E2C 80221B2C 0002103F */   dsra32    $v0, $v0, 0

glabel func_80221B30
/* 2E30 80221B30 AFA40000 */  sw         $a0, ($sp)
/* 2E34 80221B34 AFA50004 */  sw         $a1, 4($sp)
/* 2E38 80221B38 AFA60008 */  sw         $a2, 8($sp)
/* 2E3C 80221B3C AFA7000C */  sw         $a3, 0xc($sp)
/* 2E40 80221B40 DFAF0008 */  ld         $t7, 8($sp)
/* 2E44 80221B44 DFAE0000 */  ld         $t6, ($sp)
/* 2E48 80221B48 01CF001F */  ddivu      $zero, $t6, $t7
/* 2E4C 80221B4C 15E00002 */  bnez       $t7, .L80221B58
/* 2E50 80221B50 00000000 */   nop
/* 2E54 80221B54 0007000D */  break      7
.L80221B58:
/* 2E58 80221B58 00001010 */   mfhi      $v0
/* 2E5C 80221B5C 0002183C */  dsll32     $v1, $v0, 0
/* 2E60 80221B60 0003183F */  dsra32     $v1, $v1, 0
/* 2E64 80221B64 03E00008 */  jr         $ra
/* 2E68 80221B68 0002103F */   dsra32    $v0, $v0, 0

glabel __ll_div
/* 2E6C 80221B6C AFA40000 */  sw         $a0, ($sp)
/* 2E70 80221B70 AFA50004 */  sw         $a1, 4($sp)
/* 2E74 80221B74 AFA60008 */  sw         $a2, 8($sp)
/* 2E78 80221B78 AFA7000C */  sw         $a3, 0xc($sp)
/* 2E7C 80221B7C DFAF0008 */  ld         $t7, 8($sp)
/* 2E80 80221B80 DFAE0000 */  ld         $t6, ($sp)
/* 2E84 80221B84 01CF001E */  ddiv       $zero, $t6, $t7
/* 2E88 80221B88 00000000 */  nop
/* 2E8C 80221B8C 15E00002 */  bnez       $t7, .L80221B98
/* 2E90 80221B90 00000000 */   nop
/* 2E94 80221B94 0007000D */  break      7
.L80221B98:
/* 2E98 80221B98 6401FFFF */   daddiu    $at, $zero, -1
/* 2E9C 80221B9C 15E10005 */  bne        $t7, $at, .L80221BB4
/* 2EA0 80221BA0 64010001 */   daddiu    $at, $zero, 1
/* 2EA4 80221BA4 00010FFC */  dsll32     $at, $at, 0x1f
/* 2EA8 80221BA8 15C10002 */  bne        $t6, $at, .L80221BB4
/* 2EAC 80221BAC 00000000 */  nop
/* 2EB0 80221BB0 0006000D */  break      6
.L80221BB4:
/* 2EB4 80221BB4 00001012 */   mflo      $v0
/* 2EB8 80221BB8 0002183C */  dsll32     $v1, $v0, 0
/* 2EBC 80221BBC 0003183F */  dsra32     $v1, $v1, 0
/* 2EC0 80221BC0 03E00008 */  jr         $ra
/* 2EC4 80221BC4 0002103F */   dsra32    $v0, $v0, 0

glabel __ll_mul
/* 2EC8 80221BC8 AFA40000 */  sw         $a0, ($sp)
/* 2ECC 80221BCC AFA50004 */  sw         $a1, 4($sp)
/* 2ED0 80221BD0 AFA60008 */  sw         $a2, 8($sp)
/* 2ED4 80221BD4 AFA7000C */  sw         $a3, 0xc($sp)
/* 2ED8 80221BD8 DFAF0008 */  ld         $t7, 8($sp)
/* 2EDC 80221BDC DFAE0000 */  ld         $t6, ($sp)
/* 2EE0 80221BE0 01CF001D */  dmultu     $t6, $t7
/* 2EE4 80221BE4 00001012 */  mflo       $v0
/* 2EE8 80221BE8 0002183C */  dsll32     $v1, $v0, 0
/* 2EEC 80221BEC 0003183F */  dsra32     $v1, $v1, 0
/* 2EF0 80221BF0 03E00008 */  jr         $ra
/* 2EF4 80221BF4 0002103F */   dsra32    $v0, $v0, 0

glabel func_80221BF8
/* 2EF8 80221BF8 87AF0012 */  lh         $t7, 0x12($sp)
/* 2EFC 80221BFC AFA60008 */  sw         $a2, 8($sp)
/* 2F00 80221C00 AFA7000C */  sw         $a3, 0xc($sp)
/* 2F04 80221C04 DFAE0008 */  ld         $t6, 8($sp)
/* 2F08 80221C08 01E0C025 */  or         $t8, $t7, $zero
/* 2F0C 80221C0C 0300C825 */  or         $t9, $t8, $zero
/* 2F10 80221C10 01D9001F */  ddivu      $zero, $t6, $t9
/* 2F14 80221C14 17200002 */  bnez       $t9, .L80221C20
/* 2F18 80221C18 00000000 */  nop
/* 2F1C 80221C1C 0007000D */  break      7
.L80221C20:
/* 2F20 80221C20 00004012 */   mflo      $t0
/* 2F24 80221C24 FC880000 */  sd         $t0, ($a0)
/* 2F28 80221C28 87AA0012 */  lh         $t2, 0x12($sp)
/* 2F2C 80221C2C DFA90008 */  ld         $t1, 8($sp)
/* 2F30 80221C30 01405825 */  or         $t3, $t2, $zero
/* 2F34 80221C34 01606025 */  or         $t4, $t3, $zero
/* 2F38 80221C38 012C001F */  ddivu      $zero, $t1, $t4
/* 2F3C 80221C3C 15800002 */  bnez       $t4, .L80221C48
/* 2F40 80221C40 00000000 */  nop
/* 2F44 80221C44 0007000D */  break      7
.L80221C48:
/* 2F48 80221C48 00006810 */   mfhi      $t5
/* 2F4C 80221C4C FCAD0000 */  sd         $t5, ($a1)
/* 2F50 80221C50 03E00008 */  jr         $ra
/* 2F54 80221C54 00000000 */  nop

glabel __ll_rem
/* 2F58 80221C58 27BDFFF8 */  addiu      $sp, $sp, -8
/* 2F5C 80221C5C AFA40008 */  sw         $a0, 8($sp)
/* 2F60 80221C60 AFA5000C */  sw         $a1, 0xc($sp)
/* 2F64 80221C64 AFA60010 */  sw         $a2, 0x10($sp)
/* 2F68 80221C68 AFA70014 */  sw         $a3, 0x14($sp)
/* 2F6C 80221C6C DFAF0010 */  ld         $t7, 0x10($sp)
/* 2F70 80221C70 DFAE0008 */  ld         $t6, 8($sp)
/* 2F74 80221C74 01CF001E */  ddiv       $zero, $t6, $t7
/* 2F78 80221C78 00000000 */  nop
/* 2F7C 80221C7C 15E00002 */  bnez       $t7, .L80221C88
/* 2F80 80221C80 00000000 */   nop
/* 2F84 80221C84 0007000D */  break      7
.L80221C88:
/* 2F88 80221C88 6401FFFF */   daddiu    $at, $zero, -1
/* 2F8C 80221C8C 15E10005 */  bne        $t7, $at, .L80221CA4
/* 2F90 80221C90 64010001 */   daddiu    $at, $zero, 1
/* 2F94 80221C94 00010FFC */  dsll32     $at, $at, 0x1f
/* 2F98 80221C98 15C10002 */  bne        $t6, $at, .L80221CA4
/* 2F9C 80221C9C 00000000 */  nop
/* 2FA0 80221CA0 0006000D */  break      6
.L80221CA4:
/* 2FA4 80221CA4 0000C010 */   mfhi      $t8
/* 2FA8 80221CA8 FFB80000 */  sd         $t8, ($sp)
/* 2FAC 80221CAC 07010003 */  bgez       $t8, .L80221CBC
/* 2FB0 80221CB0 00000000 */  nop
/* 2FB4 80221CB4 1DE00007 */  bgtz       $t7, .L80221CD4
/* 2FB8 80221CB8 00000000 */  nop
.L80221CBC:
/* 2FBC 80221CBC DFB90000 */  ld         $t9, ($sp)
/* 2FC0 80221CC0 1B200008 */  blez       $t9, .L80221CE4
/* 2FC4 80221CC4 00000000 */  nop
/* 2FC8 80221CC8 DFA80010 */  ld         $t0, 0x10($sp)
/* 2FCC 80221CCC 05010005 */  bgez       $t0, .L80221CE4
/* 2FD0 80221CD0 00000000 */  nop
.L80221CD4:
/* 2FD4 80221CD4 DFA90000 */  ld         $t1, ($sp)
/* 2FD8 80221CD8 DFAA0010 */  ld         $t2, 0x10($sp)
/* 2FDC 80221CDC 012A582D */  daddu      $t3, $t1, $t2
/* 2FE0 80221CE0 FFAB0000 */  sd         $t3, ($sp)
.L80221CE4:
/* 2FE4 80221CE4 8FA20000 */  lw         $v0, ($sp)
/* 2FE8 80221CE8 8FA30004 */  lw         $v1, 4($sp)
/* 2FEC 80221CEC 03E00008 */  jr         $ra
/* 2FF0 80221CF0 27BD0008 */   addiu     $sp, $sp, 8

glabel __ll_rshift
/* 2FF4 80221CF4 AFA40000 */  sw         $a0, ($sp)
/* 2FF8 80221CF8 AFA50004 */  sw         $a1, 4($sp)
/* 2FFC 80221CFC AFA60008 */  sw         $a2, 8($sp)
/* 3000 80221D00 AFA7000C */  sw         $a3, 0xc($sp)
/* 3004 80221D04 DFAF0008 */  ld         $t7, 8($sp)
/* 3008 80221D08 DFAE0000 */  ld         $t6, ($sp)
/* 300C 80221D0C 01EE1017 */  dsrav      $v0, $t6, $t7
/* 3010 80221D10 0002183C */  dsll32     $v1, $v0, 0
/* 3014 80221D14 0003183F */  dsra32     $v1, $v1, 0
/* 3018 80221D18 03E00008 */  jr         $ra
/* 301C 80221D1C 0002103F */   dsra32    $v0, $v0, 0
/* 3020 80221D20 00000000 */  nop
/* 3024 80221D24 00000000 */  nop
/* 3028 80221D28 00000000 */  nop
/* 302C 80221D2C 00000000 */  nop
/* 3030 80221D30 00000000 */  nop
/* 3034 80221D34 00000000 */  nop
/* 3038 80221D38 00000000 */  nop
/* 303C 80221D3C 00000000 */  nop
/* 3040 80221D40 00000000 */  nop
/* 3044 80221D44 00000000 */  nop
/* 3048 80221D48 00000000 */  nop
/* 304C 80221D4C 00000000 */  nop
/* 3050 80221D50 00000000 */  nop
/* 3054 80221D54 00000000 */  nop
/* 3058 80221D58 00000000 */  nop
/* 305C 80221D5C 00000000 */  nop
