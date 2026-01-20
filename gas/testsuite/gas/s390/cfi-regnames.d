#name: s390/s390x register names in CFI directives
#objdump: -Wf

.*: +file format .*

Contents of the .eh_frame section:

#...

# General register (GR) names r0..r15
00000018 0000000000000028 0000001c FDE cie=00000000 pc=0000000000000000..0000000000000004
  DW_CFA_advance_loc: 2 to 0000000000000002
  DW_CFA_register: r0 in r1
  DW_CFA_register: r2 in r3
  DW_CFA_register: r4 in r5
  DW_CFA_register: r6 in r7
  DW_CFA_register: r8 in r9
  DW_CFA_register: r10 in r11
  DW_CFA_register: r12 in r13
  DW_CFA_register: r14 in r15
  DW_CFA_nop
  DW_CFA_nop

# Floating-point register (FPR) names f0..f15
00000044 0000000000000028 00000048 FDE cie=00000000 pc=0000000000000004..0000000000000008
  DW_CFA_advance_loc: 2 to 0000000000000006
  DW_CFA_register: r16 \(f0\) in r20 \(f1\)
  DW_CFA_register: r17 \(f2\) in r21 \(f3\)
  DW_CFA_register: r18 \(f4\) in r22 \(f5\)
  DW_CFA_register: r19 \(f6\) in r23 \(f7\)
  DW_CFA_register: r24 \(f8\) in r28 \(f9\)
  DW_CFA_register: r25 \(f10\) in r29 \(f11\)
  DW_CFA_register: r26 \(f12\) in r30 \(f13\)
  DW_CFA_register: r27 \(f14\) in r31 \(f15\)
  DW_CFA_nop
  DW_CFA_nop

# Vector register (VR) names v0..v31
00000070 0000000000000044 00000074 FDE cie=00000000 pc=0000000000000008..000000000000000c
  DW_CFA_advance_loc: 2 to 000000000000000a
  DW_CFA_register: r16 \(f0\) in r20 \(f1\)
  DW_CFA_register: r17 \(f2\) in r21 \(f3\)
  DW_CFA_register: r18 \(f4\) in r22 \(f5\)
  DW_CFA_register: r19 \(f6\) in r23 \(f7\)
  DW_CFA_register: r24 \(f8\) in r28 \(f9\)
  DW_CFA_register: r25 \(f10\) in r29 \(f11\)
  DW_CFA_register: r26 \(f12\) in r30 \(f13\)
  DW_CFA_register: r27 \(f14\) in r31 \(f15\)
  DW_CFA_register: r68 \(v16\) in r72 \(v17\)
  DW_CFA_register: r69 \(v18\) in r73 \(v19\)
  DW_CFA_register: r70 \(v20\) in r74 \(v21\)
  DW_CFA_register: r71 \(v22\) in r75 \(v23\)
  DW_CFA_register: r76 \(v24\) in r80 \(v25\)
  DW_CFA_register: r77 \(v26\) in r81 \(v27\)
  DW_CFA_register: r78 \(v28\) in r82 \(v29\)
  DW_CFA_register: r79 \(v30\) in r83 \(v31\)
  DW_CFA_nop
  DW_CFA_nop
  DW_CFA_nop
  DW_CFA_nop
  DW_CFA_nop
  DW_CFA_nop
